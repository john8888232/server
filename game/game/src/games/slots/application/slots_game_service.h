#ifndef SLOTS_GAME_SERVICE_H
#define SLOTS_GAME_SERVICE_H

#include "core/application/interfaces/i_game_service.h"

// 前向声明
class GameFactory;

class SlotsGameService : public IGameService {
public:
    // 构造函数接受factory参数
    explicit SlotsGameService(GameFactory* factory);
    virtual ~SlotsGameService();
    
    // 实现IGameService接口
    std::shared_ptr<IGame> createGame() override;
    bool destroyGame(std::shared_ptr<IGame> game) override;

private:
    GameFactory* factory_;  // 工厂实例，用于获取配置
};

#endif // SLOTS_GAME_SERVICE_H 