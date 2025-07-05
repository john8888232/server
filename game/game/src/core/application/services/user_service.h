#ifndef USER_SERVICE_H
#define USER_SERVICE_H

#include <memory>
#include <string>
#include "core/application/interfaces/i_user_service.h"
#include "core/domain/models/user.h"
#include "core/infrastructure/proto/game.pb.h"

// 用户服务 - 应用层服务，处理用户相关业务逻辑
class UserService : public IUserService {
public:
    UserService();
    ~UserService() override;
    
    // 处理用户登录请求
    LoginResult processLogin(const proto::LoginReq& request, const std::string& sessionId) override;
    
    // 更新用户最后登录信息
    bool updateLastLogin(const std::string& loginname, const std::string& ipAddress) override;

    // 更新用户redis信息
    bool updateUserRedis(const std::string& loginname) override;
    
    // 清理用户redis信息
    bool clearUserRedis(const std::string& loginname) override;
};

#endif // USER_SERVICE_H
