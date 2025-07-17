#include "user_repository_impl.h"
#include <third_party/libuv_cpp/include/LogWriter.hpp>
#include "core/infrastructure/persistence/database_factory.h"
#include "core/infrastructure/common/app_context.h"
#include "core/infrastructure/common/dependency_container.h"

extern DependencyContainer& getDependencyContainer();

UserRepositoryImpl::UserRepositoryImpl() {
    // MySQL实现特定的初始化
}

UserRepositoryImpl::~UserRepositoryImpl() {
    // MySQL实现特定的清理
}

std::shared_ptr<MySQLClient> UserRepositoryImpl::getMySQLClient() {
    // 从依赖容器获取DatabaseFactory
    auto& container = getDependencyContainer();
    auto dbFactory = container.resolve<DatabaseFactory>();
    if (!dbFactory) {
        LOG_ERROR("DatabaseFactory not available in dependency container");
        return nullptr;
    }
    return dbFactory->getMySQLClient();
}

std::shared_ptr<RedisClient> UserRepositoryImpl::getRedisClient() {
    // 从依赖容器获取DatabaseFactory
    auto& container = getDependencyContainer();
    auto dbFactory = container.resolve<DatabaseFactory>();
    if (!dbFactory) {
        LOG_ERROR("DatabaseFactory not available in dependency container");
        return nullptr;
    }
    return dbFactory->getRedisClient();
}

std::shared_ptr<User> UserRepositoryImpl::findByLoginName(const std::string& loginName) {
    auto mysqlClient = getMySQLClient();
    if (!mysqlClient) {
        LOG_ERROR("Failed to get MySQL client for finding user by login name");
        return nullptr;
    }
    
    const std::string sql = 
        "SELECT player_id, avatar, username, login_name, currency, nick_name, amount, vip_level, player_status "
        "FROM sys_player WHERE login_name = ?";
    
    int64_t playerId = 0;
    int avatarId = 1;
    std::string username;
    std::string retrievedLoginName;
    std::string currency;
    std::string nickName;
    double amount = 0;
    int vipLevel = 0;
    int playerStatus = -1;
    
    // 执行查询并处理结果
    mysqlClient->queryWithCallback(sql, {loginName}, [&](const mysqlx::Row& row) {
        playerId = row[0].get<int64_t>();
        avatarId = row[1].get<int>();
        username = row[2].get<std::string>();
        retrievedLoginName = row[3].get<std::string>();
        currency = row[4].get<std::string>();
        nickName = row[5].get<std::string>();
        amount = row[6].get<double>();
        vipLevel = row[7].get<int>();
        playerStatus = row[8].get<int>();
    });
    
    // 检查用户是否存在
    if (retrievedLoginName.empty()) {
        LOG_WARN("User not found: %s", loginName.c_str());
        return nullptr;
    }
    
    // 创建用户对象
    User::Status userStatus = static_cast<User::Status>(playerStatus);
    return std::make_shared<User>(playerId, avatarId, retrievedLoginName, username, nickName, 
                                 amount, vipLevel, currency, userStatus);
}

bool UserRepositoryImpl::validateUserToken(const std::string& loginName, const std::string& token) {
    auto mysqlClient = getMySQLClient();
    if (!mysqlClient) {
        LOG_ERROR("Failed to get MySQL client for token validation");
        return false;
    }
    
    const std::string sql = "SELECT token FROM sys_player WHERE login_name = ?";
    std::string storedToken;
    
    mysqlClient->queryWithCallback(sql, {loginName}, [&](const mysqlx::Row& row) {
        storedToken = row[0].get<std::string>();
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
        const std::string sql = 
            "UPDATE sys_player SET "
            "last_login_day = NOW(), "
            "last_login_ip = ?, "
            "first_login_day = COALESCE(first_login_day, NOW()) "
            "WHERE login_name = ?";
        
        // 执行更新
        uint64_t affectedRows = mysqlClient->executeUpdate(sql, {ipAddress, loginName});
        
        if (affectedRows == 0) {
            LOG_WARN("No rows updated when updating last login for player: %s", loginName.c_str());
            return false;
        }
        
        LOG_DEBUG("Updated last login info for player: %s", loginName.c_str());
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
        // 从依赖容器获取AppContext
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