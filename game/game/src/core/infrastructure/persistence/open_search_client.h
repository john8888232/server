#ifndef OPEN_SEARCH_CLIENT_H
#define OPEN_SEARCH_CLIENT_H

#include <string>
#include <memory>
#include <vector>
#include <libuv_cpp/include/uv11.hpp>
#include <cpr/cpr.h>
#include <json/json.h>
#include "../common/config_manager.h"

// 前向声明，避免包含完整的AWS SDK头文件
namespace Aws {
    class SDKOptions;
    namespace Auth {
        class AWSCredentials;
    }
    namespace Client {
        class ClientConfiguration;
    }
}

class OpenSearchClient {
public:
    OpenSearchClient();
    ~OpenSearchClient();
    
    // 禁用拷贝构造和赋值操作
    OpenSearchClient(const OpenSearchClient&) = delete;
    OpenSearchClient& operator=(const OpenSearchClient&) = delete;
    
    bool initialize(const ConfigManager& configManager);
    bool connect();
    void disconnect();
    
    // 索引操作
    bool createIndex(const std::string& indexName, const std::string& mappingJson);
    bool deleteIndex(const std::string& indexName);
    bool indexExists(const std::string& indexName);
    
    // 文档操作
    bool indexDocument(const std::string& indexName, const std::string& docType, 
                       const std::string& documentId, const std::string& documentJson);
    bool updateDocument(const std::string& indexName, const std::string& docType, 
                        const std::string& documentId, const std::string& documentJson);
    bool deleteDocument(const std::string& indexName, const std::string& docType, 
                        const std::string& documentId);
    bool getDocument(const std::string& indexName, const std::string& docType, 
                     const std::string& documentId, Json::Value& result);
    
    // 搜索操作
    bool search(const std::string& indexName, const std::string& docType, 
                const std::string& queryJson, Json::Value& results);
    
    // 批量操作
    bool bulkOperation(const std::vector<std::pair<std::string, std::string>>& operations);
    
    // 执行自定义查询
    bool executeQuery(const std::string& method, 
                      const std::string& endpoint, const std::string& body, 
                      Json::Value& results);
    
    // 健康检查
    bool isHealthy();
    bool getClusterHealth(Json::Value& health);
    
private:
    // 发送HTTP请求到OpenSearch
    bool sendRequest(const std::string& method, 
                     const std::string& path,
                     const std::string& body,
                     Json::Value& result);
    
    // 使用AWS V4签名创建认证头
    std::string createAuthHeader(const std::string& method, 
                                const std::string& path,
                                const std::string& body,
                                const std::string& contentType = "application/json");
    
    // 辅助函数
    bool parseResponse(const cpr::Response& response, Json::Value& result) const;
    
    // 配置
    std::string endpoint_;
    std::string host_;        // 从endpoint中提取的主机名
    std::string accessKey_;
    std::string secretKey_;
    std::string region_;
    int connectionTimeout_ = 5000;  // 毫秒
    int requestTimeout_ = 30000;    // 毫秒
    
    // 私有实现，避免头文件依赖
    class Impl;
    std::unique_ptr<Impl> pimpl_;
};

#endif // OPEN_SEARCH_CLIENT_H 