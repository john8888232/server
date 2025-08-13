#include "connection_manager.h"
#include "core/infrastructure/common/error_code.h"
#include <algorithm>
#include <cstring>
#include "third_party/libuv_cpp/include/LogWriter.hpp"
#include "third_party/libuv_cpp/include/Timer.hpp"
#include "protocol.h"
#include "core/infrastructure/protogen/game.pb.h"

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
    auto conn = connection.lock();
    if (!conn) {
        LOG_ERROR("Invalid connection when registering gateway");
        return "";
    }
    
    // 直接使用连接地址作为gatewayId
    std::string gatewayId = conn->Name();
    std::string name = gatewayName.empty() ? ("Gateway_" + gatewayId) : gatewayName;
    
    // 使用gateway锁
    std::unique_lock<std::shared_mutex> gatewayLock(gatewayMutex_.getMutexForKey(gatewayId));
    
    auto gateway = std::make_shared<GatewayConnection>(gatewayId, name, connection);
    gateways_[gatewayId] = gateway;
    
    LOG_INFO("Gateway registered: %s (name: %s)", gatewayId.c_str(), name.c_str());
    return gatewayId;
}

bool ConnectionManager::unregisterGateway(const std::string& gatewayId) {
    std::vector<std::string> sessionsToCleanup;
    
    // 第一阶段：获取gateway信息和需要清理的会话列表
    {
        // 首先获取gateway锁
        std::unique_lock<std::shared_mutex> gatewayLock(gatewayMutex_.getMutexForKey(gatewayId));
        
        auto it = gateways_.find(gatewayId);
        if (it == gateways_.end()) {
            LOG_WARN("Gateway %s not found for unregistration", gatewayId.c_str());
            return false;
        }
        
        // 获取需要清理的会话ID列表
        {
            // 获取全局锁处理相关联的玩家会话
            std::unique_lock<std::shared_mutex> globalLock(globalMutex_);
            
            auto sessIt = gatewayPlayerSessions_.find(gatewayId);
            if (sessIt != gatewayPlayerSessions_.end()) {
                sessionsToCleanup = sessIt->second;
                gatewayPlayerSessions_.erase(sessIt);
            }
        }
        
        // 删除网关
        gateways_.erase(it);
    }
    
    // 第二阶段：清理玩家会话，在gateway锁释放后进行
    for (const auto& sessionId : sessionsToCleanup) {
        // 获取玩家锁
        std::unique_lock<std::shared_mutex> playerLock(playerMutex_.getMutexForKey(sessionId));
        
        auto it = playerSessions_.find(sessionId);
        if (it == playerSessions_.end()) {
            continue; // 会话可能已被其他操作删除
        }
        
        auto playerSession = it->second;
        std::string loginname;
        
        if (playerSession && !playerSession->getLoginname().empty()) {
            loginname = playerSession->getLoginname();
        }
        
        playerSessions_.erase(sessionId);
        
        // 如果有loginname，需要获取全局锁清理映射
        if (!loginname.empty()) {
            std::unique_lock<std::shared_mutex> globalLock(globalMutex_);
            auto lnIt = loginnameToSession_.find(loginname);
            if (lnIt != loginnameToSession_.end() && lnIt->second->getSessionId() == sessionId) {
                loginnameToSession_.erase(lnIt);
            }
        }
        
        LOG_INFO("Removed player session %s due to gateway disconnection", sessionId.c_str());
    }
    
    LOG_INFO("Gateway unregistered: %s", gatewayId.c_str());
    return true;
}

std::shared_ptr<GatewayConnection> ConnectionManager::getGateway(const std::string& gatewayId) {
    // 使用共享锁读取gateway
    std::shared_lock<std::shared_mutex> gatewayLock(gatewayMutex_.getMutexForKey(gatewayId));
    
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
    
    // 检查Gateway是否存在
    bool gatewayExists = false;
    {
        std::shared_lock<std::shared_mutex> gatewayLock(gatewayMutex_.getMutexForKey(gatewayId));
        gatewayExists = (gateways_.find(gatewayId) != gateways_.end());
    }
    
    if (!gatewayExists) {
        LOG_ERROR("Cannot register player session %s: Gateway %s not found", 
                playerSessionId.c_str(), gatewayId.c_str());
        return false;
    }
    
    // 创建会话对象
    auto session = std::make_shared<PlayerSession>(playerSessionId, gatewayId, loginname);
    
    // 第一阶段：更新玩家会话
    {
        // 获取玩家锁
        std::unique_lock<std::shared_mutex> playerLock(playerMutex_.getMutexForKey(playerSessionId));
        
        // 检查会话是否已存在
        auto existingSessionIt = playerSessions_.find(playerSessionId);
        if (existingSessionIt != playerSessions_.end()) {
            LOG_WARN("Player session %s already exists, updating gateway mapping", playerSessionId.c_str());
        }
        
        // 保存会话
        playerSessions_[playerSessionId] = session;
    }
    
    // 第二阶段：更新loginname映射（如果有）
    if (!loginname.empty()) {
        std::unique_lock<std::shared_mutex> globalLock(globalMutex_);
        
        // 检查是否已有相同登录名的会话
        auto existingIt = loginnameToSession_.find(loginname);
        if (existingIt != loginnameToSession_.end() && existingIt->second->getSessionId() != playerSessionId) {
            LOG_WARN("Loginname %s already mapped to session %s, will be remapped", 
                    loginname.c_str(), existingIt->second->getSessionId().c_str());
        }
        
        // 更新映射
        loginnameToSession_[loginname] = session;
        LOG_INFO("Mapped loginname %s to session %s", loginname.c_str(), playerSessionId.c_str());
    }
    
    // 第三阶段：更新反向映射
    {
        std::unique_lock<std::shared_mutex> gatewayLock(gatewayMutex_.getMutexForKey(gatewayId));
        gatewayPlayerSessions_[gatewayId].push_back(playerSessionId);
    }
    
    LOG_INFO("Player session registered: %s -> Gateway: %s, loginname: %s", 
             playerSessionId.c_str(), gatewayId.c_str(), loginname.c_str());
    return true;
}

bool ConnectionManager::unregisterPlayerSession(const std::string& playerSessionId) {
    std::string gatewayId;
    std::string loginname;
    
    // 第一阶段：获取会话信息
    {
        // 获取玩家锁
        std::unique_lock<std::shared_mutex> playerLock(playerMutex_.getMutexForKey(playerSessionId));
        
        auto it = playerSessions_.find(playerSessionId);
        if (it == playerSessions_.end()) {
            LOG_WARN("Player session %s not found for unregistration", playerSessionId.c_str());
            return false;
        }
        
        gatewayId = it->second->getGatewayId();
        loginname = it->second->getLoginname();
        
        // 删除玩家会话
        playerSessions_.erase(it);
    }
    
    // 第二阶段：清理loginname映射（如果有）
    if (!loginname.empty()) {
        std::unique_lock<std::shared_mutex> globalLock(globalMutex_);
        auto it = loginnameToSession_.find(loginname);
        if (it != loginnameToSession_.end() && it->second->getSessionId() == playerSessionId) {
            loginnameToSession_.erase(it);
        }
    }
    
    // 第三阶段：清理gateway反向映射
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
    // 使用共享锁读取玩家会话
    std::shared_lock<std::shared_mutex> playerLock(playerMutex_.getMutexForKey(playerSessionId));
    
    auto it = playerSessions_.find(playerSessionId);
    if (it != playerSessions_.end()) {
        return it->second;
    }
    return nullptr;
}

std::shared_ptr<PlayerSession> ConnectionManager::getPlayerSessionByLoginname(const std::string& loginname) {
    // 使用全局共享锁查找loginname映射
    std::shared_lock<std::shared_mutex> globalLock(globalMutex_);
    
    auto it = loginnameToSession_.find(loginname);
    if (it != loginnameToSession_.end()) {
        return it->second;
    }
    return nullptr;
}

void ConnectionManager::updatePlayerSessionActiveTime(const std::string& playerSessionId) {
    // 使用玩家锁更新活跃时间
    std::unique_lock<std::shared_mutex> playerLock(playerMutex_.getMutexForKey(playerSessionId));
    
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
    
    // 获取玩家会话信息（使用共享锁）
    std::shared_ptr<PlayerSession> session;
    std::string gatewayId;
    {
        std::shared_lock<std::shared_mutex> playerLock(playerMutex_.getMutexForKey(playerSessionId));
        auto it = playerSessions_.find(playerSessionId);
        if (it == playerSessions_.end()) {
            LOG_ERROR("Player session %s not found", playerSessionId.c_str());
            return false;
        }
        session = it->second;
        gatewayId = session->getGatewayId();
    }
    
    // 获取Gateway连接（使用共享锁）
    std::weak_ptr<uv::TcpConnection> conn;
    {
        std::shared_lock<std::shared_mutex> gatewayLock(gatewayMutex_.getMutexForKey(gatewayId));
        auto it = gateways_.find(gatewayId);
        if (it == gateways_.end()) {
            LOG_ERROR("Gateway %s not found for player session %s", gatewayId.c_str(), playerSessionId.c_str());
            return false;
        }
        conn = it->second->connection;
    }
    
    // 通过回调发送消息到Gateway，Gateway会转发给玩家
    return sendMessageCallback_(conn, msgId, data, playerSessionId);
}

bool ConnectionManager::sendToGateway(const std::string& gatewayId, uint32_t msgId, const std::string& data, const std::string& sessionId) {
    if (!sendMessageCallback_) {
        LOG_ERROR("Send message callback not set");
        return false;
    }
    
    // 使用gateway的共享锁
    std::shared_lock<std::shared_mutex> gatewayLock(gatewayMutex_.getMutexForKey(gatewayId));
    
    auto it = gateways_.find(gatewayId);
    if (it == gateways_.end()) {
        LOG_ERROR("Gateway %s not found", gatewayId.c_str());
        return false;
    }
    
    return sendMessageCallback_(it->second->connection, msgId, data, sessionId);
}

std::vector<std::string> ConnectionManager::getPlayerSessionsByGateway(const std::string& gatewayId) {
    // 使用gateway的共享锁
    std::shared_lock<std::shared_mutex> gatewayLock(gatewayMutex_.getMutexForKey(gatewayId));
    
    auto it = gatewayPlayerSessions_.find(gatewayId);
    if (it != gatewayPlayerSessions_.end()) {
        return it->second;
    }
    return {};
}

std::vector<std::string> ConnectionManager::getAllGateways() {
    // 使用全局共享锁获取所有gateway ID
    std::shared_lock<std::shared_mutex> globalLock(globalMutex_);
    
    std::vector<std::string> gatewayIds;
    for (const auto& pair : gateways_) {
        gatewayIds.push_back(pair.first);
    }
    return gatewayIds;
}

size_t ConnectionManager::getGatewayCount() const {
    // 使用全局共享锁
    std::shared_lock<std::shared_mutex> globalLock(globalMutex_);
    return gateways_.size();
}

size_t ConnectionManager::getPlayerSessionCount() const {
    // 使用全局共享锁
    std::shared_lock<std::shared_mutex> globalLock(globalMutex_);
    return playerSessions_.size();
}

void ConnectionManager::cleanupInactiveSessions(int timeoutSeconds) {
    std::vector<std::string> sessionsToKick;
    
    // 使用全局共享锁收集过期会话
    {
        std::shared_lock<std::shared_mutex> globalLock(globalMutex_);
        
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
        // 对每个会话使用独立的锁
        std::shared_ptr<PlayerSession> session;
        {
            std::shared_lock<std::shared_mutex> playerLock(playerMutex_.getMutexForKey(sessionId));
            auto it = playerSessions_.find(sessionId);
            if (it == playerSessions_.end()) {
                continue;
            }
            session = it->second;
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
    // 使用玩家锁
    std::shared_lock<std::shared_mutex> playerLock(playerMutex_.getMutexForKey(sessionId));
    
    auto it = playerSessions_.find(sessionId);
    if (it != playerSessions_.end()) {
        return it->second->getGatewayId();
    }
    
    return "";
}

bool ConnectionManager::createBasicPlayerSession(const std::string& playerSessionId, const std::string& gatewayId) {
    // 检查网关是否存在
    bool gatewayExists = false;
    {
        std::shared_lock<std::shared_mutex> gatewayLock(gatewayMutex_.getMutexForKey(gatewayId));
        gatewayExists = (gateways_.find(gatewayId) != gateways_.end());
    }
    
    if (!gatewayExists) {
        LOG_ERROR("Gateway %s not found", gatewayId.c_str());
        return false;
    }
    
    // 创建会话对象
    auto session = std::make_shared<PlayerSession>(playerSessionId, gatewayId, "");
    
    // 第一阶段：更新玩家会话
    {
        // 获取玩家锁
        std::unique_lock<std::shared_mutex> playerLock(playerMutex_.getMutexForKey(playerSessionId));
        
        // 检查会话是否已存在
        if (playerSessions_.find(playerSessionId) != playerSessions_.end()) {
            LOG_WARN("Player session %s already exists", playerSessionId.c_str());
            return false;
        }
        
        // 保存会话
        playerSessions_[playerSessionId] = session;
    }
    
    // 第二阶段：更新反向映射
    {
        std::unique_lock<std::shared_mutex> gatewayLock(gatewayMutex_.getMutexForKey(gatewayId));
        gatewayPlayerSessions_[gatewayId].push_back(playerSessionId);
    }
    
    LOG_INFO("Basic player session created: %s -> Gateway: %s", 
             playerSessionId.c_str(), gatewayId.c_str());
    return true;
}

bool ConnectionManager::completePlayerSession(const std::string& playerSessionId, const std::string& loginname, std::shared_ptr<User> user) {
    // 准备踢人消息的数据，避免在锁内部发送消息
    std::string oldSessionIdToKick;
    std::string oldGatewayId;
    std::string kickMessageData;
    bool needKickOldSession = false;
    std::shared_ptr<PlayerSession> session;
    
    // 第一阶段：收集信息和准备数据
    {
        // 先获取玩家锁
        std::unique_lock<std::shared_mutex> playerLock(playerMutex_.getMutexForKey(playerSessionId));
        
        // 检查玩家会话是否存在
        auto it = playerSessions_.find(playerSessionId);
        if (it == playerSessions_.end()) {
            LOG_ERROR("Player session %s not found for completion", playerSessionId.c_str());
            return false;
        }
        
        session = it->second;
        
        // 补全会话信息，在玩家锁内更新
        session->setLoginname(loginname);
        session->setPlayer(user);
    }
    
    // 处理loginname映射和检查是否有旧会话
    if (!loginname.empty()) {
        // 获取全局锁处理loginname映射
        std::unique_lock<std::shared_mutex> globalLock(globalMutex_);
        
        // 检查是否已有相同登录名的会话
        auto existingIt = loginnameToSession_.find(loginname);
        if (existingIt != loginnameToSession_.end() && existingIt->second->getSessionId() != playerSessionId) {
            oldSessionIdToKick = existingIt->second->getSessionId();
            oldGatewayId = existingIt->second->getGatewayId();
            needKickOldSession = true;
            
            // 准备踢人消息
            proto::KickPlayerNotify notify;
            notify.set_sessionid(oldSessionIdToKick);
            notify.set_reason(ErrorCode::OTHER_LOGIN);
            notify.SerializeToString(&kickMessageData);
            
            LOG_INFO("Player %s already has an active session %s, will kick out", 
                     loginname.c_str(), oldSessionIdToKick.c_str());
        }
        
        // 建立loginname到session的映射
        loginnameToSession_[loginname] = session;
        LOG_INFO("Mapped loginname %s to session %s", loginname.c_str(), playerSessionId.c_str());
    }
    
    // 第二阶段：在锁外发送踢人消息
    if (needKickOldSession) {
        sendToPlayer(oldSessionIdToKick, Protocol::SC_KICK_PLAYER_NOTIFY, kickMessageData);
        LOG_INFO("Sent kick notification to old session %s", oldSessionIdToKick.c_str());
        
        // 第三阶段：清理旧会话
        unregisterPlayerSession(oldSessionIdToKick);
    }
    
    LOG_INFO("Player session completed: %s -> loginname: %s", 
             playerSessionId.c_str(), loginname.c_str());
    return true;
}




