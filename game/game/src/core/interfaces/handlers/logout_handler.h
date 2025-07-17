#ifndef LOGOUT_HANDLER_H
#define LOGOUT_HANDLER_H

#include "../message_router.h"
#include "../../infrastructure/network/protocol.h"
#include "../../infrastructure/network/tcp_server.h"
#include <functional>
#include <memory>

class LogoutHandler : public IMessageHandler {
public:
    using ResponseCallback = std::function<void(const std::string&, uint32_t, const std::string&)>;
    
    LogoutHandler(ResponseCallback responseCallback, std::shared_ptr<TcpServer> tcpServer);
    
    void handleMessage(const std::string& sessionId, const std::string& data) override;
    uint32_t getMsgId() const override;
    
private:
    // 检查玩家是否可以退出游戏
    bool canPlayerLogout(const std::string& loginname);
    
    // 清理玩家相关的映射关系
    void cleanupPlayerMappings(const std::string& sessionId, const std::string& loginname);
                                  
    ResponseCallback responseCallback_;
    std::shared_ptr<TcpServer> tcpServer_;
};

#endif // LOGOUT_HANDLER_H 