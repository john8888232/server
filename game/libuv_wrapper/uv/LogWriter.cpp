#include "include/LogWriter.hpp"

#include <iostream>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <cstdio>
#include <thread>

// 针对不同平台的头文件
#ifdef _WIN32
    #include <direct.h>
    #include <windows.h>
    #define mkdir(path, mode) _mkdir(path)
    #define PATH_SEPARATOR '\\'
    // 为Windows定义S_ISDIR宏
    #ifndef S_ISDIR
        #define S_ISDIR(mode) (((mode) & S_IFMT) == S_IFDIR)
    #endif
    #ifndef S_IFDIR
        #define S_IFDIR _S_IFDIR
    #endif
    #ifndef S_IFMT
        #define S_IFMT _S_IFMT
    #endif
#else
    #include <sys/stat.h>
    #include <unistd.h>
    #define PATH_SEPARATOR '/'
#endif

#include <errno.h>
#include <string.h>

using namespace uv;

LogWriter* LogWriter::Instance()
{
    static LogWriter single;
    return &single;
}

void uv::LogWriter::registerInterface(WriteLogCallback callback)
{
    std::lock_guard<std::mutex> lock(logMutex_);
    callback_ = callback;
}

bool uv::LogWriter::initialize(const LogConfig& config)
{
    std::lock_guard<std::mutex> lock(logMutex_);
    config_ = config;
    level_ = static_cast<int>(config.level);
    
    // Create log directory if it doesn't exist
    try {
        // 使用传统方式创建目录
        createDirectories(config_.logDir);
    }
    catch (const std::exception& e) {
        std::cerr << "Failed to create log directory: " << e.what() << std::endl;
        return false;
    }
    
    currentDate_ = getCurrentDateTime(true);
    initialized_ = openLogFile();
    
    // 启动异步日志线程
    if (config_.asyncMode && initialized_) {
        running_ = true;
        asyncThread_ = std::thread(&LogWriter::asyncLoggingThread, this);
    }
    
    return initialized_;
}

// 使用传统方式创建目录
void uv::LogWriter::createDirectories(const std::string& dirPath) {
    // 检查目录是否存在
    struct stat st;
    if (stat(dirPath.c_str(), &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            return; // 目录已存在
        }
    }
    
    // 递归创建目录
    size_t pos = 0;
    std::string dir;
    
    #ifdef _WIN32
    // Windows路径可能以驱动器号开头 (C:\)
    if (dirPath.length() >= 2 && dirPath[1] == ':') {
        dir = dirPath.substr(0, 2) + PATH_SEPARATOR;
        pos = 3;
    }
    #else
    // Unix路径可能以/开头
    if (dirPath[0] == PATH_SEPARATOR) {
        dir = PATH_SEPARATOR;
        pos = 1;
    }
    #endif
    
    while ((pos = dirPath.find_first_of("/\\", pos)) != std::string::npos) {
        dir = dirPath.substr(0, pos++);
        if (dir.empty()) continue;
        
        if (mkdir(dir.c_str(), 0755) != 0 && errno != EEXIST) {
            throw std::runtime_error("can't create directory: " + dir + ", error: " + strerror(errno));
        }
    }
    
    // 创建最终目录
    if (dirPath.back() != PATH_SEPARATOR && mkdir(dirPath.c_str(), 0755) != 0 && errno != EEXIST) {
        throw std::runtime_error("can't create directory: " + dirPath + ", error: " + strerror(errno));
    }
}

void uv::LogWriter::shutdown()
{
    if (config_.asyncMode && running_) {
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            running_ = false;
        }
        queueCondition_.notify_one();
        
        if (asyncThread_.joinable()) {
            asyncThread_.join();
        }
        
        // 处理剩余的日志
        processLogQueue();
    }
    
    if (logFile_.is_open()) {
        logFile_.close();
    }
}

void uv::LogWriter::asyncLoggingThread()
{
    while (running_) {
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            queueCondition_.wait_for(lock, std::chrono::seconds(1), 
                [this] { return !running_ || !logQueue_.empty(); });
            
            if (!running_ && logQueue_.empty()) {
                break;
            }
        }
        
        processLogQueue();
    }
}

void uv::LogWriter::processLogQueue()
{
    std::vector<LogMessage> messagesToProcess;
    
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        while (!logQueue_.empty() && messagesToProcess.size() < 100) {
            messagesToProcess.push_back(std::move(logQueue_.front()));
            logQueue_.pop();
        }
    }
    
    if (!messagesToProcess.empty()) {
        std::lock_guard<std::mutex> lock(logMutex_);
        for (const auto& msg : messagesToProcess) {
            writeLogToTargets(msg);
        }
    }
}

void uv::LogWriter::writeLogToTargets(const LogMessage& msg)
{
    // 获取线程ID的短版本（只取最后几位）
    std::string shortThreadId = msg.threadId;
    if (shortThreadId.length() > 4) {
        shortThreadId = shortThreadId.substr(shortThreadId.length() - 4);
    }
    
    // 从文件路径中提取文件名
    std::string fileName = msg.file;
    size_t pos = fileName.find_last_of("/\\");
    if (pos != std::string::npos) {
        fileName = fileName.substr(pos + 1);
    }
    
    // 构造完整日志消息
    std::string logMessage = "[" + msg.timestamp + "]";
    
    // 添加文件和行号信息
    if (!fileName.empty()) {
        logMessage += "[" + fileName + ":" + std::to_string(msg.line) + "]";
    }
    
    // 添加线程ID和级别信息
    logMessage += "[" + shortThreadId + "][" + getLevelName(msg.level) + "] " + msg.content;
    
    // 写入回调
    if (callback_) {
        callback_(msg.level, msg.content);
    }
    
    // 写入控制台
    if (config_.enableConsole) {
        std::cout << logMessage << std::endl;
    }
    
    // 写入文件
    if (initialized_ && logFile_.is_open()) {
        checkRotation();
        
        logFile_ << logMessage << std::endl;
        logFile_.flush();
        currentFileSize_ += logMessage.length() + 1; // +1 for newline
    }
}

bool uv::LogWriter::openLogFile()
{
    // Close existing file if open
    if (logFile_.is_open()) {
        logFile_.close();
    }
    
    // Create filename with date: logs/app_YYYY-MM-DD.log
    currentLogFileName_ = config_.logDir;
    if (currentLogFileName_.back() != PATH_SEPARATOR) {
        currentLogFileName_ += PATH_SEPARATOR;
    }
    currentLogFileName_ += "app_" + currentDate_ + ".log";
    
    // Open file for append
    logFile_.open(currentLogFileName_, std::ios::app);
    if (!logFile_.is_open()) {
        std::cerr << "Failed to open log file: " << currentLogFileName_ << std::endl;
        return false;
    }
    
    // Get current file size
    logFile_.seekp(0, std::ios::end);
    currentFileSize_ = static_cast<size_t>(logFile_.tellp());
    
    return true;
}

void uv::LogWriter::checkRotation()
{
    // Check if date has changed
    std::string today = getCurrentDateTime(true);
    if (today != currentDate_) {
        currentDate_ = today;
        openLogFile();
        currentFileSize_ = 0;
        return;
    }
    
    // Check file size
    if (currentFileSize_ >= config_.maxFileSize) {
        // Create a new file with timestamp
        std::string timestamp = getCurrentDateTime(false);
        timestamp.replace(timestamp.find(' '), 1, "_");
        timestamp.replace(timestamp.find(':'), 1, "-");
        timestamp.replace(timestamp.find(':'), 1, "-");
        
        std::string newFileName = config_.logDir;
        if (newFileName.back() != PATH_SEPARATOR) {
            newFileName += PATH_SEPARATOR;
        }
        newFileName += "app_" + currentDate_ + "_" + timestamp + ".log";
        logFile_.close();
        
        // Rename current file using rename function
        if (rename(currentLogFileName_.c_str(), newFileName.c_str()) != 0) {
            std::cerr << "Failed to rename log file: " << strerror(errno) << std::endl;
        }
        
        // Open new file
        openLogFile();
        currentFileSize_ = 0;
    }
}

std::string uv::LogWriter::getCurrentDateTime(bool forFileName)
{
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    
    if (forFileName) {
        // Format: YYYY-MM-DD
        std::tm tm = *std::localtime(&time);
        ss << std::put_time(&tm, "%Y-%m-%d");
    }
    else {
        // Format: YYYY-MM-DD HH:MM:SS.mmm
        std::tm tm = *std::localtime(&time);
        ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
        
        // 添加毫秒
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;
        ss << "." << std::setfill('0') << std::setw(3) << ms.count();
    }
    
    return ss.str();
}

std::string uv::LogWriter::getThreadId()
{
    std::stringstream ss;
    ss << std::this_thread::get_id();
    return ss.str();
}

std::string uv::LogWriter::formatMessage(const char* format, va_list args)
{
    // First try with a reasonable buffer size
    const int initialSize = 256;
    char buffer[initialSize];
    
    va_list argsCopy;
    va_copy(argsCopy, args);
    int result = vsnprintf(buffer, initialSize, format, args);
    
    std::string formattedMsg;
    
    if (result < 0) {
        // Error in formatting
        formattedMsg = "Error formatting log message";
    }
    else if (result < initialSize) {
        // It fit, return the string
        formattedMsg = buffer;
    }
    else {
        // It didn't fit, allocate a bigger buffer
        size_t size = result + 1;  // +1 for null terminator
        std::vector<char> bigBuffer(size);
        result = vsnprintf(bigBuffer.data(), size, format, argsCopy);
        
        if (result >= 0 && result < static_cast<int>(size)) {
            formattedMsg = bigBuffer.data();
        }
        else {
            formattedMsg = "Error formatting log message (buffer overflow)";
        }
    }
    
    va_end(argsCopy);
    return formattedMsg;
}

void uv::LogWriter::ToHex(std::string& message, const char* data, unsigned int size)
{
    for (unsigned int i = 0; i < size; i++)
    {
        char buf[8];
        std::sprintf(buf, " 0x%x ", (unsigned char)data[i]);
        message.append(buf);
    }
}

void uv::LogWriter::ToHex(std::string& message, std::string& data)
{
    ToHex(message, data.c_str(), (unsigned)(data.size()));
}

void uv::LogWriter::write(Level level, const std::string& file, int line, const std::string& data)
{
    if ((level < LevelSize) && (level >= static_cast<Level>(level_)) && (level >= Debug))
    {
        std::string timestamp = getCurrentDateTime(false);
        std::string threadId = getThreadId();
        
        if (config_.asyncMode) {
            // 创建日志消息并加入队列
            LogMessage msg(level, threadId, data, timestamp, file, line);
            
            {
                std::lock_guard<std::mutex> lock(queueMutex_);
                // 检查队列大小，避免内存溢出
                if (logQueue_.size() < config_.maxQueueSize) {
                    logQueue_.push(std::move(msg));
                }
            }
            queueCondition_.notify_one();
        }
        else {
            // 同步模式
            std::lock_guard<std::mutex> lock(logMutex_);
            LogMessage msg(level, threadId, data, timestamp, file, line);
            writeLogToTargets(msg);
        }
    }
}

void uv::LogWriter::write(Level level, const std::string& file, int line, const std::string&& data)
{
    write(level, file, line, data);
}

void uv::LogWriter::write(Level level, const std::string& file, int line, const char* format, ...)
{
    if ((level < LevelSize) && (level >= static_cast<Level>(level_)) && (level >= Debug))
    {
        va_list args;
        va_start(args, format);
        std::string formattedMsg = formatMessage(format, args);
        va_end(args);
        
        write(level, file, line, formattedMsg);
    }
}

void uv::LogWriter::fatal(const std::string& file, int line, const std::string& data)
{
    write(Level::Fatal, file, line, data);
}

void uv::LogWriter::fatal(const std::string& file, int line, const std::string&& data)
{
    write(Level::Fatal, file, line, data);
}

void uv::LogWriter::fatal(const std::string& file, int line, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    std::string formattedMsg = formatMessage(format, args);
    va_end(args);
    
    write(Level::Fatal, file, line, formattedMsg);
}

void uv::LogWriter::warn(const std::string& file, int line, const std::string& data)
{
    write(Level::Warn, file, line, data);
}

void uv::LogWriter::warn(const std::string& file, int line, const std::string&& data)
{
    write(Level::Warn, file, line, data);
}

void uv::LogWriter::warn(const std::string& file, int line, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    std::string formattedMsg = formatMessage(format, args);
    va_end(args);
    
    write(Level::Warn, file, line, formattedMsg);
}

void uv::LogWriter::error(const std::string& file, int line, const std::string& data)
{
    write(Level::Error, file, line, data);
}

void uv::LogWriter::error(const std::string& file, int line, const std::string&& data)
{
    write(Level::Error, file, line, data);
}

void uv::LogWriter::error(const std::string& file, int line, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    std::string formattedMsg = formatMessage(format, args);
    va_end(args);
    
    write(Level::Error, file, line, formattedMsg);
}

void uv::LogWriter::info(const std::string& file, int line, const std::string& data)
{
    write(Level::Info, file, line, data);
}

void uv::LogWriter::info(const std::string& file, int line, const std::string&& data)
{
    write(Level::Info, file, line, data);
}

void uv::LogWriter::info(const std::string& file, int line, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    std::string formattedMsg = formatMessage(format, args);
    va_end(args);
    
    write(Level::Info, file, line, formattedMsg);
}

void uv::LogWriter::debug(const std::string& file, int line, const std::string& data)
{
    write(Level::Debug, file, line, data);
}

void uv::LogWriter::debug(const std::string& file, int line, const std::string&& data)
{
    write(Level::Debug, file, line, data);
}

void uv::LogWriter::debug(const std::string& file, int line, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    std::string formattedMsg = formatMessage(format, args);
    va_end(args);
    
    write(Level::Debug, file, line, formattedMsg);
}

void uv::LogWriter::setLevel(int level)
{
    std::lock_guard<std::mutex> lock(logMutex_);
    level_ = level;
}

int uv::LogWriter::getLevel()
{
    return level_;
}

const std::string& uv::LogWriter::getLevelName(int level)
{
    if (level >= 0 && level < static_cast<int>(levelStr_.size()))
    {
        return levelStr_[level];
    }
    return nullLevel_;
}

LogWriter::LogWriter()
    :callback_(nullptr),
    level_(0),
    currentFileSize_(0),
    initialized_(false),
    running_(false)
{
    levelStr_.resize(Level::LevelSize);
    levelStr_[Level::Debug] = "Debug";
    levelStr_[Level::Info] = "Info";
    levelStr_[Level::Warn] = "Warn";
    levelStr_[Level::Error] = "Error";
    levelStr_[Level::Fatal] = "Fatal";

    nullLevel_ = "NullLevel";
    
    // Default configuration
    config_.logDir = "./logs";
    config_.level = Level::Info;
    config_.maxFileSize = 10 * 1024 * 1024; // 10MB
    config_.enableConsole = true;
    config_.asyncMode = true;
    config_.maxQueueSize = 10000;
}

LogWriter::~LogWriter()
{
    shutdown();
}