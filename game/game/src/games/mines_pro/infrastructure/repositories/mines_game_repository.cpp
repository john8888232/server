#include "mines_game_repository.h"
#include <third_party/libuv_cpp/include/LogWriter.hpp>
#include "core/infrastructure/persistence/database_factory.h"
#include "core/infrastructure/common/app_context.h"
#include "core/infrastructure/common/error_code.h"
#include "third_party/nlohmann/json.hpp"

using json = nlohmann::json;

MinesGameRepository::MinesGameRepository() = default;
MinesGameRepository::~MinesGameRepository() = default;

MinesGameRepositoryImpl::MinesGameRepositoryImpl() = default;
MinesGameRepositoryImpl::~MinesGameRepositoryImpl() = default;

bool MinesGameRepositoryImpl::saveGame(const MinesGame& game) {
    try {
        auto mysqlClient = DatabaseFactory::getInstance().getMySQLClient();
        if (!mysqlClient) {
            LOG_ERROR("Failed to get MySQL client for saving game");
            return false;
        }
        
        // 序列化游戏数据
        std::string gameData = serializeGame(game);
        
        const std::string sql = 
            "INSERT INTO mines_game (game_id, user_id, status, game_data, created_at) "
            "VALUES (?, ?, ?, ?, NOW()) "
            "ON DUPLICATE KEY UPDATE status = VALUES(status), game_data = VALUES(game_data), updated_at = NOW()";
        
        // 执行保存
        uint64_t affectedRows = mysqlClient->executeUpdate(sql, {
            game.getRoundId(),
            game.getUserId(),
            static_cast<int>(game.getStatus()),
            gameData
        });
        
        return affectedRows > 0;
    } catch (const std::exception& e) {
        LOG_ERROR("Exception saving game: %s", e.what());
        return false;
    }
}

std::shared_ptr<MinesGame> MinesGameRepositoryImpl::loadActiveGame(int64_t userId) {
    try {
        auto mysqlClient = DatabaseFactory::getInstance().getMySQLClient();
        if (!mysqlClient) {
            LOG_ERROR("Failed to get MySQL client for loading active game");
            return nullptr;
        }
        
        const std::string sql = 
            "SELECT game_id, game_data FROM mines_game "
            "WHERE user_id = ? AND status IN (0, 1) "
            "ORDER BY created_at DESC LIMIT 1";
        
        std::string gameData;
        mysqlClient->queryWithCallback(sql, {userId}, [&](const mysqlx::Row& row) {
            gameData = row[1].get<std::string>();
        });
        
        if (gameData.empty()) {
            return nullptr;
        }
        
        return deserializeGame(gameData);
    } catch (const std::exception& e) {
        LOG_ERROR("Exception loading active game: %s", e.what());
        return nullptr;
    }
}

std::shared_ptr<MinesGame> MinesGameRepositoryImpl::loadGame(const std::string& gameId) {
    try {
        auto mysqlClient = DatabaseFactory::getInstance().getMySQLClient();
        if (!mysqlClient) {
            LOG_ERROR("Failed to get MySQL client for loading game");
            return nullptr;
        }
        
        const std::string sql = 
            "SELECT game_data FROM mines_game WHERE game_id = ?";
        
        std::string gameData;
        mysqlClient->queryWithCallback(sql, {gameId}, [&](const mysqlx::Row& row) {
            gameData = row[0].get<std::string>();
        });
        
        if (gameData.empty()) {
            return nullptr;
        }
        
        return deserializeGame(gameData);
    } catch (const std::exception& e) {
        LOG_ERROR("Exception loading game: %s", e.what());
        return nullptr;
    }
}

bool MinesGameRepositoryImpl::deleteGame(const std::string& gameId) {
    try {
        auto mysqlClient = DatabaseFactory::getInstance().getMySQLClient();
        if (!mysqlClient) {
            LOG_ERROR("Failed to get MySQL client for deleting game");
            return false;
        }
        
        const std::string sql = "DELETE FROM mines_game WHERE game_id = ?";
        uint64_t affectedRows = mysqlClient->executeUpdate(sql, {gameId});
        
        return affectedRows > 0;
    } catch (const std::exception& e) {
        LOG_ERROR("Exception deleting game: %s", e.what());
        return false;
    }
}

bool MinesGameRepositoryImpl::updateGameStatus(const std::string& gameId, GameStatus status) {
    try {
        auto mysqlClient = DatabaseFactory::getInstance().getMySQLClient();
        if (!mysqlClient) {
            LOG_ERROR("Failed to get MySQL client for updating game status");
            return false;
        }
        
        const std::string sql = 
            "UPDATE mines_game SET status = ?, updated_at = NOW() WHERE game_id = ?";
        
        uint64_t affectedRows = mysqlClient->executeUpdate(sql, {
            static_cast<int>(status),
            gameId
        });
        
        return affectedRows > 0;
    } catch (const std::exception& e) {
        LOG_ERROR("Exception updating game status: %s", e.what());
        return false;
    }
}

std::vector<std::shared_ptr<MinesGame>> MinesGameRepositoryImpl::getUserGameHistory(int64_t userId, int limit) {
    std::vector<std::shared_ptr<MinesGame>> games;
    
    try {
        auto mysqlClient = DatabaseFactory::getInstance().getMySQLClient();
        if (!mysqlClient) {
            LOG_ERROR("Failed to get MySQL client for getting user game history");
            return games;
        }
        
        const std::string sql = 
            "SELECT game_data FROM mines_game "
            "WHERE user_id = ? "
            "ORDER BY created_at DESC LIMIT ?";
        
        mysqlClient->queryWithCallback(sql, {userId, limit}, [&](const mysqlx::Row& row) {
            std::string gameData = row[0].get<std::string>();
            auto game = deserializeGame(gameData);
            if (game) {
                games.push_back(game);
            }
        });
        
    } catch (const std::exception& e) {
        LOG_ERROR("Exception getting user game history: %s", e.what());
    }
    
    return games;
}

// 更新玩家余额的方法
bool MinesGameRepositoryImpl::updatePlayerBalance(const std::string& loginName, double newBalance) {
    try {
        auto mysqlClient = DatabaseFactory::getInstance().getMySQLClient();
        if (!mysqlClient) {
            LOG_ERROR("Failed to get MySQL client for updating player balance");
            return false;
        }
        
        const std::string sql = 
            "UPDATE sys_player SET amount = ?, update_time = NOW() WHERE login_name = ?";
        
        uint64_t affectedRows = mysqlClient->executeUpdate(sql, {newBalance, loginName});
        
        if (affectedRows == 0) {
            LOG_ERROR("No rows updated when updating balance for player: %s", loginName.c_str());
            return false;
        }
        
        LOG_INFO("Successfully updated balance for player %s to %.2f", loginName.c_str(), newBalance);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Exception updating player balance for %s: %s", loginName.c_str(), e.what());
        return false;
    }
}

// 获取玩家当前余额
double MinesGameRepositoryImpl::getPlayerBalance(const std::string& loginName) {
    try {
        auto mysqlClient = DatabaseFactory::getInstance().getMySQLClient();
        if (!mysqlClient) {
            LOG_ERROR("Failed to get MySQL client for getting player balance");
            return -1.0;
        }
        
        const std::string sql = 
            "SELECT amount FROM sys_player WHERE login_name = ?";
        
        double balance = -1.0;
        mysqlClient->queryWithCallback(sql, {loginName}, [&](const mysqlx::Row& row) {
            balance = row[0].get<double>();
        });
        
        return balance;
    } catch (const std::exception& e) {
        LOG_ERROR("Exception getting player balance for %s: %s", loginName.c_str(), e.what());
        return -1.0;
    }
}

// 批量更新玩家余额 - 每个玩家单独事务处理
std::vector<PlayerBalanceUpdateResult> MinesGameRepositoryImpl::updatePlayerBalancesBatch(
    const std::vector<PlayerBalanceUpdate>& updates) {
    
    std::vector<PlayerBalanceUpdateResult> results;
    results.reserve(updates.size());
    
    for (const auto& update : updates) {
        PlayerBalanceUpdateResult result;
        result.loginName = update.loginName;
        result.originalBalance = update.originalBalance;
        result.newBalance = update.newBalance;
        result.success = false;
        
        try {
            auto mysqlClient = DatabaseFactory::getInstance().getMySQLClient();
            if (!mysqlClient) {
                LOG_ERROR("Failed to get MySQL client for updating player balance: %s", 
                         update.loginName.c_str());
                result.errorMessage = "Database connection failed";
                results.push_back(result);
                continue;
            }
            
            // 每个玩家使用单独的事务
            bool transactionResult = mysqlClient->executeTransaction([&](MySQLClient& client) -> bool {
                // 先检查当前余额是否符合预期
                const std::string selectSql = 
                    "SELECT amount FROM sys_player WHERE login_name = ? FOR UPDATE";
                
                double currentBalance = -1.0;
                client.queryWithCallback(selectSql, {update.loginName}, [&](const mysqlx::Row& row) {
                    currentBalance = row[0].get<double>();
                });
                
                if (currentBalance < 0) {
                    LOG_ERROR("Player %s not found in database", update.loginName.c_str());
                    return false;
                }
                
                // 检查余额是否匹配（允许小的浮点误差）
                if (std::abs(currentBalance - update.originalBalance) > 0.001) {
                    LOG_ERROR("Balance mismatch for player %s: expected %.2f, got %.2f", 
                             update.loginName.c_str(), update.originalBalance, currentBalance);
                    return false;
                }
                
                // 更新余额
                const std::string updateSql = 
                    "UPDATE sys_player SET amount = ?, update_time = NOW() WHERE login_name = ?";
                
                uint64_t affectedRows = client.executeUpdate(updateSql, {
                    update.newBalance, 
                    update.loginName
                });
                
                if (affectedRows == 0) {
                    LOG_ERROR("No rows updated when updating balance for player: %s", 
                             update.loginName.c_str());
                    return false;
                }
                
                return true;
            });
            
            if (transactionResult) {
                result.success = true;
                result.actualBalance = update.newBalance;
                LOG_INFO("Successfully updated balance for player %s: %.2f -> %.2f", 
                        update.loginName.c_str(), update.originalBalance, update.newBalance);
            } else {
                // 获取实际余额用于返回
                double actualBalance = getPlayerBalance(update.loginName);
                result.actualBalance = actualBalance >= 0 ? actualBalance : update.originalBalance;
                result.errorMessage = "Database transaction failed";
                LOG_ERROR("Failed to update balance for player %s", update.loginName.c_str());
            }
            
        } catch (const std::exception& e) {
            result.errorMessage = e.what();
            result.actualBalance = update.originalBalance;
            LOG_ERROR("Exception updating balance for player %s: %s", 
                     update.loginName.c_str(), e.what());
        }
        
        results.push_back(result);
    }
    
    return results;
}

std::string MinesGameRepositoryImpl::serializeGame(const MinesGame& game) {
    try {
        json gameJson;
        gameJson["game_id"] = game.getRoundId();
        gameJson["user_id"] = game.getUserId();
        gameJson["status"] = static_cast<int>(game.getStatus());
        gameJson["mines_count"] = game.getMinesCount();
        gameJson["total_tiles"] = game.getTotalTiles();
        gameJson["seed"] = game.getSeed();
        
        // 序列化游戏结果
        auto results = game.getGameResults();
        json resultsJson = json::array();
        for (const auto& result : results) {
            json resultJson;
            resultJson["index"] = result.index();
            resultJson["multi"] = result.multi();
            resultsJson.push_back(resultJson);
        }
        gameJson["results"] = resultsJson;
        
        return gameJson.dump();
    } catch (const std::exception& e) {
        LOG_ERROR("Exception serializing game: %s", e.what());
        return "";
    }
}

std::shared_ptr<MinesGame> MinesGameRepositoryImpl::deserializeGame(const std::string& jsonData) {
    try {
        json gameJson = json::parse(jsonData);
        
        // 由于MinesGame构造函数可能比较复杂，这里简化处理
        // 实际实现需要根据MinesGame的构造函数来调整
        auto game = std::make_shared<MinesGame>();
        
        // 设置基本属性
        if (gameJson.contains("seed")) {
            game->setSeed(gameJson["seed"].get<std::string>());
        }
        
        // 可以根据需要添加更多的反序列化逻辑
        
        return game;
    } catch (const std::exception& e) {
        LOG_ERROR("Exception deserializing game: %s", e.what());
        return nullptr;
    }
}