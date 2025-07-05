#ifndef I_GAME_SERVICE_H
#define I_GAME_SERVICE_H

#include <string>
#include <memory>
#include "core/domain/interfaces/i_game.h"

// 游戏服务的基础接口 - 只负责游戏的创建和销毁
class IGameService {
public:
    virtual ~IGameService() = default;
    
    // 创建游戏
    virtual std::shared_ptr<IGame> createGame() = 0;
    
    // 销毁游戏
    virtual bool destroyGame(std::shared_ptr<IGame> game) = 0;
};

#endif // I_GAME_SERVICE_H
