#include "timeshiftx/libcurl_network_plugin.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace timeshiftx;

namespace {

class FastServer {
public:
    FastServer() {
        fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd_ < 0) return;

        int opt = 1;
        setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in addr {};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        if (::bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) return;
        if (::listen(fd_, 64) != 0) return;

        socklen_t len = sizeof(addr);
        if (::getsockname(fd_, reinterpret_cast<sockaddr*>(&addr), &len) != 0) return;
        port_ = ntohs(addr.sin_port);

        running_ = true;
        worker_ = std::thread([this]() { loop(); });
    }

    ~FastServer() {
        running_ = false;
        if (fd_ >= 0) {
            ::shutdown(fd_, SHUT_RDWR);
            ::close(fd_);
        }
        if (worker_.joinable()) worker_.join();
    }

    bool ok() const { return fd_ >= 0 && port_ > 0; }
    std::string url() const { return "http://127.0.0.1:" + std::to_string(port_) + "/p"; }

private:
    void loop() {
        while (running_) {
            sockaddr_in caddr {};
            socklen_t clen = sizeof(caddr);
            const int cfd = ::accept(fd_, reinterpret_cast<sockaddr*>(&caddr), &clen);
            if (cfd < 0) continue;

            char buf[1024];
            (void)::read(cfd, buf, sizeof(buf));
            const std::string body = "ok";
            const std::string resp = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok";
            (void)::write(cfd, resp.data(), resp.size());
            ::shutdown(cfd, SHUT_RDWR);
            ::close(cfd);
        }
    }

    int fd_ {-1};
    int port_ {0};
    std::atomic<bool> running_ {false};
    std::thread worker_;
};

} // namespace

int main() {
    FastServer server;
    if (!server.ok()) return EXIT_FAILURE;

    auto plugin = createLibcurlNetworkPlugin();
    if (!plugin) return EXIT_FAILURE;

    constexpr int kRequests = 64;
    std::mutex m;
    std::condition_variable cv;
    int done = 0;
    bool all_ok = true;

    NetworkRequest req;
    req.url = server.url();
    req.timeout_seconds = 2;

    const auto begin = std::chrono::steady_clock::now();
    for (int i = 0; i < kRequests; ++i) {
        plugin->performAsync(req, [&](NetworkResponse resp) {
            {
                std::lock_guard<std::mutex> lock(m);
                if (!resp.error.ok()) all_ok = false;
                ++done;
            }
            cv.notify_one();
        });
    }

    {
        std::unique_lock<std::mutex> lock(m);
        cv.wait_for(lock, std::chrono::seconds(10), [&]() { return done == kRequests; });
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - begin).count();

    if (done != kRequests) return EXIT_FAILURE;
    if (!all_ok) return EXIT_FAILURE;
    if (elapsed > 10000) return EXIT_FAILURE;

    return EXIT_SUCCESS;
}
