#ifndef MYSQL_CLIENT_H
#define MYSQL_CLIENT_H

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <mysql-cppconn/mysqlx/xdevapi.h>
#include "mysql_data_def.h"
#include "../common/config_manager.h"

// 查询构建器类的前向声明
class QueryBuilder;

class MySQLClient {
public:
    // 构造函数
    MySQLClient() = default;
    
    // 析构函数
    ~MySQLClient();
    
    // 禁用拷贝构造和赋值操作
    MySQLClient(const MySQLClient&) = delete;
    MySQLClient& operator=(const MySQLClient&) = delete;
    
    // 初始化数据库配置
    bool initialize(const ConfigManager& configManager);
    
    // 连接到MySQL服务器
    bool connect();
    
    // 断开连接
    void disconnect();

    // 创建查询构建器
    QueryBuilder query(const std::string& sql);
    
    // 执行SQL查询并返回结果
    std::shared_ptr<mysqlx::SqlResult> executeQuery(const std::string& sql, const std::vector<MySQLParamValue>& params = {});
    
    // 执行SQL更新（INSERT, UPDATE, DELETE）并返回受影响的行数
    uint64_t executeUpdate(const std::string& sql, const std::vector<MySQLParamValue>& params = {});
    
    // 执行事务，transactionBody里面不要递归使用getSession/releaseSession
    bool executeTransaction(std::function<bool(MySQLClient&)> transactionBody);
    
    // 使用回调函数处理每一行结果
    void queryWithCallback(const std::string& sql, 
                          const std::vector<MySQLParamValue>& params,
                          std::function<void(const mysqlx::Row&)> rowCallback);

private:
    // 创建新的数据库连接
    std::shared_ptr<mysqlx::Session> createSession();
    
    // 从连接池获取连接
    std::shared_ptr<mysqlx::Session> getSession();
    
    // 将连接释放回连接池
    void releaseSession(std::shared_ptr<mysqlx::Session> session);
    
    // 绑定参数到SQL语句
    void bindParam(mysqlx::SqlStatement& stmt, const MySQLParamValue& param);

    // MySQL连接信息
    std::string host_;
    unsigned short port_;
    std::string user_;
    std::string password_;
    std::string database_;
    unsigned int max_connections_;
    
    // MySQL连接池
    std::vector<std::shared_ptr<mysqlx::Session>> session_pool_;
    std::mutex poolMutex_;
};

// 查询构建器类
class QueryBuilder {
private:
    MySQLClient& client_;
    std::string sql_;
    std::vector<MySQLParamValue> params_;
    
public:
    QueryBuilder(MySQLClient& client, const std::string& sql)
        : client_(client), sql_(sql) {}
    
    // 添加参数
    template<typename T>
    QueryBuilder& addParam(T value) {
        params_.push_back(value);
        return *this;
    }
    
    // 执行查询并返回结果
    std::shared_ptr<mysqlx::SqlResult> execute();
    
    // 执行更新并返回受影响的行数
    uint64_t executeUpdate();
};

#endif // MYSQL_CLIENT_H
