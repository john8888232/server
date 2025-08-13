#ifndef WORKER_THREAD_H
#define WORKER_THREAD_H

#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <functional>
#include <memory>
#include <chrono>
#include <future>
#include <type_traits>

// 带有消息队列的工作线程类
class ThreadWrapper {
public:
    using Task = std::function<void()>;
    
    // 任务优先级枚举
    enum class Priority {
        LOW = 0,
        NORMAL = 1,
        HIGH = 2,
        CRITICAL = 3
    };
    
    // 线程状态枚举
    enum class Status {
        STOPPED,
        STARTING,
        RUNNING,
        STOPPING
    };
    
    // 任务统计信息
    struct Statistics {
        std::atomic<uint64_t> totalTasksProcessed{0};
        std::atomic<uint64_t> totalTasksFailed{0};
        std::atomic<uint64_t> currentQueueSize{0};
        std::atomic<uint64_t> maxQueueSize{0};
        std::chrono::steady_clock::time_point startTime;
        std::chrono::steady_clock::time_point lastTaskTime;
        
        // 提供拷贝构造函数和赋值操作符
        Statistics() = default;
        Statistics(const Statistics& other) 
            : totalTasksProcessed(other.totalTasksProcessed.load())
            , totalTasksFailed(other.totalTasksFailed.load())
            , currentQueueSize(other.currentQueueSize.load())
            , maxQueueSize(other.maxQueueSize.load())
            , startTime(other.startTime)
            , lastTaskTime(other.lastTaskTime) {}
        
        Statistics& operator=(const Statistics& other) {
            if (this != &other) {
                totalTasksProcessed.store(other.totalTasksProcessed.load());
                totalTasksFailed.store(other.totalTasksFailed.load());
                currentQueueSize.store(other.currentQueueSize.load());
                maxQueueSize.store(other.maxQueueSize.load());
                startTime = other.startTime;
                lastTaskTime = other.lastTaskTime;
            }
            return *this;
        }
    };

private:
    // 优先级任务包装器
    struct PriorityTask {
        Task task;
        Priority priority;
        std::chrono::steady_clock::time_point enqueueTime;
        
        PriorityTask(Task&& t, Priority p) 
            : task(std::move(t)), priority(p), enqueueTime(std::chrono::steady_clock::now()) {}
        
        // 优先级比较器（优先级高的先执行）
        bool operator<(const PriorityTask& other) const {
            return priority < other.priority;
        }
    };

public:
    // maxQueueSize 最大队列大小，0表示无限制
    // askTimeoutMs 任务执行超时警告阈值（毫秒），0表示不监控
    explicit ThreadWrapper(size_t maxQueueSize = 0, uint32_t taskTimeoutMs = 100);
    
    ~ThreadWrapper();
    
    ThreadWrapper(const ThreadWrapper&) = delete;
    ThreadWrapper& operator=(const ThreadWrapper&) = delete;
    
    ThreadWrapper(ThreadWrapper&& other) noexcept;
    ThreadWrapper& operator=(ThreadWrapper&& other) noexcept;
    
    // return true 启动成功，false 启动失败或已经在运行
    bool start();
    
    // graceful 是否优雅停止（等待队列中的任务完成）
    // timeoutMs 超时时间（毫秒），0表示无限等待
    // return true 停止成功，false 超时
    bool stop(bool graceful = true, uint32_t timeoutMs = 5000);
    
    bool isRunning() const noexcept { return status_.load() == Status::RUNNING; }
    
    // 获取线程状态
    Status getStatus() const noexcept { return status_.load(); }
    
    // 设置任务执行超时警告阈值
    // timeoutMs 超时时间（毫秒），0表示不监控
    void setTaskTimeoutWarning(uint32_t timeoutMs) noexcept { taskTimeoutMs_ = timeoutMs; }
    
    // 获取任务执行超时警告阈值
    uint32_t getTaskTimeoutWarning() const noexcept { return taskTimeoutMs_; }
    
    // 投递任务到队列（异步）
    // task 要执行的任务
    // priority 任务优先级
    // true 投递成功，false 投递失败（队列满或线程已停止）
    bool postTask(Task&& task, Priority priority = Priority::NORMAL);
    
    // 投递任务到队列（模板版本，支持lambda和函数对象）
    template<typename F, typename... Args>
    bool postTask(Priority priority, F&& f, Args&&... args);
    

    // 投递有返回值的任务（异步）
    // task 要执行的任务
    // priority 任务优先级
    // return std::future<T> 任务结果的future对象
    template<typename F, typename... Args>
    auto postTaskAsync(Priority priority, F&& f, Args&&... args) 
        -> std::future<typename std::invoke_result_t<F, Args...>>;
    
    // 清空任务队列
    // return 清空的任务数量
    size_t clearQueue();
    
    // 获取当前队列大小
    size_t getQueueSize() const;
    
    // 获取统计信息
    Statistics getStatistics() const;
    
    // 重置统计信息
    void resetStatistics();
    
    // 设置异常处理回调
    // handler 异常处理函数
    void setExceptionHandler(std::function<void(const std::exception&)> handler);
    
    // 等待所有任务完成
    // timeoutMs 超时时间（毫秒），0表示无限等待
    // return true 所有任务完成，false 超时
    bool waitForEmpty(uint32_t timeoutMs = 0);

private:
    // 线程主循环
    void workerLoop();
    
    // 处理单个任务
    void processTask(const PriorityTask& task);
    
    // 更新统计信息
    void updateStatistics(bool success);
    
    // 检查是否应该停止
    bool shouldStop() const;

private:
    size_t maxQueueSize_;                                       // 最大队列大小
    std::atomic<uint32_t> taskTimeoutMs_;                       // 任务执行超时警告阈值（毫秒）
    std::atomic<Status> status_{Status::STOPPED};            // 线程状态
    
    std::thread worker_;                                       // 工作线程
    std::priority_queue<PriorityTask> taskQueue_;              // 优先级任务队列
    mutable std::mutex queueMutex_;                            // 队列互斥量
    std::condition_variable queueCondition_;                   // 队列条件变量
    std::condition_variable emptyCondition_;                   // 空队列条件变量
    
    std::atomic<bool> stopping_{false};                     // 停止标志
    std::atomic<bool> gracefulStop_{true};                  // 优雅停止标志
    
    Statistics stats_;                                            // 统计信息
    std::function<void(const std::exception&)> exceptionHandler_; // 异常处理器
    mutable std::mutex exceptionHandlerMutex_;                    // 异常处理器互斥量
};

// 模板方法实现
template<typename F, typename... Args>
bool ThreadWrapper::postTask(Priority priority, F&& f, Args&&... args) {
    auto task = [f = std::forward<F>(f), args...]() mutable {
        f(args...);
    };
    return postTask(std::move(task), priority);
}

template<typename F, typename... Args>
auto ThreadWrapper::postTaskAsync(Priority priority, F&& f, Args&&... args) 
    -> std::future<typename std::invoke_result_t<F, Args...>> {
    
    using ReturnType = typename std::invoke_result_t<F, Args...>;
    
    auto promise = std::make_shared<std::promise<ReturnType>>();
    auto future = promise->get_future();
    
    auto task = [promise, f = std::forward<F>(f), args...]() mutable {
        try {
            if constexpr (std::is_void_v<ReturnType>) {
                f(args...);
                promise->set_value();
            } else {
                promise->set_value(f(args...));
            }
        } catch (...) {
            promise->set_exception(std::current_exception());
        }
    };
    
    if (!postTask(std::move(task), priority)) {
        promise->set_exception(std::make_exception_ptr(
            std::runtime_error("Failed to post task to worker thread")));
    }
    
    return future;
}

#endif // WORKER_THREAD_H
