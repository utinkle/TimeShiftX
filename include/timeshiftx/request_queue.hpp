#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>
#include <vector>

#include "timeshiftx/errors.hpp"
#include "timeshiftx/http_client.hpp"

namespace timeshiftx {

class RequestQueue {
public:
    static RequestQueue& instance();
    ~RequestQueue();

    RequestQueue(const RequestQueue&) = delete;
    RequestQueue& operator=(const RequestQueue&) = delete;

    void setMaxConcurrent(int max);
    int maxConcurrent() const { return max_concurrent_.load(); }

    std::future<HttpClient::HttpResponse> enqueueGet(const std::string& url, long timeout_seconds, int max_retries);
    std::future<Error> enqueueHead(const std::string& url, long timeout_seconds, int max_retries);

    void shutdown();
private:
    RequestQueue();

    using Task = std::function<void()>;

    void workerLoop();
    void addWorkers(int count);

    template<typename F>
    auto enqueueTask(F&& func) -> std::future<decltype(func())> {
        using Ret = decltype(func());
        auto promise = std::make_shared<std::promise<Ret>>();
        std::future<Ret> fut = promise->get_future();

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stop_.load()) {
                throw std::runtime_error("RequestQueue already shutdown");
            }
            pending_tasks_.emplace([promise, func = std::forward<F>(func)]() mutable {
                try {
                    promise->set_value(func());
                } catch (...) {
                    promise->set_exception(std::current_exception());
                }
            });
        }
        cv_.notify_one();
        return fut;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<Task> pending_tasks_;

    std::vector<Task> local_tasks_;
    std::atomic<bool> stop_{false};
    std::atomic<int> max_concurrent_{8};
    std::vector<std::thread> workers_;
};

} // namespace timeshiftx