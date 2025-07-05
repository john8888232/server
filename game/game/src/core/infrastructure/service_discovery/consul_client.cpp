#include "consul_client.h"
#include <third_party/libuv_cpp/include/LogWriter.hpp>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstring>

using namespace ppconsul;

ConsulClient::ConsulClient(uv::EventLoop* loop) : loop_(loop) {}

ConsulClient::~ConsulClient() {
    if (initialized_) {
        stopHealthCheck();
        // 注意：不在析构函数中调用deregisterService()
        // 因为这会导致重复注销（AppContext::destroyComponents()中已经调用过了）
    }
}

bool ConsulClient::initialize(const ConfigManager& configManager) {
    try {
        consulUrl_ = configManager.getServerConfig()["consul"]["url"];
        token_ = configManager.getServerConfig()["consul"]["token"];
        serviceName_ = configManager.getServerConfig()["consul"]["service_name"];
        serviceId_ = configManager.getServerConfig()["consul"]["service_id"];
        port_ = configManager.getServerConfig()["server"]["port"].get<unsigned short>();
        tags_ = configManager.getServerConfig()["server"]["game_type"].get<std::vector<std::string>>();
        healthCheckInterval_ = configManager.getServerConfig()["consul"]["service_check_interval"].get<int>();
        
        // 获取本机IP地址
        serviceAddress_ = getLocalIPAddress();
        if (serviceAddress_.empty()) {
            LOG_ERROR("Failed to get local IP address");
            return false;
        }
        LOG_INFO("Service will be registered with address: %s", serviceAddress_.c_str());
        
        if (!token_.empty()) {
            consul_ = std::make_unique<Consul>(consulUrl_, kw::token = token_);
        } else {
            consul_ = std::make_unique<Consul>(consulUrl_);
        }
        
        agent_ = std::make_unique<agent::Agent>(*consul_);
        initialized_ = true;
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to initialize Consul client: %s", e.what());
        return false;
    }
}

bool ConsulClient::registerService() {
    if (!initialized_) {
        LOG_ERROR("Consul client not initialized");
        return false;
    }
    try {
        agent_->registerService(
            agent::kw::id = serviceId_,
            agent::kw::name = serviceName_,
            agent::kw::address = serviceAddress_,  // 添加服务地址
            agent::kw::port = port_,
            agent::kw::tags = ppconsul::Tags(tags_.begin(), tags_.end()),
            agent::kw::check = agent::TtlCheck(std::chrono::seconds(healthCheckInterval_)));
        serviceRegistered_ = true;  // 标记服务已注册
        LOG_INFO("Service registered: %s (%s) at %s:%d", serviceName_.c_str(), serviceId_.c_str(), serviceAddress_.c_str(), port_);
        return true;
    } catch (const std::exception& e) {
        serviceRegistered_ = false;  // 注册失败，确保标志正确
        LOG_ERROR("Failed to register service with Consul: %s", e.what());
        return false;
    }
}

bool ConsulClient::deregisterService() {
    if (!initialized_) {
        LOG_ERROR("Consul client not initialized");
        return false;
    }
    
    if (!serviceRegistered_) {
        LOG_WARN("Service %s was not registered, skipping deregistration", serviceId_.c_str());
        return true;  // 没有注册就不需要注销，返回true避免错误日志
    }
    
    try {
        agent_->deregisterService(serviceId_);
        serviceRegistered_ = false;  // 标记服务已注销
        LOG_INFO("Service deregistered: %s", serviceId_.c_str());
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to deregister service from Consul: %s", e.what());
        return false;
    }
}

bool ConsulClient::startHealthCheck() {
    if (!initialized_) {
        LOG_ERROR("Consul client not initialized");
        return false;
    }
    
    if (isRunningHealthCheck_) {
        LOG_INFO("Health check is already running");
        return true;
    }
    
    try {
        int timerIntervalMs = (healthCheckInterval_ * 1000) / 2;
        
        healthCheckTimer_ = std::make_shared<uv::Timer>(loop_, timerIntervalMs, timerIntervalMs, 
            [this](uv::Timer* timer) {
                this->onHealthCheckTimer();
            });
        
        healthCheckTimer_->start();
        
        isRunningHealthCheck_ = true;
        LOG_INFO("Health check timer started with interval %d ms", timerIntervalMs);
        
        return reportHealth();
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to start health check: %s", e.what());
        return false;
    }
}

bool ConsulClient::stopHealthCheck() {
    if (!isRunningHealthCheck_ || !healthCheckTimer_) {
        return true;
    }
    
    try {
        healthCheckTimer_->close([](uv::Timer* timer) {
            LOG_INFO("Health check timer closed");
        });
        
        isRunningHealthCheck_ = false;
        LOG_INFO("Health check stopped");
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to stop health check: %s", e.what());
        return false;
    }
}

bool ConsulClient::reportHealth(bool isHealthy) {
    if (!initialized_) {
        LOG_ERROR("Consul client not initialized");
        return false;
    }
    
    try {
        if (isHealthy) {
            agent_->servicePass(serviceId_);
            LOG_DEBUG("Reported health check pass to Consul");
        } else {
            agent_->serviceFail(serviceId_, "Service is unhealthy");
            LOG_WARN("Reported health check failure to Consul");
        }
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to report health status to Consul: %s", e.what());
        return false;
    }
}

void ConsulClient::onHealthCheckTimer() {
    reportHealth();
}

std::string ConsulClient::getLocalIPAddress() {
    struct ifaddrs *ifaddrs_ptr = nullptr;
    std::string result;
    
    if (getifaddrs(&ifaddrs_ptr) == -1) {
        LOG_ERROR("Failed to get network interfaces");
        return "";
    }
    
    for (struct ifaddrs *ifa = ifaddrs_ptr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == nullptr) continue;
        
        // 只处理IPv4地址
        if (ifa->ifa_addr->sa_family == AF_INET) {
            struct sockaddr_in* addr_in = (struct sockaddr_in*)ifa->ifa_addr;
            char ip_str[INET_ADDRSTRLEN];
            
            if (inet_ntop(AF_INET, &(addr_in->sin_addr), ip_str, INET_ADDRSTRLEN) != nullptr) {
                std::string ip(ip_str);
                
                // 跳过回环地址和无效地址
                if (ip != "127.0.0.1" && ip != "0.0.0.0") {
                    // 优先选择内网地址 (10.x.x.x, 172.16-31.x.x, 192.168.x.x)
                    if (ip.substr(0, 3) == "10." || 
                        ip.substr(0, 8) == "192.168." || 
                        (ip.substr(0, 4) == "172." && 
                         std::stoi(ip.substr(4, ip.find('.', 4) - 4)) >= 16 && 
                         std::stoi(ip.substr(4, ip.find('.', 4) - 4)) <= 31)) {
                        result = ip;
                        break;  // 找到内网地址，优先使用
                    } else if (result.empty()) {
                        result = ip;  // 暂存第一个非回环地址
                    }
                }
            }
        }
    }
    
    freeifaddrs(ifaddrs_ptr);
    
    if (result.empty()) {
        LOG_ERROR("No suitable IP address found");
        return "";
    }
    
    LOG_INFO("Detected local IP address: %s", result.c_str());
    return result;
}