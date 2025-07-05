#include "http_server.h"
#include <third_party/libuv_cpp/include/LogWriter.hpp>
#include "../common/app_context.h"
#include <third_party/nlohmann/json.hpp>
#include <chrono>
#include <thread>
#include <ctime>

using json = nlohmann::json;

HttpServer::HttpServer(uv::EventLoop* loop) 
    : loop_(loop), port_(8080) {
    
    // 根据官方示例，设置缓冲区模式 - 使用CycleBuffer (官方示例用的是CycleBuffer)
    uv::http::HttpServer::SetBufferMode(uv::GlobalConfig::BufferMode::CycleBuffer);
    
    LOG_DEBUG("HttpServer initialized with CycleBuffer mode");
}

HttpServer::~HttpServer() {
    stop();
}

bool HttpServer::initialize(const ConfigManager& configManager) {
    try {
        port_ = configManager.getServerConfig()["server"]["http_port"].get<unsigned short>();
        
        LOG_DEBUG("HTTP server initialized - Port: %d", port_);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Error initializing HTTP server: %s", e.what());
        return false;
    }
}

bool HttpServer::start() {
    try {
        server_ = std::make_unique<uv::http::HttpServer>(loop_);
        
        // reload命令
        server_->Post("/reload", std::bind(&HttpServer::handleReloadConfig, this, 
                                          std::placeholders::_1, std::placeholders::_2));
        
        //添加一个默认路由用于测试
        server_->Get("/*", [this](uv::http::Request& req, uv::http::Response* resp) {
            resp->setVersion(uv::http::HttpVersion::Http1_1);
            resp->setStatus(uv::http::Response::StatusCode::OK, "OK");
            resp->appendHead("Server", "game-server");
            std::string str("Game server is running. Use POST /reload for config reload.");
            resp->swapContent(str);
        });
        
        uv::SocketAddr addr("0.0.0.0", port_);
        int result = server_->bindAndListen(addr);
        
        if (result != 0) {
            LOG_ERROR("Failed to bind and listen on HTTP port %d, error: %s", port_, uv::EventLoop::GetErrorMessage(result));
            return false;
        }
        
        LOG_INFO("HTTP server started successfully on port %d", port_);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Exception starting HTTP server: %s", e.what());
        return false;
    }
}

void HttpServer::stop() {
    LOG_INFO("Stopping HTTP server...");
    
    if (server_) {
        try {
            server_->close([](){});
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        } catch (const std::exception& e) {
            LOG_WARN("Exception while closing HTTP server: %s", e.what());
        }
        
        server_.reset();
    }
    
    LOG_INFO("HTTP server stopped");
}

void HttpServer::handleReloadConfig(uv::http::Request& req, uv::http::Response* resp) {
    LOG_INFO("Received reload config request");
    
    try {
        resp->setVersion(uv::http::HttpVersion::Http1_1);
        resp->appendHead("Server", "game-server");
        resp->appendHead("Content-Type", "application/json");
        
        const std::string& content = req.getContent();
        if (content.empty()) {
            resp->setStatus(uv::http::Response::StatusCode::BadRequest, "Bad Request");
            std::string errorMsg = R"({"success":false,"message":"Request body is empty"})";
            resp->swapContent(errorMsg);
            return;
        }
        
        json requestJson;
        try {
            requestJson = json::parse(content);
        } catch (const json::parse_error& e) {
            resp->setStatus(uv::http::Response::StatusCode::BadRequest, "Bad Request");
            std::string errorMsg = R"({"success":false,"message":"Invalid JSON format"})";
            resp->swapContent(errorMsg);
            return;
        }
        
        if (!requestJson.contains("game_type") || !requestJson["game_type"].is_string()) {
            resp->setStatus(uv::http::Response::StatusCode::BadRequest, "Bad Request");
            std::string errorMsg = R"({"success":false,"message":"Missing or invalid 'game_type' parameter"})";
            resp->swapContent(errorMsg);
            return;
        }
        
        std::string gameType = requestJson["game_type"].get<std::string>();
        LOG_INFO("Reloading config for game type: %s", gameType.c_str());
        
        auto& appContext = AppContext::getInstance();
        bool success = appContext.reloadGameConfig(gameType);
        
        if (success) {
            resp->setStatus(uv::http::Response::StatusCode::OK, "OK");
            json responseJson = {
                {"success", true},
                {"message", "Game config reloaded successfully for " + gameType},
                {"timestamp", std::time(nullptr)}
            };
            std::string responseBody = responseJson.dump();
            resp->swapContent(responseBody);
            LOG_INFO("Successfully reloaded config for game type: %s", gameType.c_str());
        } else {
            resp->setStatus(uv::http::Response::StatusCode::InternalServerError, "Internal Server Error");
            json responseJson = {
                {"success", false},
                {"message", "Failed to reload game config for " + gameType},
                {"timestamp", std::time(nullptr)}
            };
            std::string responseBody = responseJson.dump();
            resp->swapContent(responseBody);
            LOG_ERROR("Failed to reload config for game type: %s", gameType.c_str());
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("Exception handling reload config request: %s", e.what());
        resp->setStatus(uv::http::Response::StatusCode::InternalServerError, "Internal Server Error");
        std::string errorMsg = R"({"success":false,"message":"Internal server error"})";
        resp->swapContent(errorMsg);
    }
}