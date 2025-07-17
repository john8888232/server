#include "thread_pool.h"
#include <third_party/libuv_cpp/include/LogWriter.hpp>

ThreadPool::ThreadPool(size_t numThreads) : stopping(false) {
    for (size_t i = 0; i < numThreads; ++i) {
        workers.emplace_back([this]() {
            while (true) {
                std::function<void()> task;
                
                {
                    std::unique_lock<std::mutex> lock(taskMutex);
                    condition.wait(lock, [this]() { return stopping || !tasks.empty(); });
                    
                    if (stopping && tasks.empty()) {
                        return;
                    }
                    
                    task = std::move(tasks.front());
                    tasks.pop();
                }
                
                try {
                    task();
                } catch (const std::exception& e) {
                    LOG_ERROR("Exception in thread pool task: %s", e.what());
                } catch (...) {
                    LOG_ERROR("Unknown exception in thread pool task");
                }
            }
        });
    }
    
    LOG_INFO("ThreadPool initialized with %d worker threads", numThreads);
}

ThreadPool::~ThreadPool() {
    stop();
}

void ThreadPool::stop() {
    {
        std::unique_lock<std::mutex> lock(taskMutex);
        stopping = true;
    }
    
    condition.notify_all();
    
    for (std::thread& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    
    LOG_INFO("ThreadPool stopped");
}

size_t ThreadPool::getQueueSize() const {
    std::lock_guard<std::mutex> lock(taskMutex);
    return tasks.size();
}

size_t ThreadPool::getWorkerCount() const {
    return workers.size();
} 