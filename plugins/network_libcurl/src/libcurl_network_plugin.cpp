#include "timeshiftx/libcurl_network_plugin.hpp"

#include <curl/curl.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace timeshiftx {
namespace {

size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    const size_t total = size * nmemb;
    auto* body = static_cast<std::string*>(userp);
    body->append(static_cast<char*>(contents), total);
    return total;
}

ErrorCode mapHttpStatusToError(long http_code) {
    if (http_code >= 200 && http_code < 300) {
        return ErrorCode::OK;
    }
    if (http_code == 404) {
        return ErrorCode::ERR_CATCHUP_EXPIRED;
    }
    return ErrorCode::ERR_NETWORK_HTTP_STATUS;
}

struct PendingTask {
    std::uint64_t id {0};
    NetworkRequest request;
    NetworkCallback callback;
};

struct InflightTask {
    std::uint64_t id {0};
    NetworkRequest request;
    NetworkCallback callback;
    std::chrono::steady_clock::time_point begin;

    CURL* easy {nullptr};
    curl_slist* headers {nullptr};
    std::string body;
};

class LibcurlNetworkPlugin final : public INetworkPlugin {
public:
    explicit LibcurlNetworkPlugin(int max_concurrent = 8)
        : max_concurrent_(max_concurrent > 0 ? max_concurrent : 1) {
        multi_ = curl_multi_init();
        worker_ = std::thread(&LibcurlNetworkPlugin::workerLoop, this);
    }

    ~LibcurlNetworkPlugin() override {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
        }
        cv_.notify_all();
        if (worker_.joinable()) {
            worker_.join();
        }

        if (multi_) {
            curl_multi_cleanup(multi_);
            multi_ = nullptr;
        }
    }

    std::string name() const override {
        return "libcurl";
    }

    NetworkResponse perform(const NetworkRequest& request) override {
        auto promise = std::make_shared<std::promise<NetworkResponse>>();
        auto fut = promise->get_future();

        performAsync(request, [promise](NetworkResponse resp) {
            promise->set_value(std::move(resp));
        });

        return fut.get();
    }

    void performAsync(const NetworkRequest& request, NetworkCallback callback) override {
        (void)performAsyncTracked(request, std::move(callback));
    }

    std::uint64_t performAsyncTracked(const NetworkRequest& request, NetworkCallback callback) override {
        return enqueue(NetworkRequest(request), std::move(callback));
    }

    std::uint64_t enqueue(NetworkRequest request, NetworkCallback callback) {
        const std::uint64_t id = next_id_.fetch_add(1);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            pending_.push_back(PendingTask{id, std::move(request), std::move(callback)});
        }

        cv_.notify_one();
        return id;
    }

    bool cancel(std::uint64_t id) {
        std::lock_guard<std::mutex> lock(mutex_);
        cancelled_.insert(id);
        return true;
    }

private:
    void workerLoop() {
        while (true) {
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait_for(lock, std::chrono::milliseconds(20), [this] {
                    return stop_ || !pending_.empty() || !inflight_.empty();
                });

                if (stop_ && pending_.empty() && inflight_.empty()) {
                    break;
                }

                while (!pending_.empty() && static_cast<int>(inflight_.size()) < max_concurrent_) {
                    PendingTask task = std::move(pending_.front());
                    pending_.pop_front();

                    if (cancelled_.find(task.id) != cancelled_.end()) {
                        cancelled_.erase(task.id);
                        if (task.callback) {
                            task.callback({Error{ErrorCode::ERR_INTERNAL, "request canceled"}, 0, {}, {}, 0});
                        }
                        continue;
                    }

                    startRequest(std::move(task));
                }
            }

            int running_handles = 0;
            curl_multi_perform(multi_, &running_handles);

            int msgs_in_queue = 0;
            while (CURLMsg* msg = curl_multi_info_read(multi_, &msgs_in_queue)) {
                if (msg->msg != CURLMSG_DONE) {
                    continue;
                }

                CURL* easy = msg->easy_handle;
                InflightTask task;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    auto it = handle_to_id_.find(easy);
                    if (it == handle_to_id_.end()) {
                        continue;
                    }
                    const auto inflight_it = inflight_.find(it->second);
                    if (inflight_it == inflight_.end()) {
                        handle_to_id_.erase(it);
                        continue;
                    }
                    task = std::move(inflight_it->second);
                    inflight_.erase(inflight_it);
                    handle_to_id_.erase(it);
                }

                finalizeRequest(std::move(task), msg->data.result);
            }

            int numfds = 0;
            curl_multi_poll(multi_, nullptr, 0, 20, &numfds);
        }

        // shutdown cleanup for pending
        std::deque<PendingTask> rest;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            rest = std::move(pending_);
        }
        for (auto& t : rest) {
            if (t.callback) {
                t.callback({Error{ErrorCode::ERR_INTERNAL, "plugin stopped"}, 0, {}, {}, 0});
            }
        }
    }

    void startRequest(PendingTask task) {
        CURL* easy = curl_easy_init();
        if (!easy) {
            if (task.callback) {
                task.callback({Error{ErrorCode::ERR_INTERNAL, "curl_easy_init failed"}, 0, {}, {}, 0});
            }
            return;
        }

        InflightTask inflight;
        inflight.id = task.id;
        inflight.request = std::move(task.request);
        inflight.callback = std::move(task.callback);
        inflight.begin = std::chrono::steady_clock::now();
        inflight.easy = easy;

        curl_easy_setopt(easy, CURLOPT_URL, inflight.request.url.c_str());
        curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, inflight.request.follow_redirect ? 1L : 0L);
        curl_easy_setopt(easy, CURLOPT_TIMEOUT, inflight.request.timeout_seconds);
        curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT, inflight.request.timeout_seconds);
        curl_easy_setopt(easy, CURLOPT_USERAGENT, inflight.request.user_agent.c_str());
        curl_easy_setopt(easy, CURLOPT_SSL_VERIFYPEER, inflight.request.strict_ssl ? 1L : 0L);
        curl_easy_setopt(easy, CURLOPT_SSL_VERIFYHOST, inflight.request.strict_ssl ? 2L : 0L);

        for (const auto& kv : inflight.request.headers) {
            const std::string header = kv.first + ": " + kv.second;
            inflight.headers = curl_slist_append(inflight.headers, header.c_str());
        }
        if (inflight.headers) {
            curl_easy_setopt(easy, CURLOPT_HTTPHEADER, inflight.headers);
        }

        if (inflight.request.method == NetworkMethod::HEAD) {
            curl_easy_setopt(easy, CURLOPT_NOBODY, 1L);
        } else {
            curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, writeCallback);
            curl_easy_setopt(easy, CURLOPT_WRITEDATA, &inflight.body);
        }

        curl_easy_setopt(easy, CURLOPT_PRIVATE, reinterpret_cast<void*>(task.id));

        curl_multi_add_handle(multi_, easy);

        handle_to_id_[easy] = task.id;
        inflight_[task.id] = std::move(inflight);
    }

    void finalizeRequest(InflightTask task, CURLcode rc) {
        long http_code = 0;
        curl_easy_getinfo(task.easy, CURLINFO_RESPONSE_CODE, &http_code);

        curl_multi_remove_handle(multi_, task.easy);
        if (task.headers) {
            curl_slist_free_all(task.headers);
        }
        curl_easy_cleanup(task.easy);

        NetworkResponse resp;
        resp.http_status = http_code;
        resp.latency_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - task.begin).count();

        bool was_cancelled = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = cancelled_.find(task.id);
            if (it != cancelled_.end()) {
                cancelled_.erase(it);
                was_cancelled = true;
            }
        }

        if (was_cancelled) {
            resp.error = {ErrorCode::ERR_INTERNAL, "request canceled"};
        } else if (rc != CURLE_OK) {
            resp.error = {ErrorCode::ERR_NETWORK_TIMEOUT, std::string("Network request failed: ") + curl_easy_strerror(rc)};
        } else {
            const ErrorCode mapped = mapHttpStatusToError(http_code);
            if (mapped == ErrorCode::OK) {
                resp.error = {ErrorCode::OK, "HTTP request successful"};
                resp.body = std::move(task.body);
            } else {
                resp.error = {mapped, std::string("HTTP status code abnormal: ") + std::to_string(http_code)};
            }
        }

        if (task.callback) {
            task.callback(std::move(resp));
        }
    }

private:
    CURLM* multi_ {nullptr};
    std::thread worker_;

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool stop_ {false};

    int max_concurrent_ {8};
    std::atomic<std::uint64_t> next_id_ {1};

    std::deque<PendingTask> pending_;
    std::unordered_map<std::uint64_t, InflightTask> inflight_;
    std::unordered_map<CURL*, std::uint64_t> handle_to_id_;
    std::unordered_set<std::uint64_t> cancelled_;
};

} // namespace

std::shared_ptr<INetworkPlugin> createLibcurlNetworkPlugin() {
    return std::make_shared<LibcurlNetworkPlugin>();
}

} // namespace timeshiftx
