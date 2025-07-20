#include "user_balance_repository_impl.h"
#include <third_party/libuv_cpp/include/LogWriter.hpp>
#include <cmath>
#include "core/infrastructure/persistence/database_factory.h"
#include "core/infrastructure/persistence/mysql_client.h"
#include "core/infrastructure/common/dependency_container.h"

extern DependencyContainer& getDependencyContainer();

// 小数精度误差容忍范围 - 增大以处理浮点数精度问题
constexpr double DECIMAL_EPSILON = 0.01;

// 辅助函数：将double四舍五入到2位小数，与MySQL的DECIMAL(32,2)匹配
double roundToTwoDecimals(double value) {
    return std::round(value * 100.0) / 100.0;
}

UserBalanceRepositoryImpl::UserBalanceRepositoryImpl() {
    // 从依赖容器获取DatabaseFactory
    auto& container = getDependencyContainer();
    dbFactory_ = container.resolve<DatabaseFactory>();
    
    if (!dbFactory_) {
        LOG_ERROR("Failed to resolve DatabaseFactory from dependency container");
    }
}

UserBalanceRepositoryImpl::~UserBalanceRepositoryImpl() {
    // MySQL实现特定的清理
}

bool UserBalanceRepositoryImpl::updatePlayerBalancesBatch(
    const std::vector<PlayerBalanceUpdate>& updates, 
    std::vector<PlayerBalanceUpdateResult>& results) 
{
    results.clear();
    
    int totalCount = updates.size();
    int successCount = 0;
    int failCount = 0;
    
    LOG_INFO("Starting batch update of balances for %d players", totalCount);
    
    // 处理每个玩家的余额更新，每个玩家使用独立事务
    for (const auto& update : updates) {
        PlayerBalanceUpdateResult result;
        result.loginName = update.loginName;
        result.originalBalance = update.originalBalance;
        result.newBalance = update.newBalance;
        
        // 对每个玩家使用独立事务处理
        bool success = updateSinglePlayerBalance(update, result);
        
        if (success) {
            successCount++;
        } else {
            failCount++;
            LOG_ERROR("Failed to update balance for player %s: %s", 
                      update.loginName.c_str(), result.errorMessage.c_str());
        }
        
        results.push_back(result);
    }
    
    LOG_INFO("Completed batch update of balances: total=%d, success=%d, fail=%d", 
             totalCount, successCount, failCount);
    
    // 即使部分玩家失败，整体操作也视为完成
    return true;
}

PlayerBalanceUpdateResult UserBalanceRepositoryImpl::updatePlayerBalance(
    const PlayerBalanceUpdate& update)
{
    PlayerBalanceUpdateResult result;
    result.loginName = update.loginName;
    result.originalBalance = update.originalBalance;
    result.newBalance = update.newBalance;
    
    updateSinglePlayerBalance(update, result);
    return result;
}

bool UserBalanceRepositoryImpl::updateSinglePlayerBalance(
    const PlayerBalanceUpdate& update,
    PlayerBalanceUpdateResult& result)
{
    if (!dbFactory_) {
        result.success = false;
        result.errorMessage = "Database factory not available";
        return false;
    }
    
    auto mysqlClient = dbFactory_->getMySQLClient();
    
    if (!mysqlClient) {
        result.success = false;
        result.errorMessage = "Failed to get MySQL client";
        return false;
    }
    
    bool success = false;
    
    // 使用事务进行余额更新
    success = mysqlClient->executeTransaction([&](MySQLClient& client) {
        try {
            // 读取当前余额（使用SELECT FOR UPDATE锁定记录）
            double currentBalance = 0.0;
            std::string updateTime;
            
            // 查询当前余额 - 使用CAST读取精确值
            std::string querySql = "SELECT CAST(amount AS CHAR), update_time FROM sys_player WHERE login_name = ? FOR UPDATE";
            std::vector<MySQLParamValue> queryParams = {update.loginName};
            
            LOG_INFO("Executing SELECT FOR UPDATE: %s with params [%s]", 
                     querySql.c_str(), update.loginName.c_str());
            
            bool foundPlayer = false;
            client.queryWithCallback(querySql, queryParams, [&](const mysqlx::Row& row) {
                std::string amountStr = row[0].get<std::string>();
                currentBalance = std::stod(amountStr);
                updateTime = row[1].get<std::string>();
                foundPlayer = true;
                LOG_INFO("SELECT result: amount=%s, update_time=%s", 
                         amountStr.c_str(), updateTime.c_str());
            });
            
            if (!foundPlayer) {
                result.success = false;
                result.errorMessage = "Player not found";
                return false;
            }
            
            // 检查原始余额是否匹配（允许小数精度误差）
            if (std::abs(currentBalance - update.originalBalance) > DECIMAL_EPSILON) {
                result.success = false;
                result.actualBalance = currentBalance;
                result.errorMessage = "Original balance mismatch: expected " + 
                                     std::to_string(update.originalBalance) + 
                                     ", actual " + std::to_string(currentBalance);
                return false;
            }
            
            // 将新余额四舍五入到2位小数，确保与MySQL的DECIMAL(32,2)精度匹配
            double roundedNewBalance = roundToTwoDecimals(update.newBalance);
            
            // 使用参数化查询更新余额
            std::string updateSql = "UPDATE sys_player SET amount = ?, update_time = NOW() WHERE login_name = ?";
            std::vector<MySQLParamValue> updateParams = {roundedNewBalance, update.loginName};
            
            LOG_INFO("Executing UPDATE: %s with params [%.2f, %s]", 
                     updateSql.c_str(), roundedNewBalance, update.loginName.c_str());
            
            uint64_t rowsAffected = client.executeUpdate(updateSql, updateParams);
            
            LOG_INFO("UPDATE result: rows affected = %lu", rowsAffected);
            
            if (rowsAffected != 1) {
                result.success = false;
                result.actualBalance = currentBalance;
                result.errorMessage = "Failed to update player balance, rows affected: " + std::to_string(rowsAffected);
                return false;
            }
            
            // 验证更新是否成功，并获取实际更新后的值 - 使用CAST读取精确值
            double updatedBalance = 0.0;
            std::string updatedBalanceStr;
            std::string verifySQL = "SELECT CAST(amount AS CHAR) FROM sys_player WHERE login_name = ?";
            
            client.queryWithCallback(verifySQL, {update.loginName}, [&](const mysqlx::Row& row) {
                updatedBalanceStr = row[0].get<std::string>();
                updatedBalance = std::stod(updatedBalanceStr);
                LOG_INFO("Verify SELECT result: updated amount=%s", updatedBalanceStr.c_str());
            });
            
            // 更新结果中保存实际的数据库值，用于同步内存
            result.success = true;
            result.actualBalance = updatedBalance;
            
            LOG_INFO("Successfully updated balance for player %s: %.2f -> %s", 
                     update.loginName.c_str(), currentBalance, updatedBalanceStr.c_str());
            
            return true;
        } catch (const std::exception& e) {
            result.success = false;
            result.errorMessage = "Database error: " + std::string(e.what());
            LOG_ERROR("Database error during balance update for %s: %s", 
                     update.loginName.c_str(), e.what());
            return false;
        }
    });
    
    return success;
} 