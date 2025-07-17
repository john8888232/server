#include "Logger_helper.h"
#include "third_party/libuv_cpp/include/LogWriter.hpp"

bool LoggerHelper::Initialize() {
    // 从配置中获取日志设置
    try{
        uv::LogWriter::LogConfig logConfig;
        return uv::LogWriter::Instance()->initialize(logConfig);
    } catch (const std::exception& e) {
        return false;
    }
}

void LoggerHelper::Cleanup() {
    uv::LogWriter::Instance()->shutdown();
}
