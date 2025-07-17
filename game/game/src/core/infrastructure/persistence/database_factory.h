#ifndef DATABASE_FACTORY_H
#define DATABASE_FACTORY_H

#include <memory>
#include <mutex>
#include "../common/config_manager.h"
#include "mysql_client.h"
#include "redis_client.h"
#include "elastic_client.h"
#include "open_search_client.h"


class DatabaseFactory {
public:
    // 构造函数
    DatabaseFactory() = default;
    
    // 析构函数
    ~DatabaseFactory() = default;
    
    // 禁止拷贝
    DatabaseFactory(const DatabaseFactory&) = delete;
    DatabaseFactory& operator=(const DatabaseFactory&) = delete;
    
    bool initialize(const ConfigManager& configManager);
    
    // 清理数据库连接
    void cleanup();
    
    // 获取数据库客户端
    std::shared_ptr<MySQLClient> getMySQLClient();
    std::shared_ptr<RedisClient> getRedisClient();
    std::shared_ptr<ElasticClient> getElasticClient();
    std::shared_ptr<OpenSearchClient> getOpenSearchClient();
    
private:
    bool isInitialized_ = false;
    std::mutex mutex_;
    
    std::shared_ptr<MySQLClient> mysqlClient_;
    std::shared_ptr<RedisClient> redisClient_;
    std::shared_ptr<ElasticClient> elasticClient_;
    std::shared_ptr<OpenSearchClient> openSearchClient_;
};

#endif // DATABASE_FACTORY_H
