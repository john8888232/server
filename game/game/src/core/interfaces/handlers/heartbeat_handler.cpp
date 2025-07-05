#include "heartbeat_handler.h"
#include "../../infrastructure/proto/game.pb.h"
#include <third_party/libuv_cpp/include/LogWriter.hpp>

HeartbeatHandler::HeartbeatHandler(ResponseCallback responseCallback)
    : responseCallback_(std::move(responseCallback)) {
}

void HeartbeatHandler::handleMessage(const std::string& sessionId, const std::string& data) {
    LOG_DEBUG("Processing heartbeat from session %s", sessionId.c_str());
    
    try {
        proto::HeartBeat request;
        if (!request.ParseFromString(data)) {
            LOG_ERROR("Failed to parse heartbeat request from session %s", sessionId.c_str());
            return;
        }
        
        LOG_DEBUG("Received heartbeat from session %s, client timestamp: %lld", 
                 sessionId.c_str(), request.timestamp());
        
        proto::HeartBeat response;
        response.set_timestamp(request.timestamp());
        
        std::string responseData = response.SerializeAsString();
        responseCallback_(sessionId, Protocol::HEARTBEAT_PROTOCOL_ID, responseData);
        
        LOG_DEBUG("Sent heartbeat response to session %s, echoed timestamp: %lld", 
                 sessionId.c_str(), request.timestamp());
                 
    } catch (const std::exception& e) {
        LOG_ERROR("Exception processing heartbeat from session %s: %s", 
                 sessionId.c_str(), e.what());
    }
}

uint32_t HeartbeatHandler::getMsgId() const {
    return Protocol::HEARTBEAT_PROTOCOL_ID;
} 