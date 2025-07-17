#include "message_router.h"
#include <third_party/libuv_cpp/include/LogWriter.hpp>

MessageRouter::MessageRouter() {
}

MessageRouter::~MessageRouter() {
}

void MessageRouter::registerHandler(uint32_t msgId, MessageHandler handler) {
    std::lock_guard<std::mutex> lock(handlersMutex_);
    
    if (handlers_.find(msgId) != handlers_.end()) {
        LOG_WARN("Handler for message ID %u already exists, overwriting", msgId);
    }
    
    handlers_[msgId] = handler;
    LOG_DEBUG("Registered handler function for message ID %u", msgId);
}

void MessageRouter::unregisterHandler(uint32_t msgId) {
    std::lock_guard<std::mutex> lock(handlersMutex_);
    
    auto it = handlers_.find(msgId);
    if (it != handlers_.end()) {
        handlers_.erase(it);
        LOG_DEBUG("Unregistered handler function for message ID %u", msgId);
    } else {
        LOG_WARN("No handler function found for message ID %u", msgId);
    }
}

void MessageRouter::registerHandlerObject(std::shared_ptr<IMessageHandler> handler) {
    if (!handler) {
        LOG_ERROR("Attempted to register null handler object");
        return;
    }
    
    uint32_t msgId = handler->getMsgId();
    std::lock_guard<std::mutex> lock(handlersMutex_);
    
    if (handlerObjects_.find(msgId) != handlerObjects_.end()) {
        LOG_WARN("Handler object for message ID %u already exists, overwriting", msgId);
    }
    
    handlerObjects_[msgId] = handler;
    LOG_DEBUG("Registered handler object for message ID %u", msgId);
}

void MessageRouter::unregisterHandlerObject(uint32_t msgId) {
    std::lock_guard<std::mutex> lock(handlersMutex_);
    
    auto it = handlerObjects_.find(msgId);
    if (it != handlerObjects_.end()) {
        handlerObjects_.erase(it);
        LOG_DEBUG("Unregistered handler object for message ID %u", msgId);
    } else {
        LOG_WARN("No handler object found for message ID %u", msgId);
    }
}

void MessageRouter::routeMessage(const std::string& sessionId, uint32_t msgId, const std::string& data) {
        std::lock_guard<std::mutex> lock(handlersMutex_);
    
    // 首先检查是否有处理器对象
    auto objectIt = handlerObjects_.find(msgId);
    if (objectIt != handlerObjects_.end()) {
        try {
            LOG_DEBUG("Routing message ID %u from session %s to handler object", msgId, sessionId.c_str());
            objectIt->second->handleMessage(sessionId, data);
            return;
        } catch (const std::exception& e) {
            LOG_ERROR("Exception in message handler object for ID %u: %s", msgId, e.what());
            return;
        }
    }
    
    // 然后检查是否有处理器函数
    auto funcIt = handlers_.find(msgId);
    if (funcIt != handlers_.end()) {
    try {
            LOG_DEBUG("Routing message ID %u from session %s to handler function", msgId, sessionId.c_str());
            funcIt->second(sessionId, data);
            return;
    } catch (const std::exception& e) {
            LOG_ERROR("Exception in message handler function for ID %u: %s", msgId, e.what());
            return;
    }
    }
    
    LOG_ERROR("No handler registered for message ID %u", msgId);
}
