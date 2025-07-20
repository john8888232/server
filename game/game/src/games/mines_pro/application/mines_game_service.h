#ifndef MINES_GAME_SERVICE_H
#define MINES_GAME_SERVICE_H

#include <memory>
#include <string>
#include "core/application/interfaces/i_game_service.h"
#include "games/mines_pro/domain/models/mines_game.h"
#include "core/infrastructure/common/app_context.h"
#include "core/infrastructure/persistence/database_factory.h"


// 前向声明
class GameFactory;

class MinesGameService : public IGameService {
public:
    // 构造函数只接受factory参数，其他依赖从容器中获取
    MinesGameService(GameFactory* factory);
    virtual ~MinesGameService();
    
    // IGameService接口实现
    std::shared_ptr<IGame> createGame() override;
    bool destroyGame(std::shared_ptr<IGame> game) override;
    
    // 停止所有该类型的游戏实例
    void stopAllGames();
    
    // MinesGameService特有的方法
    std::pair<bool, std::string> startGame(const std::string& sessionId, int64_t userId);
    
    std::shared_ptr<MinesGame> getUserGame(int64_t userId);
    std::shared_ptr<MinesGame> getGameByRoundId(const std::string& roundId);
    std::shared_ptr<MinesGame> getCurrentGame();  // 获取当前活跃的游戏实例

private:
    GameFactory* factory_;  // 工厂实例，用于获取配置
    std::shared_ptr<AppContext> appContext_;
    std::shared_ptr<DatabaseFactory> dbFactory_;
};

#endif // MINES_GAME_SERVICE_H
