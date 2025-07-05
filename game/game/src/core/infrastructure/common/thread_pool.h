#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <vector>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <functional>
#include <future>

class ThreadPool {
private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    mutable std::mutex taskMutex;
    std::condition_variable condition;
    std::atomic<bool> stopping;

public:
    ThreadPool(size_t numThreads = 4);
    ~ThreadPool();
    
    template<typename F>
    void enqueue(F&& f);
    
    // 获取任务队列大小
    size_t getQueueSize() const;
    
    // 获取工作线程数量
    size_t getWorkerCount() const;
    
    // 停止线程池
    void stop();
};

// 模板方法实现
template<typename F>
void ThreadPool::enqueue(F&& f) {
    {
        std::unique_lock<std::mutex> lock(taskMutex);
        if (stopping) {
            return;
        }
        tasks.emplace(std::forward<F>(f));
    }
    condition.notify_one();
}

#endif // THREAD_POOL_H 