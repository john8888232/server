#ifndef USER_REPOSITORY_IMPL_H
#define USER_REPOSITORY_IMPL_H

#include <memory>
#include <string>
#include "core/domain/repositories/i_user_repository.h"
#include "core/domain/models/user.h"
#include "core/infrastructure/persistence/mysql_client.h"
#include "core/infrastructure/persistence/redis_client.h"

// 用户仓库MySQL实现
class UserRepositoryImpl : public IUserRepository {
public:
    UserRepositoryImpl();
    ~UserRepositoryImpl() override;
    
    // 根据登录名查找用户
    std::shared_ptr<User> findByLoginName(const std::string& loginName) override;
    
    // 根据登录名和令牌验证用户
    bool validateUserToken(const std::string& loginName, const std::string& token) override;
    
    // 更新用户最后登录信息
    bool updateLastLogin(const std::string& loginName, const std::string& ipAddress) override;
    
    // 更新用户Redis信息
    bool updateUserRedis(const std::string& loginName) override;
    
    // 清理用户Redis信息
    bool clearUserRedis(const std::string& loginName) override;
    
private:
    // 获取MySQL客户端
    std::shared_ptr<MySQLClient> getMySQLClient();
    
    // 获取Redis客户端
    std::shared_ptr<RedisClient> getRedisClient();
};

#endif // USER_REPOSITORY_IMPL_H 