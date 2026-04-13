#include "timeshiftx/request_queue.hpp"

#include <algorithm>
#include <cassert>

namespace timeshiftx {

RequestQueue& RequestQueue::instance() {
    static RequestQueue queue;
    return queue;
}

RequestQueue::RequestQueue() {
    addWorkers(max_concurrent_.load());
}

RequestQueue::~RequestQueue() {
    shutdown();
}

void RequestQueue::setMaxConcurrent(int max) {
    if (max <= 0) max = 1;
    int old = max_concurrent_.exchange(max);
    if (max > old) {
        addWorkers(max - old);
    }
    // 如果 max < old，忽略（不减少线程）
}

void RequestQueue::addWorkers(int count) {
    for (int i = 0; i < count; ++i) {
        workers_.emplace_back(&RequestQueue::workerLoop, this);
    }
}

void RequestQueue::shutdown() {
    bool expected = false;
    if (!stop_.compare_exchange_strong(expected, true)) {
        return; // already stopped
    }
    cv_.notify_all();
    for (auto& t : workers_) {
        if (t.joinable()) t.join();
    }
    workers_.clear();
    // 清空未处理任务
    std::lock_guard<std::mutex> lock(mutex_);
    while (!pending_tasks_.empty()) pending_tasks_.pop();
    local_tasks_.clear();
}

// 工作线程主循环（双缓冲）
void RequestQueue::workerLoop() {
    while (!stop_.load()) {
        Task task;
        // 优先处理本地队列
        if (!local_tasks_.empty()) {
            task = std::move(local_tasks_.back());
            local_tasks_.pop_back();
        } else {
            // 从全局队列批量交换
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this] { return !pending_tasks_.empty() || stop_.load(); });
                if (stop_.load() && pending_tasks_.empty()) break;
                // 交换
                local_tasks_.reserve(pending_tasks_.size());
                while (!pending_tasks_.empty()) {
                    local_tasks_.push_back(std::move(pending_tasks_.front()));
                    pending_tasks_.pop();
                }
            }
            if (local_tasks_.empty()) continue;
            task = std::move(local_tasks_.back());
            local_tasks_.pop_back();
        }

        if (task) {
            task();
        }
    }
}

// 提交 GET 请求
std::future<HttpClient::HttpResponse> RequestQueue::enqueueGet(
    const std::string& url,
    long timeout_seconds,
    int max_retries) {

    return enqueueTask([url, timeout_seconds, max_retries]() {
        HttpClient::HttpResponse resp;
        resp.error = HttpClient::get(url, resp.body, timeout_seconds, max_retries);
        return resp;
    });
}

// 提交 HEAD 请求
std::future<Error> RequestQueue::enqueueHead(
    const std::string& url,
    long timeout_seconds,
    int max_retries) {
    return enqueueTask([url, timeout_seconds, max_retries]() {
        return HttpClient::head(url, timeout_seconds, max_retries);
    });
}

} // namespace timeshiftx