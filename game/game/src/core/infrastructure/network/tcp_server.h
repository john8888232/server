#ifndef TCP_SERVER_H
#define TCP_SERVER_H

#include <memory>
#include <string>
#include <functional>
#include <unordered_map>
#include <mutex>
#include <set>
#include "third_party/libuv_cpp/include/uv11.hpp"
#include "../common/config_manager.h"
#include "../common/session_thread_pool.h"
#include "../common/utils.h"
#include "../../interfaces/message_router.h"
#include "connection_manager.h"
#include "protocol.h"
#include "core/domain/models/user.h"

// 数据包校验结果
enum class PacketValidationResult {
    Valid,
    InvalidSize,
    InvalidLength,
    Corrupted
};

class TcpServer {
public:
    using OnConnectionCallback = std::function<void(const std::string& gatewayId)>;
    using OnDisconnectionCallback = std::function<void(const std::string& gatewayId)>;
    using OnMessageCallback = std::function<void(const std::string& playerSessionId, uint32_t msgId, const std::string& data)>;

    TcpServer(uv::EventLoop* loop);
    ~TcpServer();

    // 初始化服务器配置
    bool initialize(const ConfigManager& configManager);
    
    // 启动服务器
    bool start();
    
    // 停止服务器
    void stop();
    
    // 发送消息给指定玩家会话
    bool sendToPlayer(const std::string& playerSessionId, uint32_t msgId, const std::string& data);
    
    // 发送系统消息给指定Gateway
    bool sendToGateway(const std::string& gatewayId, uint32_t msgId, const std::string& data);
    
    // 广播消息给所有连接
    void broadcastMessage(uint32_t msgId, const std::string& data);
    
    // 注册玩家会话
    bool registerPlayerSession(const std::string& playerSessionId, const std::string& gatewayId, const std::string& loginname);
    
    // 注销玩家会话
    bool unregisterPlayerSession(const std::string& playerSessionId);
    
    // 获取连接管理器
    ConnectionManager* getConnectionManager() { return &connectionManager_; }
    
    // 设置回调函数
    void setOnConnectionCallback(OnConnectionCallback callback);
    void setOnDisconnectionCallback(OnDisconnectionCallback callback);
    void setOnMessageCallback(OnMessageCallback callback);
    
    // 设置消息路由器 - 使用shared_ptr替代裸指针
    void setMessageRouter(std::shared_ptr<MessageRouter> router);

private:
    // 内部方法 - 连接管理
    void onNewConnection(std::weak_ptr<uv::TcpConnection> connection);
    void onConnectionClose(const std::string& gatewayId);
    void onMessage(std::shared_ptr<uv::TcpConnection> connection, const char* buf, ssize_t size);
    
    // 基础数据包校验（在IO线程中执行，必须非常快）
    PacketValidationResult validatePacketBasic(uv::Packet& packet);
    
    // 记录校验失败信息
    void logValidationFailure(PacketValidationResult result, uv::Packet& packet);
    
    // 将已验证的包投入按sessionID分配的线程池处理
    void enqueuePacket(std::shared_ptr<uv::TcpConnection> connection, 
                              uv::Packet packet, const std::string& gatewayId);
    
    // 在线程池中处理已验证的数据包
    void processPacket(std::shared_ptr<uv::TcpConnection> connection, 
                                     uv::Packet packet, const std::string& gatewayId);
    
    // 内部发送消息方法
    bool sendMessageToConnection(std::weak_ptr<uv::TcpConnection> connection, 
                                uint32_t msgId, const std::string& data, 
                                const std::string& sessionId);
    
    // 同步发送响应（在当前线程中）
    void sendResponse(std::shared_ptr<uv::TcpConnection> connection, 
                     std::shared_ptr<uv::Packet> responsePacket, const std::string& type);
    
    // 异步发送响应（确保在事件循环线程中执行）
    void sendResponseAsync(std::shared_ptr<uv::TcpConnection> connection, 
                           std::shared_ptr<uv::Packet> responsePacket, 
                           const std::string& type);

private:
    uv::EventLoop* loop_;
    std::unique_ptr<uv::TcpServer> server_;
    std::shared_ptr<MessageRouter> messageRouter_;
    ConnectionManager connectionManager_;
    std::unique_ptr<SessionThreadPool> sessionThreadPool_;
    
    // 配置
    unsigned short port_;
    size_t threadPoolSize_;
    size_t maxQueueSizePerWorker_;  // 每个工作线程的最大队列大小
    
    // 回调函数
    OnConnectionCallback onConnectionCallback_;
    OnDisconnectionCallback onDisconnectionCallback_;
    OnMessageCallback onMessageCallback_;
};

#endif // TCP_SERVER_H
