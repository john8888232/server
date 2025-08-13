#ifndef DATABASE_FACTORY_H
#define DATABASE_FACTORY_H

#include <memory>
#include <mutex>
#include "core/infrastructure/common/config_manager.h"
#include "mysql_clientV2.h"
#include "redis_client.h"

class DatabaseFactory {
public:
    DatabaseFactory() = default;
    
    ~DatabaseFactory() = default;
    
    DatabaseFactory(const DatabaseFactory&) = delete;
    DatabaseFactory& operator=(const DatabaseFactory&) = delete;
    
    bool initialize(const ConfigManager& configManager);
    
    // 清理数据库连接
    void cleanup();
    
    // 获取数据库客户端
    std::shared_ptr<MySQLClientV2> getMySQLClientV2();
    std::shared_ptr<RedisClient> getRedisClient();
    
private:
    bool isInitialized_ = false;
    std::mutex mutex_;
    
    std::shared_ptr<MySQLClientV2> mysqlClientV2_;
    std::shared_ptr<RedisClient> redisClient_;
};

#endif // DATABASE_FACTORY_H
