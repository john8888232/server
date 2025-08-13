#include "database_factory.h"
#include "third_party/libuv_cpp/include/LogWriter.hpp"
#include "mysql_clientV2.h"
#include "redis_client.h"

bool DatabaseFactory::initialize(const ConfigManager& configManager) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (isInitialized_) {
        LOG_WARN("Database factory already initialized");
        return true;
    }
    
    try {
        // 初始化MySQLV2
        mysqlClientV2_ = std::make_shared<MySQLClientV2>();
        if (!mysqlClientV2_->initialize(configManager)) { 
            LOG_ERROR("MySQLV2 initialization failed");
            return false;
        }
        if (!mysqlClientV2_->connect()) {
            LOG_ERROR("MySQLV2 connection failed");
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

        isInitialized_ = true;
        LOG_INFO("Database factory initialized successfully");
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Database initialization failed: %s", e.what());
        // 清理已创建的资源
        mysqlClientV2_.reset();
        redisClient_.reset();
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
        // 关闭MySQLV2连接
        if (mysqlClientV2_) {
            LOG_INFO("Closing MySQLV2 connections...");
            mysqlClientV2_->disconnect();
            mysqlClientV2_.reset();
        }
        
        // 关闭Redis连接
        if (redisClient_) {
            LOG_INFO("Closing Redis connections...");
            redisClient_->disconnect();
            redisClient_.reset();
        }
        
        isInitialized_ = false;
        LOG_INFO("Database connections closed successfully");
    } catch (const std::exception& e) {
        LOG_ERROR("Error during database cleanup: %s", e.what());
    }
}

std::shared_ptr<MySQLClientV2> DatabaseFactory::getMySQLClientV2() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!isInitialized_) {
        LOG_ERROR("DatabaseFactory not initialized");
        return nullptr;
    }
    
    return mysqlClientV2_;
}

std::shared_ptr<RedisClient> DatabaseFactory::getRedisClient() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!isInitialized_) {
        LOG_ERROR("DatabaseFactory not initialized");
        return nullptr;
    }
    
    return redisClient_;
}

// ES 和 OpenSearch 的 getter 方法已移除，因为不再初始化这些客户端