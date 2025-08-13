#ifndef MYSQL_CLIENT_V2_H
#define MYSQL_CLIENT_V2_H

#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <functional>
#include <queue>
#include <condition_variable>
#include <atomic>
#include <mysql/jdbc.h>
#include "mysql_data_def.h"
#include "core/infrastructure/common/config_manager.h"

// 前向声明
class QueryBuilderV2;
class MySQLClientV2;

// RAII安全连接包装器
class SafeMySQLConnection {
private:
    std::shared_ptr<sql::Connection> connection_;
    std::weak_ptr<MySQLClientV2> client_;
    
public:
    SafeMySQLConnection(std::shared_ptr<sql::Connection> conn, 
                       std::weak_ptr<MySQLClientV2> client);
    ~SafeMySQLConnection();
    
    // 禁用拷贝构造和赋值
    SafeMySQLConnection(const SafeMySQLConnection&) = delete;
    SafeMySQLConnection& operator=(const SafeMySQLConnection&) = delete;
    
    // 移动构造和赋值
    SafeMySQLConnection(SafeMySQLConnection&& other) noexcept;
    SafeMySQLConnection& operator=(SafeMySQLConnection&& other) noexcept;
    
    // 访问连接
    sql::Connection* get() const { return connection_.get(); }
    sql::Connection* operator->() const { return connection_.get(); }
    sql::Connection& operator*() const { return *connection_; }
    
    // 检查连接是否有效
    bool isValid() const;
};

class MySQLClientV2 : public std::enable_shared_from_this<MySQLClientV2> {
public:
    MySQLClientV2() = default;
    
    ~MySQLClientV2();
    
    // 禁用拷贝构造和赋值操作
    MySQLClientV2(const MySQLClientV2&) = delete;
    MySQLClientV2& operator=(const MySQLClientV2&) = delete;
    
    // 初始化数据库配置
    bool initialize(const ConfigManager& configManager);
    
    // 连接到MySQL服务器
    bool connect();
    
    // 断开连接
    void disconnect();

    // 创建查询构建器
    QueryBuilderV2 query(const std::string& sql);
    
    // 执行SQL查询并返回结果
    std::shared_ptr<sql::ResultSet> executeQuery(const std::string& sql, const std::vector<MySQLParamValue>& params = {});
    
    // 执行SQL更新（INSERT, UPDATE, DELETE）并返回受影响的行数
    uint64_t executeUpdate(const std::string& sql, const std::vector<MySQLParamValue>& params = {});
    
    // 执行事务
    bool executeTransaction(std::function<bool(MySQLClientV2&)> transactionBody);
    
    // 安全执行SQL更新
    int64_t safeExecuteUpdate(const std::string& sql, const std::vector<MySQLParamValue>& params = {}, uint64_t expectedRows = 0);
    
    // 安全执行事务
    bool safeExecuteTransaction(std::function<bool(MySQLClientV2&)> transactionBody);
    
    // 使用回调函数处理每一行结果
    void queryWithCallback(const std::string& sql, 
                          const std::vector<MySQLParamValue>& params,
                          std::function<void(sql::ResultSet*)> rowCallback);
    
    // 获取安全连接包装器
    std::unique_ptr<SafeMySQLConnection> getSafeConnection();
    
    // 返回连接到池中（由SafeMySQLConnection调用）
    void returnConnection(std::shared_ptr<sql::Connection> conn);
    
private:
    // 创建新的数据库连接
    std::shared_ptr<sql::Connection> createConnection();
    
    // 绑定参数到SQL语句
    void bindParam(sql::PreparedStatement* stmt, size_t index, const MySQLParamValue& param);

    // MySQL连接信息
    std::string host_;
    unsigned short port_;
    std::string user_;
    std::string password_;
    std::string database_;
    unsigned int max_connections_;
    std::string ssl_mode_; // 添加SSL模式配置
    
    // 改进的连接池 - 使用shared_ptr和队列
    std::queue<std::shared_ptr<sql::Connection>> connection_pool_;
    std::mutex pool_mutex_;
    std::condition_variable pool_cv_;
    std::atomic<bool> shutdown_{false};
    std::atomic<size_t> current_connections_{0};
    
    // MySQL驱动实例
    sql::Driver* driver_;
};

// 查询构建器类
class QueryBuilderV2 {
private:
    MySQLClientV2& client_;
    std::string sql_;
    std::vector<MySQLParamValue> params_;
    
public:
    QueryBuilderV2(MySQLClientV2& client, const std::string& sql)
        : client_(client), sql_(sql) {}
    
    // 添加参数
    template<typename T>
    QueryBuilderV2& addParam(T value) {
        params_.push_back(value);
        return *this;
    }
    
    // 执行查询并返回结果
    std::shared_ptr<sql::ResultSet> execute();
    
    // 执行更新并返回受影响的行数
    uint64_t executeUpdate();
};

#endif // MYSQL_CLIENT_V2_H
