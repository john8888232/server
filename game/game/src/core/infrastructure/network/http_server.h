#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include <memory>
#include <string>
#include <functional>
#include "third_party/libuv_cpp/include/uv11.hpp"
#include "../common/config_manager.h"

class AppContext;

class HttpServer {
public:
    HttpServer(uv::EventLoop* loop);
    ~HttpServer();

    // 初始化服务器配置
    bool initialize(const ConfigManager& configManager);
    
    // 启动服务器
    bool start();
    
    // 停止服务器
    void stop();

private:
    // 处理重新加载游戏配置的请求
    void handleReloadConfig(uv::http::Request& req, uv::http::Response* resp);

    uv::EventLoop* loop_;
    std::unique_ptr<uv::http::HttpServer> server_;
    unsigned short port_;
};

#endif // HTTP_SERVER_H