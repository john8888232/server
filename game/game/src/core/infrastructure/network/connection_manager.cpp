#include "connection_manager.h"
#include "core/infrastructure/common/error_code.h"
#include <random>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <third_party/libuv_cpp/include/LogWriter.hpp>
#include <third_party/libuv_cpp/include/Packet.hpp>
#include <third_party/libuv_cpp/include/Timer.hpp>
#include "protocol.h"
#include "core/infrastructure/proto/game.pb.h"

ConnectionManager::ConnectionManager(uv::EventLoop* loop) 
    : loop_(loop), sessionCleanupTimer_(nullptr) {
    LOG_DEBUG("ConnectionManager initialized");
}

ConnectionManager::~ConnectionManager() {
    stopSessionCleanupTimer();
    LOG_INFO("ConnectionManager destroyed");
}

void ConnectionManager::startSessionCleanupTimer(int intervalSeconds) {
    if (sessionCleanupTimer_ != nullptr) {
        LOG_WARN("Session cleanup timer already started");
        return;
    }
    
    sessionCleanupTimer_ = new uv::Timer(loop_, 1 * 1000, 1 * 1000,
                                        [this,intervalSeconds](uv::Timer* timer) {
                                            this->cleanupInactiveSessions(intervalSeconds);
                                        });
    
    sessionCleanupTimer_->start();
    LOG_INFO("Session cleanup timer started with interval %d seconds", intervalSeconds);
}

void ConnectionManager::stopSessionCleanupTimer() {
    if (sessionCleanupTimer_) {
        sessionCleanupTimer_->close([](uv::Timer* timer) {
            delete timer;
        });
        sessionCleanupTimer_ = nullptr;
        LOG_INFO("Session cleanup timer stopped");
    }
}

void ConnectionManager::setSendMessageCallback(SendMessageCallback callback) {
    sendMessageCallback_ = callback;
}

std::string ConnectionManager::registerGateway(std::weak_ptr<uv::TcpConnection> connection, const std::string& gatewayName) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto conn = connection.lock();
    if (!conn) {
        LOG_ERROR("Invalid connection when registering gateway");
        return "";
    }
    
    // 直接使用连接地址作为gatewayId
    std::string gatewayId = conn->Name();
    std::string name = gatewayName.empty() ? ("Gateway_" + gatewayId) : gatewayName;
    
    auto gateway = std::make_shared<GatewayConnection>(gatewayId, name, connection);
    gateways_[gatewayId] = gateway;
    
    LOG_INFO("Gateway registered: %s (name: %s)", gatewayId.c_str(), name.c_str());
    return gatewayId;
}

bool ConnectionManager::unregisterGateway(const std::string& gatewayId) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = gateways_.find(gatewayId);
    if (it == gateways_.end()) {
        LOG_WARN("Gateway %s not found for unregistration", gatewayId.c_str());
        return false;
    }
    
    // 清理该Gateway下的所有玩家会话
    auto sessIt = gatewayPlayerSessions_.find(gatewayId);
    if (sessIt != gatewayPlayerSessions_.end()) {
        for (const auto& sessionId : sessIt->second) {
            auto playerSession = playerSessions_[sessionId];
            if (playerSession && !playerSession->getLoginname().empty()) {
                loginnameToSession_.erase(playerSession->getLoginname());
            }
            playerSessions_.erase(sessionId);
            LOG_INFO("Removed player session %s due to gateway disconnection", sessionId.c_str());
        }
        gatewayPlayerSessions_.erase(sessIt);
    }
    
    gateways_.erase(it);
    LOG_INFO("Gateway unregistered: %s", gatewayId.c_str());
    return true;
}

std::shared_ptr<GatewayConnection> ConnectionManager::getGateway(const std::string& gatewayId) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = gateways_.find(gatewayId);
    if (it != gateways_.end()) {
        return it->second;
    }
    return nullptr;
}

bool ConnectionManager::registerPlayerSession(const std::string& playerSessionId, const std::string& gatewayId, const std::string& loginname) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 验证SessionID格式
    if (!utils::isValidSessionId(playerSessionId)) {
        LOG_ERROR("Invalid session ID format: %s (must be 32 bytes)", playerSessionId.c_str());
        return false;
    }
    
    // 检查Gateway是否存在
    if (gateways_.find(gatewayId) == gateways_.end()) {
        LOG_ERROR("Cannot register player session %s: Gateway %s not found", 
                 playerSessionId.c_str(), gatewayId.c_str());
        return false;
    }
    
    // 检查会话是否已存在
    auto existingSessionIt = playerSessions_.find(playerSessionId);
    if (existingSessionIt != playerSessions_.end()) {
        LOG_WARN("Player session %s already exists, updating gateway mapping", playerSessionId.c_str());
        
        // 如果有旧的loginname映射，先移除
        if (!existingSessionIt->second->getLoginname().empty()) {
            loginnameToSession_.erase(existingSessionIt->second->getLoginname());
        }
    }
    
    auto session = std::make_shared<PlayerSession>(playerSessionId, gatewayId, loginname);
    playerSessions_[playerSessionId] = session;
    
    // 如果提供了loginname，建立loginname到session的映射
    if (!loginname.empty()) {
        loginnameToSession_[loginname] = session;
        LOG_INFO("Mapped loginname %s to session %s", loginname.c_str(), playerSessionId.c_str());
    }
    
    // 更新反向映射
    gatewayPlayerSessions_[gatewayId].push_back(playerSessionId);
    
    LOG_INFO("Player session registered: %s -> Gateway: %s, loginname: %s", 
             playerSessionId.c_str(), gatewayId.c_str(), loginname.c_str());
    return true;
}

bool ConnectionManager::unregisterPlayerSession(const std::string& playerSessionId) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = playerSessions_.find(playerSessionId);
    if (it == playerSessions_.end()) {
        LOG_WARN("Player session %s not found for unregistration", playerSessionId.c_str());
        return false;
    }
    
    std::string gatewayId = it->second->getGatewayId();
    
    // 如果有loginname映射，移除它
    if (!it->second->getLoginname().empty()) {
        loginnameToSession_.erase(it->second->getLoginname());
    }
    
    playerSessions_.erase(it);
    
    // 从反向映射中移除
    auto gwIt = gatewayPlayerSessions_.find(gatewayId);
    if (gwIt != gatewayPlayerSessions_.end()) {
        auto& sessions = gwIt->second;
        sessions.erase(std::remove(sessions.begin(), sessions.end(), playerSessionId), sessions.end());
        
        if (sessions.empty()) {
            gatewayPlayerSessions_.erase(gwIt);
        }
    }
    
    LOG_INFO("Player session unregistered: %s", playerSessionId.c_str());
    return true;
}

std::shared_ptr<PlayerSession> ConnectionManager::getPlayerSession(const std::string& playerSessionId) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = playerSessions_.find(playerSessionId);
    if (it != playerSessions_.end()) {
        return it->second;
    }
    return nullptr;
}

std::shared_ptr<PlayerSession> ConnectionManager::getPlayerSessionByLoginname(const std::string& loginname) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = loginnameToSession_.find(loginname);
    if (it != loginnameToSession_.end()) {
        return it->second;
    }
    return nullptr;
}

void ConnectionManager::updatePlayerSessionActiveTime(const std::string& playerSessionId) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = playerSessions_.find(playerSessionId);
    if (it != playerSessions_.end()) {
        it->second->updateActiveTime();
        LOG_DEBUG("Updated active time for session %s", playerSessionId.c_str());
    }
}

bool ConnectionManager::sendToPlayer(const std::string& playerSessionId, uint32_t msgId, const std::string& data) {
    if (!sendMessageCallback_) {
        LOG_ERROR("Send message callback not set");
        return false;
    }
    
    // 获取玩家会话信息
    auto session = getPlayerSession(playerSessionId);
    if (!session) {
        LOG_ERROR("Player session %s not found", playerSessionId.c_str());
        return false;
    }
    
    // 获取Gateway连接
    auto gateway = getGateway(session->getGatewayId());
    if (!gateway) {
        LOG_ERROR("Gateway %s not found for player session %s", 
                 session->getGatewayId().c_str(), playerSessionId.c_str());
        return false;
    }
    
    // 通过回调发送消息到Gateway，Gateway会转发给玩家
    return sendMessageCallback_(gateway->connection, msgId, data, playerSessionId);
}

bool ConnectionManager::sendToGateway(const std::string& gatewayId, uint32_t msgId, const std::string& data) {
    if (!sendMessageCallback_) {
        LOG_ERROR("Send message callback not set");
        return false;
    }
    
    auto gateway = getGateway(gatewayId);
    if (!gateway) {
        LOG_ERROR("Gateway %s not found", gatewayId.c_str());
        return false;
    }
    
    return sendMessageCallback_(gateway->connection, msgId, data, "");
}

std::vector<std::string> ConnectionManager::getPlayerSessionsByGateway(const std::string& gatewayId) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = gatewayPlayerSessions_.find(gatewayId);
    if (it != gatewayPlayerSessions_.end()) {
        return it->second;
    }
    return {};
}

std::vector<std::string> ConnectionManager::getAllGateways() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<std::string> gatewayIds;
    for (const auto& pair : gateways_) {
        gatewayIds.push_back(pair.first);
    }
    return gatewayIds;
}

size_t ConnectionManager::getGatewayCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return gateways_.size();
}

size_t ConnectionManager::getPlayerSessionCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return playerSessions_.size();
}

void ConnectionManager::cleanupInactiveSessions(int timeoutSeconds) {
    std::vector<std::string> sessionsToKick;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        
        for (const auto& pair : playerSessions_) {
            if (pair.second->isExpired(timeoutSeconds)) {
                sessionsToKick.push_back(pair.first);
            }
        }
    }
    
    if (sessionsToKick.empty()) {
        return;
    }
    
    LOG_DEBUG("Found %d inactive sessions to clean up", sessionsToKick.size());
    
    for (const auto& sessionId : sessionsToKick) {
        auto session = getPlayerSession(sessionId);
        if (!session) {
            continue; 
        }
        if (!session->getLoginname().empty()) {
            LOG_INFO("Sending kick notification for inactive session %s (loginname: %s)", 
                     sessionId.c_str(), session->getLoginname().c_str());
            proto::KickPlayerNotify notify;
            notify.set_sessionid(sessionId);
            notify.set_reason(ErrorCode::INACTIVE); // 长时间未活动
            sendToPlayer(sessionId, Protocol::SC_KICK_PLAYER_NOTIFY, notify.SerializeAsString());
        }
        LOG_INFO("Cleaning up inactive session: %s", sessionId.c_str());
        unregisterPlayerSession(sessionId);
    }
    
    LOG_INFO("Cleaned up %d inactive sessions", sessionsToKick.size());
}

std::string ConnectionManager::getGatewayIdBySessionId(const std::string& sessionId) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = playerSessions_.find(sessionId);
    if (it != playerSessions_.end()) {
        return it->second->getGatewayId();
    }
    
    return "";
}

bool ConnectionManager::createBasicPlayerSession(const std::string& playerSessionId, const std::string& gatewayId) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 检查会话是否已存在
    if (playerSessions_.find(playerSessionId) != playerSessions_.end()) {
        LOG_WARN("Player session %s already exists", playerSessionId.c_str());
        return false;
    }
    
    // 检查网关是否存在
    if (gateways_.find(gatewayId) == gateways_.end()) {
        LOG_ERROR("Gateway %s not found", gatewayId.c_str());
        return false;
    }
    
    // 创建基础的玩家会话（只有sessionId和gatewayId，其他信息后续补全）
    auto session = std::make_shared<PlayerSession>(playerSessionId, gatewayId, "");
    playerSessions_[playerSessionId] = session;
    
    // 更新反向映射
    gatewayPlayerSessions_[gatewayId].push_back(playerSessionId);
    
    LOG_INFO("Basic player session created: %s -> Gateway: %s", 
             playerSessionId.c_str(), gatewayId.c_str());
    return true;
}

bool ConnectionManager::completePlayerSession(const std::string& playerSessionId, const std::string& loginname, std::shared_ptr<User> user) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = playerSessions_.find(playerSessionId);
    if (it == playerSessions_.end()) {
        LOG_ERROR("Player session %s not found for completion", playerSessionId.c_str());
        return false;
    }
    
    auto session = it->second;
    
    // 检查是否已有相同登录名的会话（需要踢出旧会话）
    std::shared_ptr<PlayerSession> existingSession = nullptr;
    if (!loginname.empty()) {
        auto existingIt = loginnameToSession_.find(loginname);
        if (existingIt != loginnameToSession_.end() && existingIt->second->getSessionId() != playerSessionId) {
            existingSession = existingIt->second;
            LOG_INFO("Player %s already has an active session %s, will kick out", 
                     loginname.c_str(), existingSession->getSessionId().c_str());
        }
    }
    
    // 如果有旧会话，先处理踢出
    if (existingSession) {
        std::string oldSessionId = existingSession->getSessionId();
        std::string oldGatewayId = existingSession->getGatewayId();
        
        // 构建踢人消息
        proto::KickPlayerNotify notify;
        notify.set_sessionid(oldSessionId);
        notify.set_reason(ErrorCode::OTHER_LOGIN); // 在其他地方登录
        
        // 发送踢人通知
        std::string responseData;
        if (notify.SerializeToString(&responseData)) {
            // 临时释放锁来发送消息（避免死锁）
            mutex_.unlock();
            sendToPlayer(oldSessionId, Protocol::SC_KICK_PLAYER_NOTIFY, responseData);
            mutex_.lock();
            LOG_INFO("Sent kick notification to old session %s", oldSessionId.c_str());
        }
        
        // 注销旧会话
        auto oldIt = playerSessions_.find(oldSessionId);
        if (oldIt != playerSessions_.end()) {
            // 如果有loginname映射，移除它
            if (!oldIt->second->getLoginname().empty()) {
                loginnameToSession_.erase(oldIt->second->getLoginname());
            }
            
            playerSessions_.erase(oldIt);
            
            // 从反向映射中移除
            auto gwIt = gatewayPlayerSessions_.find(oldGatewayId);
            if (gwIt != gatewayPlayerSessions_.end()) {
                auto& sessions = gwIt->second;
                sessions.erase(std::remove(sessions.begin(), sessions.end(), oldSessionId), sessions.end());
                
                if (sessions.empty()) {
                    gatewayPlayerSessions_.erase(gwIt);
                }
            }
            
            LOG_INFO("Old player session unregistered: %s", oldSessionId.c_str());
        }
    }
    
    // 补全会话信息
    session->setLoginname(loginname);
    session->setPlayer(user);
    
    // 建立loginname到session的映射
    if (!loginname.empty()) {
        loginnameToSession_[loginname] = session;
        LOG_INFO("Mapped loginname %s to session %s", loginname.c_str(), playerSessionId.c_str());
    }
    
    LOG_INFO("Player session completed: %s -> loginname: %s", 
             playerSessionId.c_str(), loginname.c_str());
    return true;
}




