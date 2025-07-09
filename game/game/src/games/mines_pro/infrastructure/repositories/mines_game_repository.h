#ifndef MINES_GAME_REPOSITORY_H
#define MINES_GAME_REPOSITORY_H

#include <memory>
#include <string>
#include <vector>
#include "games/mines_pro/domain/models/mines_game.h"

// 玩家余额更新请求结构
struct PlayerBalanceUpdate {
    std::string loginName;
    double originalBalance;
    double newBalance;
    std::string reason;  // 更新原因，如"game_payout", "game_bet"等
};

// 玩家余额更新结果结构
struct PlayerBalanceUpdateResult {
    std::string loginName;
    double originalBalance;
    double newBalance;
    double actualBalance;  // 实际数据库中的余额
    bool success;
    std::string errorMessage;
};

class MinesGameRepository {
public:
    MinesGameRepository();
    virtual ~MinesGameRepository();
    
    // 保存游戏状态到数据库
    virtual bool saveGame(const MinesGame& game) = 0;
    
    // 根据用户ID加载活跃游戏
    virtual std::shared_ptr<MinesGame> loadActiveGame(int64_t userId) = 0;
    
    // 根据游戏ID加载游戏
    virtual std::shared_ptr<MinesGame> loadGame(const std::string& gameId) = 0;
    
    // 删除游戏记录
    virtual bool deleteGame(const std::string& gameId) = 0;
    
    // 更新游戏状态
    virtual bool updateGameStatus(const std::string& gameId, GameStatus status) = 0;
    
    // 获取用户的游戏历史
    virtual std::vector<std::shared_ptr<MinesGame>> getUserGameHistory(int64_t userId, int limit = 10) = 0;
    
    // 更新玩家余额
    virtual bool updatePlayerBalance(const std::string& loginName, double newBalance) = 0;
    
    // 获取玩家当前余额
    virtual double getPlayerBalance(const std::string& loginName) = 0;
    
    // 批量更新玩家余额
    virtual std::vector<PlayerBalanceUpdateResult> updatePlayerBalancesBatch(
        const std::vector<PlayerBalanceUpdate>& updates) = 0;
};

// MySQL实现
class MinesGameRepositoryImpl : public MinesGameRepository {
public:
    MinesGameRepositoryImpl();
    ~MinesGameRepositoryImpl() override;
    
    bool saveGame(const MinesGame& game) override;
    std::shared_ptr<MinesGame> loadActiveGame(int64_t userId) override;
    std::shared_ptr<MinesGame> loadGame(const std::string& gameId) override;
    bool deleteGame(const std::string& gameId) override;
    bool updateGameStatus(const std::string& gameId, GameStatus status) override;
    std::vector<std::shared_ptr<MinesGame>> getUserGameHistory(int64_t userId, int limit = 10) override;
    
    // 余额相关方法
    bool updatePlayerBalance(const std::string& loginName, double newBalance) override;
    double getPlayerBalance(const std::string& loginName) override;
    std::vector<PlayerBalanceUpdateResult> updatePlayerBalancesBatch(
        const std::vector<PlayerBalanceUpdate>& updates) override;
    
private:
    // 序列化游戏状态为JSON
    std::string serializeGame(const MinesGame& game);
    
    // 从JSON反序列化游戏状态
    std::shared_ptr<MinesGame> deserializeGame(const std::string& json);
};

#endif // MINES_GAME_REPOSITORY_H
