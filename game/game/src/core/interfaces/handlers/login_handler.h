#ifndef LOGIN_HANDLER_H
#define LOGIN_HANDLER_H

#include "../message_router.h"
#include "../../infrastructure/network/protocol.h"
#include "../../infrastructure/network/tcp_server.h"
#include <functional>
#include <memory>
#include "core/application/interfaces/i_user_service.h"
#include "core/domain/interfaces/i_game.h"

class LoginHandler : public IMessageHandler {
public:
    using ResponseCallback = std::function<void(const std::string&, uint32_t, const std::string&)>;
    
    LoginHandler(ResponseCallback responseCallback, std::shared_ptr<TcpServer> tcpServer);
    
    void handleMessage(const std::string& sessionId, const std::string& data) override;
    uint32_t getMsgId() const override;
    
private:
    // 处理玩家鉴权成功后的会话注册
        bool handlePlayerAuthenticated(const std::string& playerSessionId, 
                                   const std::string& loginname, std::shared_ptr<User> player, const std::string& gameType);
    
    // 发送游戏快照给客户端
    void sendGameSnapshot(const std::string& sessionId, std::shared_ptr<IGame> game);
                                  
    ResponseCallback responseCallback_;
    std::unique_ptr<IUserService> userService_;
    std::shared_ptr<TcpServer> tcpServer_;  // 使用智能指针管理生命周期
};

#endif // LOGIN_HANDLER_H 