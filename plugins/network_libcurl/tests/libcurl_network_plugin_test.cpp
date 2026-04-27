#include "timeshiftx/libcurl_network_plugin.hpp"
#include "timeshiftx/network_service.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

using namespace timeshiftx;

namespace {

struct Reply {
    int code {200};
    std::string body;
    int delay_ms {0};
    bool never_respond {false};
};

class MockServer {
public:
    MockServer() {
        fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd_ < 0) return;

        int opt = 1;
        setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in addr {};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        if (::bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) return;
        if (::listen(fd_, 16) != 0) return;

        socklen_t len = sizeof(addr);
        if (::getsockname(fd_, reinterpret_cast<sockaddr*>(&addr), &len) != 0) return;
        port_ = ntohs(addr.sin_port);

        running_ = true;
        worker_ = std::thread([this]() { this->loop(); });
    }

    ~MockServer() {
        running_ = false;
        if (fd_ >= 0) {
            ::shutdown(fd_, SHUT_RDWR);
            ::close(fd_);
        }
        if (worker_.joinable()) worker_.join();
    }

    bool ok() const { return fd_ >= 0 && port_ > 0; }

    std::string url() const {
        return "http://127.0.0.1:" + std::to_string(port_) + "/demo";
    }

    void push(Reply r) {
        std::lock_guard<std::mutex> lock(mutex_);
        replies_.push_back(std::move(r));
    }

private:
    void loop() {
        while (running_) {
            sockaddr_in caddr {};
            socklen_t clen = sizeof(caddr);
            const int cfd = ::accept(fd_, reinterpret_cast<sockaddr*>(&caddr), &clen);
            if (cfd < 0) {
                continue;
            }

            char buf[1024];
            (void)::read(cfd, buf, sizeof(buf));

            Reply r;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!replies_.empty()) {
                    r = replies_.front();
                    replies_.pop_front();
                }
            }

            if (!r.never_respond) {
                if (r.delay_ms > 0) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(r.delay_ms));
                }
                const std::string resp = "HTTP/1.1 " + std::to_string(r.code) + " OK\r\nContent-Length: " +
                                         std::to_string(r.body.size()) + "\r\nConnection: close\r\n\r\n" + r.body;
                (void)::write(cfd, resp.data(), resp.size());
            }

            ::shutdown(cfd, SHUT_RDWR);
            ::close(cfd);
        }
    }

private:
    int fd_ {-1};
    int port_ {0};
    std::atomic<bool> running_ {false};
    std::thread worker_;

    mutable std::mutex mutex_;
    std::deque<Reply> replies_;
};

NetworkResponse waitAsync(INetworkPlugin& plugin, const NetworkRequest& req) {
    std::mutex m;
    std::condition_variable cv;
    bool done = false;
    NetworkResponse out;

    plugin.performAsync(req, [&](NetworkResponse resp) {
        {
            std::lock_guard<std::mutex> lock(m);
            out = std::move(resp);
            done = true;
        }
        cv.notify_one();
    });

    std::unique_lock<std::mutex> lock(m);
    cv.wait_for(lock, std::chrono::seconds(5), [&]() { return done; });
    return out;
}

} // namespace

int main() {
    MockServer server;
    if (!server.ok()) return EXIT_FAILURE;

    auto plugin = createLibcurlNetworkPlugin();
    if (!plugin) return EXIT_FAILURE;

    NetworkRequest req;
    req.url = server.url();
    req.timeout_seconds = 1;
    req.max_retries = 2;
    req.retry_delay_ms = 50;

    server.push({200, "ok", 0, false});
    NetworkResponse ok = waitAsync(*plugin, req);
    if (!ok.error.ok() || ok.body != "ok") return EXIT_FAILURE;

    server.push({404, "no", 0, false});
    NetworkResponse n404 = waitAsync(*plugin, req);
    if (n404.error.code != ErrorCode::ERR_CATCHUP_EXPIRED) return EXIT_FAILURE;

    // core retry with injected plugin
    server.push({200, "", 0, true});
    server.push({200, "retry-ok", 0, false});
    auto retry = NetworkService::request(req, {}, plugin.get());
    if (!retry.error.ok() || retry.body != "retry-ok") return EXIT_FAILURE;

    // cancel
    server.push({200, "", 300, false});
    std::mutex m;
    std::condition_variable cv;
    bool done = false;
    NetworkResponse canceled;
    const std::uint64_t id = plugin->performAsyncTracked(req, [&](NetworkResponse resp) {
        {
            std::lock_guard<std::mutex> lock(m);
            canceled = std::move(resp);
            done = true;
        }
        cv.notify_one();
    });
    plugin->cancel(id);
    {
        std::unique_lock<std::mutex> lock(m);
        cv.wait_for(lock, std::chrono::seconds(5), [&]() { return done; });
    }
    if (canceled.error.ok()) return EXIT_FAILURE;

    return EXIT_SUCCESS;
}
