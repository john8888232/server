#include "redis_client.h"
#include <iostream>

bool RedisClient::initialize(const ConfigManager& configManager) {
    try {
        // 从配置中获取Redis设置
        host_ = configManager.getServerConfig()["database"]["redis"]["host"];
        port_ = configManager.getServerConfig()["database"]["redis"]["port"].get<unsigned short>();
        db_ = configManager.getServerConfig()["database"]["redis"]["db"].get<unsigned int>();
        connectionTimeout_ = configManager.getServerConfig()["database"]["redis"]["connection_timeout"].get<unsigned int>();
        commandTimeout_ = configManager.getServerConfig()["database"]["redis"]["command_timeout"].get<unsigned int>();
        poolSize_ = configManager.getServerConfig()["database"]["redis"]["pool_size"].get<unsigned int>();
        keepAlive_ = configManager.getServerConfig()["database"]["redis"]["keep_alive"].get<bool>();
        LOG_DEBUG("Redis client initialized");
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Error initializing Redis client: %s", e.what());
        return false;
    }
}

bool RedisClient::connect() {
    try {
        // 创建连接选项
        sw::redis::ConnectionOptions connection_options;
        connection_options.host = host_;
        connection_options.port = port_;
        connection_options.password = password_;
        connection_options.db = db_;
        
        // 设置超时选项
        connection_options.socket_timeout = std::chrono::milliseconds(commandTimeout_ * 1000);
        connection_options.connect_timeout = std::chrono::milliseconds(connectionTimeout_ * 1000);
        connection_options.keep_alive = keepAlive_;
        
        // 创建连接池选项
        sw::redis::ConnectionPoolOptions pool_options;
        pool_options.size = poolSize_;  // 连接池大小
        pool_options.wait_timeout = std::chrono::milliseconds(commandTimeout_ * 100);
        pool_options.connection_lifetime = std::chrono::minutes(10);
        
        // 创建 Redis 客户端
        _redis = std::make_unique<sw::redis::Redis>(connection_options, pool_options);
        
        // 测试连接
        _redis->ping();
        
        LOG_INFO("Connected to Redis: %s:%d/%d", host_.c_str(), port_, db_);
        return true;
    } catch (const sw::redis::Error& e) {
        LOG_ERROR("Redis connection error: %s", e.what());
        return false;
    } catch (const std::exception& e) {
        LOG_ERROR("Error connecting to Redis: %s", e.what());
        return false;
    }
}

// 新增：接受直接参数的连接方法
bool RedisClient::connect(const std::string& host, int port, const std::string& password, 
                        int db, int connectionTimeout, int commandTimeout, 
                        int poolSize, bool keepAlive) {
    // 先保存配置参数
    host_ = host;
    port_ = port;
    password_ = password;
    db_ = db;
    connectionTimeout_ = connectionTimeout;
    commandTimeout_ = commandTimeout;
    poolSize_ = poolSize;
    keepAlive_ = keepAlive;
    
    // 调用原有的connect方法
    return connect();
}

void RedisClient::disconnect() {
    try {
        if (_redis) {
            _redis.reset();
            LOG_INFO("Disconnected from Redis");
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Error disconnecting from Redis: %s", e.what());
    }
}

// 字符串操作
bool RedisClient::set(const std::string& key, const std::string& value) {
    try {
        _redis->set(key, value);
        return true;
    } catch (const sw::redis::Error& e) {
        LOG_ERROR("Redis SET error: %s", e.what());
        return false;
    }
}

bool RedisClient::setex(const std::string& key, const std::string& value, long long ttl) {
    try {
        _redis->setex(key, ttl, value);
        return true;
    } catch (const sw::redis::Error& e) {
        LOG_ERROR("Redis SETEX error: %s", e.what());
        return false;
    }
}

bool RedisClient::setnx(const std::string& key, const std::string& value) {
    try {
        return _redis->setnx(key, value);
    } catch (const sw::redis::Error& e) {
        LOG_ERROR("Redis SETNX error: %s", e.what());
        return false;
    }
}

std::optional<std::string> RedisClient::get(const std::string& key) {
    try {
        auto val = _redis->get(key);
        if (val) {
            return *val;
        }
        return std::nullopt;
    } catch (const sw::redis::Error& e) {
        LOG_ERROR("Redis GET error: %s", e.what());
        return std::nullopt;
    }
}

bool RedisClient::del(const std::string& key) {
    try {
        _redis->del(key);
        return true;
    } catch (const sw::redis::Error& e) {
        LOG_ERROR("Redis DEL error: %s", e.what());
        return false;
    }
}

long long RedisClient::incr(const std::string& key) {
    try {
        return _redis->incr(key);
    } catch (const sw::redis::Error& e) {
        LOG_ERROR("Redis INCR error: %s", e.what());
        return -1;
    }
}

// 哈希表操作
bool RedisClient::hset(const std::string& key, const std::string& field, const std::string& value) {
    try {
        _redis->hset(key, field, value);
        return true;
    } catch (const sw::redis::Error& e) {
        LOG_ERROR("Redis HSET error: %s", e.what());
        return false;
    }
}

std::optional<std::string> RedisClient::hget(const std::string& key, const std::string& field) {
    try {
        auto val = _redis->hget(key, field);
        if (val) {
            return *val;
        }
        return std::nullopt;
    } catch (const sw::redis::Error& e) {
        LOG_ERROR("Redis HGET error: %s", e.what());
        return std::nullopt;
    }
}

bool RedisClient::hdel(const std::string& key, const std::string& field) {
    try {
        _redis->hdel(key, field);
        return true;
    } catch (const sw::redis::Error& e) {
        LOG_ERROR("Redis HDEL error: %s", e.what());
        return false;
    }
}

std::vector<std::string> RedisClient::hgetall(const std::string& key) {
    try {
        std::vector<std::string> result;
        _redis->hgetall(key, std::back_inserter(result));
        return result;
    } catch (const sw::redis::Error& e) {
        LOG_ERROR("Redis HGETALL error: %s", e.what());
        return {};
    }
}

// 列表操作
long long RedisClient::lpush(const std::string& key, const std::string& value) {
    try {
        return _redis->lpush(key, value);
    } catch (const sw::redis::Error& e) {
        LOG_ERROR("Redis LPUSH error: %s", e.what());
        return -1;
    }
}

long long RedisClient::rpush(const std::string& key, const std::string& value) {
    try {
        return _redis->rpush(key, value);
    } catch (const sw::redis::Error& e) {
        LOG_ERROR("Redis RPUSH error: %s", e.what());
        return -1;
    }
}

std::optional<std::string> RedisClient::lpop(const std::string& key) {
    try {
        auto val = _redis->lpop(key);
        if (val) {
            return *val;
        }
        return std::nullopt;
    } catch (const sw::redis::Error& e) {
        LOG_ERROR("Redis LPOP error: %s", e.what());
        return std::nullopt;
    }
}

std::optional<std::string> RedisClient::rpop(const std::string& key) {
    try {
        auto val = _redis->rpop(key);
        if (val) {
            return *val;
        }
        return std::nullopt;
    } catch (const sw::redis::Error& e) {
        LOG_ERROR("Redis RPOP error: %s", e.what());
        return std::nullopt;
    }
}

std::vector<std::string> RedisClient::lrange(const std::string& key, long long start, long long stop) {
    try {
        std::vector<std::string> result;
        _redis->lrange(key, start, stop, std::back_inserter(result));
        return result;
    } catch (const sw::redis::Error& e) {
        LOG_ERROR("Redis LRANGE error: %s", e.what());
        return {};
    }
}

// 集合操作
long long RedisClient::sadd(const std::string& key, const std::string& member) {
    try {
        return _redis->sadd(key, member);
    } catch (const sw::redis::Error& e) {
        LOG_ERROR("Redis SADD error: %s", e.what());
        return -1;
    }
}

bool RedisClient::sismember(const std::string& key, const std::string& member) {
    try {
        return _redis->sismember(key, member);
    } catch (const sw::redis::Error& e) {
        LOG_ERROR("Redis SISMEMBER error: %s", e.what());
        return false;
    }
}

std::vector<std::string> RedisClient::smembers(const std::string& key) {
    try {
        std::vector<std::string> result;
        _redis->smembers(key, std::back_inserter(result));
        return result;
    } catch (const sw::redis::Error& e) {
        LOG_ERROR("Redis SMEMBERS error: %s", e.what());
        return {};
    }
}

// 有序集合操作
bool RedisClient::zadd(const std::string& key, long long score, const std::string& member) {
    try {
        _redis->zadd(key, member, score);
        return true;
    } catch (const sw::redis::Error& e) {
        LOG_ERROR("Redis ZADD error: %s", e.what());
        return false;
    }
}

// 获取不带分数的元素
std::vector<std::string> RedisClient::zrange(const std::string& key, long long start, long long stop) {
    try {
        std::vector<std::string> result;
        _redis->zrange(key, start, stop, std::back_inserter(result));
        return result;
    } catch (const sw::redis::Error& e) {
        LOG_ERROR("Redis ZRANGE error: %s", e.what());
        return {};
    }
}

// 键操作
bool RedisClient::exists(const std::string& key) {
    try {
        return _redis->exists(key);
    } catch (const sw::redis::Error& e) {
        LOG_ERROR("Redis EXISTS error: %s", e.what());
        return false;
    }
}

bool RedisClient::expire(const std::string& key, long long ttl) {
    try {
        return _redis->expire(key, ttl);
    } catch (const sw::redis::Error& e) {
        LOG_ERROR("Redis EXPIRE error: %s", e.what());
        return false;
    }
}

std::vector<std::string> RedisClient::keys(const std::string& pattern) {
    try {
        std::vector<std::string> result;
        _redis->keys(pattern, std::back_inserter(result));
        return result;
    } catch (const sw::redis::Error& e) {
        LOG_ERROR("Redis KEYS error: %s", e.what());
        return {};
    }
}

// 事务操作
std::unique_ptr<sw::redis::Transaction> RedisClient::multi() {
    try {
        return std::make_unique<sw::redis::Transaction>(_redis->transaction());
    } catch (const sw::redis::Error& e) {
        LOG_ERROR("Redis MULTI error: %s", e.what());
        return nullptr;
    }
}