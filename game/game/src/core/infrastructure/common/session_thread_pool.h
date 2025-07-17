#ifndef SESSION_THREAD_POOL_H
#define SESSION_THREAD_POOL_H

#include <vector>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <functional>
#include <unordered_map>
#include <string>
#include <memory>

// 线程池中单个线程的工作队列
class WorkerThread {
public:
    WorkerThread();
    ~WorkerThread();
    
    void start();
    void stop();
    void enqueue(std::function<void()> task);
    size_t getQueueSize() const;
    
private:
    std::thread worker_;
    std::queue<std::function<void()>> tasks_;
    mutable std::mutex taskMutex_;
    std::condition_variable condition_;
    std::atomic<bool> stopping_;
    
    void workerLoop();
};

// 按SessionID分配的线程池
class SessionThreadPool {
public:
    SessionThreadPool(size_t numThreads = 8);
    ~SessionThreadPool();
    
    // 启动线程池
    void start();
    
    // 停止线程池
    void stop();
    
    // 根据sessionID将任务分配到固定线程
    void enqueueBySession(const std::string& sessionId, std::function<void()> task);
    
    // 获取线程池统计信息
    size_t getWorkerCount() const;
    size_t getTotalQueueSize() const;
    size_t getMaxQueueSize() const;
    
    // 获取指定会话的队列大小
    size_t getSessionQueueSize(const std::string& sessionId) const;
    
private:
    std::vector<std::unique_ptr<WorkerThread>> workers_;
    size_t numThreads_;
    bool started_;
    
    // 会话ID到线程索引的映射（避免频繁计算hash）
    mutable std::unordered_map<std::string, size_t> sessionToThreadMap_;
    mutable std::mutex mapMutex_;
    
    // 根据sessionID计算线程索引
    size_t getThreadIndex(const std::string& sessionId) const;
    
    // 计算字符串hash
    std::hash<std::string> hasher_;
};

#endif // SESSION_THREAD_POOL_H 