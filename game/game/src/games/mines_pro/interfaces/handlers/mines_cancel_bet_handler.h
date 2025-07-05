#ifndef MINES_CANCEL_BET_HANDLER_H
#define MINES_CANCEL_BET_HANDLER_H

#include "core/interfaces/message_router.h"
#include "core/infrastructure/network/protocol.h"
#include "games/mines_pro/application/mines_game_service.h"
#include <functional>
#include <memory>

class MinesCancelBetHandler : public IMessageHandler {
public:
    using ResponseCallback = std::function<void(const std::string&, uint32_t, const std::string&)>;
    
    MinesCancelBetHandler(ResponseCallback responseCallback);
    
    void handleMessage(const std::string& sessionId, const std::string& data) override;
    uint32_t getMsgId() const override;
    
private:
    bool initialize();
    void sendErrorResponse(const std::string& sessionId, int errorCode, const std::string& roundId, double balance, double refund = 0.0);
    
    ResponseCallback responseCallback_;
    std::shared_ptr<MinesGameService> gameService_;
};

#endif // MINES_CANCEL_BET_HANDLER_H 