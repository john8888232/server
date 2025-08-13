#include "user_service.h"
#include "third_party/libuv_cpp/include/LogWriter.hpp"
#include "core/infrastructure/common/error_code.h"
#include "core/infrastructure/repositories/user_repository_impl.h"

UserService::UserService() {
    // 创建用户仓库实例
    userRepository_ = std::make_shared<UserRepositoryImpl>();
}

UserService::~UserService() {
}

UserService::LoginResult UserService::processLogin(const proto::LoginReq& request, const std::string& sessionId) {
    LoginResult result;
    result.success = false;
    
    try {
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
        
        // 验证用户令牌 - 直接通过接口调用，不再使用dynamic_cast
        if (!userRepository_->validateUserToken(request.loginname(), request.token())) {
            LOG_WARN("Invalid token for user: %s", request.loginname().c_str());
            result.errorCode = ErrorCode::INVALID_CREDENTIALS;
            result.errorMessage = ErrorCode::getErrorMessage(result.errorCode);
            return result;
        }
        
        // 使用仓库查找用户，传递游戏类型
        auto user = userRepository_->findByLoginName(request.loginname(), request.game_type());
        
        // 检查用户是否存在
        if (!user) {
            LOG_WARN("User not found: %s", request.loginname().c_str());
            result.errorCode = ErrorCode::NOT_FOUND;
            result.errorMessage = ErrorCode::getErrorMessage(result.errorCode);
            return result;
        }
        
        // 检查账号状态
        if (user->getStatus() == User::Status::Disabled) {
            LOG_WARN("User account is disabled: %s", request.loginname().c_str());
            result.errorCode = ErrorCode::DISABLED;
            result.errorMessage = ErrorCode::getErrorMessage(result.errorCode);
            return result;
        }
        
        // 更新最后登录信息
        auto success = updateLastLogin(request.loginname(), request.client_ip());
        if (!success) {
            LOG_ERROR("Failed to update last login info for player ID: %s", request.loginname().c_str());
            result.errorCode = ErrorCode::DATABASE_ERROR;
            result.errorMessage = ErrorCode::getErrorMessage(result.errorCode);
            return result;
        }
        
        // 更新Redis信息
        success = updateUserRedis(request.loginname());
        if (!success) {
            LOG_ERROR("Failed to update user redis for player ID: %s", request.loginname().c_str());
            result.errorCode = ErrorCode::DATABASE_ERROR;
            result.errorMessage = ErrorCode::getErrorMessage(result.errorCode);
            return result;
        }
        
        // 设置成功结果
        result.success = true;
        result.errorCode = ErrorCode::SUCCESS;
        result.errorMessage = ErrorCode::getErrorMessage(result.errorCode);
        result.user = user;
        
        LOG_INFO("User %s (ID: %ld) logged in successfully from IP: %s", 
                user->getLoginName().c_str(), user->getPlayerId(), request.client_ip().c_str());
        
        return result;
    } catch (const std::exception& e) {
        LOG_ERROR("Exception during login processing: %s", e.what());
        result.errorCode = ErrorCode::SYSTEM_ERROR;
        result.errorMessage = ErrorCode::getErrorMessage(result.errorCode);
        return result;
    }
}

bool UserService::updateLastLogin(const std::string& loginname, const std::string& ipAddress) {
    // 委托给仓库实现
    return userRepository_->updateLastLogin(loginname, ipAddress);
}

bool UserService::updateUserRedis(const std::string& loginname) {
    // 委托给仓库实现
    return userRepository_->updateUserRedis(loginname);
}

bool UserService::clearUserRedis(const std::string& loginname) {
    // 委托给仓库实现
    return userRepository_->clearUserRedis(loginname);
}
