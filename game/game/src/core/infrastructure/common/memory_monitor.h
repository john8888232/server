#pragma once

#include <atomic>
#include <chrono>
#include <string>
#include <unordered_map>
#include <mutex>
#include <unistd.h>

class MemoryMonitor {
public:
    static MemoryMonitor& getInstance();
    
    // 内存分配跟踪
    void trackAllocation(void* ptr, size_t size, const char* file = nullptr, int line = 0);
    void trackDeallocation(void* ptr);
    
    // 获取内存统计信息
    size_t getTotalAllocated() const { return total_allocated_.load(); }
    size_t getCurrentAllocated() const { return current_allocated_.load(); }
    size_t getPeakAllocated() const { return peak_allocated_.load(); }
    size_t getAllocationCount() const { return allocation_count_.load(); }
    size_t getDeallocationCount() const { return deallocation_count_.load(); }
    
    // 检测内存泄漏
    void dumpMemoryLeaks();
    
    // 启用/禁用监控
    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool isEnabled() const { return enabled_; }
    
    // 安全的内存信息记录（用于信号处理器）
    void safeMemoryReport(int fd);

private:
    MemoryMonitor() = default;
    ~MemoryMonitor() = default;
    MemoryMonitor(const MemoryMonitor&) = delete;
    MemoryMonitor& operator=(const MemoryMonitor&) = delete;
    
    struct AllocationInfo {
        size_t size;
        std::chrono::steady_clock::time_point timestamp;
        std::string file;
        int line;
    };
    
    std::atomic<bool> enabled_{true};
    std::atomic<size_t> total_allocated_{0};
    std::atomic<size_t> current_allocated_{0};
    std::atomic<size_t> peak_allocated_{0};
    std::atomic<size_t> allocation_count_{0};
    std::atomic<size_t> deallocation_count_{0};
    
    mutable std::mutex allocations_mutex_;
    std::unordered_map<void*, AllocationInfo> allocations_;
};

// 安全的信号处理器日志记录
class SafeSignalLogger {
public:
    static void logCrash(int signal, int fd = STDERR_FILENO);
    static void logMemoryCorruption(int fd = STDERR_FILENO);
    static void writeString(int fd, const char* str);
    static void writeNumber(int fd, long num);
    
private:
    static constexpr size_t BUFFER_SIZE = 1024;
    static char buffer_[BUFFER_SIZE];
};

#define TRACK_MALLOC(ptr, size) MemoryMonitor::getInstance().trackAllocation(ptr, size, __FILE__, __LINE__)
#define TRACK_FREE(ptr) MemoryMonitor::getInstance().trackDeallocation(ptr)