#ifndef I_USER_REPOSITORY_H
#define I_USER_REPOSITORY_H

#include <memory>
#include <string>
#include "core/domain/models/user.h"

// 用户仓库接口 - 定义用户数据访问的契约
class IUserRepository {
public:
    virtual ~IUserRepository() = default;
    
    // 根据登录名查找用户
    virtual std::shared_ptr<User> findByLoginName(const std::string& loginName, const std::string& gameType = "") = 0;
    
    // 根据登录名和令牌验证用户
    virtual bool validateUserToken(const std::string& loginName, const std::string& token) = 0;
    
    // 更新用户最后登录信息
    virtual bool updateLastLogin(const std::string& loginName, const std::string& ipAddress) = 0;
    
    // 更新用户Redis信息
    virtual bool updateUserRedis(const std::string& loginName) = 0;
    
    // 清理用户Redis信息
    virtual bool clearUserRedis(const std::string& loginName) = 0;
};

#endif // I_USER_REPOSITORY_H 