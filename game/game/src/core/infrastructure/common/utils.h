#ifndef UTILS_H
#define UTILS_H

#include <string>
#include <cstdint>
#include <chrono>
#include <functional>

namespace utils {

// 生成Gateway连接ID
std::string generateGatewayId();

// 生成32字节的Session ID
std::string generateSessionId(int64_t userId = 0);

// 格式化SessionID为固定32字节
std::string formatSessionId(const std::string& sessionId);

// 验证SessionID格式是否正确（32字节）
bool isValidSessionId(const std::string& sessionId);

// 生成随机字符串
std::string generateRandomString(size_t length);

// 生成时间戳字符串
std::string generateTimestamp();

// RAII风格的性能计时器
class PerformanceTimer {
public:
    enum class TimeUnit {
        NANOSECONDS,
        MICROSECONDS,
        MILLISECONDS,
        SECONDS
    };
    
    // 构造函数，可选择是否自动开始计时
    explicit PerformanceTimer(bool autoStart = true);
    
    // 带描述的构造函数，析构时自动输出日志
    PerformanceTimer(const std::string& description, TimeUnit unit = TimeUnit::MILLISECONDS, bool autoStart = true);
    
    // 析构函数，如果启用了自动日志输出，会记录耗时
    ~PerformanceTimer();
    
    // 手动开始计时
    void start();
    
    // 手动结束计时并返回耗时
    int64_t stop();
    
    // 获取当前耗时（不停止计时）
    int64_t elapsed() const;
    
    // 重置计时器
    void reset();
    
    // 设置时间单位
    void setTimeUnit(TimeUnit unit);
    
    // 设置描述（用于日志输出）
    void setDescription(const std::string& description);
    
    // 启用/禁用自动日志输出
    void setAutoLog(bool enable);
    
    // 设置自定义日志回调
    void setLogCallback(std::function<void(const std::string&, int64_t, TimeUnit)> callback);
    
    // 静态方法：执行一个函数并返回耗时
    template<typename Func>
    static int64_t measure(Func&& func, TimeUnit unit = TimeUnit::MILLISECONDS);
    
    // 静态方法：执行一个函数并输出耗时日志
    template<typename Func>
    static auto measureWithLog(const std::string& description, Func&& func, TimeUnit unit = TimeUnit::MILLISECONDS) -> decltype(func());

private:
    std::chrono::high_resolution_clock::time_point startTime_;
    std::chrono::high_resolution_clock::time_point endTime_;
    bool isRunning_;
    bool autoLog_;
    TimeUnit timeUnit_;
    std::string description_;
    std::function<void(const std::string&, int64_t, TimeUnit)> logCallback_;
    
    // 将时间转换为指定单位
    int64_t convertTime(const std::chrono::high_resolution_clock::duration& duration) const;
    
    // 获取时间单位的字符串表示
    static const char* getTimeUnitString(TimeUnit unit);
    
    // 默认日志输出函数
    static void defaultLogOutput(const std::string& description, int64_t duration, TimeUnit unit);
};

// 模板方法实现
template<typename Func>
int64_t PerformanceTimer::measure(Func&& func, TimeUnit unit) {
    PerformanceTimer timer(false);
    timer.setTimeUnit(unit);
    timer.start();
    func();
    return timer.stop();
}

template<typename Func>
auto PerformanceTimer::measureWithLog(const std::string& description, Func&& func, TimeUnit unit) -> decltype(func()) {
    PerformanceTimer timer(description, unit, true);
    return func();
}

} // namespace utils

#endif // UTILS_H