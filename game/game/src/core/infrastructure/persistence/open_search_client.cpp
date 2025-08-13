#include "open_search_client.h"
#include <sstream>
#include "third_party/libuv_cpp/include/LogWriter.hpp"
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <iomanip>
#include <ctime>
#include <chrono>

// AWS V4签名相关常量
namespace {
    const char* ALGORITHM = "AWS4-HMAC-SHA256";
    const char* SERVICE = "es";
    const char* AWS4_REQUEST = "aws4_request";
    const char* DATE_FORMAT = "%Y%m%d";
    const char* TIME_FORMAT = "%Y%m%dT%H%M%SZ";
}

class OpenSearchClient::Impl {
public:
    // 创建AWS V4签名
    std::string createSignatureV4(
        const std::string& httpMethod,
        const std::string& uri,
        const std::string& queryParams,
        const std::string& payload,
        const std::string& region,
        const std::string& accessKey,
        const std::string& secretKey,
        const std::string& host
    ) {
        // 获取当前时间，转换为GMT/UTC
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        std::tm tm_now;
        gmtime_r(&time_t_now, &tm_now);
        
        // 格式化日期和时间
        char dateStr[9];  // YYYYMMDD
        char timeStr[17]; // YYYYMMDDTHHMMSSZ
        std::strftime(dateStr, sizeof(dateStr), DATE_FORMAT, &tm_now);
        std::strftime(timeStr, sizeof(timeStr), TIME_FORMAT, &tm_now);

        // 准备签名所需要的各种变量
        std::string dateStamp(dateStr);
        std::string amzDate(timeStr);
        std::string credentialScope = dateStamp + "/" + region + "/" + SERVICE + "/" + AWS4_REQUEST;
        
        // 计算请求体的SHA256哈希
        std::string payloadHash = sha256Hash(payload);
        
        // 1. 规范化URI
        std::string canonicalUri = uri;
        if (canonicalUri.empty()) {
            canonicalUri = "/";
        }
        
        // 2. 规范化查询字符串
        std::string canonicalQueryString = queryParams;
        
        // 3. 规范化头部 - 按照字母顺序排列
        std::map<std::string, std::string> headers;
        headers["content-type"] = "application/json";
        headers["host"] = host;
        headers["x-amz-content-sha256"] = payloadHash;
        headers["x-amz-date"] = amzDate;
        
        std::string canonicalHeaders;
        std::string signedHeaders;
        
        // 构建规范化头部和签名头部
        for (const auto& header : headers) {
            canonicalHeaders += header.first + ":" + header.second + "\n";
        }
        
        // 构建签名头部列表（按字母顺序）
        signedHeaders = "content-type;host;x-amz-content-sha256;x-amz-date";
        
        // 4. 组合规范化请求
        std::string canonicalRequest = httpMethod + "\n" +
                                      canonicalUri + "\n" +
                                      canonicalQueryString + "\n" +
                                      canonicalHeaders + "\n" +
                                      signedHeaders + "\n" +
                                      payloadHash;
        
        LOG_DEBUG("Canonical Request: %s", canonicalRequest.c_str());
        
        // 5. 创建待签名字符串
        std::string stringToSign = std::string(ALGORITHM) + "\n" +
                                  amzDate + "\n" +
                                  credentialScope + "\n" +
                                  sha256Hash(canonicalRequest);
        
        LOG_DEBUG("String to Sign: %s", stringToSign.c_str());
        
        // 6. 计算签名
        std::string signature = calculateSignature(stringToSign, secretKey, dateStamp, region);
        
        // 7. 构建Authorization头
        std::string authorization = std::string(ALGORITHM) +
                                   " Credential=" + accessKey + "/" + credentialScope +
                                   ", SignedHeaders=" + signedHeaders +
                                   ", Signature=" + signature;
        
        // 保存amzDate供后续使用
        amzDate_ = amzDate;
        
        return authorization;
    }
    
    // SHA256哈希
    std::string sha256Hash(const std::string& data) {
        unsigned char hash[SHA256_DIGEST_LENGTH];
        
        // 使用EVP接口替代废弃的SHA256函数
        EVP_MD_CTX* ctx = EVP_MD_CTX_new();
        if (!ctx) {
            LOG_ERROR("Failed to create EVP_MD_CTX");
            return "";
        }
        
        if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1) {
            LOG_ERROR("Failed to initialize SHA256 digest");
            EVP_MD_CTX_free(ctx);
            return "";
        }
        
        if (EVP_DigestUpdate(ctx, data.c_str(), data.size()) != 1) {
            LOG_ERROR("Failed to update SHA256 digest");
            EVP_MD_CTX_free(ctx);
            return "";
        }
        
        unsigned int len = 0;
        if (EVP_DigestFinal_ex(ctx, hash, &len) != 1) {
            LOG_ERROR("Failed to finalize SHA256 digest");
            EVP_MD_CTX_free(ctx);
            return "";
        }
        
        EVP_MD_CTX_free(ctx);

        std::stringstream ss;
        for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
            ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
        }
        
        return ss.str();
    }
    
    // 计算签名
    std::string calculateSignature(
        const std::string& stringToSign,
        const std::string& secretKey,
        const std::string& dateStamp,
        const std::string& region
    ) {
        std::string kSecret = "AWS4" + secretKey;
        
        // 1. 派生密钥
        std::vector<unsigned char> kDate = hmacSha256(
            std::vector<unsigned char>(kSecret.begin(), kSecret.end()),
            dateStamp
        );
        
        std::vector<unsigned char> kRegion = hmacSha256(kDate, region);
        std::vector<unsigned char> kService = hmacSha256(kRegion, SERVICE);
        std::vector<unsigned char> kSigning = hmacSha256(kService, AWS4_REQUEST);
        
        // 2. 签名
        std::vector<unsigned char> signature = hmacSha256(kSigning, stringToSign);
        
        // 3. 转换为十六进制
        return bytesToHex(signature.data(), signature.size());
    }
    
    // 将字节转换为十六进制字符串
    std::string bytesToHex(const unsigned char* data, size_t len) {
        std::stringstream ss;
        ss << std::hex << std::setfill('0');
        for (size_t i = 0; i < len; ++i) {
            ss << std::setw(2) << static_cast<unsigned>(data[i]);
        }
        return ss.str();
    }
    
    // HMAC SHA256
    std::vector<unsigned char> hmacSha256(const std::vector<unsigned char>& key, const std::string& data) {
        unsigned char* digest = new unsigned char[SHA256_DIGEST_LENGTH];
        unsigned int len = SHA256_DIGEST_LENGTH;
        
        HMAC(EVP_sha256(), key.data(), key.size(),
             reinterpret_cast<const unsigned char*>(data.c_str()), data.length(),
             digest, &len);
             
        std::vector<unsigned char> result(digest, digest + len);
        delete[] digest;
        return result;
    }
    
    // 获取最后生成的amzDate
    std::string getAmzDate() const {
        return amzDate_;
    }
    
private:
    std::string amzDate_; // 保存最后一次生成的amzDate
};

// OpenSearchClient实现

OpenSearchClient::OpenSearchClient() : pimpl_(std::make_unique<Impl>()) {
}

OpenSearchClient::~OpenSearchClient() {
    disconnect();
}

bool OpenSearchClient::initialize(const ConfigManager& configManager) {
    try {
        // 从配置中获取OpenSearch设置
        endpoint_ = configManager.getServerConfig()["database"]["opensearch"]["endpoint"].get<std::string>();
        accessKey_ = configManager.getServerConfig()["database"]["opensearch"]["access_key"].get<std::string>();
        secretKey_ = configManager.getServerConfig()["database"]["opensearch"]["secret_key"].get<std::string>();
        region_ = configManager.getServerConfig()["database"]["opensearch"]["region"].get<std::string>();
        connectionTimeout_ = configManager.getServerConfig()["database"]["opensearch"]["connection_timeout"].get<int>();
        requestTimeout_ = configManager.getServerConfig()["database"]["opensearch"]["request_timeout"].get<int>();
        
        // 提取主机名
        size_t hostStart = endpoint_.find("://");
        if (hostStart != std::string::npos) {
            hostStart += 3;
            size_t hostEnd = endpoint_.find("/", hostStart);
            if (hostEnd != std::string::npos) {
                host_ = endpoint_.substr(hostStart, hostEnd - hostStart);
            } else {
                host_ = endpoint_.substr(hostStart);
            }
        } else {
            host_ = endpoint_;
        }
        
        LOG_DEBUG("OpenSearch client initialized with endpoint: %s, host: %s, region: %s", 
                 endpoint_.c_str(), host_.c_str(), region_.c_str());
        
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Error initializing OpenSearch client: %s", e.what());
        return false;
    } catch (...) {
        LOG_ERROR("Unknown error initializing OpenSearch client");
        return false;
    }
}

bool OpenSearchClient::connect() {
    try {
        // 测试连接是否有效
        LOG_DEBUG("Testing connection to OpenSearch endpoint: %s", endpoint_.c_str());
        LOG_DEBUG("Using AWS credentials: AccessKey=%s, Region=%s", accessKey_.c_str(), region_.c_str());
        LOG_DEBUG("Using AWS V4 Signature Authentication");
        
        // 发送一个简单的请求来测试连接
        Json::Value health;
        LOG_INFO("Sending test request to AWS OpenSearch with SigV4 authentication...");
        bool success = getClusterHealth(health);
        
        if (success) {
            LOG_INFO("AWS SigV4 authentication successful!");
            return true;
        } else {
            LOG_ERROR("AWS OpenSearch connection FAILED!");
            return false;
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Error connecting to AWS OpenSearch: %s", e.what());
        return false;
    } catch (...) {
        LOG_ERROR("Unknown error connecting to AWS OpenSearch");
        return false;
    }
}

void OpenSearchClient::disconnect() {
    LOG_INFO("Disconnected from OpenSearch");
}

std::string OpenSearchClient::createAuthHeader(const std::string& method, 
                                            const std::string& path, 
                                            const std::string& body, 
                                            const std::string& contentType) {
    return pimpl_->createSignatureV4(
        method,
        path,
        "", // 查询参数
        body,
        region_,
        accessKey_,
        secretKey_,
        host_
    );
}

bool OpenSearchClient::sendRequest(const std::string& method, 
                                  const std::string& path,
                                  const std::string& body,
                                  Json::Value& result) {
    try {
        // 构建完整URL
        std::string url = endpoint_;
        // 确保URL不以/结尾，而path以/开头
        if (!path.empty()) {
            if (url.back() == '/' && path.front() == '/') {
                url += path.substr(1);
            } else if (url.back() != '/' && path.front() != '/') {
                url += "/" + path;
            } else {
                url += path;
            }
        }
        
        // 规范化路径，用于签名
        std::string canonicalPath = path;
        if (canonicalPath.empty() || canonicalPath == "/") {
            canonicalPath = "/";
        } else if (canonicalPath.front() != '/') {
            canonicalPath = "/" + canonicalPath;
        }
        
        LOG_DEBUG("Preparing %s request to %s", method.c_str(), url.c_str());
        LOG_DEBUG("Canonical path for signing: %s", canonicalPath.c_str());
        
        // 获取认证头
        std::string authHeader = createAuthHeader(method, canonicalPath, body);
        
        // 设置超时
        cpr::Timeout timeout(connectionTimeout_ * 1000);
        
        // 计算请求体哈希值
        std::string payloadHash = pimpl_->sha256Hash(body);
        
        // 设置请求头
        cpr::Header headers = {
            {"Content-Type", "application/json"},
            {"Host", host_},
            {"X-Amz-Date", pimpl_->getAmzDate()},
            {"X-Amz-Content-Sha256", payloadHash},
            {"Authorization", authHeader}
        };
        
        LOG_DEBUG("Request headers:");
        for (const auto& header : headers) {
            LOG_DEBUG("  %s: %s", header.first.c_str(), header.second.c_str());
        }
        
        if (!body.empty()) {
            LOG_DEBUG("Request body length: %zu", body.size());
            if (body.size() < 1000) {
                LOG_DEBUG("Request body: %s", body.c_str());
            } else {
                LOG_DEBUG("Request body too large to log");
            }
        }
        
        // 发送请求
        cpr::Response response;
        
        if (method == "GET") {
            response = cpr::Get(
                cpr::Url{url},
                headers,
                timeout
            );
        } else if (method == "POST") {
            response = cpr::Post(
                cpr::Url{url},
                headers,
                cpr::Body{body},
                timeout
            );
        } else if (method == "PUT") {
            response = cpr::Put(
                cpr::Url{url},
                headers,
                cpr::Body{body},
                timeout
            );
        } else if (method == "DELETE") {
            response = cpr::Delete(
                cpr::Url{url},
                headers,
                timeout
            );
        } else if (method == "HEAD") {
            response = cpr::Head(
                cpr::Url{url},
                headers,
                timeout
            );
            // HEAD请求只检查状态码
            return response.status_code == 200;
        } else {
            LOG_ERROR("Unsupported HTTP method: %s", method.c_str());
            return false;
        }
        
        LOG_DEBUG("Response received with status code: %d", response.status_code);
        
        // 解析响应
        return parseResponse(response, result);
    } catch (const std::exception& e) {
        LOG_ERROR("Error sending request to OpenSearch: %s", e.what());
        return false;
    } catch (...) {
        LOG_ERROR("Unknown error sending request to OpenSearch");
        return false;
    }
}

bool OpenSearchClient::parseResponse(const cpr::Response& response, Json::Value& result) const {
    if (response.status_code < 200 || response.status_code >= 300) {
        LOG_ERROR("AWS OpenSearch error: HTTP status code %d", response.status_code);
        LOG_ERROR("Error response body: %s", response.text.c_str());
        return false;
    }
    
    if (response.text.empty()) {
        LOG_ERROR("Empty response string from OpenSearch");
        return false;
    }
    
    LOG_DEBUG("Response body length: %zu bytes", response.text.size());
    
    // 如果响应比较短，直接记录
    if (response.text.size() < 500) {
        LOG_DEBUG("Full response: %s", response.text.c_str());
    } else {
        // 否则只记录前100个字符
        LOG_DEBUG("Response preview: %.100s...", response.text.c_str());
    }
    
    // 解析JSON
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
        LOG_ERROR("Failed to parse OpenSearch JSON response: %s", errors.c_str());
        LOG_ERROR("Invalid JSON response: %s", response.text.c_str());
        return false;
    }
    
    LOG_DEBUG("Successfully parsed JSON response with %u elements", result.size());
    return true;
}

bool OpenSearchClient::isHealthy() {
    Json::Value health;
    return getClusterHealth(health);
}

bool OpenSearchClient::getClusterHealth(Json::Value& health) {
    return sendRequest("GET", "/_cluster/health", "", health);
}

bool OpenSearchClient::createIndex(const std::string& indexName, const std::string& mappingJson) {
    Json::Value result;
    bool success = sendRequest("PUT", indexName, mappingJson, result);
    
    if (success) {
        LOG_INFO("Created index: %s", indexName.c_str());
    }
    
    return success;
}

bool OpenSearchClient::deleteIndex(const std::string& indexName) {
    Json::Value result;
    bool success = sendRequest("DELETE", indexName, "", result);
    
    if (success) {
        LOG_INFO("Deleted index: %s", indexName.c_str());
    }
    
    return success;
}

bool OpenSearchClient::indexExists(const std::string& indexName) {
    Json::Value result;
    return sendRequest("HEAD", indexName, "", result);
}

bool OpenSearchClient::indexDocument(const std::string& indexName, const std::string& docType, 
                                   const std::string& documentId, const std::string& documentJson) {
    std::string path = indexName + "/" + docType + "/" + documentId;
    Json::Value result;
    bool success = sendRequest("PUT", path, documentJson, result);
    
    if (success) {
        LOG_DEBUG("Indexed document %s in %s/%s", documentId.c_str(), indexName.c_str(), docType.c_str());
    }
    
    return success;
}

bool OpenSearchClient::updateDocument(const std::string& indexName, const std::string& docType, 
                                    const std::string& documentId, const std::string& documentJson) {
    std::string path = indexName + "/" + docType + "/" + documentId + "/_update";
    std::string body = "{\"doc\":" + documentJson + "}";
    Json::Value result;
    bool success = sendRequest("POST", path, body, result);
    
    if (success) {
        LOG_DEBUG("Updated document %s in %s/%s", documentId.c_str(), indexName.c_str(), docType.c_str());
    }
    
    return success;
}

bool OpenSearchClient::deleteDocument(const std::string& indexName, const std::string& docType, 
                                    const std::string& documentId) {
    std::string path = indexName + "/" + docType + "/" + documentId;
    Json::Value result;
    bool success = sendRequest("DELETE", path, "", result);
    
    if (success) {
        LOG_DEBUG("Deleted document %s from %s/%s", documentId.c_str(), indexName.c_str(), docType.c_str());
    }
    
    return success;
}

bool OpenSearchClient::getDocument(const std::string& indexName, const std::string& docType, 
                                 const std::string& documentId, Json::Value& result) {
    std::string path = indexName + "/" + docType + "/" + documentId;
    return sendRequest("GET", path, "", result);
}

bool OpenSearchClient::search(const std::string& indexName, const std::string& docType, 
                            const std::string& queryJson, Json::Value& results) {
    std::string path = indexName + "/" + docType + "/_search";
    return sendRequest("POST", path, queryJson, results);
}

bool OpenSearchClient::bulkOperation(const std::vector<std::pair<std::string, std::string>>& operations) {
    std::string body;
    for (const auto& op : operations) {
        body += op.first + "\n" + op.second + "\n";
    }
    
    Json::Value result;
    bool success = sendRequest("POST", "_bulk", body, result);
    
    if (success) {
        LOG_DEBUG("Bulk operation completed with %zu operations", operations.size());
    }
    
    return success;
}

bool OpenSearchClient::executeQuery(const std::string& method, 
                                  const std::string& endpoint, 
                                  const std::string& body, 
                                  Json::Value& results) {
    return sendRequest(method, endpoint, body, results);
} 