#include "mysql_clientV2.h"
#include "third_party/libuv_cpp/include/LogWriter.hpp"

// SafeMySQLConnection 实现
SafeMySQLConnection::SafeMySQLConnection(std::shared_ptr<sql::Connection> conn, 
                                       std::weak_ptr<MySQLClientV2> client)
    : connection_(std::move(conn)), client_(client) {
}

SafeMySQLConnection::~SafeMySQLConnection() {
    if (connection_) {
        if (auto client_ptr = client_.lock()) {
            client_ptr->returnConnection(connection_);
        }
    }
}

SafeMySQLConnection::SafeMySQLConnection(SafeMySQLConnection&& other) noexcept
    : connection_(std::move(other.connection_)), client_(std::move(other.client_)) {
}

SafeMySQLConnection& SafeMySQLConnection::operator=(SafeMySQLConnection&& other) noexcept {
    if (this != &other) {
        // 先清理当前连接
        if (connection_) {
            if (auto client_ptr = client_.lock()) {
                client_ptr->returnConnection(connection_);
            }
        }
        // 移动新连接
        connection_ = std::move(other.connection_);
        client_ = std::move(other.client_);
    }
    return *this;
}

bool SafeMySQLConnection::isValid() const {
    if (!connection_) return false;
    try {
        return !connection_->isClosed() && connection_->isValid();
    } catch (...) {
        return false;
    }
}

MySQLClientV2::~MySQLClientV2() {
    disconnect();
}

bool MySQLClientV2::initialize(const ConfigManager& configManager) {
    try {
        // 从配置中获取MySQL设置
        host_ = configManager.getServerConfig()["database"]["mysql"]["host"];
        port_ = configManager.getServerConfig()["database"]["mysql"]["port"].get<unsigned short>();
        user_ = configManager.getServerConfig()["database"]["mysql"]["user"];
        password_ = configManager.getServerConfig()["database"]["mysql"]["password"];
        database_ = configManager.getServerConfig()["database"]["mysql"]["database"];
        max_connections_ = configManager.getServerConfig()["database"]["mysql"]["max_connections"].get<unsigned int>();
        ssl_mode_ = configManager.getServerConfig()["database"]["mysql"]["ssl_mode"];
        
        // 获取MySQL驱动实例
        driver_ = get_driver_instance();
        
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Error initializing MySQLClientV2: %s", e.what());
        return false;
    } catch (...) {
        LOG_ERROR("Unknown error initializing MySQLClientV2");
        return false;
    }
}

bool MySQLClientV2::connect() {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    try {
        shutdown_ = false;
        current_connections_ = 0;
        
        // 创建初始连接池
        for (unsigned int i = 0; i < max_connections_; i++) {
            auto conn = createConnection();
            if (conn != nullptr) {
                connection_pool_.push(conn);
                current_connections_++;
            }
        }

        LOG_INFO("Connected to MySQL V2 with host: %s:%d, Schema: %s, max_connections: %d, created: %zu", 
                 host_.c_str(), port_, database_.c_str(), max_connections_, current_connections_.load());
        return true;
    } catch (const sql::SQLException& e) {
        LOG_ERROR("MySQLClientV2 connection error: %s", e.what());
        return false;
    } catch (const std::exception& e) {
        LOG_ERROR("Error connecting to MySQLClientV2: %s", e.what());
        return false;
    } catch (...) {
        LOG_ERROR("Unknown error connecting to MySQLClientV2");
        return false;
    }
}

std::shared_ptr<sql::Connection> MySQLClientV2::createConnection() {
    try {
        // 构建连接URL
        std::string url = "tcp://" + host_ + ":" + std::to_string(port_);
        
        // 创建连接
        std::shared_ptr<sql::Connection> conn(driver_->connect(url, user_, password_));
        
        // 设置数据库schema
        conn->setSchema(database_);
        
        // 根据配置设置SSL模式
        if (ssl_mode_ == "DISABLED") {
            conn->setClientOption("sslMode", "DISABLED");
            LOG_DEBUG("MySQLClientV2 SSL mode set to DISABLED");
        } else if (ssl_mode_ == "REQUIRED") {
            conn->setClientOption("sslMode", "REQUIRED");
            LOG_DEBUG("MySQLClientV2 SSL mode set to REQUIRED");
        } else if (ssl_mode_ == "VERIFY_CA") {
            conn->setClientOption("sslMode", "VERIFY_CA");
            LOG_DEBUG("MySQLClientV2 SSL mode set to VERIFY_CA");
        } else if (ssl_mode_ == "VERIFY_IDENTITY") {
            conn->setClientOption("sslMode", "VERIFY_IDENTITY");
            LOG_DEBUG("MySQLClientV2 SSL mode set to VERIFY_IDENTITY");
        } else {
            conn->setClientOption("sslMode", "REQUIRED");
            LOG_DEBUG("MySQLClientV2 SSL mode set to default (REQUIRED)");
        }
        
        return conn;
    } catch (const sql::SQLException& e) {
        LOG_ERROR("SQL Exception in createConnection: %s", e.what());
        return nullptr;
    } catch (const std::exception& e) {
        LOG_ERROR("Error creating MySQLClientV2 connection: %s", e.what());
        return nullptr;
    } catch (...) {
        LOG_ERROR("Unknown error creating MySQLClientV2 connection");
        return nullptr;
    }
}

void MySQLClientV2::disconnect() {
    shutdown_ = true;
    
    {
        std::lock_guard<std::mutex> lock(pool_mutex_);
        
        // 清空连接池
        while (!connection_pool_.empty()) {
            auto conn = connection_pool_.front();
            connection_pool_.pop();
            try {
                if (conn && !conn->isClosed()) {
                    conn->close();
                }
            } catch (const sql::SQLException& e) {
                LOG_ERROR("Error closing MySQLClientV2 connection: %s", e.what());
            } catch (const std::exception& e) {
                LOG_ERROR("Error closing MySQLClientV2 connection: %s", e.what());
            } catch (...) {
                LOG_ERROR("Unknown error closing MySQLClientV2 connection");
            }
        }
        current_connections_ = 0;
    }
    
    // 通知所有等待的线程
    pool_cv_.notify_all();
    LOG_INFO("Disconnected from MySQL V2");
}

// 获取安全连接包装器
std::unique_ptr<SafeMySQLConnection> MySQLClientV2::getSafeConnection() {
    if (shutdown_) {
        return nullptr;
    }
    
    std::unique_lock<std::mutex> lock(pool_mutex_);
    
    // 等待可用连接或创建新连接
    while (connection_pool_.empty() && !shutdown_) {
        if (current_connections_ < max_connections_) {
            // 释放锁来创建新连接
            lock.unlock();
            auto new_conn = createConnection();
            lock.lock();
            
            if (new_conn && !shutdown_) {
                current_connections_++;
                return std::make_unique<SafeMySQLConnection>(new_conn, weak_from_this());
            }
        }
        
        if (shutdown_) {
            return nullptr;
        }
        
        // 等待连接返回
        pool_cv_.wait(lock);
    }
    
    if (!connection_pool_.empty() && !shutdown_) {
        auto conn = connection_pool_.front();
        connection_pool_.pop();
        
        // 检查连接有效性
        try {
            if (conn && !conn->isClosed() && conn->isValid()) {
                return std::make_unique<SafeMySQLConnection>(conn, weak_from_this());
            } else {
                // 连接无效，创建新连接
                lock.unlock();
                auto new_conn = createConnection();
                lock.lock();
                
                if (new_conn && !shutdown_) {
                    return std::make_unique<SafeMySQLConnection>(new_conn, weak_from_this());
                }
            }
        } catch (...) {
            // 连接检查失败，创建新连接
            lock.unlock();
            auto new_conn = createConnection();
            lock.lock();
            
            if (new_conn && !shutdown_) {
                return std::make_unique<SafeMySQLConnection>(new_conn, weak_from_this());
            }
        }
    }
    
    return nullptr;
}

// 返回连接到池中
void MySQLClientV2::returnConnection(std::shared_ptr<sql::Connection> conn) {
    if (!conn || shutdown_) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(pool_mutex_);
    
    try {
        // 检查连接是否仍然有效
        if (!conn->isClosed() && conn->isValid()) {
            if (connection_pool_.size() < max_connections_) {
                connection_pool_.push(conn);
                pool_cv_.notify_one();
                LOG_DEBUG("Connection returned to pool, pool size: %zu", connection_pool_.size());
            } else {
                LOG_DEBUG("Pool is full, closing connection");
                current_connections_--;
            }
        } else {
            LOG_DEBUG("Invalid connection, not returning to pool");
            current_connections_--;
        }
    } catch (...) {
        LOG_DEBUG("Exception checking connection validity, not returning to pool");
        current_connections_--;
    }
}



// 绑定参数到SQL语句
void MySQLClientV2::bindParam(sql::PreparedStatement* stmt, size_t index, const MySQLParamValue& param) {
    std::visit([&stmt, index](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        
        if constexpr (std::is_same_v<T, MySQLNull>) {
            stmt->setNull(index, sql::DataType::UNKNOWN);
        }
        else if constexpr (std::is_same_v<T, MySQLDateTime>) {
            char buffer[64];
            snprintf(buffer, sizeof(buffer), 
                     "%04d-%02d-%02d %02d:%02d:%02d.%06d",
                     value.year, value.month, value.day,
                     value.hour, value.minute, value.second, value.microsecond);
            stmt->setString(index, std::string(buffer));
        }
        else if constexpr (std::is_same_v<T, BinaryData>) {
            std::string binaryStr(value.begin(), value.end());
            stmt->setString(index, binaryStr);
        }
        else if constexpr (std::is_same_v<T, std::optional<std::string>>) {
            if (value.has_value()) stmt->setString(index, *value);
            else stmt->setNull(index, sql::DataType::VARCHAR);
        }
        else if constexpr (std::is_same_v<T, std::optional<int32_t>>) {
            if (value.has_value()) stmt->setInt(index, *value);
            else stmt->setNull(index, sql::DataType::INTEGER);
        }
        else if constexpr (std::is_same_v<T, std::optional<int64_t>>) {
            if (value.has_value()) stmt->setInt64(index, *value);
            else stmt->setNull(index, sql::DataType::BIGINT);
        }
        else if constexpr (std::is_same_v<T, std::optional<double>>) {
            if (value.has_value()) {
                stmt->setDouble(index, *value);
            }
            else stmt->setNull(index, sql::DataType::DOUBLE);
        }
        else if constexpr (std::is_same_v<T, int8_t>) {
            stmt->setInt(index, static_cast<int32_t>(value));
        }
        else if constexpr (std::is_same_v<T, int16_t>) {
            stmt->setInt(index, static_cast<int32_t>(value));
        }
        else if constexpr (std::is_same_v<T, int32_t>) {
            stmt->setInt(index, value);
        }
        else if constexpr (std::is_same_v<T, int64_t>) {
            stmt->setInt64(index, value);
        }
        else if constexpr (std::is_same_v<T, uint8_t>) {
            stmt->setUInt(index, static_cast<uint32_t>(value));
        }
        else if constexpr (std::is_same_v<T, uint16_t>) {
            stmt->setUInt(index, static_cast<uint32_t>(value));
        }
        else if constexpr (std::is_same_v<T, uint32_t>) {
            stmt->setUInt(index, value);
        }
        else if constexpr (std::is_same_v<T, uint64_t>) {
            stmt->setUInt64(index, value);
        }
        else if constexpr (std::is_same_v<T, float>) {
            stmt->setDouble(index, static_cast<double>(value));
        }
        else if constexpr (std::is_same_v<T, double>) {
            stmt->setDouble(index, value);
        }
        else if constexpr (std::is_same_v<T, std::string>) {
            stmt->setString(index, value);
        }
        else if constexpr (std::is_same_v<T, bool>) {
            stmt->setBoolean(index, value);
        }
        else {
            stmt->setNull(index, sql::DataType::UNKNOWN);
        }
    }, param);
}

// 执行SQL查询并返回结果集
std::shared_ptr<sql::ResultSet> MySQLClientV2::executeQuery(const std::string& sql, const std::vector<MySQLParamValue>& params) {
    auto conn_wrapper = getSafeConnection();
    if (!conn_wrapper) {
        LOG_ERROR("Failed to get database connection");
        return nullptr;
    }
    
    try {
        // 创建预编译语句
        std::unique_ptr<sql::PreparedStatement> stmt;
        try {
            LOG_DEBUG("Preparing SQL statement: %s", sql.c_str());
            stmt.reset(conn_wrapper->get()->prepareStatement(sql));
            LOG_DEBUG("SQL statement prepared successfully");
        } catch (const sql::SQLException& e) {
            LOG_ERROR("Failed to prepare statement: %s, error: %s", sql.c_str(), e.what());
            return nullptr;
        }
        
        if (!stmt) {
            LOG_ERROR("Failed to create prepared statement for query: %s", sql.c_str());
            return nullptr;
        }
        
        // 绑定所有参数
        for (size_t i = 0; i < params.size(); ++i) {
            try {
                bindParam(stmt.get(), i + 1, params[i]);
            } catch (const sql::SQLException& e) {
                LOG_ERROR("Failed to bind parameter %zu: %s", i, e.what());
                return nullptr;
            }
        }
        
        // 执行查询
        std::shared_ptr<sql::ResultSet> result;
        try {
            LOG_DEBUG("Executing query...");
            result.reset(stmt->executeQuery());
            LOG_DEBUG("Query executed successfully");
        } catch (const sql::SQLException& e) {
            LOG_ERROR("Failed to execute query: %s", e.what());
            return nullptr;
        }
        
        return result;
    }
    catch (const sql::SQLException& e) {
        LOG_ERROR("SQL Error in executeQuery: %s", e.what());
        return nullptr;
    }
    catch (const std::exception& e) {
        LOG_ERROR("Error in executeQuery: %s", e.what());
        return nullptr;
    }
}

// 执行SQL更新（INSERT, UPDATE, DELETE）并返回受影响的行数
uint64_t MySQLClientV2::executeUpdate(const std::string& sql, const std::vector<MySQLParamValue>& params) {
    try {
        auto conn_wrapper = getSafeConnection();
        if (!conn_wrapper) {
            LOG_ERROR("Failed to get database connection");
            return 0;
        }
        
        // 连接有效性由SafeMySQLConnection自动处理
        
        // 直接创建预编译语句
        std::unique_ptr<sql::PreparedStatement> stmt;
        try {
            stmt.reset(conn_wrapper->get()->prepareStatement(sql));
        } catch (const sql::SQLException& e) {
            LOG_ERROR("Failed to prepare statement: %s, error: %s", sql.c_str(), e.what());
            // Connection automatically returned by RAII
            return 0;
        }
        
        if (!stmt) {
            LOG_ERROR("Failed to create prepared statement for update: %s", sql.c_str());
            // Connection automatically returned by RAII
            return 0;
        }
        
        // 绑定所有参数
        for (size_t i = 0; i < params.size(); ++i) {
            try {
                bindParam(stmt.get(), i + 1, params[i]);
            } catch (const sql::SQLException& e) {
                LOG_ERROR("Failed to bind parameter %zu: %s", i, e.what());
                // Connection automatically returned by RAII
                return 0;
            }
        }
        
        // 执行更新
        int affectedRows = 0;
        try {
            LOG_DEBUG("Executing update...");
            affectedRows = stmt->executeUpdate();
            LOG_DEBUG("Update executed successfully, affected rows: %d", affectedRows);
        } catch (const sql::SQLException& e) {
            LOG_ERROR("Failed to execute update: %s", e.what());
            // 安全释放预编译语句
            try {
                stmt.reset();
                LOG_DEBUG("Prepared statement released safely after update error");
            } catch (...) {
                LOG_WARN("Exception occurred while releasing prepared statement after update error");
            }
            // Connection automatically returned by RAII
            return 0;
        } catch (const std::exception& e) {
            LOG_ERROR("Exception executing update: %s", e.what());
            try {
                stmt.reset();
            } catch (...) {
                LOG_WARN("Exception occurred while releasing prepared statement after update error");
            }
            // Connection automatically returned by RAII
            return 0;
        } catch (...) {
            LOG_ERROR("Unknown exception executing update");
            try {
                stmt.reset();
            } catch (...) {
                LOG_WARN("Exception occurred while releasing prepared statement after update error");
            }
            // Connection automatically returned by RAII
            return 0;
        }
        
        // 安全释放预编译语句
        try {
            LOG_DEBUG("Releasing prepared statement after update...");
            stmt.reset();
            LOG_DEBUG("Prepared statement released successfully after update");
        } catch (const sql::SQLException& e) {
            LOG_WARN("SQL exception while releasing prepared statement after update: %s", e.what());
        } catch (const std::exception& e) {
            LOG_WARN("Exception while releasing prepared statement after update: %s", e.what());
        } catch (...) {
            LOG_WARN("Unknown exception while releasing prepared statement after update");
        }
        
        // 释放连接回连接池
        // Connection automatically returned by RAII
        
        return static_cast<uint64_t>(affectedRows);
    }
    catch (const sql::SQLException& e) {
        LOG_ERROR("SQL Error in executeUpdate: %s", e.what());
        return 0;
    }
    catch (const std::exception& e) {
        LOG_ERROR("Error in executeUpdate: %s", e.what());
        return 0;
    }
}

// 执行事务
bool MySQLClientV2::executeTransaction(std::function<bool(MySQLClientV2&)> transactionBody) {
    auto conn_wrapper = getSafeConnection();
    if (!conn_wrapper) {
        LOG_ERROR("Failed to get database connection for transaction");
        return false;
    }
    
    try {
        // 开始事务
        conn_wrapper->get()->setAutoCommit(false);
        
        // 执行事务主体
        bool success = transactionBody(*this);
        
        // 根据结果提交或回滚
        if (success) {
            conn_wrapper->get()->commit();
            LOG_INFO("Transaction committed successfully");
        } else {
            conn_wrapper->get()->rollback();
            LOG_INFO("Transaction rolled back");
        }
        
        // 恢复自动提交
        conn_wrapper->get()->setAutoCommit(true);
        
        // 释放连接回连接池
        // Connection automatically returned by RAII
        
        return success;
    }
    catch (const sql::SQLException& e) {
        LOG_ERROR("SQL Error in transaction: %s", e.what());
        try { 
            conn_wrapper->get()->rollback(); 
            conn_wrapper->get()->setAutoCommit(true);
        } catch (...) {}
        // Connection automatically returned by RAII
        return false;
    }
    catch (const std::exception& e) {
        LOG_ERROR("Error in transaction: %s", e.what());
        try { 
            conn_wrapper->get()->rollback(); 
            conn_wrapper->get()->setAutoCommit(true);
        } catch (...) {}
        // Connection automatically returned by RAII
        return false;
    }
}

// 使用回调函数处理每一行结果
void MySQLClientV2::queryWithCallback(const std::string& sql, 
                                     const std::vector<MySQLParamValue>& params,
                                     std::function<void(sql::ResultSet*)> rowCallback) {
    // 使用安全连接包装器
    auto conn_wrapper = getSafeConnection();
    if (!conn_wrapper) {
        LOG_ERROR("Failed to get database connection for queryWithCallback");
        return;
    }
    
    try {
        // 创建预编译语句
        std::unique_ptr<sql::PreparedStatement> stmt(conn_wrapper->get()->prepareStatement(sql));
        
        // 绑定参数
        for (size_t i = 0; i < params.size(); ++i) {
            try {
                bindParam(stmt.get(), i + 1, params[i]);
            } catch (const sql::SQLException& e) {
                LOG_ERROR("Failed to bind parameter %zu: %s", i, e.what());
                return;
            }
        }
        
        // 执行查询
        std::unique_ptr<sql::ResultSet> result(stmt->executeQuery());
        
        // 确保ResultSet在连接有效期内使用
        while (result->next()) {
            rowCallback(result.get());
        }
        // result在这里自动销毁，连接仍然有效
    }
    catch (const sql::SQLException& e) {
        LOG_ERROR("SQL Error in queryWithCallback: %s", e.what());
    }
    catch (const std::exception& e) {
        LOG_ERROR("Error in queryWithCallback: %s", e.what());
    }
    // conn_wrapper在这里析构，安全返回连接到池中
}

// 创建查询构建器 
QueryBuilderV2 MySQLClientV2::query(const std::string& sql) {
    return QueryBuilderV2(*this, sql);
}

// QueryBuilderV2::execute 实现
std::shared_ptr<sql::ResultSet> QueryBuilderV2::execute() {
    return client_.executeQuery(sql_, params_);
}

// QueryBuilderV2::executeUpdate 实现
uint64_t QueryBuilderV2::executeUpdate() {
    return client_.executeUpdate(sql_, params_);
}

// 安全执行SQL更新（带重试机制）
int64_t MySQLClientV2::safeExecuteUpdate(const std::string& sql, const std::vector<MySQLParamValue>& params, 
                                         uint64_t expectedRows) {
    const int maxRetries = 3;
    int retryCount = 0;
    
    while (retryCount < maxRetries) {
        try {
            uint64_t rowsAffected = executeUpdate(sql, params);
            if (rowsAffected == 0 && expectedRows > 0) {
                LOG_ERROR("Database operation returned 0 rows, expected %lu - connection likely lost", expectedRows);
                // executeUpdate已经会获取新连接，直接重试即可
                if (retryCount < maxRetries - 1) {
                    LOG_INFO("Retrying database operation (attempt %d/%d)", retryCount + 1, maxRetries);
                    retryCount++;
                    continue;
                }
                return -1;
            }
            return static_cast<int64_t>(rowsAffected);
        } catch (const sql::SQLException& e) {
            std::string errorMsg = e.what();
            if (errorMsg.find("Lost connection") != std::string::npos || 
                errorMsg.find("MySQL server has gone away") != std::string::npos ||
                errorMsg.find("inactivity") != std::string::npos ||
                errorMsg.find("disconnected by the server") != std::string::npos) {
                
                LOG_ERROR("Database connection error in safeExecuteUpdate: %s", e.what());
                
                // executeUpdate会自动获取新连接，直接重试即可
                if (retryCount < maxRetries - 1) {
                    LOG_INFO("Retrying database operation after connection error (attempt %d/%d)", retryCount + 1, maxRetries);
                    retryCount++;
                    continue;
                }
                return -1;
            }
            LOG_ERROR("SQL Error in safeExecuteUpdate: %s", e.what());
            return -1;
        } catch (const std::exception& e) {
            LOG_ERROR("Exception in safeExecuteUpdate: %s", e.what());
            return -1;
        }
    }
    
    LOG_ERROR("Max retries exceeded for safeExecuteUpdate");
    return -1;
}


// 安全执行事务（带重试机制）
bool MySQLClientV2::safeExecuteTransaction(std::function<bool(MySQLClientV2&)> transactionBody) {
    const int maxRetries = 3;
    int retryCount = 0;
    
    while (retryCount < maxRetries) {
        try {
            bool success = executeTransaction(transactionBody);
            return success;
        } catch (const sql::SQLException& e) {
            std::string errorMsg = e.what();
            if (errorMsg.find("Lost connection") != std::string::npos || 
                errorMsg.find("MySQL server has gone away") != std::string::npos ||
                errorMsg.find("inactivity") != std::string::npos ||
                errorMsg.find("disconnected by the server") != std::string::npos) {
                
                LOG_ERROR("Database connection error in safeExecuteTransaction: %s", e.what());
                
                if (retryCount < maxRetries - 1) {
                    LOG_INFO("Retrying transaction after connection error (attempt %d/%d)", retryCount + 1, maxRetries);
                    retryCount++;
                    continue;
                }
                return false;
            }
            LOG_ERROR("SQL Error in safeExecuteTransaction: %s", e.what());
            return false;
        } catch (const std::exception& e) {
            LOG_ERROR("Exception in safeExecuteTransaction: %s", e.what());
            return false;
        }
    }
    
    LOG_ERROR("Max retries exceeded for safeExecuteTransaction");
    return false;
}
