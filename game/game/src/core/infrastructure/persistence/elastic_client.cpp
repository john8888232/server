#include "elastic_client.h"
#include <sstream>
#include <third_party/libuv_cpp/include/LogWriter.hpp>

ElasticClient::~ElasticClient() {
    disconnect();
}

bool ElasticClient::initialize(const ConfigManager& configManager) {
    try {
        // 从配置中获取Elasticsearch设置
        hosts_ = configManager.getServerConfig()["database"]["elasticsearch"]["hosts"].get<std::vector<std::string>>();
        connectionTimeout_ = configManager.getServerConfig()["database"]["elasticsearch"]["connection_timeout"].get<int>();
        requestTimeout_ = configManager.getServerConfig()["database"]["elasticsearch"]["request_timeout"].get<int>();
        maxConnections_ = configManager.getServerConfig()["database"]["elasticsearch"]["max_connections"].get<int>();
        
        // 毫秒转换
        connectionTimeout_ *= 1000;
        requestTimeout_ *= 1000;
        
        LOG_DEBUG("Elasticsearch client initialized with %zu hosts, max connections: %d", hosts_.size(), maxConnections_);
        for (const auto& host : hosts_) {
            LOG_DEBUG("  - %s", host.c_str());
        }
        
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Error initializing Elasticsearch client: %s", e.what());
        return false;
    } catch (...) {
        LOG_ERROR("Unknown error initializing Elasticsearch client");
        return false;
    }
}

std::shared_ptr<elasticlient::Client> ElasticClient::createClient() {
    try {
        // 创建带认证的客户端
        auto client = std::make_shared<elasticlient::Client>(
            hosts_, 
            elasticlient::Client::TimeoutOption(requestTimeout_),
            elasticlient::Client::ConnectTimeoutOption(connectionTimeout_)
        );
        
        return client;
    } catch (const std::exception& e) {
        LOG_ERROR("Error creating Elasticsearch client: %s", e.what());
        return nullptr;
    } catch (...) {
        LOG_ERROR("Unknown error creating Elasticsearch client");
        return nullptr;
    }
}

bool ElasticClient::connect() {
    std::lock_guard<std::mutex> lock(poolMutex_);
    try {
        // 创建连接池
        for (unsigned int i = 0; i < maxConnections_; i++) {
            auto client = createClient();
            if (client != nullptr) {
                clientPool_.push_back(client);
            }
        }
        
        // 测试连接池中的第一个连接
        if (!clientPool_.empty() && isClientHealthy(clientPool_[0])) {
            LOG_INFO("Connected to Elasticsearch cluster with %d connections in pool", maxConnections_);
            return true;
        } else {
            LOG_ERROR("Failed to connect to Elasticsearch cluster");
            clientPool_.clear();
            return false;
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Error connecting to Elasticsearch: %s", e.what());
        return false;
    } catch (...) {
        LOG_ERROR("Unknown error connecting to Elasticsearch");
        return false;
    }
}

void ElasticClient::disconnect() {
    std::lock_guard<std::mutex> lock(poolMutex_);
    clientPool_.clear();
    LOG_INFO("Disconnected from Elasticsearch");
}

std::shared_ptr<elasticlient::Client> ElasticClient::getClient() {
    std::lock_guard<std::mutex> lock(poolMutex_);
    if (clientPool_.empty()) {
        return createClient();
    }
    
    // 从连接池获取连接
    std::shared_ptr<elasticlient::Client> client = clientPool_.back();
    clientPool_.pop_back();
    return client;
}

void ElasticClient::releaseClient(std::shared_ptr<elasticlient::Client> client) {
    if (!client) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(poolMutex_);
    
    // 检查连接池大小
    if (clientPool_.size() >= maxConnections_) {
        return;
    }
    
    // 将连接放回连接池
    clientPool_.push_back(client);
}

bool ElasticClient::isClientHealthy(std::shared_ptr<elasticlient::Client> client) {
    if (!client) {
        return false;
    }
    
    try {
        cpr::Response response = client->performRequest(
            elasticlient::Client::HTTPMethod::GET,
            "_cluster/health",
            ""
        );
        
        return response.status_code == 200;
    } catch (const elasticlient::ConnectionException& e) {
        LOG_ERROR("Connection error in health check: %s", e.what());
        return false;
    } catch (const std::exception& e) {
        LOG_ERROR("Health check failed: %s", e.what());
        return false;
    } catch (...) {
        LOG_ERROR("Unknown error in health check");
        return false;
    }
}

bool ElasticClient::parseResponse(const cpr::Response& response, Json::Value& result) const {
    if (response.status_code < 200 || response.status_code >= 300) {
        LOG_ERROR("Elasticsearch error: %s", response.text.c_str());
        return false;
    }
    
    Json::CharReaderBuilder builder;
    Json::CharReader* reader = builder.newCharReader();
    std::string errors;
    
    bool parsingSuccessful = reader->parse(
        response.text.c_str(),
        response.text.c_str() + response.text.size(),
        &result,
        &errors
    );
    
    delete reader;
    
    if (!parsingSuccessful) {
        LOG_ERROR("Failed to parse Elasticsearch response: %s", errors.c_str());
        return false;
    }
    
    return true;
}

bool ElasticClient::createIndex(const std::string& indexName, const std::string& mappingJson) {
    auto client = getClient();
    if (!client) {
        LOG_ERROR("Elasticsearch client not connected");
        return false;
    }
    
    try {
        cpr::Response response = client->performRequest(
            elasticlient::Client::HTTPMethod::PUT,
            indexName,
            mappingJson
        );
        
        Json::Value result;
        bool success = parseResponse(response, result);
        releaseClient(client);
        
        if (success) {
            LOG_INFO("Created index: %s", indexName.c_str());
            return true;
        }
        return false;
    } catch (const elasticlient::ConnectionException& e) {
        LOG_ERROR("Connection error creating index %s: %s", indexName.c_str(), e.what());
        releaseClient(client);
        return false;
    } catch (const std::exception& e) {
        LOG_ERROR("Error creating index %s: %s", indexName.c_str(), e.what());
        releaseClient(client);
        return false;
    } catch (...) {
        LOG_ERROR("Unknown error creating index %s", indexName.c_str());
        releaseClient(client);
        return false;
    }
}

bool ElasticClient::deleteIndex(const std::string& indexName) {
    auto client = getClient();
    if (!client) {
        LOG_ERROR("Elasticsearch client not connected");
        return false;
    }
    
    try {
        cpr::Response response = client->performRequest(
            elasticlient::Client::HTTPMethod::DELETE,
            indexName,
            ""
        );
        
        Json::Value result;
        bool success = parseResponse(response, result);
        releaseClient(client);
        
        if (success) {
            LOG_INFO("Deleted index: %s", indexName.c_str());
            return true;
        }
        return false;
    } catch (const elasticlient::ConnectionException& e) {
        LOG_ERROR("Connection error deleting index %s: %s", indexName.c_str(), e.what());
        releaseClient(client);
        return false;
    } catch (const std::exception& e) {
        LOG_ERROR("Error deleting index %s: %s", indexName.c_str(), e.what());
        releaseClient(client);
        return false;
    } catch (...) {
        LOG_ERROR("Unknown error deleting index %s", indexName.c_str());
        releaseClient(client);
        return false;
    }
}

bool ElasticClient::indexExists(const std::string& indexName) {
    auto client = getClient();
    if (!client) {
        LOG_ERROR("Elasticsearch client not connected");
        return false;
    }
    
    try {
        cpr::Response response = client->performRequest(
            elasticlient::Client::HTTPMethod::HEAD,
            indexName,
            ""
        );
        
        bool exists = response.status_code == 200;
        releaseClient(client);
        return exists;
    } catch (const elasticlient::ConnectionException& e) {
        LOG_ERROR("Connection error checking index %s: %s", indexName.c_str(), e.what());
        releaseClient(client);
        return false;
    } catch (const std::exception& e) {
        LOG_ERROR("Error checking index %s: %s", indexName.c_str(), e.what());
        releaseClient(client);
        return false;
    } catch (...) {
        LOG_ERROR("Unknown error checking index %s", indexName.c_str());
        releaseClient(client);
        return false;
    }
}

bool ElasticClient::indexDocument(const std::string& indexName, const std::string& docType, 
                                 const std::string& documentId, const std::string& documentJson, 
                                 const std::string& routing) {
    auto client = getClient();
    if (!client) {
        LOG_ERROR("Elasticsearch client not connected");
        return false;
    }
    
    try {
        cpr::Response response = client->index(indexName, docType, documentId, documentJson, routing);
        
        Json::Value result;
        bool success = parseResponse(response, result);
        releaseClient(client);
        return success;
    } catch (const elasticlient::ConnectionException& e) {
        LOG_ERROR("Connection error indexing document: %s", e.what());
        releaseClient(client);
        return false;
    } catch (const std::exception& e) {
        LOG_ERROR("Error indexing document: %s", e.what());
        releaseClient(client);
        return false;
    } catch (...) {
        LOG_ERROR("Unknown error indexing document");
        releaseClient(client);
        return false;
    }
}

bool ElasticClient::updateDocument(const std::string& indexName, const std::string& docType, 
                                  const std::string& documentId, const std::string& documentJson, 
                                  const std::string& routing) {
    auto client = getClient();
    if (!client) {
        LOG_ERROR("Elasticsearch client not connected");
        return false;
    }
    
    try {
        // 构建更新请求
        std::string updateJson = "{\"doc\":" + documentJson + "}";
        
        std::ostringstream urlPath;
        urlPath << indexName << "/" << docType << "/" << documentId << "/_update";
        if (!routing.empty()) {
            urlPath << "?routing=" << routing;
        }
        
        cpr::Response response = client->performRequest(
            elasticlient::Client::HTTPMethod::POST,
            urlPath.str(),
            updateJson
        );
        
        Json::Value result;
        bool success = parseResponse(response, result);
        releaseClient(client);
        return success;
    } catch (const elasticlient::ConnectionException& e) {
        LOG_ERROR("Connection error updating document: %s", e.what());
        releaseClient(client);
        return false;
    } catch (const std::exception& e) {
        LOG_ERROR("Error updating document: %s", e.what());
        releaseClient(client);
        return false;
    } catch (...) {
        LOG_ERROR("Unknown error updating document");
        releaseClient(client);
        return false;
    }
}

bool ElasticClient::deleteDocument(const std::string& indexName, const std::string& docType, 
                                  const std::string& documentId, const std::string& routing) {
    auto client = getClient();
    if (!client) {
        LOG_ERROR("Elasticsearch client not connected");
        return false;
    }
    
    try {
        cpr::Response response = client->remove(indexName, docType, documentId, routing);
        
        Json::Value result;
        bool success = parseResponse(response, result);
        releaseClient(client);
        return success;
    } catch (const elasticlient::ConnectionException& e) {
        LOG_ERROR("Connection error deleting document: %s", e.what());
        releaseClient(client);
        return false;
    } catch (const std::exception& e) {
        LOG_ERROR("Error deleting document: %s", e.what());
        releaseClient(client);
        return false;
    } catch (...) {
        LOG_ERROR("Unknown error deleting document");
        releaseClient(client);
        return false;
    }
}

bool ElasticClient::getDocument(const std::string& indexName, const std::string& docType, 
                               const std::string& documentId, Json::Value& result, 
                               const std::string& routing) {
    auto client = getClient();
    if (!client) {
        LOG_ERROR("Elasticsearch client not connected");
        return false;
    }
    
    try {
        cpr::Response response = client->get(indexName, docType, documentId, routing);
        
        if (response.status_code == 404) {
            LOG_DEBUG("Document %s not found in index %s", documentId.c_str(), indexName.c_str());
            releaseClient(client);
            return false;
        }
        
        bool success = parseResponse(response, result);
        releaseClient(client);
        return success;
    } catch (const elasticlient::ConnectionException& e) {
        LOG_ERROR("Connection error getting document: %s", e.what());
        releaseClient(client);
        return false;
    } catch (const std::exception& e) {
        LOG_ERROR("Error getting document: %s", e.what());
        releaseClient(client);
        return false;
    } catch (...) {
        LOG_ERROR("Unknown error getting document");
        releaseClient(client);
        return false;
    }
}

bool ElasticClient::search(const std::string& indexName, const std::string& docType, 
                          const std::string& queryJson, Json::Value& results, 
                          const std::string& routing) {
    auto client = getClient();
    if (!client) {
        LOG_ERROR("Elasticsearch client not connected");
        return false;
    }
    
    try {
        cpr::Response response = client->search(indexName, docType, queryJson, routing);
        
        bool success = parseResponse(response, results);
        releaseClient(client);
        return success;
    } catch (const elasticlient::ConnectionException& e) {
        LOG_ERROR("Connection error searching documents: %s", e.what());
        releaseClient(client);
        return false;
    } catch (const std::exception& e) {
        LOG_ERROR("Error searching documents: %s", e.what());
        releaseClient(client);
        return false;
    } catch (...) {
        LOG_ERROR("Unknown error searching documents");
        releaseClient(client);
        return false;
    }
}

bool ElasticClient::bulkOperation(const std::vector<std::pair<std::string, std::string>>& operations) {
    auto client = getClient();
    if (!client) {
        LOG_ERROR("Elasticsearch client not connected");
        return false;
    }
    
    try {
        // 构建批量操作请求体
        std::string bulkBody;
        for (const auto& op : operations) {
            bulkBody += op.first + "\n";
            bulkBody += op.second + "\n";
        }
        
        cpr::Response response = client->performRequest(
            elasticlient::Client::HTTPMethod::POST,
            "_bulk",
            bulkBody
        );
        
        Json::Value result;
        if (!parseResponse(response, result)) {
            releaseClient(client);
            return false;
        }
        
        // 检查是否有错误
        bool success = true;
        if (result.isMember("errors") && result["errors"].asBool()) {
            LOG_ERROR("Bulk operation completed with errors");
            success = false;
        }
        
        releaseClient(client);
        return success;
    } catch (const elasticlient::ConnectionException& e) {
        LOG_ERROR("Connection error performing bulk operation: %s", e.what());
        releaseClient(client);
        return false;
    } catch (const std::exception& e) {
        LOG_ERROR("Error performing bulk operation: %s", e.what());
        releaseClient(client);
        return false;
    } catch (...) {
        LOG_ERROR("Unknown error performing bulk operation");
        releaseClient(client);
        return false;
    }
}

bool ElasticClient::executeQuery(elasticlient::Client::HTTPMethod method, 
                                const std::string& endpoint, const std::string& body, 
                                Json::Value& results) {
    auto client = getClient();
    if (!client) {
        LOG_ERROR("Elasticsearch client not connected");
        return false;
    }
    
    try {
        cpr::Response response = client->performRequest(method, endpoint, body);
        
        bool success = parseResponse(response, results);
        releaseClient(client);
        return success;
    } catch (const elasticlient::ConnectionException& e) {
        LOG_ERROR("Connection error executing query: %s", e.what());
        releaseClient(client);
        return false;
    } catch (const std::exception& e) {
        LOG_ERROR("Error executing query: %s", e.what());
        releaseClient(client);
        return false;
    } catch (...) {
        LOG_ERROR("Unknown error executing query");
        releaseClient(client);
        return false;
    }
}

bool ElasticClient::isHealthy() {
    // 从连接池获取一个连接检查健康状态
    auto client = getClient();
    bool healthy = isClientHealthy(client);
    releaseClient(client);
    return healthy;
}

bool ElasticClient::getClusterHealth(Json::Value& health) {
    auto client = getClient();
    if (!client) {
        LOG_ERROR("Elasticsearch client not connected");
        return false;
    }
    
    try {
        cpr::Response response = client->performRequest(
            elasticlient::Client::HTTPMethod::GET,
            "_cluster/health",
            ""
        );
        
        bool success = parseResponse(response, health);
        releaseClient(client);
        return success;
    } catch (const elasticlient::ConnectionException& e) {
        LOG_ERROR("Connection error getting cluster health: %s", e.what());
        releaseClient(client);
        return false;
    } catch (const std::exception& e) {
        LOG_ERROR("Error getting cluster health: %s", e.what());
        releaseClient(client);
        return false;
    } catch (...) {
        LOG_ERROR("Unknown error getting cluster health");
        releaseClient(client);
        return false;
    }
}
