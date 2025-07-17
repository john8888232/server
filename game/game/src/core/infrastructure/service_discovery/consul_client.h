#ifndef CONSUL_CLIENT_H
#define CONSUL_CLIENT_H

#include <string>
#include <vector>
#include <memory>
#include "ppconsul/ppconsul.h"
#include "../common/config_manager.h"
#include "third_party/libuv_cpp/include/uv11.hpp"

class ConsulClient {
public:
    ConsulClient(uv::EventLoop* loop);
    ~ConsulClient();

    bool initialize(const ConfigManager& configManager);
    bool registerService();
    bool deregisterService();
    
    bool startHealthCheck();
    bool stopHealthCheck();
    bool reportHealth(bool isHealthy = true);

private:
    uv::EventLoop* loop_;
    std::unique_ptr<ppconsul::Consul> consul_;
    std::unique_ptr<ppconsul::agent::Agent> agent_;
    std::string consulUrl_;
    std::string token_;
    std::string serviceId_;
    std::string serviceName_;
    std::string serviceAddress_;
    std::vector<std::string> tags_;
    unsigned short port_;
    int healthCheckInterval_;
    bool initialized_ = false;
    bool serviceRegistered_ = false;  // 追踪服务是否已注册
    
    // 健康检查定时器
    std::shared_ptr<uv::Timer> healthCheckTimer_;
    bool isRunningHealthCheck_ = false;
    
    // 定时器回调
    void onHealthCheckTimer();
    
    // 获取本机IP地址
    std::string getLocalIPAddress();
};

#endif // CONSUL_CLIENT_H