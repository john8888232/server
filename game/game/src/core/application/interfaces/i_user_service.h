#ifndef I_USER_SERVICE_H
#define I_USER_SERVICE_H

#include <memory>
#include <string>
#include "core/domain/models/user.h"
#include "core/infrastructure/proto/game.pb.h"

// 用户服务接口 - 定义应用层用户服务的契约
class IUserService {
public:
    // 登录结果结构
    struct LoginResult {
        bool success;
        int errorCode;
        std::string errorMessage;
        std::shared_ptr<User> user;
    };
    
    virtual ~IUserService() = default;
    
    // 处理用户登录请求
    virtual LoginResult processLogin(const proto::LoginReq& request, const std::string& sessionId) = 0;
    
    // 更新用户最后登录信息
    virtual bool updateLastLogin(const std::string& loginname, const std::string& ipAddress) = 0;

    // 更新用户redis信息
    virtual bool updateUserRedis(const std::string& loginname) = 0;
    
    // 清理用户redis信息
    virtual bool clearUserRedis(const std::string& loginname) = 0;
};

#endif // I_USER_SERVICE_H

