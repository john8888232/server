#ifndef USER_BALANCE_SERVICE_H
#define USER_BALANCE_SERVICE_H

#include <memory>
#include <string>
#include <vector>
#include "core/domain/repositories/i_user_balance_repository.h"
#include "core/infrastructure/persistence/database_factory.h"

// 用户余额服务 - 应用层服务，处理与用户余额相关的业务逻辑
class UserBalanceService {
public:
    // 默认构造函数 - 从依赖容器获取依赖
    UserBalanceService();
    
    // 构造函数 - 使用指定的仓库实现
    explicit UserBalanceService(std::shared_ptr<IUserBalanceRepository> repository);
    
    // 析构函数
    ~UserBalanceService();
    
    // 批量更新玩家余额
    std::vector<PlayerBalanceUpdateResult> updateBalancesBatch(
        const std::vector<PlayerBalanceUpdate>& updates);
    
    // 更新单个玩家的余额
    PlayerBalanceUpdateResult updateBalance(
        const std::string& loginName,
        double originalBalance,
        double newBalance,
        const std::string& reason);

private:
    std::shared_ptr<IUserBalanceRepository> repository_;
};

#endif // USER_BALANCE_SERVICE_H