#ifndef HEARTBEAT_HANDLER_H
#define HEARTBEAT_HANDLER_H

#include "core/interfaces/message_router.h"
#include "core/infrastructure/network/protocol.h"
#include <functional>

class HeartbeatHandler : public IMessageHandler {
public:
    using ResponseCallback = std::function<void(const std::string&, uint32_t, const std::string&)>;
    
    explicit HeartbeatHandler(ResponseCallback responseCallback);
    
    void handleMessage(const std::string& sessionId, const std::string& data) override;
    uint32_t getMsgId() const override;
    
private:
    ResponseCallback responseCallback_;
};

#endif // HEARTBEAT_HANDLER_H 