#include "utils.h"
#include <random>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <cstring>
#include <atomic>
#include <cmath>
#include "third_party/libuv_cpp/include/LogWriter.hpp"
#include <openssl/evp.h>
#include <openssl/sha.h>

namespace utils {

std::string generateGatewayId() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    
    std::stringstream ss;
    ss << "gw_";
    
    // 添加时间戳
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    ss << std::hex << timestamp << "_";
    
    // 添加随机字符
    for (int i = 0; i < 8; ++i) {
        ss << std::hex << dis(gen);
    }
    
    return ss.str();
}

std::string generateSessionId(int64_t userId) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    
    std::stringstream ss;
    
    // 前8字节：用户ID（16进制）
    ss << std::hex << std::setfill('0') << std::setw(16) << userId;
    
    // 后面填充时间戳和随机数
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
    ss << std::hex << std::setfill('0') << std::setw(16) << timestamp;
    
    std::string sessionId = ss.str();
    
    // 确保是32字节，不足的用随机字符填充
    while (sessionId.length() < 32) {
        sessionId += generateRandomString(1);
    }
    
    // 截断到32字节
    sessionId = sessionId.substr(0, 32);
    
    return sessionId;
}

std::string formatSessionId(const std::string& sessionId) {
    if (sessionId.length() >= 32) {
        return sessionId.substr(0, 32);
    }
    
    std::string formatted = sessionId;
    // 不足32字节的用0填充
    while (formatted.length() < 32) {
        formatted += '0';
    }
    
    return formatted;
}

bool isValidSessionId(const std::string& sessionId) {
    return sessionId.length() == 32;
}

std::string generateRandomString(size_t length) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    
    std::stringstream ss;
    for (size_t i = 0; i < length; ++i) {
        ss << std::hex << dis(gen);
    }
    
    return ss.str();
}

std::string generateTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    
    std::stringstream ss;
    ss << std::hex << timestamp;
    return ss.str();
}

std::string generateOrderId() {
    static std::atomic<uint64_t> orderSequence_{0};
    
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    
    // 雪花算法简化版本
    uint64_t timestamp = static_cast<uint64_t>(millis);
    uint64_t machineId = 1; // 固定机器ID
    uint64_t seq = orderSequence_.fetch_add(1) & 0xFFF; // 12位序列号
    
    uint64_t id = (timestamp << 22) | (machineId << 12) | seq;
    
    return std::to_string(id);
}

std::string generateTransId() {
    static std::atomic<uint64_t> transSequence_{0};
    
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    
    uint64_t timestamp = static_cast<uint64_t>(millis);
    uint64_t machineId = 2; // 交易ID使用不同的机器ID
    uint64_t seq = transSequence_.fetch_add(1) & 0xFFF;
    
    uint64_t id = (timestamp << 22) | (machineId << 12) | seq;
    
    return std::to_string(id);
}

double roundToTwoDecimals(double value) {
    return std::round(value * 100.0) / 100.0;
}

// PerformanceTimer 实现
PerformanceTimer::PerformanceTimer(bool autoStart) 
    : isRunning_(false), autoLog_(false), timeUnit_(TimeUnit::MILLISECONDS) {
    if (autoStart) {
        start();
    }
}

PerformanceTimer::PerformanceTimer(const std::string& description, TimeUnit unit, bool autoStart)
    : isRunning_(false), autoLog_(true), timeUnit_(unit), description_(description) {
    if (autoStart) {
        start();
    }
}

PerformanceTimer::~PerformanceTimer() {
    if (autoLog_ && isRunning_) {
        int64_t duration = elapsed();
        if (logCallback_) {
            logCallback_(description_, duration, timeUnit_);
        } else {
            defaultLogOutput(description_, duration, timeUnit_);
        }
    }
}

void PerformanceTimer::start() {
    startTime_ = std::chrono::high_resolution_clock::now();
    isRunning_ = true;
}

int64_t PerformanceTimer::stop() {
    if (!isRunning_) {
        return 0;
    }
    
    endTime_ = std::chrono::high_resolution_clock::now();
    isRunning_ = false;
    
    auto duration = endTime_ - startTime_;
    return convertTime(duration);
}

int64_t PerformanceTimer::elapsed() const {
    if (!isRunning_) {
        auto duration = endTime_ - startTime_;
        return convertTime(duration);
    }
    
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = now - startTime_;
    return convertTime(duration);
}

void PerformanceTimer::reset() {
    isRunning_ = false;
    startTime_ = std::chrono::high_resolution_clock::time_point{};
    endTime_ = std::chrono::high_resolution_clock::time_point{};
}

void PerformanceTimer::setTimeUnit(TimeUnit unit) {
    timeUnit_ = unit;
}

void PerformanceTimer::setDescription(const std::string& description) {
    description_ = description;
}

void PerformanceTimer::setAutoLog(bool enable) {
    autoLog_ = enable;
}

void PerformanceTimer::setLogCallback(std::function<void(const std::string&, int64_t, TimeUnit)> callback) {
    logCallback_ = callback;
}

int64_t PerformanceTimer::convertTime(const std::chrono::high_resolution_clock::duration& duration) const {
    switch (timeUnit_) {
        case TimeUnit::NANOSECONDS:
            return std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
        case TimeUnit::MICROSECONDS:
            return std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
        case TimeUnit::MILLISECONDS:
            return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
        case TimeUnit::SECONDS:
            return std::chrono::duration_cast<std::chrono::seconds>(duration).count();
        default:
            return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    }
}

const char* PerformanceTimer::getTimeUnitString(TimeUnit unit) {
    switch (unit) {
        case TimeUnit::NANOSECONDS: return "ns";
        case TimeUnit::MICROSECONDS: return "μs";
        case TimeUnit::MILLISECONDS: return "ms";
        case TimeUnit::SECONDS: return "s";
        default: return "ms";
    }
}

void PerformanceTimer::defaultLogOutput(const std::string& description, int64_t duration, TimeUnit unit) {
    if (!description.empty()) {
        LOG_DEBUG("%s completed in %ld%s", description.c_str(), duration, getTimeUnitString(unit));
    }
}

std::string sha256(const std::string& input) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    
    // 使用OpenSSL的EVP接口计算SHA256哈希值
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (ctx == nullptr) {
        LOG_ERROR("Failed to create EVP_MD_CTX");
        return "";
    }
    
    if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1) {
        LOG_ERROR("Failed to initialize SHA256 digest");
        EVP_MD_CTX_free(ctx);
        return "";
    }
    
    if (EVP_DigestUpdate(ctx, input.c_str(), input.length()) != 1) {
        LOG_ERROR("Failed to update SHA256 digest");
        EVP_MD_CTX_free(ctx);
        return "";
    }
    
    unsigned int len = SHA256_DIGEST_LENGTH;
    if (EVP_DigestFinal_ex(ctx, hash, &len) != 1) {
        LOG_ERROR("Failed to finalize SHA256 digest");
        EVP_MD_CTX_free(ctx);
        return "";
    }
    
    EVP_MD_CTX_free(ctx);
    
    // 将哈希值转换为十六进制字符串
    std::stringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    
    return ss.str();
}

} // namespace utils 