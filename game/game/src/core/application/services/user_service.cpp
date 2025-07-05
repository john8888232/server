#include "user_service.h"
#include <third_party/libuv_cpp/include/LogWriter.hpp>
#include "core/infrastructure/persistence/database_factory.h"
#include "core/infrastructure/common/app_context.h"
#include "core/infrastructure/common/error_code.h"

UserService::UserService() {
}

UserService::~UserService() {
}

UserService::LoginResult UserService::processLogin(const proto::LoginReq& request, const std::string& sessionId) {
    LoginResult result;
    result.success = false;
    
    try {
        // 获取数据库连接
        auto mysqlClient = DatabaseFactory::getInstance().getMySQLClient();
        auto redisClient = DatabaseFactory::getInstance().getRedisClient();

        if (!mysqlClient || !redisClient) {
            LOG_ERROR("Failed to get MySQL client for login processing");
            result.errorCode = ErrorCode::DATABASE_ERROR;
            result.errorMessage = ErrorCode::getErrorMessage(result.errorCode);
            return result;
        }
                
        if (request.loginname().empty()) {
            LOG_WARN("Empty user ID in login request");
            result.errorCode = ErrorCode::INVALID_USER_ID;
            result.errorMessage = ErrorCode::getErrorMessage(result.errorCode);
            return result;
        }
        
        if (request.token().empty()) {
            LOG_WARN("Empty token in login request");
            result.errorCode = ErrorCode::INVALID_TOKEN;
            result.errorMessage = ErrorCode::getErrorMessage(result.errorCode);
            return result;
        }
        
        const std::string sql = 
            "SELECT player_id, avatar, username, login_name, currency, nick_name, amount, vip_level, player_status, token "
            "FROM sys_player WHERE login_name = ?";
        
        int64_t playerId = 0;
        int avatarId = 1;
        std::string username;
        std::string loginName;
        std::string currency;
        std::string nickName;
        double amount = 0;
        int vipLevel = 0;
        int playerStatus = -1;
        std::string token;
        
        // 执行查询并处理结果
        mysqlClient->queryWithCallback(sql, {request.loginname()}, [&](const mysqlx::Row& row) {
            playerId = row[0].get<int64_t>();
            avatarId = row[1].get<int>();
            username = row[2].get<std::string>();
            loginName = row[3].get<std::string>();
            currency = row[4].get<std::string>();
            nickName = row[5].get<std::string>();
            amount = row[6].get<double>();
            vipLevel = row[7].get<int>();
            playerStatus = row[8].get<int>();
            token = row[9].get<std::string>();
        });
        
        // 检查用户是否存在
        if (loginName.empty()) {
            LOG_WARN("User not found: %s", request.loginname().c_str());
            result.errorCode = ErrorCode::NOT_FOUND;
            result.errorMessage = ErrorCode::getErrorMessage(result.errorCode);
            return result;
        }
        
        // 验证密码/令牌
        if (token != request.token()) {
            LOG_WARN("Invalid token for user: %s", request.loginname().c_str());
            result.errorCode = ErrorCode::INVALID_CREDENTIALS;
            result.errorMessage = ErrorCode::getErrorMessage(result.errorCode);
            return result;
        }
        
        // 检查账号状态
        if (playerStatus == 1) {
            LOG_WARN("User account is disabled: %s (status: %d)", request.loginname().c_str(), playerStatus);
            result.errorCode = ErrorCode::DISABLED;
            result.errorMessage = ErrorCode::getErrorMessage(result.errorCode);
            return result;
        }
        
        // 创建用户对象
        User::Status userStatus = static_cast<User::Status>(playerStatus);
        result.user = std::make_shared<User>(playerId, avatarId, loginName, nickName, 
                                            amount, vipLevel, currency, userStatus);
        
        auto success = updateLastLogin(loginName, request.client_ip());
        if (!success) {
            LOG_ERROR("Failed to update last login info for player ID: %s", loginName.c_str());
            result.errorCode = ErrorCode::DATABASE_ERROR;
            result.errorMessage = ErrorCode::getErrorMessage(result.errorCode);
            return result;
        }
        success = updateUserRedis(loginName);
        if (!success) {
            LOG_ERROR("Failed to update user redis for player ID: %s", loginName.c_str());
            result.errorCode = ErrorCode::DATABASE_ERROR;
            result.errorMessage = ErrorCode::getErrorMessage(result.errorCode);
            return result;
        }
        // 设置成功结果
        result.success = true;
        result.errorCode = ErrorCode::SUCCESS;
        result.errorMessage = ErrorCode::getErrorMessage(result.errorCode);
        
        LOG_INFO("User %s (ID: %ld) logged in successfully from IP: %s", 
                loginName.c_str(), playerId, request.client_ip().c_str());
        
        return result;
    } catch (const std::exception& e) {
        LOG_ERROR("Exception during login processing: %s", e.what());
        result.errorCode = ErrorCode::SYSTEM_ERROR;
        result.errorMessage = ErrorCode::getErrorMessage(result.errorCode);
        return result;
    }
}

bool UserService::updateLastLogin(const std::string& loginname, const std::string& ipAddress) {
    try {
        // 获取数据库连接
        auto mysqlClient = DatabaseFactory::getInstance().getMySQLClient();
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
        uint64_t affectedRows = mysqlClient->executeUpdate(sql, {ipAddress, loginname});
        
        if (affectedRows == 0) {
            LOG_WARN("No rows updated when updating last login for player : %s", loginname.c_str());
            return false;
        }
        
        LOG_DEBUG("Updated last login info for player : %s", loginname.c_str());
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Exception updating last login: %s", e.what());
        return false;
    }
}

bool UserService::updateUserRedis(const std::string& loginname) {
    auto redisClient = DatabaseFactory::getInstance().getRedisClient();
    if (!redisClient) {
        LOG_ERROR("Failed to get Redis client for updating user");
        return false;
    }
    std::string key = "player:" + loginname + ":onlineInfo";
    try {
        auto& appContext = AppContext::getInstance();
        auto configManager = appContext.getConfigManager();
        std::string value = configManager->getServerConfig()["consul"]["service_id"].get<std::string>();
        redisClient->setnx(key, value);
    } catch (const std::exception& e) {
        LOG_ERROR("Exception updating user redis: %s", e.what());
        return false;
    }
    return true;
}

bool UserService::clearUserRedis(const std::string& loginname) {
    auto redisClient = DatabaseFactory::getInstance().getRedisClient();
    if (!redisClient) {
        LOG_ERROR("Failed to get Redis client for clearing user");
        return false;
    }
    std::string key = "player:" + loginname + ":onlineInfo";
    try {
        bool deleted = redisClient->del(key);
        if (deleted) {
            LOG_INFO("Successfully cleared Redis key for player: %s", loginname.c_str());
        } else {
            LOG_WARN("Redis key not found or already deleted for player: %s", loginname.c_str());
        }
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Exception clearing user redis for %s: %s", loginname.c_str(), e.what());
        return false;
    }
}
