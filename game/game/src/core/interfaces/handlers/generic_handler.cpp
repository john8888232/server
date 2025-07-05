#include "generic_handler.h"
#include <third_party/libuv_cpp/include/LogWriter.hpp>
#include <chrono>
#include <thread>

GenericHandler::GenericHandler(uint32_t msgId, ResponseCallback responseCallback)
    : msgId_(msgId), responseCallback_(std::move(responseCallback)) {
}

void GenericHandler::handleMessage(const std::string& sessionId, const std::string& data) {
    LOG_DEBUG("Processing generic message ID 0x%x from session %s", msgId_, sessionId.c_str());
    
    // 模拟一些业务处理
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // 简单回显数据
    responseCallback_(sessionId, msgId_, data);
    
    LOG_DEBUG("Generic message ID 0x%x processed for session %s", msgId_, sessionId.c_str());
}

uint32_t GenericHandler::getMsgId() const {
    return msgId_;
} 