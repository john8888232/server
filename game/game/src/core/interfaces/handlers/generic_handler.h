#ifndef GENERIC_HANDLER_H
#define GENERIC_HANDLER_H

#include "../message_router.h"
#include <functional>

class GenericHandler : public IMessageHandler {
public:
    using ResponseCallback = std::function<void(const std::string&, uint32_t, const std::string&)>;
    
    GenericHandler(uint32_t msgId, ResponseCallback responseCallback);
    
    void handleMessage(const std::string& sessionId, const std::string& data) override;
    uint32_t getMsgId() const override;
    
private:
    uint32_t msgId_;
    ResponseCallback responseCallback_;
};

#endif // GENERIC_HANDLER_H 