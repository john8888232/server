#ifndef MINES_BET_REPOSITORY_H
#define MINES_BET_REPOSITORY_H

#include <memory>
#include <string>
#include <vector>
#include "games/mines_pro/domain/models/mines_bet.h"

class MinesBetRepository {
public:
    MinesBetRepository();
    ~MinesBetRepository();
    
    // 保存投注记录
    bool saveBet(const std::shared_ptr<MinesBet>& bet);
    
    // 更新投注记录
    bool updateBet(const std::shared_ptr<MinesBet>& bet);
    
    // 根据ID查找投注记录
    std::shared_ptr<MinesBet> findById(const std::string& betId);
    
    // 根据用户ID查找投注记录
    std::vector<std::shared_ptr<MinesBet>> findByUserId(int64_t userId);
    
    // 根据游戏ID查找投注记录
    std::vector<std::shared_ptr<MinesBet>> findByGameId(const std::string& gameId);
    
    // 删除投注记录
    bool deleteBet(const std::string& betId);

private:
    // 这里可以添加数据库连接等私有成员
};

#endif // MINES_BET_REPOSITORY_H 