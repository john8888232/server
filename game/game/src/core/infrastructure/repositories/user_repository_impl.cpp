#include "user_repository_impl.h"
#include "third_party/libuv_cpp/include/LogWriter.hpp"
#include "core/infrastructure/persistence/database_factory.h"
#include "core/infrastructure/common/app_context.h"
#include "core/infrastructure/common/dependency_container.h"

extern DependencyContainer& getDependencyContainer();

UserRepositoryImpl::UserRepositoryImpl() {
}

UserRepositoryImpl::~UserRepositoryImpl() {
}

std::shared_ptr<MySQLClientV2> UserRepositoryImpl::getMySQLClient() {
    auto& container = getDependencyContainer();
    auto dbFactory = container.resolve<DatabaseFactory>();
    if (!dbFactory) {
        LOG_ERROR("DatabaseFactory not available in dependency container");
        return nullptr;
    }
    return dbFactory->getMySQLClientV2();
}

std::shared_ptr<RedisClient> UserRepositoryImpl::getRedisClient() {
    auto& container = getDependencyContainer();
    auto dbFactory = container.resolve<DatabaseFactory>();
    if (!dbFactory) {
        LOG_ERROR("DatabaseFactory not available in dependency container");
        return nullptr;
    }
    return dbFactory->getRedisClient();
}

std::shared_ptr<User> UserRepositoryImpl::findByLoginName(const std::string& loginName, const std::string& gameType) {
    auto mysqlClient = getMySQLClient();
    if (!mysqlClient) {
        LOG_ERROR("Failed to get MySQL client for finding user by login name");
        return nullptr;
    }
    
    // 根据游戏类型确定头像表名
    std::string avatarTableName = "mines_pro_player";
    if (gameType == "mines_pro") {
        avatarTableName = "mines_pro_player";
    }
    const std::string sql = 
        "SELECT p.player_id, a.avatar_url avatarUrl, p.username, p.login_name, m.currency, p.nick_name, "
        "COALESCE(b.amount, 0.00) as amount, p.player_status, p.merchant_id "
        "FROM sys_player p "
        "LEFT JOIN " + avatarTableName + " mp ON p.player_id = mp.player_id "
        "LEFT JOIN sys_avatar a ON mp.avatar = a.id "
        "LEFT JOIN sys_player_balance b ON p.player_id = b.player_id "
        "LEFT JOIN sys_user m ON p.merchant_id = m.superior_id "
        "WHERE p.login_name = ?";
    
    uint64_t playerId = 0;
    std::string avatarUrl;
    std::string username;
    std::string retrievedLoginName;
    std::string currency;
    std::string nickName;
    double amount = 0;
    int playerStatus = -1;
    int merchantId = 0;

    mysqlClient->queryWithCallback(sql, {loginName}, [&](sql::ResultSet* row) {
        playerId = row->getInt64(1);
        avatarUrl = row->getString(2);
        username = row->getString(3);
        retrievedLoginName = row->getString(4);
        currency = row->getString(5);
        nickName = row->getString(6);
        amount = row->getDouble(7);
        playerStatus = row->getInt(8);
        merchantId = row->getInt(9);
    });
    
    // 检查用户是否存在
    if (retrievedLoginName.empty()) {
        LOG_WARN("User not found: %s", loginName.c_str());
        return nullptr;
    }
    
    // 创建用户对象
    User::Status userStatus = static_cast<User::Status>(playerStatus);
    return std::make_shared<User>(playerId, avatarUrl, retrievedLoginName, username, nickName, 
                                 amount, currency, userStatus, merchantId);
}

bool UserRepositoryImpl::validateUserToken(const std::string& loginName, const std::string& token) {
    auto mysqlClient = getMySQLClient();
    if (!mysqlClient) {
        LOG_ERROR("Failed to get MySQL client for token validation");
        return false;
    }
    
    const std::string sql = "SELECT token FROM sys_player WHERE login_name = ?";
    std::string storedToken;
    
    mysqlClient->queryWithCallback(sql, {loginName}, [&](sql::ResultSet* row) {
        storedToken = row->getString(1);
    });
    
    if (storedToken.empty()) {
        LOG_WARN("No token found for user: %s", loginName.c_str());
        return false;
    }
    
    // 验证令牌
    bool isValid = (storedToken == token);
    if (!isValid) {
        LOG_WARN("Invalid token for user: %s", loginName.c_str());
    }
    
    return isValid;
}

bool UserRepositoryImpl::updateLastLogin(const std::string& loginName, const std::string& ipAddress) {
    try {
        // 获取数据库连接
        auto mysqlClient = getMySQLClient();
        if (!mysqlClient) {
            LOG_ERROR("Failed to get MySQL client for updating last login");
            return false;
        }
        // 准备SQL更新
        const std::string sql = R"(
            UPDATE 
                sys_player 
            SET 
                last_login_date = NOW(), 
                last_login_ip = ? 
            WHERE login_name = ?)";
        LOG_INFO("Executing UPDATE SQL: %s with params: ip='%s', login_name='%s'", 
                 sql.c_str(), ipAddress.c_str(), loginName.c_str());
        
        int64_t result = mysqlClient->safeExecuteUpdate(sql, {ipAddress, loginName}, 1);
        
        LOG_INFO("UPDATE result for user %s: %ld rows affected", loginName.c_str(), result);
        
        if (result == -1) {
            LOG_ERROR("Failed to update last login for player: %s after retries", loginName.c_str());
            return false;
        }
        if (result == 0) {
            LOG_ERROR("UPDATE returned 0 rows for user: %s - user exists but UPDATE failed!", loginName.c_str());
            return false;
        };
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Exception updating last login: %s", e.what());
        return false;
    }
}

bool UserRepositoryImpl::updateUserRedis(const std::string& loginName) {
    auto redisClient = getRedisClient();
    if (!redisClient) {
        LOG_ERROR("Failed to get Redis client for updating user");
        return false;
    }
    std::string key = "player:" + loginName + ":onlineInfo";
    try {
        auto& container = getDependencyContainer();
        auto appContext = container.resolve<AppContext>();
        if (!appContext) {
            LOG_ERROR("AppContext not available in dependency container");
            return false;
        }
        
        auto configManager = appContext->getConfigManager();
        if (!configManager) {
            LOG_ERROR("ConfigManager is null");
            return false;
        }
        
        const auto& serverConfig = configManager->getServerConfig();
        if (!serverConfig.contains("consul") || !serverConfig["consul"].contains("service_id")) {
            LOG_ERROR("Missing consul service_id in server config");
            return false;
        }
        
        std::string value = serverConfig["consul"]["service_id"].get<std::string>();
        redisClient->setnx(key, value);
    } catch (const std::exception& e) {
        LOG_ERROR("Exception updating user redis: %s", e.what());
        return false;
    }
    return true;
}

bool UserRepositoryImpl::clearUserRedis(const std::string& loginName) {
    auto redisClient = getRedisClient();
    if (!redisClient) {
        LOG_ERROR("Failed to get Redis client for clearing user");
        return false;
    }
    std::string key = "player:" + loginName + ":onlineInfo";
    try {
        bool deleted = redisClient->del(key);
        if (deleted) {
            LOG_INFO("Successfully cleared Redis key for player: %s", loginName.c_str());
        } else {
            LOG_WARN("Redis key not found or already deleted for player: %s", loginName.c_str());
        }
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Exception clearing user redis for %s: %s", loginName.c_str(), e.what());
        return false;
    }
} 