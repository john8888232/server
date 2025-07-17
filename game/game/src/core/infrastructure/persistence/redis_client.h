#ifndef REDIS_CLIENT_H
#define REDIS_CLIENT_H

#include <string>
#include <vector>
#include <optional>
#include <memory>
#include <functional>
#include <sw/redis++/redis++.h>
#include "../common/config_manager.h"
#include "third_party/libuv_cpp/include/uv11.hpp"

class RedisClient {
public:
    RedisClient() = default;
    ~RedisClient() = default;
    
    // 禁用拷贝构造和赋值操作
    RedisClient(const RedisClient&) = delete;
    RedisClient& operator=(const RedisClient&) = delete;
    
    // 初始化并连接
    bool initialize(const ConfigManager& configManager);
    bool connect();
    bool connect(const std::string& host, int port, const std::string& password, int db,
                 int connectionTimeout, int commandTimeout, int poolSize, bool keepAlive);
    void disconnect();
    
    // 字符串操作
    bool set(const std::string& key, const std::string& value);
    bool setex(const std::string& key, const std::string& value, long long ttl);
    bool setnx(const std::string& key, const std::string& value);
    std::optional<std::string> get(const std::string& key);
    bool del(const std::string& key);
    long long incr(const std::string& key);
    
    // 哈希表操作
    bool hset(const std::string& key, const std::string& field, const std::string& value);
    std::optional<std::string> hget(const std::string& key, const std::string& field);
    bool hdel(const std::string& key, const std::string& field);
    std::vector<std::string> hgetall(const std::string& key);
    
    // 列表操作
    long long lpush(const std::string& key, const std::string& value);
    long long rpush(const std::string& key, const std::string& value);
    std::optional<std::string> lpop(const std::string& key);
    std::optional<std::string> rpop(const std::string& key);
    std::vector<std::string> lrange(const std::string& key, long long start, long long stop);
    
    // 集合操作
    long long sadd(const std::string& key, const std::string& member);
    bool sismember(const std::string& key, const std::string& member);
    std::vector<std::string> smembers(const std::string& key);
    
    // 有序集合操作
    bool zadd(const std::string& key, long long score, const std::string& member);
    std::vector<std::string> zrange(const std::string& key, long long start, long long stop);
    
    // 键操作
    bool exists(const std::string& key);
    bool expire(const std::string& key, long long ttl);
    std::vector<std::string> keys(const std::string& pattern);
    
    // 事务操作
    std::unique_ptr<sw::redis::Transaction> multi();

    
    // 直接访问底层 Redis 对象
    sw::redis::Redis& getRedis() { return *_redis; }
    
private:
    std::unique_ptr<sw::redis::Redis> _redis;
    
    // 配置
    std::string host_;
    int port_;
    std::string password_;
    int db_;
    int connectionTimeout_;
    int commandTimeout_;
    int poolSize_;
    bool keepAlive_;
};

#endif // REDIS_CLIENT_H
