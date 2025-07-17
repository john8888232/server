#include "user_balance_service.h"
#include <third_party/libuv_cpp/include/LogWriter.hpp>
#include "core/infrastructure/repositories/user_balance_repository_impl.h"

UserBalanceService::UserBalanceService(std::shared_ptr<DatabaseFactory> dbFactory)
    : repository_(std::make_shared<UserBalanceRepositoryImpl>(dbFactory)) {
}

UserBalanceService::UserBalanceService(std::shared_ptr<IUserBalanceRepository> repository)
    : repository_(repository) {
}

UserBalanceService::~UserBalanceService() {
}

std::vector<PlayerBalanceUpdateResult> UserBalanceService::updateBalancesBatch(
    const std::vector<PlayerBalanceUpdate>& updates)
{
    std::vector<PlayerBalanceUpdateResult> results;
    try {
        if (updates.empty()) {
            LOG_INFO("No player balances to update");
            return results;
        }
        
        LOG_INFO("Updating balances for %d players", updates.size());
        
        // 委托给仓库执行批量更新
        repository_->updatePlayerBalancesBatch(updates, results);
        
        // 可以在这里添加其他业务逻辑，例如发送余额变更通知等
        
        return results;
    } catch (const std::exception& e) {
        LOG_ERROR("Exception during batch balance update: %s", e.what());
        return results;
    }
}

PlayerBalanceUpdateResult UserBalanceService::updateBalance(
    const std::string& loginName,
    double originalBalance,
    double newBalance,
    const std::string& reason)
{
    try {
        PlayerBalanceUpdate update;
        update.loginName = loginName;
        update.originalBalance = originalBalance;
        update.newBalance = newBalance;
        update.reason = reason;
        
        LOG_INFO("Updating balance for player %s: %.2f -> %.2f (%s)",
                 loginName.c_str(), originalBalance, newBalance, reason.c_str());
        
        // 委托给仓库执行单个更新
        return repository_->updatePlayerBalance(update);
    } catch (const std::exception& e) {
        LOG_ERROR("Exception during balance update for %s: %s", loginName.c_str(), e.what());
        PlayerBalanceUpdateResult result;
        result.loginName = loginName;
        result.originalBalance = originalBalance;
        result.newBalance = newBalance;
        result.success = false;
        result.errorMessage = "System error: " + std::string(e.what());
        return result;
    }
}