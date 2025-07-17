#include "connection_manager.h"
#include "core/infrastructure/common/error_code.h"
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
    std::unique_lock<std::shared_mutex> lock(configMutex_);
    
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
    std::unique_lock<std::shared_mutex> lock(configMutex_);
    
    if (sessionCleanupTimer_) {
        sessionCleanupTimer_->close([](uv::Timer* timer) {
            delete timer;
        });
        sessionCleanupTimer_ = nullptr;
        LOG_INFO("Session cleanup timer stopped");
    }
}

void ConnectionManager::setSendMessageCallback(SendMessageCallback callback) {
    std::unique_lock<std::shared_mutex> lock(configMutex_);
    sendMessageCallback_ = callback;
}

std::string ConnectionManager::registerGateway(std::weak_ptr<uv::TcpConnection> connection, const std::string& gatewayName) {
    auto conn = connection.lock();
    if (!conn) {
        LOG_ERROR("Invalid connection when registering gateway");
        return "";
    }
    
    // 直接使用连接地址作为gatewayId
    std::string gatewayId = conn->Name();
    std::string name = gatewayName.empty() ? ("Gateway_" + gatewayId) : gatewayName;
    
    // 获取针对此gatewayId的锁
    std::unique_lock<std::shared_mutex> lock(gatewayMutex_.getMutexForKey(gatewayId));
    
    auto gateway = std::make_shared<GatewayConnection>(gatewayId, name, connection);
    gateways_[gatewayId] = gateway;
    
    LOG_INFO("Gateway registered: %s (name: %s)", gatewayId.c_str(), name.c_str());
    return gatewayId;
}

bool ConnectionManager::unregisterGateway(const std::string& gatewayId) {
    // 获取针对此gatewayId的锁
    std::unique_lock<std::shared_mutex> gatewayLock(gatewayMutex_.getMutexForKey(gatewayId));
    
    auto it = gateways_.find(gatewayId);
    if (it == gateways_.end()) {
        LOG_WARN("Gateway %s not found for unregistration", gatewayId.c_str());
        return false;
    }
    
    // 获取该gateway关联的所有会话
    std::vector<std::string> sessionsToRemove;
    {
        // 首先获取会话列表，不需要持有锁太久
        auto sessIt = gatewayPlayerSessions_.find(gatewayId);
        if (sessIt != gatewayPlayerSessions_.end()) {
            sessionsToRemove = sessIt->second;
            gatewayPlayerSessions_.erase(sessIt);
        }
    }
    
    // 逐个处理会话，每个会话使用自己的锁
    for (const auto& sessionId : sessionsToRemove) {
        // 为每个会话获取对应的锁
        std::unique_lock<std::shared_mutex> sessionLock(playerSessionMutex_.getMutexForKey(sessionId));
        
        auto sessionIt = playerSessions_.find(sessionId);
        if (sessionIt != playerSessions_.end()) {
            auto playerSession = sessionIt->second;
            
            if (playerSession && !playerSession->getLoginname().empty()) {
                // 需要移除loginname映射
                loginnameToSession_.erase(playerSession->getLoginname());
            }
            
            playerSessions_.erase(sessionId);
            LOG_INFO("Removed player session %s due to gateway disconnection", sessionId.c_str());
        }
    }
    
    // 最后移除Gateway
    gateways_.erase(it);
    
    LOG_INFO("Gateway unregistered: %s", gatewayId.c_str());
    return true;
}

std::shared_ptr<GatewayConnection> ConnectionManager::getGateway(const std::string& gatewayId) {
    // 获取针对此gatewayId的共享锁（只读）
    std::shared_lock<std::shared_mutex> lock(gatewayMutex_.getMutexForKey(gatewayId));
    
    auto it = gateways_.find(gatewayId);
    if (it != gateways_.end()) {
        return it->second;
    }
    return nullptr;
}

bool ConnectionManager::registerPlayerSession(const std::string& playerSessionId, const std::string& gatewayId, const std::string& loginname) {
    // 验证SessionID格式
    if (!utils::isValidSessionId(playerSessionId)) {
        LOG_ERROR("Invalid session ID format: %s (must be 32 bytes)", playerSessionId.c_str());
        return false;
    }
    
    // 检查Gateway是否存在 - 使用共享锁
    {
        std::shared_lock<std::shared_mutex> gatewayLock(gatewayMutex_.getMutexForKey(gatewayId));
        if (gateways_.find(gatewayId) == gateways_.end()) {
            LOG_ERROR("Cannot register player session %s: Gateway %s not found", 
                    playerSessionId.c_str(), gatewayId.c_str());
            return false;
        }
    }
    
    // 获取针对session的独占锁
    std::unique_lock<std::shared_mutex> sessionLock(playerSessionMutex_.getMutexForKey(playerSessionId));
    
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
    
    // 更新反向映射 - 需要gateway锁
    {
        std::unique_lock<std::shared_mutex> gatewayLock(gatewayMutex_.getMutexForKey(gatewayId));
        gatewayPlayerSessions_[gatewayId].push_back(playerSessionId);
    }
    
    LOG_INFO("Player session registered: %s -> Gateway: %s, loginname: %s", 
             playerSessionId.c_str(), gatewayId.c_str(), loginname.c_str());
    return true;
}

bool ConnectionManager::unregisterPlayerSession(const std::string& playerSessionId) {
    // 获取针对session的独占锁
    std::unique_lock<std::shared_mutex> sessionLock(playerSessionMutex_.getMutexForKey(playerSessionId));
    
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
    
    // 从反向映射中移除 - 需要gateway锁
    {
        std::unique_lock<std::shared_mutex> gatewayLock(gatewayMutex_.getMutexForKey(gatewayId));
        auto gwIt = gatewayPlayerSessions_.find(gatewayId);
        if (gwIt != gatewayPlayerSessions_.end()) {
            auto& sessions = gwIt->second;
            sessions.erase(std::remove(sessions.begin(), sessions.end(), playerSessionId), sessions.end());
            
            if (sessions.empty()) {
                gatewayPlayerSessions_.erase(gwIt);
            }
        }
    }
    
    LOG_INFO("Player session unregistered: %s", playerSessionId.c_str());
    return true;
}

std::shared_ptr<PlayerSession> ConnectionManager::getPlayerSession(const std::string& playerSessionId) {
    // 获取针对session的共享锁（只读）
    std::shared_lock<std::shared_mutex> lock(playerSessionMutex_.getMutexForKey(playerSessionId));
    
    auto it = playerSessions_.find(playerSessionId);
    if (it != playerSessions_.end()) {
        return it->second;
    }
    return nullptr;
}

std::shared_ptr<PlayerSession> ConnectionManager::getPlayerSessionByLoginname(const std::string& loginname) {
    // 为loginname映射创建一个简单的哈希获取锁
    // 注意：这个简单实现可能会导致loginname和sessionId的锁冲突，
    // 但对于大多数情况下，这应该是可以接受的
    std::shared_lock<std::shared_mutex> lock(playerSessionMutex_.getMutexForKey(loginname));
    
    auto it = loginnameToSession_.find(loginname);
    if (it != loginnameToSession_.end()) {
        return it->second;
    }
    return nullptr;
}

void ConnectionManager::updatePlayerSessionActiveTime(const std::string& playerSessionId) {
    // 获取针对session的独占锁
    std::unique_lock<std::shared_mutex> lock(playerSessionMutex_.getMutexForKey(playerSessionId));
    
    auto it = playerSessions_.find(playerSessionId);
    if (it != playerSessions_.end()) {
        it->second->updateActiveTime();
        LOG_DEBUG("Updated active time for session %s", playerSessionId.c_str());
    }
}

bool ConnectionManager::sendToPlayer(const std::string& playerSessionId, uint32_t msgId, const std::string& data) {
    // 首先检查回调函数
    {
        std::shared_lock<std::shared_mutex> configLock(configMutex_);
        if (!sendMessageCallback_) {
            LOG_ERROR("Send message callback not set");
            return false;
        }
    }
    
    // 获取玩家会话信息 - 只读
    std::shared_ptr<PlayerSession> session;
    std::string gatewayId;
    {
        std::shared_lock<std::shared_mutex> sessionLock(playerSessionMutex_.getMutexForKey(playerSessionId));
        auto it = playerSessions_.find(playerSessionId);
        if (it == playerSessions_.end()) {
            LOG_ERROR("Player session %s not found", playerSessionId.c_str());
            return false;
        }
        session = it->second;
        gatewayId = session->getGatewayId();
    }
    
    // 获取Gateway连接 - 只读
    std::weak_ptr<uv::TcpConnection> connection;
    {
        std::shared_lock<std::shared_mutex> gatewayLock(gatewayMutex_.getMutexForKey(gatewayId));
        auto it = gateways_.find(gatewayId);
        if (it == gateways_.end()) {
            LOG_ERROR("Gateway %s not found for player session %s", 
                    gatewayId.c_str(), playerSessionId.c_str());
            return false;
        }
        connection = it->second->connection;
    }
    
    // 通过回调发送消息到Gateway，Gateway会转发给玩家
    std::shared_lock<std::shared_mutex> configLock(configMutex_);
    return sendMessageCallback_(connection, msgId, data, playerSessionId);
}

bool ConnectionManager::sendToGateway(const std::string& gatewayId, uint32_t msgId, const std::string& data) {
    // 首先检查回调函数
    {
        std::shared_lock<std::shared_mutex> configLock(configMutex_);
        if (!sendMessageCallback_) {
            LOG_ERROR("Send message callback not set");
            return false;
        }
    }
    
    // 获取Gateway连接 - 只读
    std::weak_ptr<uv::TcpConnection> connection;
    {
        std::shared_lock<std::shared_mutex> gatewayLock(gatewayMutex_.getMutexForKey(gatewayId));
        auto it = gateways_.find(gatewayId);
        if (it == gateways_.end()) {
            LOG_ERROR("Gateway %s not found", gatewayId.c_str());
            return false;
        }
        connection = it->second->connection;
    }
    
    // 发送系统消息到Gateway
    std::shared_lock<std::shared_mutex> configLock(configMutex_);
    return sendMessageCallback_(connection, msgId, data, "");
}

std::vector<std::string> ConnectionManager::getPlayerSessionsByGateway(const std::string& gatewayId) {
    // 获取针对gateway的共享锁（只读）
    std::shared_lock<std::shared_mutex> lock(gatewayMutex_.getMutexForKey(gatewayId));
    
    auto it = gatewayPlayerSessions_.find(gatewayId);
    if (it != gatewayPlayerSessions_.end()) {
        return it->second;
    }
    return {};
}

std::vector<std::string> ConnectionManager::getAllGateways() {
    std::vector<std::string> result;
    
    // 这是一个遍历所有gateway的操作，需要对整个gateway集合加锁
    // 理论上可以分片锁定，但简单起见，我们获取所有gateway分片的共享锁
    gatewayMutex_.lockSharedAll();
    
    result.reserve(gateways_.size());
    for (const auto& pair : gateways_) {
        result.push_back(pair.first);
    }
    
    gatewayMutex_.unlockSharedAll();
    return result;
}

size_t ConnectionManager::getGatewayCount() const {
    // 这是一个统计操作，需要对整个gateway集合加锁
    gatewayMutex_.lockSharedAll();
    size_t count = gateways_.size();
    gatewayMutex_.unlockSharedAll();
    
    return count;
}

size_t ConnectionManager::getPlayerSessionCount() const {
    // 这是一个统计操作，需要对整个player session集合加锁
    playerSessionMutex_.lockSharedAll();
    size_t count = playerSessions_.size();
    playerSessionMutex_.unlockSharedAll();
    
    return count;
}

void ConnectionManager::cleanupInactiveSessions(int timeoutSeconds) {
    auto now = std::chrono::system_clock::now();
    std::vector<std::string> sessionsToRemove;
    
    // 收集需要清理的会话 - 只读操作
    playerSessionMutex_.lockSharedAll();
    
    for (const auto& pair : playerSessions_) {
        const auto& session = pair.second;
        auto lastActiveTime = session->getLastActiveTime();
        auto inactiveTime = std::chrono::duration_cast<std::chrono::seconds>(now - lastActiveTime).count();
        
        if (inactiveTime > timeoutSeconds) {
            sessionsToRemove.push_back(pair.first);
            LOG_INFO("Session %s inactive for %d seconds, marking for cleanup", 
                     pair.first.c_str(), static_cast<int>(inactiveTime));
        }
    }
    
    playerSessionMutex_.unlockSharedAll();
    
    // 逐个清理会话
    for (const auto& sessionId : sessionsToRemove) {
        unregisterPlayerSession(sessionId);
    }
    
    if (!sessionsToRemove.empty()) {
        LOG_INFO("Cleaned up %zu inactive sessions", sessionsToRemove.size());
    }
}

std::string ConnectionManager::getGatewayIdBySessionId(const std::string& sessionId) {
    // 获取针对session的共享锁（只读）
    std::shared_lock<std::shared_mutex> lock(playerSessionMutex_.getMutexForKey(sessionId));
    
    auto it = playerSessions_.find(sessionId);
    if (it != playerSessions_.end()) {
        return it->second->getGatewayId();
    }
    return "";
}

bool ConnectionManager::createBasicPlayerSession(const std::string& playerSessionId, const std::string& gatewayId) {
    return registerPlayerSession(playerSessionId, gatewayId, "");
}

bool ConnectionManager::completePlayerSession(const std::string& playerSessionId, const std::string& loginname, std::shared_ptr<User> user) {
    if (loginname.empty()) {
        LOG_ERROR("Cannot complete player session with empty loginname");
        return false;
    }
    
    // 获取针对session的独占锁
    std::unique_lock<std::shared_mutex> sessionLock(playerSessionMutex_.getMutexForKey(playerSessionId));
    
    // 查找会话
    auto it = playerSessions_.find(playerSessionId);
    if (it == playerSessions_.end()) {
        LOG_ERROR("Player session %s not found for completion", playerSessionId.c_str());
        return false;
    }
    
    auto session = it->second;
    
    // 检查是否已经有登录名
    if (!session->getLoginname().empty()) {
        // 如果登录名相同，说明已经完成过，直接更新用户数据即可
        if (session->getLoginname() == loginname) {
            session->setPlayer(user);
            session->updateActiveTime();
            LOG_INFO("Updated existing player session %s for user %s", 
                     playerSessionId.c_str(), loginname.c_str());
            return true;
        }
        
        // 如果登录名不同，需要先移除旧的映射
        loginnameToSession_.erase(session->getLoginname());
        LOG_INFO("Changing loginname for session %s: %s -> %s", 
                 playerSessionId.c_str(), session->getLoginname().c_str(), loginname.c_str());
    }
    
    // 更新会话
    session->setLoginname(loginname);
    session->setPlayer(user);
    session->updateActiveTime();
    
    // 更新loginname映射
    loginnameToSession_[loginname] = session;
    
    LOG_INFO("Completed player session %s for user %s", playerSessionId.c_str(), loginname.c_str());
    return true;
}




