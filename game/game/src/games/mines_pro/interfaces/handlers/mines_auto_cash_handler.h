#ifndef MINES_AUTO_CASH_HANDLER_H
#define MINES_AUTO_CASH_HANDLER_H

#include <string>
#include <functional>
#include <memory>
#include "core/interfaces/message_router.h"
#include "games/mines_pro/application/mines_game_service.h"

// 前向声明
class AppContext;

class MinesAutoCashHandler : public IMessageHandler {
public:
    using ResponseCallback = std::function<void(const std::string&, uint32_t, const std::string&)>;
    
    // 简化构造函数，使用依赖容器
    MinesAutoCashHandler(ResponseCallback responseCallback);
    
    // 实现IMessageHandler接口
    void handleMessage(const std::string& sessionId, const std::string& data) override;
    uint32_t getMsgId() const override;
    
private:
    bool initialize();
    void sendErrorResponse(const std::string& sessionId, int errorCode, const std::string& roundId);
    
    ResponseCallback responseCallback_;
    std::shared_ptr<MinesGameService> gameService_;
};

#endif // MINES_AUTO_CASH_HANDLER_H 