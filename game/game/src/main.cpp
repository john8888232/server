// src/main.cpp
#include <iostream>
#include <signal.h>
#include <execinfo.h>
#include <unistd.h>
#include "core/infrastructure/common/config_manager.h"
#include "core/infrastructure/persistence/database_factory.h"
#include "core/infrastructure/common/app_context.h"
#include "core/infrastructure/common/Logger_helper.h"
#include "core/infrastructure/common/dependency_container.h"
#include "core/infrastructure/common/memory_monitor.h"

extern DependencyContainer& getDependencyContainer();

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
        auto& container = getDependencyContainer();
        auto appContext = container.resolve<AppContext>();
        if (appContext) {
            appContext->cleanup();
        }
        
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
    static volatile sig_atomic_t sigint_count = 0;
    static volatile sig_atomic_t in_crash_handler = 0;
    
    switch (signum) {
        case SIGINT:
            sigint_count++;
            if (sigint_count == 1) {
                // 使用async-signal-safe的方式记录
                const char msg[] = "Received SIGINT, initiating graceful shutdown\n";
                write(STDERR_FILENO, msg, sizeof(msg) - 1);
                graceful_shutdown();
            } else {
                // 第二次收到SIGINT，强制退出
                const char msg[] = "Received SIGINT again, forcing immediate exit\n";
                write(STDERR_FILENO, msg, sizeof(msg) - 1);
                std::_Exit(1);
            }
            break;
        case SIGTERM:
            {
                const char msg[] = "Received SIGTERM, initiating graceful shutdown\n";
                write(STDERR_FILENO, msg, sizeof(msg) - 1);
                graceful_shutdown();
            }
            break;
        case SIGSEGV:
        case SIGABRT:
        case SIGFPE:
        case SIGILL:
            {
                // 防止信号处理器递归调用
                if (in_crash_handler) {
                    const char msg[] = "Recursive crash detected, generating core dump immediately\n";
                    write(STDERR_FILENO, msg, sizeof(msg) - 1);
                    // 直接生成核心转储，不使用_Exit
                    signal(signum, SIG_DFL);
                    raise(signum);
                    return;
                }
                in_crash_handler = 1;
                
                // 使用安全的崩溃日志记录
                SafeSignalLogger::logCrash(signum, STDERR_FILENO);
                
                // 如果是内存相关错误，记录内存损坏信息
                if (signum == SIGABRT) {
                    SafeSignalLogger::logMemoryCorruption(STDERR_FILENO);
                }
                
                // 强制刷新所有输出
                fsync(STDERR_FILENO);
                fsync(STDOUT_FILENO);
                
                // 重置信号处理器为默认并生成核心转储
                signal(signum, SIG_DFL);
                raise(signum);
            }
            break;
        default:
            {
                const char msg[] = "Received unknown signal: ";
                write(STDERR_FILENO, msg, sizeof(msg) - 1);
                SafeSignalLogger::writeNumber(STDERR_FILENO, signum);
                write(STDERR_FILENO, "\n", 1);
            }
            break;
    }
}

// 设置信号处理器
void setup_signal_handlers() {
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESETHAND; // 信号处理后自动重置为默认处理器
    // 注册终止信号
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    // 注册崩溃信号
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
    // 忽略SIGPIPE
    signal(SIGPIPE, SIG_IGN);
    
    LOG_INFO("Signal handlers installed");
}

int main(void) {
    // 初始化日志
    if (!LoggerHelper::Initialize()) {
        std::cerr << "Failed to initialize logger" << std::endl;
        return -1;
    }
    
    LOG_INFO("Game server starting up...");
    
    // 初始化内存监控
    auto& memoryMonitor = MemoryMonitor::getInstance();
    memoryMonitor.setEnabled(true);
    LOG_INFO("Memory monitor initialized");
    
    // 获取依赖容器
    auto& container = getDependencyContainer();
    
    // 创建配置管理器
    auto configManager = container.create<ConfigManager>();
    
    // 加载服务配置文件
    if (!configManager->loadServerConfig()) {
        LOG_ERROR("Failed to load configuration");
        return -1;
    }
    LOG_INFO("Server configuration loaded");

    // 创建并初始化数据库工厂
    auto dbFactory = container.create<DatabaseFactory>();
    
    if (!dbFactory->initialize(*configManager)) {
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

    // 创建应用上下文
    auto appContext = container.create<AppContext>();
    
    // 初始化应用上下文
    if (!appContext->initialize()) {
        LOG_ERROR("Failed to initialize application context");
        return -1;
    }
    LOG_INFO("Application context initialized");

    // 启动所有游戏服务
    if (!appContext->startAllGames()) {
        LOG_ERROR("Failed to start game services");
        return -1;
    }
    LOG_INFO("All game services started successfully");
    
    // 启动TCP服务器
    if (!appContext->getTcpServer()->start()) {
        LOG_ERROR("Failed to start TCP server");
        return -1;
    }
    LOG_INFO("TCP server started successfully");
    
    // 启动HTTP服务器
    if (!appContext->getHttpServer()->start()) {
        LOG_ERROR("Failed to start HTTP server");
        return -1;
    }
    LOG_INFO("HTTP server started successfully");
    
    // 在Consul中注册服务
    if (!appContext->getConsulClient()->registerService()) {
        LOG_ERROR("Failed to register service with Consul");
        return -1;
    }
    LOG_INFO("Service registered with Consul");
    
    // 启动Consul健康检查
    if (!appContext->getConsulClient()->startHealthCheck()) {
        LOG_ERROR("Failed to start Consul health check");
        return -1;
    }
    LOG_INFO("Consul health check started");

    // 处理退出信号
    setup_signal_handlers();
    
    // 主事件循环
    LOG_INFO("Running main event loop...");
    appContext->getEventLoop()->run();
    
    // 如果事件循环退出，执行清理
    LOG_INFO("Main event loop exited");
    graceful_shutdown();
    
    // 检查内存泄漏
    LOG_INFO("Checking for memory leaks...");
    memoryMonitor.dumpMemoryLeaks();
    
    LOG_INFO("Server shutdown complete");
    return 0;
}
