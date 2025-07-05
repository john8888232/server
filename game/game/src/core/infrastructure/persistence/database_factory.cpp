#include "database_factory.h"
#include <third_party/libuv_cpp/include/LogWriter.hpp>
#include "mysql_client.h"
#include "redis_client.h"
#include "elastic_client.h"

bool DatabaseFactory::initialize(const ConfigManager& configManager) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (isInitialized_) {
        LOG_WARN("Database factory already initialized");
        return true;
    }
    
    try {
        // 初始化MySQL
        mysqlClient_ = std::make_shared<MySQLClient>();
        if (!mysqlClient_->initialize(configManager)) { 
            LOG_ERROR("MySQL initialization failed");
            return false;
        }
        if (!mysqlClient_->connect()) {
            LOG_ERROR("MySQL connection failed");
            return false;
        }

        // 初始化Redis
        redisClient_ = std::make_shared<RedisClient>();
        if (!redisClient_->initialize(configManager)) {
            LOG_ERROR("Redis initialization failed");
            return false;
        }
        if (!redisClient_->connect()) {
            LOG_ERROR("Redis connection failed");
            return false;
        }

        // 初始化Elasticsearch
        elasticClient_ = std::make_shared<ElasticClient>();
        if (!elasticClient_->initialize(configManager)) {
            LOG_ERROR("ES initialization failed");
            return false;
        }
        if (!elasticClient_->connect()) {
            LOG_ERROR("ES connection failed");
            return false;
        }

        isInitialized_ = true;
        LOG_INFO("Database factory initialized successfully");
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Database initialization failed: %s", e.what());
        // 清理已创建的资源
        mysqlClient_.reset();
        redisClient_.reset();
        elasticClient_.reset();
        return false;
    }
}

void DatabaseFactory::cleanup() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!isInitialized_) {
        LOG_WARN("DatabaseFactory not initialized, nothing to clean up");
        return;
    }
    
    try {
        // 关闭MySQL连接
        if (mysqlClient_) {
            LOG_INFO("Closing MySQL connections...");
            mysqlClient_->disconnect();
            mysqlClient_.reset();
        }
        
        // 关闭Redis连接
        if (redisClient_) {
            LOG_INFO("Closing Redis connections...");
            redisClient_->disconnect();
            redisClient_.reset();
        }
        
        // 关闭Elasticsearch连接
        if (elasticClient_) {
            LOG_INFO("Closing Elasticsearch connections...");
            elasticClient_->disconnect();
            elasticClient_.reset();
        }
        
        isInitialized_ = false;
        LOG_INFO("Database connections closed successfully");
    } catch (const std::exception& e) {
        LOG_ERROR("Error during database cleanup: %s", e.what());
    }
}

std::shared_ptr<MySQLClient> DatabaseFactory::getMySQLClient() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!isInitialized_) {
        LOG_ERROR("DatabaseFactory not initialized");
        return nullptr;
    }
    
    return mysqlClient_;
}

std::shared_ptr<RedisClient> DatabaseFactory::getRedisClient() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!isInitialized_) {
        LOG_ERROR("DatabaseFactory not initialized");
        return nullptr;
    }
    
    return redisClient_;
}

std::shared_ptr<ElasticClient> DatabaseFactory::getElasticClient() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!isInitialized_) {
        LOG_ERROR("DatabaseFactory not initialized");
        return nullptr;
    }
    
    return elasticClient_;
} 