#ifndef DISCONNECT_HANDLER_H
#define DISCONNECT_HANDLER_H

#include "core/interfaces/message_router.h"
#include "core/infrastructure/network/tcp_server.h"
#include <functional>
#include <string>
#include <memory>

// 客户端断线通知处理器 - 处理网关发送的客户端断线通知 (0x10001)
class DisconnectHandler : public IMessageHandler {
public:
    using ResponseCallback = std::function<void(const std::string&, uint32_t, const std::string&)>;
    
    explicit DisconnectHandler(ResponseCallback responseCallback, std::shared_ptr<TcpServer> tcpServer);
    virtual ~DisconnectHandler();
    
    // IMessageHandler 接口实现
    void handleMessage(const std::string& sessionId, const std::string& data) override;
    uint32_t getMsgId() const override;

private:
    ResponseCallback responseCallback_;
    std::shared_ptr<TcpServer> tcpServer_;

    void immediateCleanupPlayerSession(const std::string& sessionId);
};

#endif // DISCONNECT_HANDLER_H 