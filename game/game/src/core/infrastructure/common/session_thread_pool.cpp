#include "session_thread_pool.h"
#include <third_party/libuv_cpp/include/LogWriter.hpp>

// WorkerThread实现
WorkerThread::WorkerThread() : stopping_(false) {
}

WorkerThread::~WorkerThread() {
    stop();
}

void WorkerThread::start() {
    if (worker_.joinable()) {
        return; // 已经启动
    }
    
    stopping_ = false;
    worker_ = std::thread(&WorkerThread::workerLoop, this);
}

void WorkerThread::stop() {
    {
        std::unique_lock<std::mutex> lock(taskMutex_);
        stopping_ = true;
    }
    
    condition_.notify_all();
    
    if (worker_.joinable()) {
        worker_.join();
    }
}

void WorkerThread::enqueue(std::function<void()> task) {
    {
        std::unique_lock<std::mutex> lock(taskMutex_);
        if (stopping_) {
            return;
        }
        tasks_.emplace(std::move(task));
    }
    condition_.notify_one();
}

size_t WorkerThread::getQueueSize() const {
    std::unique_lock<std::mutex> lock(taskMutex_);
    return tasks_.size();
}

void WorkerThread::workerLoop() {
    while (true) {
        std::function<void()> task;
        
        {
            std::unique_lock<std::mutex> lock(taskMutex_);
            condition_.wait(lock, [this]() { return stopping_ || !tasks_.empty(); });
            
            if (stopping_ && tasks_.empty()) {
                return;
            }
            
            task = std::move(tasks_.front());
            tasks_.pop();
        }
        
        try {
            task();
        } catch (const std::exception& e) {
            LOG_ERROR("Exception in worker thread task: %s", e.what());
        } catch (...) {
            LOG_ERROR("Unknown exception in worker thread task");
        }
    }
}

// SessionThreadPool实现
SessionThreadPool::SessionThreadPool(size_t numThreads) 
    : numThreads_(numThreads), started_(false) {
    
    workers_.reserve(numThreads_);
    for (size_t i = 0; i < numThreads_; ++i) {
        workers_.emplace_back(std::make_unique<WorkerThread>());
    }
    
    LOG_INFO("SessionThreadPool created with %d worker threads", numThreads_);
}

SessionThreadPool::~SessionThreadPool() {
    stop();
}

void SessionThreadPool::start() {
    if (started_) {
        return;
    }
    
    for (auto& worker : workers_) {
        worker->start();
    }
    
    started_ = true;
    LOG_INFO("SessionThreadPool started with %d workers", numThreads_);
}

void SessionThreadPool::stop() {
    if (!started_) {
        return;
    }
    
    for (auto& worker : workers_) {
        worker->stop();
    }
    
    started_ = false;
    LOG_INFO("SessionThreadPool stopped");
}

void SessionThreadPool::enqueueBySession(const std::string& sessionId, std::function<void()> task) {
    if (!started_) {
        LOG_ERROR("SessionThreadPool not started");
        return;
    }
    
    size_t threadIndex = getThreadIndex(sessionId);
    workers_[threadIndex]->enqueue(std::move(task));
}

size_t SessionThreadPool::getWorkerCount() const {
    return numThreads_;
}

size_t SessionThreadPool::getTotalQueueSize() const {
    size_t totalSize = 0;
    for (const auto& worker : workers_) {
        totalSize += worker->getQueueSize();
    }
    return totalSize;
}

size_t SessionThreadPool::getMaxQueueSize() const {
    size_t maxSize = 0;
    for (const auto& worker : workers_) {
        size_t size = worker->getQueueSize();
        if (size > maxSize) {
            maxSize = size;
        }
    }
    return maxSize;
}

size_t SessionThreadPool::getSessionQueueSize(const std::string& sessionId) const {
    if (!started_) {
        return 0;
    }
    
    size_t threadIndex = getThreadIndex(sessionId);
    return workers_[threadIndex]->getQueueSize();
}

size_t SessionThreadPool::getThreadIndex(const std::string& sessionId) const {
    // 首先检查缓存
    {
        std::lock_guard<std::mutex> lock(mapMutex_);
        auto it = sessionToThreadMap_.find(sessionId);
        if (it != sessionToThreadMap_.end()) {
            return it->second;
        }
    }
    
    // 计算hash并缓存结果
    size_t hash = hasher_(sessionId);
    size_t threadIndex = hash % numThreads_;
    
    {
        std::lock_guard<std::mutex> lock(mapMutex_);
        // 限制缓存大小，避免内存无限增长
        if (sessionToThreadMap_.size() > 10000) {
            sessionToThreadMap_.clear();
        }
        sessionToThreadMap_[sessionId] = threadIndex;
    }
    
    return threadIndex;
} 