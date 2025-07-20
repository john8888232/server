#include "mysql_client.h"
#include <sstream>
#include <iomanip>
#include <third_party/libuv_cpp/include/LogWriter.hpp>

MySQLClient::~MySQLClient() {
    disconnect();
}

bool MySQLClient::initialize(const ConfigManager& configManager) {
    try {
        // 从配置中获取MySQL设置
        host_ = configManager.getServerConfig()["database"]["mysql"]["host"];
        port_ = configManager.getServerConfig()["database"]["mysql"]["port"].get<unsigned short>();
        user_ = configManager.getServerConfig()["database"]["mysql"]["user"];
        password_ = configManager.getServerConfig()["database"]["mysql"]["password"];
        database_ = configManager.getServerConfig()["database"]["mysql"]["database"];
        max_connections_ = configManager.getServerConfig()["database"]["mysql"]["max_connections"].get<unsigned int>();
        ssl_mode_ = configManager.getServerConfig()["database"]["mysql"]["ssl_mode"];
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Error initializing MySQL client: %s", e.what());
        return false;
    }catch (...) {
        LOG_ERROR("Unknown error initializing MySQL client");
        return false;
    }
}

bool MySQLClient::connect() {
    std::lock_guard<std::mutex> lock(poolMutex_);
    try {
        // 创建连接池
        for (int i = 0; i < max_connections_; i++) {
            auto session = createSession();
            if (session != nullptr) {
                session_pool_.push_back(session);
            }
        }

        LOG_INFO("Connected to MySQL with host: %s:%d, Schema: %s, max_connections: %d", host_.c_str(), port_, database_.c_str(), max_connections_);
        return true;
    } catch (const mysqlx::Error& e) {
        LOG_ERROR("MySQL connection error: %s", e.what());
        return false;
    } catch (const std::exception& e) {
        LOG_ERROR("Error connecting to MySQL: %s", e.what());
        return false;
    }catch (...) {
        LOG_ERROR("Unknown error connecting to MySQL");
        return false;
    }
}

std::shared_ptr<mysqlx::Session> MySQLClient::createSession() {
    try {
        mysqlx::SessionSettings session_settings(host_, port_, user_, password_, database_);
        
        // 根据配置设置SSL模式
        if (ssl_mode_ == "DISABLED") {
            session_settings.set(mysqlx::SessionOption::SSL_MODE, mysqlx::SSLMode::DISABLED);
            LOG_DEBUG("MySQL SSL mode set to DISABLED");
        } else if (ssl_mode_ == "REQUIRED") {
            session_settings.set(mysqlx::SessionOption::SSL_MODE, mysqlx::SSLMode::REQUIRED);
            LOG_DEBUG("MySQL SSL mode set to REQUIRED");
        } else if (ssl_mode_ == "VERIFY_CA") {
            session_settings.set(mysqlx::SessionOption::SSL_MODE, mysqlx::SSLMode::VERIFY_CA);
            LOG_DEBUG("MySQL SSL mode set to VERIFY_CA");
        } else if (ssl_mode_ == "VERIFY_IDENTITY") {
            session_settings.set(mysqlx::SessionOption::SSL_MODE, mysqlx::SSLMode::VERIFY_IDENTITY);
            LOG_DEBUG("MySQL SSL mode set to VERIFY_IDENTITY");
        } else {
            session_settings.set(mysqlx::SessionOption::SSL_MODE, mysqlx::SSLMode::REQUIRED);
            LOG_DEBUG("MySQL SSL mode set to default (REQUIRED)");
        }
        std::shared_ptr<mysqlx::Session> session = std::make_shared<mysqlx::Session>(session_settings);
        return session;
    } catch (const mysqlx::Error& e) {
        LOG_ERROR("SQL Exception in createSession: %s", e.what());
        return nullptr;
    } catch (const std::exception& e) {
        LOG_ERROR("Error creating MySQL session: %s", e.what());
        return nullptr;
    }catch (...) {
        LOG_ERROR("Unknown error creating MySQL session");
        return nullptr;
    }
}

void MySQLClient::disconnect() {
    std::lock_guard<std::mutex> lock(poolMutex_);
    for (auto session : session_pool_) {
        try {
            session->close();
        } catch (const mysqlx::Error& e) {
            LOG_ERROR("Error closing MySQL session: %s", e.what());
        } catch (const std::exception& e) {
            LOG_ERROR("Error closing MySQL session: %s", e.what());
        } catch (...) {
            LOG_ERROR("Unknown error closing MySQL session");
        }
    }
    session_pool_.clear();
    LOG_INFO("Disconnected from MySQL");
}

std::shared_ptr<mysqlx::Session> MySQLClient::getSession() {
    std::lock_guard<std::mutex> lock(poolMutex_);
    if (session_pool_.empty()) {
        return createSession();
    }
    
    // 从连接池获取连接
    std::shared_ptr<mysqlx::Session> session = session_pool_.back();
    session_pool_.pop_back();
    return session;
}

void MySQLClient::releaseSession(std::shared_ptr<mysqlx::Session> session) {
    if (!session) {
        return;
    }
    std::lock_guard<std::mutex> lock(poolMutex_);
    
    // 检查连接池大小
    if (session_pool_.size() >= max_connections_) {
        return;
    }
    // 将连接放回连接池
    session_pool_.push_back(session);
}

// 绑定参数到SQL语句
void MySQLClient::bindParam(mysqlx::SqlStatement& stmt, const MySQLParamValue& param) {
    std::visit([&stmt](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        
        if constexpr (std::is_same_v<T, MySQLNull>) {
            stmt.bind(nullptr);
        }
        else if constexpr (std::is_same_v<T, MySQLDateTime>) {
            char buffer[64];
            snprintf(buffer, sizeof(buffer), 
                     "%04d-%02d-%02d %02d:%02d:%02d.%06d",
                     value.year, value.month, value.day,
                     value.hour, value.minute, value.second, value.microsecond);
            stmt.bind(std::string(buffer));
        }
        else if constexpr (std::is_same_v<T, BinaryData>) {
            stmt.bind(std::string(value.begin(), value.end()));
        }
        else if constexpr (std::is_same_v<T, std::optional<std::string>>) {
            if (value.has_value()) stmt.bind(*value);
            else stmt.bind(nullptr);
        }
        else if constexpr (std::is_same_v<T, std::optional<int32_t>> ||
                          std::is_same_v<T, std::optional<int64_t>>) {
            if (value.has_value()) stmt.bind(*value);
            else stmt.bind(nullptr);
        }
        else if constexpr (std::is_same_v<T, std::optional<double>>) {
            if (value.has_value()) {
                // 对可选double类型，格式化为字符串以保留精度
                std::stringstream ss;
                ss << std::fixed << std::setprecision(2) << *value;
                stmt.bind(ss.str());
            }
            else stmt.bind(nullptr);
        }
        else if constexpr (std::is_same_v<T, double> || std::is_same_v<T, float>) {
            // 对double和float类型，格式化为字符串以保留精度
            std::stringstream ss;
            ss << std::fixed << std::setprecision(2) << value;
            stmt.bind(ss.str());
        }
        else {
            stmt.bind(value);
        }
    }, param);
}

// 执行SQL查询并返回结果
std::shared_ptr<mysqlx::SqlResult> MySQLClient::executeQuery(const std::string& sql, const std::vector<MySQLParamValue>& params) {
    try {
        auto session = getSession();
        if (!session) {
            LOG_ERROR("Failed to get database session");
            return nullptr;
        }
        
        mysqlx::SqlStatement stmt = session->sql(sql);
        
        // 绑定所有参数
        for (const auto& param : params) {
            bindParam(stmt, param);
        }
        
        // 执行查询
        mysqlx::SqlResult sqlResult = stmt.execute();
        
        // 释放会话回连接池
        releaseSession(session);
        
        return std::make_shared<mysqlx::SqlResult>(std::move(sqlResult));
    }
    catch (const mysqlx::Error& e) {
        LOG_ERROR("SQL Error in executeQuery: %s", e.what());
        return nullptr; 
    }
    catch (const std::exception& e) {
        LOG_ERROR("Error in executeQuery: %s", e.what());
        return nullptr;
    }
}

// 执行SQL更新（INSERT, UPDATE, DELETE）并返回受影响的行数
uint64_t MySQLClient::executeUpdate(const std::string& sql, const std::vector<MySQLParamValue>& params) {
    try {
        auto session = getSession();
        if (!session) {
            LOG_ERROR("Failed to get database session");
            return 0;
        }
        
        mysqlx::SqlStatement stmt = session->sql(sql);
        
        // 绑定所有参数
        for (const auto& param : params) {
            bindParam(stmt, param);
        }
        
        // 执行更新
        mysqlx::SqlResult result = stmt.execute();
        uint64_t affectedRows = result.getAffectedItemsCount();
        
        // 释放会话回连接池
        releaseSession(session);
        
        return affectedRows;
    }
    catch (const mysqlx::Error& e) {
        LOG_ERROR("SQL Error in executeUpdate: %s", e.what());
        return 0;
    }
    catch (const std::exception& e) {
        LOG_ERROR("Error in executeUpdate: %s", e.what());
        return 0;
    }
}

// 执行事务
bool MySQLClient::executeTransaction(std::function<bool(MySQLClient&)> transactionBody) {
    auto session = getSession();
    if (!session) {
        LOG_ERROR("Failed to get database session for transaction");
        return false;
    }
    
    try {
        // 开始事务
        session->startTransaction();
        
        // 执行事务主体
        bool success = transactionBody(*this);
        
        // 根据结果提交或回滚
        if (success) {
            session->commit();
            LOG_INFO("Transaction committed successfully");
        } else {
            session->rollback();
            LOG_INFO("Transaction rolled back");
        }
        
        // 释放会话回连接池
        releaseSession(session);
        
        return success;
    }
    catch (const mysqlx::Error& e) {
        LOG_ERROR("SQL Error in transaction: %s", e.what());
        session->rollback();
        releaseSession(session);
        return false;
    }
    catch (const std::exception& e) {
        LOG_ERROR("Error in transaction: %s", e.what());
        session->rollback();
        releaseSession(session);
        return false;
    }
}

// 使用回调函数处理每一行结果
void MySQLClient::queryWithCallback(const std::string& sql, 
                                   const std::vector<MySQLParamValue>& params,
                                   std::function<void(const mysqlx::Row&)> rowCallback) {
    try {
        auto result = executeQuery(sql, params);
        if (!result) {
            LOG_ERROR("Failed to execute query with callback %s", sql.c_str());
            return;
        }
        mysqlx::Row row;
        
        // 使用 fetchOne() 逐行获取结果
        while ((row = result->fetchOne())) {
            rowCallback(row);
        }
    }
    catch (const std::exception& e) {
        LOG_ERROR("Error in queryWithCallback: %s", e.what());
    }
}

// 创建查询构建器 
QueryBuilder MySQLClient::query(const std::string& sql) {
    return QueryBuilder(*this, sql);
}

// QueryBuilder::execute 实现
std::shared_ptr<mysqlx::SqlResult> QueryBuilder::execute() {
    return client_.executeQuery(sql_, params_);
}

// QueryBuilder::executeUpdate 实现
uint64_t QueryBuilder::executeUpdate() {
    return client_.executeUpdate(sql_, params_);
}