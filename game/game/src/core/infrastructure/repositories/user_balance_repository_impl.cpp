#include "user_balance_repository_impl.h"
#include "third_party/libuv_cpp/include/LogWriter.hpp"
#include "core/infrastructure/persistence/mysql_clientV2.h"
#include "core/infrastructure/persistence/mysql_data_def.h"
#include "core/infrastructure/common/dependency_container.h"
#include "core/infrastructure/common/app_context.h"
#include "games/game_def.h"
#include <unordered_map>

extern DependencyContainer& getDependencyContainer();

// 小数精度误差容忍范围 - 增大以处理浮点数精度问题
constexpr double DECIMAL_EPSILON = 0.01;

// 静态成员初始化
std::atomic<uint64_t> UserBalanceRepositoryImpl::orderSequence_{0};
std::atomic<uint64_t> UserBalanceRepositoryImpl::transSequence_{0};

// 辅助函数：将double四舍五入到2位小数，与MySQL的DECIMAL(32,2)匹配
double roundToTwoDecimals(double value) {
    return std::round(value * 100.0) / 100.0;
}

UserBalanceRepositoryImpl::UserBalanceRepositoryImpl() {
    auto& container = getDependencyContainer();
    dbFactory_ = container.resolve<DatabaseFactory>();
    
    if (!dbFactory_) {
        LOG_ERROR("Failed to resolve DatabaseFactory from dependency container");
    }
}

UserBalanceRepositoryImpl::~UserBalanceRepositoryImpl() {
}

std::string UserBalanceRepositoryImpl::generateOrderId() {
    // 雪花算法: 1位符号位(0) + 41位时间戳 + 10位机器ID + 12位序列号
    static const uint64_t EPOCH = 1640995200000; // 2022-01-01 00:00:00 UTC
    
    // 获取服务器ID作为机器ID
    uint64_t machineId = 1; // 默认值
    
    auto& container = getDependencyContainer();
    auto appContext = container.resolve<AppContext>();
    if (appContext) {
        auto tcpServer = appContext->getTcpServer();
        if (tcpServer) {
            machineId = tcpServer->getServerNumericId();
            // 确保machineId在10位以内 (0-1023)
            machineId &= 0x3FF;
        }
    }
    
    auto now = std::chrono::system_clock::now();
    uint64_t timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    
    uint64_t seq = orderSequence_.fetch_add(1) & 0xFFF; // 12位序列号
    uint64_t snowflakeId = ((timestamp - EPOCH) << 22) | (machineId << 12) | seq;
    
    return "ORD" + std::to_string(snowflakeId);
}

std::string UserBalanceRepositoryImpl::generateTransId() {
    static const uint64_t EPOCH = 1640995200000;
    
    // 获取服务器ID作为机器ID
    uint64_t machineId = 1; // 默认值
    
    auto& container = getDependencyContainer();
    auto appContext = container.resolve<AppContext>();
    if (appContext) {
        auto tcpServer = appContext->getTcpServer();
        if (tcpServer) {
            machineId = tcpServer->getServerNumericId();
            // 确保machineId在10位以内 (0-1023)
            machineId &= 0x3FF;
        }
    }
    
    auto now = std::chrono::system_clock::now();
    uint64_t timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    
    uint64_t seq = transSequence_.fetch_add(1) & 0xFFF;
    uint64_t snowflakeId = ((timestamp - EPOCH) << 22) | (machineId << 12) | seq;
    
    return "TXN" + std::to_string(snowflakeId);
}

double UserBalanceRepositoryImpl::getPlayerBalance(const uint64_t& playerId) {
    if (!dbFactory_) {
        LOG_ERROR("Database factory not available for getPlayerBalance");
        return -1.0;
    }
    
    auto mysqlClient = dbFactory_->getMySQLClientV2();
    if (!mysqlClient) {
        LOG_ERROR("Failed to get MySQL client for getPlayerBalance");
        return -1.0;
    }
    
    try {
        double balance = -1.0;
        std::string sql = "SELECT CAST(b.amount AS CHAR) FROM sys_player_balance b WHERE b.player_id = ?";
        std::vector<MySQLParamValue> params = {playerId};
        
        LOG_DEBUG("Executing getPlayerBalance query: %s with params [%llu]", 
                 sql.c_str(), playerId);
        
        bool found = false;
        mysqlClient->queryWithCallback(sql, params, [&](sql::ResultSet* row) {
            if (row && !row->isNull(1)) {
                std::string amountStr = row->getString(1);
                balance = std::stod(amountStr);
                found = true;
                LOG_DEBUG("Found player %lld balance: %s (%.2f)", 
                    playerId, amountStr.c_str(), balance);
            }
        });
        
        if (!found) {
            LOG_WARN("Player %lld not found in database", playerId);
            return -1.0;
        }
        
        return balance;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Exception while getting player %lld balance: %s", 
            playerId, e.what());
        return -1.0;
    }
}

// 更新单个玩家余额
bool UserBalanceRepositoryImpl::updatePlayerBalance(const PlayerRoundInfo& info, uint32_t reason) {
    if (!dbFactory_) {
        LOG_ERROR("Database factory not available for player balance update");
        return false;
    }
    
    auto mysqlClient = dbFactory_->getMySQLClientV2();
    if (!mysqlClient) {
        LOG_ERROR("Failed to get MySQL client for player balance update");
        return false;
    }
    if (info.bets_.empty()) {
        LOG_ERROR("No bet information found in PlayerRoundInfo");
        return false;
    }
    
    const auto& firstBet = info.bets_[0];

    uint64_t playerId = info.info_.player_id_;
    double betAmount = firstBet.amount_;
    
    LOG_INFO("Starting balance update for player %lu, bet amount: %.2f", playerId, betAmount);
    
    // 使用安全事务处理单个玩家的扣款
    bool success = mysqlClient->safeExecuteTransaction([&](MySQLClientV2& client) {
        try {
            // 1. 首先获取当前余额
            std::string selectBalanceSql = "SELECT CAST(amount AS CHAR) FROM sys_player_balance WHERE player_id = ?";
            std::vector<MySQLParamValue> selectParams;
            selectParams.push_back(static_cast<int32_t>(playerId));
            
            double beforeAmount = 0.0;
            bool balanceFound = false;
            
            client.queryWithCallback(selectBalanceSql, selectParams, [&](sql::ResultSet* row) {
                if (row && !row->isNull(1)) {
                    std::string balanceStr = row->getString(1);
                    beforeAmount = std::stod(balanceStr);
                    balanceFound = true;
                }
            });
            
            if (!balanceFound) {
                LOG_ERROR("Player %lu balance not found", playerId);
                return false;
            }
            
            double newBalance = beforeAmount - betAmount;
            
            // 2. 更新玩家余额
            std::string updateBalanceSql = "UPDATE sys_player_balance SET amount = ? WHERE player_id = ?";
            std::vector<MySQLParamValue> balanceParams;
            balanceParams.push_back(newBalance);
            balanceParams.push_back(static_cast<int32_t>(playerId));
            
            int64_t updateCount = client.safeExecuteUpdate(updateBalanceSql, balanceParams, 1);
            if (updateCount == -1) {
                LOG_ERROR("Failed to update balance for player %lu", playerId);
                return false;
            }
            
            // 2. 生成订单ID
            std::string orderId = generateOrderId();
            
            // 3. 插入订单记录
            std::string orderSql = "INSERT INTO mines_pro_order (round_id, player_id, order_no, play_type, "
                                  "bet_amount, win_amount, win_multiple, bet_time, settle_time) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)";
            std::vector<MySQLParamValue> orderParams;
            orderParams.push_back(std::string("TBD")); // round_id 将在游戏处理时更新
            orderParams.push_back(static_cast<int32_t>(playerId));
            orderParams.push_back(orderId);
            orderParams.push_back(static_cast<int32_t>(firstBet.playType_));
            orderParams.push_back(betAmount);
            orderParams.push_back(0.0); // win_amount 初始为0
            orderParams.push_back(0.0); // win_multiple 初始为0
            
            // 格式化时间
            auto time_t = std::chrono::system_clock::to_time_t(firstBet.bet_time_);
            char timeStr[64];
            std::strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", std::localtime(&time_t));
            orderParams.push_back(std::string(timeStr));
            orderParams.push_back(std::string("")); // settle_time 暂时为空
            
            int64_t orderInsertCount = client.safeExecuteUpdate(orderSql, orderParams, 1);
            if (orderInsertCount == -1) {
                LOG_ERROR("Failed to insert order for player %lu", playerId);
                return false;
            }
            
            // 4. 插入交易记录
            std::string transactionSql = "INSERT INTO player_change_record (player_id, order_no, change_type, "
                                        "before_amount, change_amount, after_amount, description, oper_ip, "
                                        "create_by, update_by, remark, status, merchant_id) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
            std::vector<MySQLParamValue> transactionParams;
            transactionParams.push_back(static_cast<int32_t>(playerId));
            transactionParams.push_back(orderId);
            transactionParams.push_back(static_cast<int32_t>(reason)); // reason: 3 for bet deduction
            transactionParams.push_back(beforeAmount);
            transactionParams.push_back(-betAmount); // 负数表示扣款
            transactionParams.push_back(newBalance);
            transactionParams.push_back(std::string("mines_pro_bet_deduction"));
            transactionParams.push_back(info.info_.client_ip_);
            transactionParams.push_back(std::string("system"));
            transactionParams.push_back(std::string("")); // update_by 为空
            transactionParams.push_back(std::string("bet_deduction"));
            transactionParams.push_back(static_cast<int32_t>(1)); // status: 1 for active
            transactionParams.push_back(static_cast<int32_t>(info.info_.merchant_id_)); // merchant_id
            
            int64_t transInsertCount = client.safeExecuteUpdate(transactionSql, transactionParams, 1);
            if (transInsertCount == -1) {
                LOG_ERROR("Failed to insert transaction record for player %lu", playerId);
                return false;
            }
            
            LOG_INFO("Balance update completed successfully for player %lu, new balance: %.2f", 
                    playerId, newBalance);
            return true;
            
        } catch (const std::exception& e) {
            LOG_ERROR("Database error during player balance update: %s", e.what());
            return false;
        }
    });
    
    return success;
}