#ifndef MINES_GAME_REPOSITORY_H
#define MINES_GAME_REPOSITORY_H

#include <memory>
#include <string>
#include <vector>
#include <chrono>
#include "core/domain/repositories/i_game_repository.h"
#include "core/infrastructure/persistence/database_factory.h"
#include "games/mines_pro/application/dto/mines_game_result.h"
#include "core/domain/models/player_in_game.h"

// MySQL实现
class MinesGameRepositoryImpl : public IGameRepository {
public:
    MinesGameRepositoryImpl();
    ~MinesGameRepositoryImpl() override;
    
    // 开始游戏
    bool startGame(const std::string& roundID) override;

    // 保存游戏结果到数据库
    bool saveGame(std::shared_ptr<IGameResult> pGame) override;
    
    // 获取最近的游戏结果列表
    std::vector<std::shared_ptr<MinesGameResult>> getRecentGames(int limit);
    
    // 下注扣款 - change_amount为负数
    bool updatePlayerBet(int64_t playerId, double change_amount, const std::string& roundID, 
                        int32_t playtype, const std::string& playerIP, std::shared_ptr<PlayerInGame> playerInGame);
    
    // 派奖加款 - change_amount>0
    bool updatePlayerReckon(int64_t playerId, double change_amount, const std::string& roundID, 
                           int32_t playtype, double multiple, const std::string& playerIP, std::shared_ptr<PlayerInGame> playerInGame);
    
    // 取消订单 - change_amount>0
    bool updatePlayerCancel(int64_t playerId, double change_amount, const std::string& roundID, 
                           int32_t playtype, const std::string& playerIP, std::shared_ptr<PlayerInGame> playerInGame);
    
    // 批量结算失败订单 - 雷爆炸时使用
    // 返回值：>=0表示更新的订单数量，-1表示出现错误
    int64_t updateFailedOrders(const std::string& roundID);

private:
    // 数据库工厂
    std::shared_ptr<DatabaseFactory> dbFactory_;
};

#endif // MINES_GAME_REPOSITORY_H