#include "worker_thread.h"
#include "third_party/libuv_cpp/include/LogWriter.hpp"

ThreadWrapper::ThreadWrapper(size_t maxQueueSize, uint32_t taskTimeoutMs)
    : maxQueueSize_(maxQueueSize), taskTimeoutMs_(taskTimeoutMs) {
    stats_.startTime = std::chrono::steady_clock::now();
    LOG_DEBUG("ThreadWrapper created with max queue size: %zu, task timeout warning: %ums", maxQueueSize_, taskTimeoutMs);
}

ThreadWrapper::~ThreadWrapper() {
    if (isRunning()) {
        LOG_WARN("ThreadWrapper destroyed while running, detaching thread");
        stopping_ = true;
        queueCondition_.notify_all();
        
        if (worker_.joinable()) {
            worker_.detach(); // 直接分离线程，不等待
        }
        status_ = Status::STOPPED;
    }
}

ThreadWrapper::ThreadWrapper(ThreadWrapper&& other) noexcept
    : maxQueueSize_(other.maxQueueSize_)
    , taskTimeoutMs_(other.taskTimeoutMs_.load())
    , status_(other.status_.load())
    , worker_(std::move(other.worker_))
    , taskQueue_(std::move(other.taskQueue_))
    , stopping_(other.stopping_.load())
    , gracefulStop_(other.gracefulStop_.load())
    , stats_(other.stats_)
    , exceptionHandler_(std::move(other.exceptionHandler_)) {
    
    other.status_ = Status::STOPPED;
    other.stopping_ = false;
    other.gracefulStop_ = true;
}

ThreadWrapper& ThreadWrapper::operator=(ThreadWrapper&& other) noexcept {
    if (this != &other) {
        // 先停止当前线程
        if (isRunning()) {
            stop(false, 1000);
        }
        
        maxQueueSize_ = other.maxQueueSize_;
        taskTimeoutMs_ = other.taskTimeoutMs_.load();
        status_ = other.status_.load();
        worker_ = std::move(other.worker_);
        taskQueue_ = std::move(other.taskQueue_);
        stopping_ = other.stopping_.load();
        gracefulStop_ = other.gracefulStop_.load();
        stats_ = other.stats_;
        exceptionHandler_ = std::move(other.exceptionHandler_);
        
        other.status_ = Status::STOPPED;
        other.stopping_ = false;
        other.gracefulStop_ = true;
    }
    return *this;
}

bool ThreadWrapper::start() {
    Status expected = Status::STOPPED;
    if (!status_.compare_exchange_strong(expected, Status::STARTING)) {
        LOG_WARN("ThreadWrapper start failed: already running or starting");
        return false;
    }
    
    try {
        stopping_ = false;
        gracefulStop_ = true;
        stats_.startTime = std::chrono::steady_clock::now();
        
        worker_ = std::thread(&ThreadWrapper::workerLoop, this);
        
        // 等待线程真正启动
        std::unique_lock<std::mutex> lock(queueMutex_);
        queueCondition_.wait(lock, [this] { 
            return status_.load() == Status::RUNNING || status_.load() == Status::STOPPED; 
        });
        
        if (status_.load() == Status::RUNNING) {
            LOG_INFO("ThreadWrapper started successfully");
            return true;
        } else {
            LOG_ERROR("ThreadWrapper failed to start");
            return false;
        }
    } catch (const std::exception& e) {
        status_ = Status::STOPPED;
        LOG_ERROR("ThreadWrapper start failed with exception: %s", e.what());
        return false;
    }
}

bool ThreadWrapper::stop(bool graceful, uint32_t timeoutMs) {
    Status currentStatus = status_.load();
    if (currentStatus == Status::STOPPED || currentStatus == Status::STOPPING) {
        return true;
    }
    
    LOG_INFO("ThreadWrapper stopping (graceful: %s, timeout: %ums)", 
             graceful ? "yes" : "no", timeoutMs);
    
    status_ = Status::STOPPING;
    gracefulStop_ = graceful;
    stopping_ = true;
    
    // 唤醒工作线程
    queueCondition_.notify_all();
    
    if (worker_.joinable()) {
        if (timeoutMs == 0) {
            // 无限等待
            worker_.join();
        } else {
            // 有超时的等待
            auto start = std::chrono::steady_clock::now();
            bool joined = false;
            
            // 使用detach + 轮询的方式实现超时join
            std::thread joiner([this, &joined]() {
                if (worker_.joinable()) {
                    worker_.join();
                    joined = true;
                }
            });
            
            // 等待超时或完成
            while (!joined && 
                   std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - start).count() < timeoutMs) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            
            if (joined) {
                joiner.join();
            } else {
                joiner.detach();
                LOG_WARN("ThreadWrapper stop timeout after %ums", timeoutMs);
                status_ = Status::STOPPED;
                return false;
            }
        }
    }
    
    status_ = Status::STOPPED;
    LOG_INFO("ThreadWrapper stopped");
    return true;
}

bool ThreadWrapper::postTask(Task&& task, Priority priority) {
    if (!task) {
        LOG_WARN("ThreadWrapper: attempt to post null task");
        return false;
    }
    
    if (shouldStop()) {
        LOG_DEBUG("ThreadWrapper: task rejected - thread is stopping");
        return false;
    }
    
    {
        std::unique_lock<std::mutex> lock(queueMutex_);
        
        // 检查队列大小限制
        if (maxQueueSize_ > 0 && taskQueue_.size() >= maxQueueSize_) {
            LOG_WARN("ThreadWrapper: task queue full (size: %zu)", taskQueue_.size());
            return false;
        }
        
        taskQueue_.emplace(std::move(task), priority);
        size_t currentSize = taskQueue_.size();
        stats_.currentQueueSize = currentSize;
        
        // 更新最大队列大小统计
        uint64_t currentMax = stats_.maxQueueSize.load();
        while (currentSize > currentMax && 
               !stats_.maxQueueSize.compare_exchange_weak(currentMax, currentSize)) {
            // CAS循环更新最大值
        }
    }
    
    queueCondition_.notify_one();
    return true;
}

size_t ThreadWrapper::clearQueue() {
    std::unique_lock<std::mutex> lock(queueMutex_);
    size_t clearedCount = taskQueue_.size();
    
    // 清空优先级队列
    std::priority_queue<PriorityTask> empty;
    taskQueue_.swap(empty);
    
    stats_.currentQueueSize = 0;
    
    LOG_INFO("ThreadWrapper: cleared %zu tasks from queue", clearedCount);
    return clearedCount;
}

size_t ThreadWrapper::getQueueSize() const {
    std::lock_guard<std::mutex> lock(queueMutex_);
    return taskQueue_.size();
}

ThreadWrapper::Statistics ThreadWrapper::getStatistics() const {
    return stats_;
}

void ThreadWrapper::resetStatistics() {
    stats_.totalTasksProcessed = 0;
    stats_.totalTasksFailed = 0;
    stats_.currentQueueSize = getQueueSize();
    stats_.maxQueueSize = stats_.currentQueueSize.load();
    stats_.startTime = std::chrono::steady_clock::now();
    stats_.lastTaskTime = stats_.startTime;
    
    LOG_INFO("ThreadWrapper: statistics reset");
}

void ThreadWrapper::setExceptionHandler(std::function<void(const std::exception&)> handler) {
    std::lock_guard<std::mutex> lock(exceptionHandlerMutex_);
    exceptionHandler_ = std::move(handler);
}

bool ThreadWrapper::waitForEmpty(uint32_t timeoutMs) {
    std::unique_lock<std::mutex> lock(queueMutex_);
    
    if (timeoutMs == 0) {
        emptyCondition_.wait(lock, [this] { return taskQueue_.empty() || shouldStop(); });
        return taskQueue_.empty();
    } else {
        bool result = emptyCondition_.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                                              [this] { return taskQueue_.empty() || shouldStop(); });
        return result && taskQueue_.empty();
    }
}

void ThreadWrapper::workerLoop() {
    LOG_INFO("ThreadWrapper: worker loop started");
    status_ = Status::RUNNING;
    queueCondition_.notify_all(); // 通知start()函数线程已启动
    
    while (!shouldStop()) {
        PriorityTask task([](){}, Priority::NORMAL);
        bool hasTask = false;
        
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            
            // 等待任务或停止信号
            queueCondition_.wait(lock, [this] { 
                return !taskQueue_.empty() || shouldStop(); 
            });
            
            if (!taskQueue_.empty()) {
                task = std::move(const_cast<PriorityTask&>(taskQueue_.top()));
                taskQueue_.pop();
                stats_.currentQueueSize = taskQueue_.size();
                hasTask = true;
                
                // 如果队列空了，通知等待的线程
                if (taskQueue_.empty()) {
                    emptyCondition_.notify_all();
                }
            }
        }
        
        if (hasTask) {
            processTask(task);
        }
        
        // 如果是优雅停止，继续处理队列中的任务
        if (stopping_.load() && gracefulStop_.load()) {
            std::lock_guard<std::mutex> lock(queueMutex_);
            if (taskQueue_.empty()) {
                break;
            }
        }
    }
    
    LOG_INFO("ThreadWrapper: worker loop ended");
}

void ThreadWrapper::processTask(const PriorityTask& task) {
    auto startTime = std::chrono::steady_clock::now();
    bool success = false;
    
    try {
        task.task();
        success = true;
        stats_.lastTaskTime = std::chrono::steady_clock::now();
    } catch (const std::exception& e) {
        LOG_ERROR("ThreadWrapper: task execution failed: %s", e.what());
        
        // 调用异常处理器
        {
            std::lock_guard<std::mutex> lock(exceptionHandlerMutex_);
            if (exceptionHandler_) {
                try {
                    exceptionHandler_(e);
                } catch (...) {
                    LOG_ERROR("ThreadWrapper: exception handler itself threw exception");
                }
            }
        }
    } catch (...) {
        LOG_ERROR("ThreadWrapper: task execution failed with unknown exception");
    }
    
    updateStatistics(success);
    
    // 监控任务执行时间
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - startTime);
    
    uint32_t timeoutThreshold = taskTimeoutMs_.load();
    if (timeoutThreshold > 0 && duration.count() >= timeoutThreshold) {
        LOG_WARN("ThreadWrapper: task execution time exceeded threshold - %lld ms (threshold: %u ms, priority: %d)", 
                 duration.count(), timeoutThreshold, static_cast<int>(task.priority));
    }
}

void ThreadWrapper::updateStatistics(bool success) {
    if (success) {
        stats_.totalTasksProcessed.fetch_add(1);
    } else {
        stats_.totalTasksFailed.fetch_add(1);
    }
}

bool ThreadWrapper::shouldStop() const {
    return stopping_.load() && (!gracefulStop_.load() || 
           (gracefulStop_.load() && getQueueSize() == 0));
}