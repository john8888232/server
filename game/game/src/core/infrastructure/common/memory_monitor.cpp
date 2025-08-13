#include "memory_monitor.h"
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <execinfo.h>


// SafeSignalLogger 静态成员初始化
char SafeSignalLogger::buffer_[SafeSignalLogger::BUFFER_SIZE];

MemoryMonitor& MemoryMonitor::getInstance() {
    static MemoryMonitor instance;
    return instance;
}

void MemoryMonitor::trackAllocation(void* ptr, size_t size, const char* file, int line) {
    if (!enabled_ || !ptr) return;
    
    try {
        std::lock_guard<std::mutex> lock(allocations_mutex_);
        
        AllocationInfo info;
        info.size = size;
        info.timestamp = std::chrono::steady_clock::now();
        if (file) {
            info.file = file;
            info.line = line;
        }
        
        allocations_[ptr] = info;
        
        total_allocated_ += size;
        current_allocated_ += size;
        allocation_count_++;
        
        size_t current = current_allocated_.load();
        size_t peak = peak_allocated_.load();
        while (current > peak && !peak_allocated_.compare_exchange_weak(peak, current)) {
            // 自旋等待，直到成功更新peak值
        }
    } catch (...) {
        // 忽略异常，避免在内存监控中引起问题
    }
}

void MemoryMonitor::trackDeallocation(void* ptr) {
    if (!enabled_ || !ptr) return;
    
    try {
        std::lock_guard<std::mutex> lock(allocations_mutex_);
        
        auto it = allocations_.find(ptr);
        if (it != allocations_.end()) {
            current_allocated_ -= it->second.size;
            deallocation_count_++;
            allocations_.erase(it);
        }
    } catch (...) {
        // 忽略异常
    }
}

void MemoryMonitor::dumpMemoryLeaks() {
    if (!enabled_) return;
    
    try {
        std::lock_guard<std::mutex> lock(allocations_mutex_);
        
        if (allocations_.empty()) {
            printf("[MemoryMonitor] No memory leaks detected.\n");
            return;
        }
        
        printf("[MemoryMonitor] Memory leaks detected: %zu allocations\n", allocations_.size());
        printf("Total leaked memory: %zu bytes\n", current_allocated_.load());
        
        size_t count = 0;
        for (const auto& pair : allocations_) {
            const auto& info = pair.second;
            printf("  Leak #%zu: %p, size=%zu bytes", ++count, pair.first, info.size);
            if (!info.file.empty()) {
                printf(", at %s:%d", info.file.c_str(), info.line);
            }
            printf("\n");
            
            // 限制输出数量避免过多信息
            if (count >= 100) {
                printf("  ... and %zu more leaks\n", allocations_.size() - count);
                break;
            }
        }
    } catch (...) {
        printf("[MemoryMonitor] Error during memory leak dump\n");
    }
}

void MemoryMonitor::safeMemoryReport(int fd) {
    // 使用async-signal-safe函数
    SafeSignalLogger::writeString(fd, "[MemoryMonitor] Current allocated: ");
    SafeSignalLogger::writeNumber(fd, static_cast<long>(current_allocated_.load()));
    SafeSignalLogger::writeString(fd, " bytes\n");
    
    SafeSignalLogger::writeString(fd, "[MemoryMonitor] Peak allocated: ");
    SafeSignalLogger::writeNumber(fd, static_cast<long>(peak_allocated_.load()));
    SafeSignalLogger::writeString(fd, " bytes\n");
    
    SafeSignalLogger::writeString(fd, "[MemoryMonitor] Allocation count: ");
    SafeSignalLogger::writeNumber(fd, static_cast<long>(allocation_count_.load()));
    SafeSignalLogger::writeString(fd, "\n");
    
    SafeSignalLogger::writeString(fd, "[MemoryMonitor] Deallocation count: ");
    SafeSignalLogger::writeNumber(fd, static_cast<long>(deallocation_count_.load()));
    SafeSignalLogger::writeString(fd, "\n");
}

// SafeSignalLogger 实现
void SafeSignalLogger::logCrash(int signal, int fd) {
    writeString(fd, "\n=== CRASH DETECTED ===\n");
    writeString(fd, "Signal: ");
    writeNumber(fd, signal);
    writeString(fd, " (");
    
    switch (signal) {
        case SIGSEGV:
            writeString(fd, "SIGSEGV - Segmentation Fault");
            break;
        case SIGABRT:
            writeString(fd, "SIGABRT - Abort");
            break;
        case SIGFPE:
            writeString(fd, "SIGFPE - Floating Point Exception");
            break;
        case SIGILL:
            writeString(fd, "SIGILL - Illegal Instruction");
            break;
        default:
            writeString(fd, "Unknown Signal");
            break;
    }
    writeString(fd, ")\n");
    
    // 获取进程信息
    writeString(fd, "Process ID: ");
    writeNumber(fd, getpid());
    writeString(fd, "\n");
    
    // 获取资源使用情况
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        writeString(fd, "Max RSS: ");
        writeNumber(fd, usage.ru_maxrss);
        writeString(fd, " KB\n");
    }
    
    // 内存监控报告
    MemoryMonitor::getInstance().safeMemoryReport(fd);
    
    // 尝试获取简单的堆栈跟踪
    writeString(fd, "\nStack trace:\n");
    void* array[20];
    int size = backtrace(array, 20);
    backtrace_symbols_fd(array, size, fd);
    
    writeString(fd, "=== END CRASH REPORT ===\n\n");
}

void SafeSignalLogger::logMemoryCorruption(int fd) {
    writeString(fd, "\n=== MEMORY CORRUPTION DETECTED ===\n");
    writeString(fd, "This indicates heap corruption or double-free errors\n");
    
    // 内存监控报告
    MemoryMonitor::getInstance().safeMemoryReport(fd);
    
    writeString(fd, "=== END MEMORY CORRUPTION REPORT ===\n\n");
}

void SafeSignalLogger::writeString(int fd, const char* str) {
    if (str) {
        size_t len = strlen(str);
        write(fd, str, len);
    }
}

void SafeSignalLogger::writeNumber(int fd, long num) {
    char buffer[32];
    int i = 0;
    bool negative = false;
    
    if (num < 0) {
        negative = true;
        num = -num;
    }
    
    if (num == 0) {
        buffer[i++] = '0';
    } else {
        while (num > 0) {
            buffer[i++] = '0' + (num % 10);
            num /= 10;
        }
    }
    
    if (negative) {
        buffer[i++] = '-';
    }
    
    // 反转字符串
    for (int j = 0; j < i / 2; j++) {
        char temp = buffer[j];
        buffer[j] = buffer[i - 1 - j];
        buffer[i - 1 - j] = temp;
    }
    
    buffer[i] = '\0';
    writeString(fd, buffer);
}