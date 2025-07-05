// src/main.cpp
#include <iostream>
#include <memory>
#include <signal.h>
#include <execinfo.h>
#include <unistd.h>
#include <thread>
#include <chrono>
#include "core/infrastructure/common/config_manager.h"
#include "core/infrastructure/persistence/database_factory.h"
#include "core/infrastructure/common/app_context.h"
#include "core/infrastructure/common/Logger_helper.h"

// 将调用栈信息打印到日志的函数
void print_backtrace() {
    const int MAX_FRAMES = 64;
    void* addrlist[MAX_FRAMES];
    
    int addrlen = backtrace(addrlist, sizeof(addrlist) / sizeof(void*));
    if (addrlen == 0) {
        LOG_ERROR("No backtrace available");
        return;
    }
    
    char** symbollist = backtrace_symbols(addrlist, addrlen);
    if (symbollist == nullptr) {
        LOG_ERROR("Error retrieving backtrace symbols");
        return;
    }
    
    LOG_ERROR("Backtrace:");
    for (int i = 0; i < addrlen; i++) {
        LOG_ERROR("  #%d: %s", i, symbollist[i]);
    }
    
    free(symbollist);
}

// 优雅关闭函数
void graceful_shutdown() {
    static std::atomic<bool> shutdown_in_progress{false};
    
    // 防止重复调用
    if (shutdown_in_progress.exchange(true)) {
        return;
    }
    
    LOG_INFO("Starting graceful shutdown...");
    
    try {
        // 清理所有资源
        AppContext::getInstance().cleanup();
        
        LOG_INFO("Shutdown completed successfully");
        std::exit(0);
    } catch (const std::exception& e) {
        std::cerr << "Exception during shutdown: " << e.what() << std::endl;
        std::exit(1);
    } catch (...) {
        std::cerr << "Unknown exception during shutdown" << std::endl;
        std::exit(1);
    }
}

// 信号处理器
void signal_handler(int signum) {
    static int sigint_count = 0;
    
    switch (signum) {
        case SIGINT:
            sigint_count++;
            if (sigint_count == 1) {
                LOG_INFO("Received SIGINT, initiating graceful shutdown");
                graceful_shutdown();
            } else {
                // 第二次收到SIGINT，强制退出
                LOG_WARN("Received SIGINT again, forcing immediate exit");
                std::cout << "Forcing immediate exit due to repeated SIGINT..." << std::endl;
                std::_Exit(1);
            }
            break;
        case SIGTERM:
            LOG_INFO("Received SIGTERM, initiating graceful shutdown");
            graceful_shutdown();
            break;
        case SIGSEGV:
            LOG_ERROR("Received SIGSEGV (Segmentation Fault)");
            print_backtrace();
            signal(signum, SIG_DFL); // 重置为默认处理器
            // 日志记录后，重新抛出信号以生成核心转储
            raise(signum);
            break;
        case SIGABRT:
            LOG_ERROR("Received SIGABRT (Abort)");
            print_backtrace();
            signal(signum, SIG_DFL); // 重置为默认处理器
            raise(signum);
            break;
        default:
            LOG_INFO("Received signal %d", signum);
            break;
    }
}

// 设置信号处理器
void setup_signal_handlers() {
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    
    // 注册终止信号
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    
    // 注册崩溃信号
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
    
    LOG_INFO("Signal handlers installed");
}

int main(void) {
    // 初始化日志
    if (!LoggerHelper::Initialize()) {
        std::cerr << "Failed to initialize logger" << std::endl;
        return -1;
    }
    
    LOG_INFO("Game server starting up...");
    
    auto configManager = std::make_shared<ConfigManager>();
    
    // 加载服务配置文件
    if (!configManager->loadServerConfig()) {
        LOG_ERROR("Failed to load configuration");
        return -1;
    }
    LOG_INFO("Server configuration loaded");

    // 初始化数据库工厂（需要配置）
    if (!DatabaseFactory::getInstance().initialize(*configManager)) {
        LOG_ERROR("Failed to initialize database factory");
        return -1;
    }
    LOG_INFO("Database factory initialized");
    
    // 加载游戏配置（需要数据库连接）
    if (!configManager->loadGameConfig()) {
        LOG_ERROR("Failed to load game configs from Redis");
        return -1;
    }
    LOG_INFO("Game configs loaded from Redis");

    // 初始化应用上下文（传入配置管理器）
    if (!AppContext::getInstance().initialize(configManager)) {
        LOG_ERROR("Failed to initialize application context");
        return -1;
    }
    LOG_INFO("Application context initialized");
    
    // 注册所有消息处理器
    if (!AppContext::getInstance().registerAllHandlers()) {
        LOG_ERROR("Failed to register message handlers");
        return -1;
    }
    
    // 启动所有游戏服务
    if (!AppContext::getInstance().startAllGames()) {
        LOG_ERROR("Failed to start game services");
        return -1;
    }
    LOG_INFO("All game services started successfully");
    
    // 启动TCP服务器
    if (!AppContext::getInstance().getTcpServer()->start()) {
        LOG_ERROR("Failed to start TCP server");
        return -1;
    }
    LOG_INFO("TCP server started successfully");
    
    // 启动HTTP服务器
    if (!AppContext::getInstance().getHttpServer()->start()) {
        LOG_ERROR("Failed to start HTTP server");
        return -1;
    }
    LOG_INFO("HTTP server started successfully");
    
    // 在Consul中注册服务
    if (!AppContext::getInstance().getConsulClient()->registerService()) {
        LOG_ERROR("Failed to register service with Consul");
        return -1;
    }
    LOG_INFO("Service registered with Consul");

    // 设置信号处理
    setup_signal_handlers();
    
    // 启动健康检查定时器
    AppContext::getInstance().getConsulClient()->startHealthCheck();
    LOG_INFO("Health check timer started");

    // 运行事件循环
    LOG_INFO("MINES PRO game server started successfully, entering event loop...");
    AppContext::getInstance().getEventLoop()->run();
    
    LOG_INFO("Event loop exited, performing final cleanup");
    
    return 0;
}
