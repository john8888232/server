#ifndef CONSUL_CLIENT_H
#define CONSUL_CLIENT_H

#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include "ppconsul/ppconsul.h"
#include "core/infrastructure/common/config_manager.h"
#include "core/infrastructure/common/worker_thread.h"
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
    std::unique_ptr<ThreadWrapper> workerThread_;  // 工作线程
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
    
    // 健康检查定时器管理
    std::atomic<bool> isRunningHealthCheck_ = false;
    std::shared_ptr<uv::Timer> healthCheckTimer_;
    
    // 定时器回调函数
    void onHealthCheckTimer();
    
    // 获取本机IP地址
    std::string getLocalIPAddress();
};

#endif // CONSUL_CLIENT_H