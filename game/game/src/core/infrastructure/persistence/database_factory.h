#ifndef DATABASE_FACTORY_H
#define DATABASE_FACTORY_H

#include <memory>
#include <string>
#include <mutex>
#include "../common/config_manager.h"
#include "mysql_client.h"
#include "redis_client.h"
#include "elastic_client.h"


class DatabaseFactory {
public:
    static DatabaseFactory& getInstance() {
        static DatabaseFactory instance;
        return instance;
    }
    
    bool initialize(const ConfigManager& configManager);
    
    // 清理数据库连接
    void cleanup();
    
    // 获取数据库客户端
    std::shared_ptr<MySQLClient> getMySQLClient();
    std::shared_ptr<RedisClient> getRedisClient();
    std::shared_ptr<ElasticClient> getElasticClient();
    
private:
    DatabaseFactory() = default;
    ~DatabaseFactory() = default;
    DatabaseFactory(const DatabaseFactory&) = delete;
    DatabaseFactory& operator=(const DatabaseFactory&) = delete;
    
    bool isInitialized_ = false;
    std::mutex mutex_;
    
    std::shared_ptr<MySQLClient> mysqlClient_;
    std::shared_ptr<RedisClient> redisClient_;
    std::shared_ptr<ElasticClient> elasticClient_;
};

#endif // DATABASE_FACTORY_H
