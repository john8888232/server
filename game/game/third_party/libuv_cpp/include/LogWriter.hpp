#ifndef     UV_LOG_INTERFACE_HPP
#define     UV_LOG_INTERFACE_HPP

#include <string>
#include <functional>
#include <vector>
#include <fstream>
#include <cstdarg>
#include <mutex>
#include <memory>
#include <queue>
#include <thread>
#include <condition_variable>
#include <atomic>

#define   USED_STD_OUT     1

// 日志宏定义
#define LOG_DEBUG(format, ...) uv::LogWriter::Instance()->debug(__FILE__, __LINE__, format, ##__VA_ARGS__)
#define LOG_INFO(format, ...) uv::LogWriter::Instance()->info(__FILE__, __LINE__, format, ##__VA_ARGS__)
#define LOG_WARN(format, ...) uv::LogWriter::Instance()->warn(__FILE__, __LINE__, format, ##__VA_ARGS__)
#define LOG_ERROR(format, ...) uv::LogWriter::Instance()->error(__FILE__, __LINE__, format, ##__VA_ARGS__)
#define LOG_FATAL(format, ...) uv::LogWriter::Instance()->fatal(__FILE__, __LINE__, format, ##__VA_ARGS__)

namespace uv
{

// 日志消息结构
struct LogMessage {
    int level;
    std::string threadId;
    std::string content;
    std::string timestamp;
    std::string file;
    int line;
    
    LogMessage() : level(0), line(0) {}
    
    LogMessage(int lvl, const std::string& tid, const std::string& msg, const std::string& time, 
               const std::string& fileName = "", int lineNum = 0)
        : level(lvl), threadId(tid), content(msg), timestamp(time), file(fileName), line(lineNum) {}
};

class LogWriter
{
public:
    using WriteLogCallback = std::function<void(int,const std::string&)>;

    enum Level{
        Debug = 0,
        Info,
        Warn,
        Error,
        Fatal,
        LevelSize
    };

    struct LogConfig {
        std::string logDir;           // Directory to store log files
        Level level;                  // Minimum log level
        size_t maxFileSize;           // Max file size before rotation (in bytes)
        bool enableConsole;           // Whether to also output to console
        bool asyncMode;               // Whether to use async logging
        size_t maxQueueSize;          // Maximum size of log queue in async mode
        
        LogConfig() : 
            logDir("./logs"),
            level(Info),
            maxFileSize(10 * 1024 * 1024),  // 10MB default
            enableConsole(true),
            asyncMode(true),
            maxQueueSize(10000) {}
    };

    static LogWriter* Instance();
    static void ToHex(std::string& message, const char* data, unsigned int size);
    static void ToHex(std::string& message, std::string& data);

public:
    bool initialize(const LogConfig& config);
    void registerInterface(WriteLogCallback callback);

    void write(Level level, const std::string& file, int line, const std::string& data);
    void write(Level level, const std::string& file, int line, const std::string&& data);
    void write(Level level, const std::string& file, int line, const char* format, ...);

    void fatal(const std::string& file, int line, const std::string& data);
    void fatal(const std::string& file, int line, const std::string&& data);
    void fatal(const std::string& file, int line, const char* format, ...);

    void error(const std::string& file, int line, const std::string& data);
    void error(const std::string& file, int line, const std::string&& data);
    void error(const std::string& file, int line, const char* format, ...);

    void warn(const std::string& file, int line, const std::string& data);
    void warn(const std::string& file, int line, const std::string&& data);
    void warn(const std::string& file, int line, const char* format, ...);

    void info(const std::string& file, int line, const std::string& data);
    void info(const std::string& file, int line, const std::string&& data);
    void info(const std::string& file, int line, const char* format, ...);

    void debug(const std::string& file, int line, const std::string& data);
    void debug(const std::string& file, int line, const std::string&& data);
    void debug(const std::string& file, int line, const char* format, ...);

    void setLevel(int level);
    int getLevel();
    const std::string& getLevelName(int level);
    
    // 停止异步日志线程并清空队列
    void shutdown();

private:
    LogWriter();
    ~LogWriter();

    bool openLogFile();
    void checkRotation();
    std::string formatMessage(const char* format, va_list args);
    std::string getCurrentDateTime(bool forFileName = false);
    std::string getThreadId();
    
    // 创建目录
    void createDirectories(const std::string& dirPath);
    
    // 异步日志处理函数
    void asyncLoggingThread();
    void processLogQueue();
    void writeLogToTargets(const LogMessage& msg);

    WriteLogCallback callback_;
    int level_;
    std::vector<std::string> levelStr_;
    std::string nullLevel_;
    
    // New members for file logging
    LogConfig config_;
    std::ofstream logFile_;
    std::string currentLogFileName_;
    size_t currentFileSize_;
    std::string currentDate_;
    std::mutex logMutex_;
    bool initialized_;
    
    // 异步日志相关成员
    std::atomic<bool> running_;
    std::queue<LogMessage> logQueue_;
    std::mutex queueMutex_;
    std::condition_variable queueCondition_;
    std::thread asyncThread_;
};

}
#endif 

