#include "mines_game_repository_impl.h"
#include "third_party/libuv_cpp/include/LogWriter.hpp"
#include <iomanip>
#include <sstream>
#include <chrono>
#include "core/infrastructure/common/dependency_container.h"
#include "core/infrastructure/common/utils.h"
#include "core/infrastructure/persistence/mysql_clientV2.h"
#include "core/domain/models/user.h"
#include "games/game_def.h"

extern DependencyContainer& getDependencyContainer();

MinesGameRepositoryImpl::MinesGameRepositoryImpl() {
    auto& container = getDependencyContainer();
    dbFactory_ = container.resolve<DatabaseFactory>();
    
    if (!dbFactory_) {
        LOG_ERROR("Failed to resolve DatabaseFactory from dependency container");
    }
}

MinesGameRepositoryImpl::~MinesGameRepositoryImpl() {
}

bool MinesGameRepositoryImpl::startGame(const std::string& roundID) {
    if (!dbFactory_) {
        LOG_ERROR("Database factory not available for startGame");
        return false;
    }
    auto mysqlClient = dbFactory_->getMySQLClientV2();
    if (!mysqlClient) {
        LOG_ERROR("Failed to get MySQL client for startGame");
        return false;
    }
    
    try {
        std::string sql = R"(
            INSERT INTO mines_pro_result (round_id, start_time, game_type) 
            VALUES (?, NOW(), 'mines_pro')
        )";
        std::vector<MySQLParamValue> params = {roundID};
        int64_t result = mysqlClient->safeExecuteUpdate(sql, params, 1);
        
        if (result == 1) {
            LOG_INFO("Successfully started game with round ID: %s", roundID.c_str());
            return true;
        } else if (result == -1) {
            LOG_ERROR("Failed to start game due to database connection issues, round ID: %s", roundID.c_str());
            return false;
        } else {
            LOG_ERROR("Unexpected result when starting game, expected 1 row affected but got %ld, round ID: %s", 
                     result, roundID.c_str());
            return false;
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Exception occurred while starting game: %s, round ID: %s", e.what(), roundID.c_str());
        return false;
    }
}

bool MinesGameRepositoryImpl::saveGame(std::shared_ptr<IGameResult> pGame) {
    if (!dbFactory_) {
        LOG_ERROR("Database factory not available for saveGame");
        return false;
    }
    
    auto mysqlClient = dbFactory_->getMySQLClientV2();
    if (!mysqlClient) {
        LOG_ERROR("Failed to get MySQL client for saveGame");
        return false;
    }
    
    try {
        auto minesGame = std::dynamic_pointer_cast<MinesGameResult>(pGame);
        if (!minesGame) {
            LOG_ERROR("Invalid game result type for MinesGameRepositoryImpl::saveGame");
            return false;
        }
        
        // 更新游戏结果
        std::string sql = R"(
            UPDATE mines_pro_result 
            SET end_time = NOW(), 
                mine_index = ?, 
                multiplier = ?, 
                hash = ?, 
                seed = ? 
            WHERE round_id = ?
        )";
        std::vector<MySQLParamValue> params = {
            minesGame->mineIndex, 
            minesGame->multiplier, 
            minesGame->hash, 
            minesGame->seed,
            pGame->roundID
        };
    
        int64_t result = mysqlClient->safeExecuteUpdate(sql, params, 1);
        
        if (result == -1) {
            LOG_ERROR("Failed to save game result after retries for game %s", pGame->roundID.c_str());
            return false;
        }
        
        if (result != 1) {
            LOG_WARN("Unexpected number of rows affected: %ld when saving game %s", 
                    result, pGame->roundID.c_str());
        }
        
        LOG_INFO("Save game results succeed: %s", pGame->roundID.c_str());
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Save game results failed: %s", e.what());
        return false;
    }
}

std::vector<std::shared_ptr<MinesGameResult>> MinesGameRepositoryImpl::getRecentGames(int limit) {
    std::vector<std::shared_ptr<MinesGameResult>> results;
    
    if (!dbFactory_) {
        LOG_ERROR("Database factory not available for getRecentGames");
        return results;
    }
    
    auto mysqlClient = dbFactory_->getMySQLClientV2();
    if (!mysqlClient) {
        LOG_ERROR("Failed to get MySQL client for getRecentGames");
        return results;
    }
    
    try {
        std::string sql = R"(
            SELECT round_id, start_time, end_time, game_type, mine_index, multiplier, hash, seed 
            FROM mines_pro_result 
            ORDER BY start_time DESC 
            LIMIT ?
        )";
        std::vector<MySQLParamValue> params = {limit};
        
        LOG_DEBUG("Executing getRecentGames query: %s with limit %d", sql.c_str(), limit);
        
        mysqlClient->queryWithCallback(sql, params, [&](sql::ResultSet* row) {
            if (row) {
                auto gameResult = std::make_shared<MinesGameResult>();
                gameResult->roundID = row->getString(1);
                
                // 将MySQL datetime字符串转换为time_point
                std::string startTimeStr = row->getString(2);
                std::tm startTm = {};
                std::istringstream startSs(startTimeStr);
                startSs >> std::get_time(&startTm, "%Y-%m-%d %H:%M:%S");
                std::time_t startTime_t = std::mktime(&startTm);
                gameResult->startTime = std::chrono::system_clock::from_time_t(startTime_t);
                
                if (!row->isNull(3)) {
                    std::string endTimeStr = row->getString(3);
                    std::tm endTm = {};
                    std::istringstream endSs(endTimeStr);
                    endSs >> std::get_time(&endTm, "%Y-%m-%d %H:%M:%S");
                    std::time_t endTime_t = std::mktime(&endTm);
                    gameResult->endTime = std::chrono::system_clock::from_time_t(endTime_t);
                } else {
                    // 设置为epoch（1970-01-01 00:00:00）表示未设置
                    gameResult->endTime = std::chrono::system_clock::time_point{};
                }
                
                gameResult->gameType = row->getString(4);
                gameResult->mineIndex = row->getString(5);
                gameResult->multiplier = row->getDouble(6);
                
                // 获取hash和seed字段
                if (!row->isNull(7)) {
                    gameResult->hash = row->getString(7);
                }
                
                if (!row->isNull(8)) {
                    gameResult->seed = row->getString(8);
                }
                
                results.push_back(gameResult);
            }
        });
        
        LOG_INFO("Retrieved %zu recent game results", results.size());
        return results;
    } catch (const std::exception& e) {
        LOG_ERROR("Query history failed: %s", e.what());
        return results;
    }
}

// 下注扣款
bool MinesGameRepositoryImpl::updatePlayerBet(int64_t playerId, double change_amount, 
                                                const std::string& roundID, int32_t playtype, 
                                                const std::string& playerIP, 
                                                std::shared_ptr<PlayerInGame> playerInGame) {
    if (!dbFactory_) {
        LOG_ERROR("Database factory not available for player bet update");
        return false;
    }
    
    auto mysqlClient = dbFactory_->getMySQLClientV2();
    if (!mysqlClient) {
        LOG_ERROR("Failed to get MySQL client for player bet update");
        return false;
    }
    
    // 确保下注金额为负数（扣款）
    if (change_amount > 0) {
        LOG_ERROR("Bet should be negative, but got: %.2f", change_amount);
        return false;
    }
    
    LOG_INFO("Starting bet update for player %lu, change amount: %.2f, round: %s", 
             playerId, change_amount, roundID.c_str());
    
    bool success = mysqlClient->safeExecuteTransaction([&](MySQLClientV2& client) {
        try {
            // 1. 查询当前余额并检查是否够扣
            std::string selectSql = "SELECT CAST(amount AS CHAR) FROM sys_player_balance WHERE player_id = ? FOR UPDATE";
            std::vector<MySQLParamValue> selectParams = {playerId};
            
            double before_amount = 0.0;
            bool playerFound = false;
            
            client.queryWithCallback(selectSql, selectParams, [&](sql::ResultSet* row) {
                if (row && !row->isNull(1)) {
                    std::string amountStr = row->getString(1);
                    before_amount = std::stod(amountStr);
                    playerFound = true;
                }
            });
            
            if (!playerFound) {
                LOG_ERROR("Player %lu not found in balance table", playerId);
                return false;
            }
            
            // 检查余额是否足够
            double after_amount = before_amount + change_amount;
            if (after_amount < 0) {
                LOG_ERROR("Insufficient balance for player %lu: current=%.2f, change=%.2f", 
                         playerId, before_amount, change_amount);
                return false;
            }
            
            // 2. 更新玩家余额
            std::string updateSql = R"(
                UPDATE sys_player_balance 
                SET amount = amount + ? 
                WHERE player_id = ?
            )";
            std::vector<MySQLParamValue> updateParams = {
                utils::roundToTwoDecimals(change_amount),
                playerId
            };
            
            int64_t updateCount = client.safeExecuteUpdate(updateSql, updateParams, 1);
            if (updateCount != 1) {
                LOG_ERROR("Failed to update balance for player %lu", playerId);
                return false;
            }
            
            // 3. 先尝试UPDATE，失败则INSERT
            std::string orderNo = utils::generateOrderId(); // 预生成，以防需要INSERT
            
            // 先尝试更新现有订单（status=0的订单）
            std::string updateOrderSql = R"(
                UPDATE mines_pro_order 
                SET bet_amount = bet_amount + ?, 
                    bet_time = NOW() 
                WHERE round_id = ? 
                    AND player_id = ? 
                    AND play_type = ? 
                    AND status = 0
            )";
            std::vector<MySQLParamValue> updateOrderParams = {
                std::abs(change_amount), // bet_amount 存储为正数
                roundID,
                playerId,
                playtype
            };
            
            int64_t orderUpdateCount = client.safeExecuteUpdate(updateOrderSql, updateOrderParams);
            
            if (orderUpdateCount == 0) {
                // 没有找到可更新的记录，直接插入新订单
                std::string insertOrderSql = R"(
                    INSERT INTO mines_pro_order (round_id, player_id, order_no, play_type, bet_amount, bet_time, status) 
                    VALUES (?, ?, ?, ?, ?, NOW(), 0)
                )";
                std::vector<MySQLParamValue> insertOrderParams = {
                    roundID,
                    playerId,
                    orderNo,
                    playtype,
                    std::abs(change_amount) // bet_amount 存储为正数
                };
                
                int64_t orderInsertCount = client.safeExecuteUpdate(insertOrderSql, insertOrderParams, 1);
                if (orderInsertCount != 1) {
                    LOG_ERROR("Failed to insert order for player %lu", playerId);
                    return false;
                }
            }
            
            // 4. 插入交易记录 (change_type = 3)
            std::string transId = utils::generateTransId(); // 生成交易ID
            std::string transSql = R"(
                INSERT INTO 
                player_change_record (player_id, order_no, change_type, before_amount, change_amount, after_amount, description, oper_ip, merchant_id) 
                VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
            )";
            std::vector<MySQLParamValue> transParams = {
                playerId,
                transId, // 使用交易ID
                static_cast<int8_t>(TransactionType::BET), // change_type = 3
                utils::roundToTwoDecimals(before_amount),
                utils::roundToTwoDecimals(change_amount),
                utils::roundToTwoDecimals(before_amount + change_amount), // after_amount
                roundID, // description
                playerIP, // oper_ip
                playerInGame->getUser()->getMerchantId() // merchant_id
            };
            
            int64_t transInsertCount = client.safeExecuteUpdate(transSql, transParams, 1);
            if (transInsertCount != 1) {
                LOG_ERROR("Failed to insert transaction record for player %lu", playerId);
                return false;
            }
            playerInGame->setBalance(before_amount + change_amount);
            playerInGame->setBetPlayType(playtype);
            LOG_INFO("Bet update completed for player %lu, new balance: %.2f", playerId, playerInGame->getBalance());
            return true;
            
        } catch (const std::exception& e) {
            LOG_ERROR("Database error during bet update: %s", e.what());
            return false;
        }
    });
    
    return success;
}

// 派奖加款
bool MinesGameRepositoryImpl::updatePlayerReckon(int64_t playerId, double change_amount, 
                                                const std::string& roundID, int32_t playtype, 
                                                double multiple, const std::string& playerIP, std::shared_ptr<PlayerInGame> playerInGame) {
    if (!dbFactory_) {
        LOG_ERROR("Database factory not available for player reckon update");
        return false;
    }
    
    auto mysqlClient = dbFactory_->getMySQLClientV2();
    if (!mysqlClient) {
        LOG_ERROR("Failed to get MySQL client for player reckon update");
        return false;
    }
    
    // 确保派奖金额为正数（加款）
    if (change_amount < 0) {
        LOG_ERROR("Reckon should be positive, but got: %.2f", change_amount);
        return false;
    }
    
    LOG_INFO("Starting reckon update for player %lu, change amount: %.2f, round: %s, multiple: %.2f", 
             playerId, change_amount, roundID.c_str(), multiple);
    
    bool success = mysqlClient->safeExecuteTransaction([&](MySQLClientV2& client) {
        try {
            // 1. 对玩家余额加行锁（与下注和取消操作互斥）
            std::string selectSql = R"(
            SELECT CAST(amount AS CHAR) 
            FROM sys_player_balance 
            WHERE player_id = ? 
            FOR UPDATE
        )";
            std::vector<MySQLParamValue> selectParams = {playerId};
            
            double before_amount = 0.0;
            bool playerFound = false;
            
            client.queryWithCallback(selectSql, selectParams, [&](sql::ResultSet* row) {
                if (row && !row->isNull(1)) {
                    std::string amountStr = row->getString(1);
                    before_amount = std::stod(amountStr);
                    playerFound = true;
                }
            });
            
            if (!playerFound) {
                LOG_ERROR("Player %lu not found in balance table", playerId);
                return false;
            }
            
            // 2. 直接尝试更新订单状态（只更新status=0的订单）
            std::string orderSql = "UPDATE mines_pro_order SET "
                                  "win_amount = ?, win_multiple = ?, settle_time = NOW(), status = 1 "
                                  "WHERE round_id = ? AND player_id = ? AND play_type = ? AND status = 0";
            std::vector<MySQLParamValue> orderParams = {
                change_amount, // win_amount
                multiple, // win_multiple
                roundID,
                playerId,
                playtype
            };
            
            int64_t orderUpdateCount = client.safeExecuteUpdate(orderSql, orderParams);
            if (orderUpdateCount != 1) {
                LOG_ERROR("Order not found or cannot be settled for reckon: round=%s, player=%lu, playtype=%d", 
                         roundID.c_str(), playerId, playtype);
                return false;
            }
            
            // 3. 更新玩家余额
            std::string updateSql = R"(
                UPDATE sys_player_balance 
                SET amount = amount + ? 
                WHERE player_id = ?
            )";
            std::vector<MySQLParamValue> updateParams = {
                utils::roundToTwoDecimals(change_amount), 
                playerId
            };
            
            int64_t updateCount = client.safeExecuteUpdate(updateSql, updateParams, 1);
            if (updateCount != 1) {
                LOG_ERROR("Failed to update balance for player %lu", playerId);
                return false;
            }
            
            // 4. 插入交易记录 (change_type = 4)
            std::string transId = utils::generateTransId(); // 生成交易ID
            std::string transSql = R"(
                INSERT INTO player_change_record (player_id, order_no, change_type, before_amount, change_amount, after_amount, description, oper_ip, merchant_id) 
                VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
            )";
            std::vector<MySQLParamValue> transParams = {
                playerId,
                transId, // 使用交易ID
                static_cast<int8_t>(TransactionType::WIN), // change_type = 4
                utils::roundToTwoDecimals(before_amount),
                utils::roundToTwoDecimals(change_amount),
                utils::roundToTwoDecimals(before_amount + change_amount), // after_amount
                roundID, // description
                playerIP, // oper_ip
                playerInGame->getUser()->getMerchantId()
            };
            
            int64_t transInsertCount = client.safeExecuteUpdate(transSql, transParams, 1);
            if (transInsertCount != 1) {
                LOG_ERROR("Failed to insert transaction record for player %lu", playerId);
                return false;
            }
            
            double new_balance = before_amount + change_amount;
            
            // 直接更新PlayerInGame的余额（线程安全）
            if (playerInGame) {
                playerInGame->setBalance(new_balance);
            }
            
            LOG_INFO("Reckon update completed for player %lu, new balance: %.2f", playerId, new_balance);
            return true;
            
        } catch (const std::exception& e) {
            LOG_ERROR("Database error during reckon update: %s", e.what());
            return false;
        }
    });
    
    return success;
}

// 取消订单
bool MinesGameRepositoryImpl::updatePlayerCancel(int64_t playerId, double change_amount, 
                                                const std::string& roundID, int32_t playtype, 
                                                const std::string& playerIP, std::shared_ptr<PlayerInGame> playerInGame) {
    if (!dbFactory_) {
        LOG_ERROR("Database factory not available for player cancel update");
        return false;
    }
    
    auto mysqlClient = dbFactory_->getMySQLClientV2();
    if (!mysqlClient) {
        LOG_ERROR("Failed to get MySQL client for player cancel update");
        return false;
    }
    
    LOG_INFO("Starting cancel update for player %lu, change amount: %.2f, round: %s", 
             playerId, change_amount, roundID.c_str());
    
    bool success = mysqlClient->safeExecuteTransaction([&](MySQLClientV2& client) {
        try {
            // 1. 对玩家余额加行锁（和派奖操作互斥）
            std::string selectSql = R"(
                SELECT CAST(amount AS CHAR) 
                FROM sys_player_balance 
                WHERE player_id = ? 
                FOR UPDATE
            )";
            std::vector<MySQLParamValue> selectParams = {playerId};
            
            double before_amount = 0.0;
            bool playerFound = false;
            
            client.queryWithCallback(selectSql, selectParams, [&](sql::ResultSet* row) {
                if (row && !row->isNull(1)) {
                    std::string amountStr = row->getString(1);
                    before_amount = std::stod(amountStr);
                    playerFound = true;
                }
            });
            
            if (!playerFound) {
                LOG_ERROR("Player %lu not found in balance table", playerId);
                return false;
            }
            
            // 2. 获取订单的bet_amount并检查是否可以取消
            std::string getBetAmountSql = R"(
                SELECT bet_amount 
                FROM mines_pro_order 
                WHERE round_id = ? 
                    AND player_id = ? 
                    AND play_type = ? 
                    AND status = 0
            )";
            std::vector<MySQLParamValue> getBetParams = {roundID, playerId, playtype};
            
            double betAmount = 0.0;
            bool orderExists = false;
            
            client.queryWithCallback(getBetAmountSql, getBetParams, [&](sql::ResultSet* row) {
                if (row && !row->isNull(1)) {
                    betAmount = row->getDouble(1);
                    orderExists = true;
                }
            });
            
            if (!orderExists) {
                LOG_ERROR("Order not found or cannot be cancelled for cancel: round=%s, player=%lu, playtype=%d", 
                         roundID.c_str(), playerId, playtype);
                return false;
            }
            
            // 3. 直接更新订单状态为取消 (只更新status=0的订单)
            std::string orderSql = R"(
                UPDATE mines_pro_order 
                SET status = 2 
                WHERE round_id = ? 
                    AND player_id = ? 
                    AND play_type = ? 
                    AND status = 0
            )";
            std::vector<MySQLParamValue> orderParams = {
                roundID,
                playerId,
                playtype
            };
            
            int64_t orderUpdateCount = client.safeExecuteUpdate(orderSql, orderParams);
            if (orderUpdateCount != 1) {
                LOG_ERROR("Order not found or cannot be cancelled for cancel: round=%s, player=%lu, playtype=%d", 
                         roundID.c_str(), playerId, playtype);
                return false;
            }
            
            // 4. 退还下注金额给玩家
            std::string updateSql = R"(
                UPDATE sys_player_balance 
                SET amount = amount + ? 
                WHERE player_id = ?
            )";
            std::vector<MySQLParamValue> updateParams = {
                utils::roundToTwoDecimals(betAmount), // betAmount 是正数，退还给玩家
                playerId
            };
            
            int64_t updateCount = client.safeExecuteUpdate(updateSql, updateParams, 1);
            if (updateCount != 1) {
                LOG_ERROR("Failed to update balance for player %lu", playerId);
                return false;
            }
            
            // 5. 插入交易记录 (change_type = 5)
            std::string transId = utils::generateTransId(); // 生成交易ID
            std::string transSql = R"(
                INSERT INTO player_change_record (player_id, order_no, change_type, before_amount, change_amount, after_amount, description, oper_ip, merchant_id) 
                VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
            )";
            std::vector<MySQLParamValue> transParams = {
                playerId,
                transId, // 使用交易ID
                static_cast<int8_t>(TransactionType::CANCEL), // change_type = 5
                utils::roundToTwoDecimals(before_amount),
                utils::roundToTwoDecimals(betAmount), // change_amount 是退还的金额（正数）
                utils::roundToTwoDecimals(before_amount + betAmount), // after_amount
                roundID, // description
                playerIP, // oper_ip
                playerInGame->getUser()->getMerchantId() // merchant_id
            };
            
            int64_t transInsertCount = client.safeExecuteUpdate(transSql, transParams, 1);
            if (transInsertCount != 1) {
                LOG_ERROR("Failed to insert transaction record for player %lu", playerId);
                return false;
            }
            
            double new_balance = before_amount + betAmount;
            
            // 直接更新PlayerInGame的余额（线程安全）
            if (playerInGame) {
                playerInGame->setBalance(new_balance);
            }
            
            LOG_INFO("Cancel update completed for player %lu, new balance: %.2f", playerId, new_balance);
            return true;
            
        } catch (const std::exception& e) {
            LOG_ERROR("Database error during cancel update: %s", e.what());
            return false;
        }
    });
    
    return success;
}

// 批量结算失败订单
int64_t MinesGameRepositoryImpl::updateFailedOrders(const std::string& roundID) {
    if (!dbFactory_) {
        LOG_ERROR("Database factory not available for losed orders update");
        return -1;
    }
    
    auto mysqlClient = dbFactory_->getMySQLClientV2();
    if (!mysqlClient) {
        LOG_ERROR("Failed to get MySQL client for losed orders update");
        return -1;
    }
    
    try {
        // 处理status=0的订单，将状态更新为1
        std::string updateSqlStatus0 = R"(
            UPDATE mines_pro_order 
            SET win_amount = 0, 
                win_multiple = 0, 
                settle_time = NOW(), 
                status = 1 
            WHERE round_id = ? 
                AND status = 0
        )";
        
        // 处理status=2的订单，只更新结算字段，不更新状态
        std::string updateSqlStatus2 = R"(
            UPDATE mines_pro_order 
            SET win_amount = 0, 
                win_multiple = 0, 
                settle_time = NOW()
            WHERE round_id = ? 
                AND status = 2
        )";
        
        std::vector<MySQLParamValue> params = { roundID };
        
        int64_t affectedRows0 = mysqlClient->safeExecuteUpdate(updateSqlStatus0, params);
        if (affectedRows0 < 0) {
            LOG_ERROR("Failed to update status=0 orders for round %s", roundID.c_str());
            return -1;
        }
        
        int64_t affectedRows2 = mysqlClient->safeExecuteUpdate(updateSqlStatus2, params);
        if (affectedRows2 < 0) {
            LOG_ERROR("Failed to update status=2 orders for round %s", roundID.c_str());
            return -1;
        }
        
        int64_t totalUpdated = affectedRows0 + affectedRows2;
        LOG_INFO("Mine explosion: updated %ld status=0 orders and %ld status=2 orders (total: %ld) for round %s", 
                 affectedRows0, affectedRows2, totalUpdated, roundID.c_str());
        
        return totalUpdated;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to update losed orders for round %s: %s", roundID.c_str(), e.what());
        return -1;
    }
}