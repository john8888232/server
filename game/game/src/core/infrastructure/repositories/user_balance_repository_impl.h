#ifndef USER_BALANCE_REPOSITORY_IMPL_H
#define USER_BALANCE_REPOSITORY_IMPL_H

#include <memory>
#include <vector>
#include "core/domain/repositories/i_user_balance_repository.h"
#include "core/infrastructure/persistence/database_factory.h"

// 用户余额仓库MySQL实现
class UserBalanceRepositoryImpl : public IUserBalanceRepository {
public:
    // 构造函数 - 接收DatabaseFactory依赖
    explicit UserBalanceRepositoryImpl(std::shared_ptr<DatabaseFactory> dbFactory);
    
    ~UserBalanceRepositoryImpl() override;
    
    // 批量更新玩家余额
    bool updatePlayerBalancesBatch(
        const std::vector<PlayerBalanceUpdate>& updates, 
        std::vector<PlayerBalanceUpdateResult>& results) override;
    
    // 更新单个玩家的余额
    PlayerBalanceUpdateResult updatePlayerBalance(
        const PlayerBalanceUpdate& update) override;
    
private:
    // 单个玩家的余额更新（独立事务）
    bool updateSinglePlayerBalance(
        const PlayerBalanceUpdate& update,
        PlayerBalanceUpdateResult& result);
        
    // 数据库工厂
    std::shared_ptr<DatabaseFactory> dbFactory_;
};

#endif // USER_BALANCE_REPOSITORY_IMPL_H 