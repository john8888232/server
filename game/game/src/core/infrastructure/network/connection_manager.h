#ifndef CONNECTION_MANAGER_H
#define CONNECTION_MANAGER_H

#include <string>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <functional>
#include "third_party/libuv_cpp/include/uv11.hpp"
#include "../common/utils.h"
#include "core/domain/models/user.h"
#include "core/domain/models/player_session.h"

// Gateway连接信息
struct GatewayConnection {
    std::string gatewayId;           // Gateway连接ID (使用连接地址作为ID，如 "172.31.28.14:52504")
    std::string gatewayName;         // Gateway名称/标识
    std::weak_ptr<uv::TcpConnection> connection;  // TCP连接
    std::chrono::system_clock::time_point connectTime;
    
    GatewayConnection(const std::string& id, const std::string& name, 
                     std::weak_ptr<uv::TcpConnection> conn)
        : gatewayId(id), gatewayName(name), connection(conn),
          connectTime(std::chrono::system_clock::now()) {}
};

// 消息发送回调函数类型
using SendMessageCallback = std::function<bool(std::weak_ptr<uv::TcpConnection>, uint32_t, const std::string&, const std::string&)>;

class ConnectionManager {
public:
    ConnectionManager(uv::EventLoop* loop);
    ~ConnectionManager();
    
    // 设置消息发送回调（由TcpServer注入）
    void setSendMessageCallback(SendMessageCallback callback);
    
    // Gateway连接管理
    std::string registerGateway(std::weak_ptr<uv::TcpConnection> connection, const std::string& gatewayName = "");
    bool unregisterGateway(const std::string& gatewayId);  // gatewayId就是连接地址
    std::shared_ptr<GatewayConnection> getGateway(const std::string& gatewayId);
    
    // 玩家会话管理
    bool createBasicPlayerSession(const std::string& playerSessionId, const std::string& gatewayId);
    bool completePlayerSession(const std::string& playerSessionId, const std::string& loginname, std::shared_ptr<User> user);
    bool registerPlayerSession(const std::string& playerSessionId, const std::string& gatewayId, const std::string& loginname = "");
    bool unregisterPlayerSession(const std::string& playerSessionId);
    std::shared_ptr<PlayerSession> getPlayerSession(const std::string& playerSessionId);
    std::shared_ptr<PlayerSession> getPlayerSessionByLoginname(const std::string& loginname);
    std::string getGatewayIdBySessionId(const std::string& sessionId);
    
    // 更新玩家会话活跃时间
    void updatePlayerSessionActiveTime(const std::string& playerSessionId);
    
    bool sendToPlayer(const std::string& playerSessionId, uint32_t msgId, const std::string& data);
    bool sendToGateway(const std::string& gatewayId, uint32_t msgId, const std::string& data);
    
    // 查询功能
    std::vector<std::string> getPlayerSessionsByGateway(const std::string& gatewayId);
    std::vector<std::string> getAllGateways();
    size_t getGatewayCount() const;
    size_t getPlayerSessionCount() const;
    
    // 清理功能
    void cleanupInactiveSessions(int timeoutSeconds); 
    
    // 启动和停止会话清理定时器
    void startSessionCleanupTimer(int intervalSeconds);
    void stopSessionCleanupTimer();
    
private:
    // Gateway连接映射: gatewayId(连接地址) -> GatewayConnection
    std::unordered_map<std::string, std::shared_ptr<GatewayConnection>> gateways_;
    
    // 玩家会话映射: playerSessionId -> PlayerSession
    std::unordered_map<std::string, std::shared_ptr<PlayerSession>> playerSessions_;
    
    // 登录名到会话的映射: loginname -> PlayerSession
    std::unordered_map<std::string, std::shared_ptr<PlayerSession>> loginnameToSession_;
    
    // 反向映射: gatewayId -> playerSessionIds
    std::unordered_map<std::string, std::vector<std::string>> gatewayPlayerSessions_;
    
    // 使用单一互斥量保护所有数据
    mutable std::mutex mutex_;
    
    // 消息发送回调（由TcpServer注入）
    SendMessageCallback sendMessageCallback_;
    
    // 事件循环引用
    uv::EventLoop* loop_;
    
    // 会话清理定时器
    uv::Timer* sessionCleanupTimer_;
};

#endif // CONNECTION_MANAGER_H
