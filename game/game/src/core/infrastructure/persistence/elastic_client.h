#ifndef ELASTIC_CLIENT_H
#define ELASTIC_CLIENT_H

#include <string>
#include <memory>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <libuv_cpp/include/uv11.hpp>
#include <elasticlient/client.h>
#include <cpr/cpr.h>
#include <json/json.h>
#include "../common/config_manager.h"

class ElasticClient {
public:
    ElasticClient() = default;
    ~ElasticClient();
    
    // 禁用拷贝构造和赋值操作
    ElasticClient(const ElasticClient&) = delete;
    ElasticClient& operator=(const ElasticClient&) = delete;
    
    bool initialize(const ConfigManager& configManager);
    bool connect();
    void disconnect();
    
    // 索引操作
    bool createIndex(const std::string& indexName, const std::string& mappingJson);
    bool deleteIndex(const std::string& indexName);
    bool indexExists(const std::string& indexName);
    
    // 文档操作
    bool indexDocument(const std::string& indexName, const std::string& docType, 
                       const std::string& documentId, const std::string& documentJson, 
                       const std::string& routing = "");
    bool updateDocument(const std::string& indexName, const std::string& docType, 
                        const std::string& documentId, const std::string& documentJson, 
                        const std::string& routing = "");
    bool deleteDocument(const std::string& indexName, const std::string& docType, 
                        const std::string& documentId, const std::string& routing = "");
    bool getDocument(const std::string& indexName, const std::string& docType, 
                     const std::string& documentId, Json::Value& result, 
                     const std::string& routing = "");
    
    // 搜索操作
    bool search(const std::string& indexName, const std::string& docType, 
                const std::string& queryJson, Json::Value& results, 
                const std::string& routing = "");
    
    // 批量操作
    bool bulkOperation(const std::vector<std::pair<std::string, std::string>>& operations);
    
    // 高级查询
    bool executeQuery(elasticlient::Client::HTTPMethod method, 
                      const std::string& endpoint, const std::string& body, 
                      Json::Value& results);
    
    // 健康检查
    bool isHealthy();
    bool getClusterHealth(Json::Value& health);
    
private:
    // 创建新的客户端连接
    std::shared_ptr<elasticlient::Client> createClient();
    
    // 从连接池获取连接
    std::shared_ptr<elasticlient::Client> getClient();
    
    // 将连接释放回连接池
    void releaseClient(std::shared_ptr<elasticlient::Client> client);
    
    // 检查单个连接是否健康（不加锁版本）
    bool isClientHealthy(std::shared_ptr<elasticlient::Client> client);
    
    // 配置
    std::vector<std::string> hosts_;
    std::string username_;
    std::string password_;
    int connectionTimeout_ = 5000;  // 毫秒
    int requestTimeout_ = 30000;    // 毫秒
    unsigned int maxConnections_ = 10; // 连接池大小
    
    // 连接池管理
    std::vector<std::shared_ptr<elasticlient::Client>> clientPool_;
    std::mutex poolMutex_;
    
    // 辅助函数
    bool parseResponse(const cpr::Response& response, Json::Value& result) const;
};

#endif // ELASTIC_CLIENT_H
