#include "tcp_server.h"
#include "core/infrastructure/common/error_code.h"
#include <random>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <third_party/libuv_cpp/include/LogWriter.hpp>
#include <third_party/libuv_cpp/include/Packet.hpp>
#include "../common/utils.h"
#include "protocol.h"
#include "core/infrastructure/proto/game.pb.h"

TcpServer::TcpServer(uv::EventLoop* loop) 
    : loop_(loop), messageRouter_(nullptr), 
      connectionManager_(loop),
      threadPoolSize_(8), maxQueueSizePerWorker_(1000) {
    
    // 设置为大端模式
    uv::Packet::Mode = uv::Packet::DataMode::BigEndian;
    
    // 启用缓冲区模式 - 使用CycleBuffer
    uv::TcpServer::SetBufferMode(uv::GlobalConfig::BufferMode::CycleBuffer);
    
    LOG_DEBUG("TcpServer initialized with BigEndian mode and CycleBuffer");
}

TcpServer::~TcpServer() {
    stop();
}

bool TcpServer::initialize(const ConfigManager& configManager) {
    try {
        port_ = configManager.getServerConfig()["server"]["port"].get<unsigned short>();
        threadPoolSize_ = configManager.getServerConfig()["server"]["thread_pool_size"].get<size_t>();
        maxQueueSizePerWorker_ = configManager.getServerConfig()["server"]["max_queue_size_per_worker"].get<size_t>();
        
        sessionThreadPool_ = std::make_unique<SessionThreadPool>(threadPoolSize_);
        
        LOG_DEBUG("TCP server initialized - Port: %d, ThreadPool: %d, MaxQueuePerWorker: %d", 
                 port_,  threadPoolSize_, maxQueueSizePerWorker_);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Error initializing TCP server: %s", e.what());
        return false;
    }
}

bool TcpServer::start() {
    try {
        sessionThreadPool_->start();
        server_ = std::make_unique<uv::TcpServer>(loop_);
        
        server_->setNewConnectCallback([this](std::weak_ptr<uv::TcpConnection> connection) {
            this->onNewConnection(connection);
        });
        
        server_->setConnectCloseCallback([this](std::weak_ptr<uv::TcpConnection> connection) {
            if (auto conn = connection.lock()) {
                std::string gatewayId = conn->Name();  // gatewayId就是连接地址
                
                if (this->connectionManager_.unregisterGateway(gatewayId)) {
                    this->onConnectionClose(gatewayId);
                } else {
                    LOG_WARN("Gateway not found for unregistration: %s", gatewayId.c_str());
                }
            }
        });
        
        server_->setMessageCallback([this](std::shared_ptr<uv::TcpConnection> connection, const char* buf, ssize_t size) {
            this->onMessage(connection, buf, size);
        });
        
        uv::SocketAddr addr("0.0.0.0", port_);
        int result = server_->bindAndListen(addr);
        
        if (result != 0) {
            LOG_ERROR("Failed to bind and listen on %d, error: %s", port_, uv::EventLoop::GetErrorMessage(result));
            return false;
        }
        
        connectionManager_.setSendMessageCallback([this](std::weak_ptr<uv::TcpConnection> connection, 
                                                         uint32_t msgId, const std::string& data, 
                                                         const std::string& sessionId) {
            return this->sendMessageToConnection(connection, msgId, data, sessionId);
        });
        
        // 启动会话清理定时器，超过180s的不活跃会话会被清理
        connectionManager_.startSessionCleanupTimer(60);
        
        LOG_INFO("TCP server started successfully on port %d", port_);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Exception starting TCP server: %s", e.what());
        return false;
    }
}

void TcpServer::stop() {
    LOG_INFO("Stopping TCP server...");
    
    // 1. 停止线程池
    if (sessionThreadPool_) {
        sessionThreadPool_->stop();
        sessionThreadPool_.reset();
    }
    
    // 2. 关闭服务器
    if (server_) {
        try {
            // 异步关闭，但不等待回调
            server_->close([](){});
            
            // 给一个短暂的时间让关闭操作开始
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        } catch (const std::exception& e) {
            LOG_WARN("Exception while closing server: %s", e.what());
        }
        
        server_.reset();
    }
    
    LOG_INFO("TCP server stopped");
}

bool TcpServer::sendToPlayer(const std::string& playerSessionId, uint32_t msgId, const std::string& data) {
    return connectionManager_.sendToPlayer(playerSessionId, msgId, data);
}

bool TcpServer::sendToGateway(const std::string& gatewayId, uint32_t msgId, const std::string& data) {
    return connectionManager_.sendToGateway(gatewayId, msgId, data);
}

void TcpServer::broadcastMessage(uint32_t msgId, const std::string& data) {
    auto gateways = connectionManager_.getAllGateways();
    
    for (const auto& gatewayId : gateways) {
        connectionManager_.sendToGateway(gatewayId, msgId, data);
    }
}

bool TcpServer::registerPlayerSession(const std::string& playerSessionId, const std::string& gatewayId, const std::string& loginname) {
    return connectionManager_.registerPlayerSession(playerSessionId, gatewayId, loginname);
}

bool TcpServer::unregisterPlayerSession(const std::string& playerSessionId) {
    return connectionManager_.unregisterPlayerSession(playerSessionId);
}

void TcpServer::setOnConnectionCallback(OnConnectionCallback callback) {
    onConnectionCallback_ = callback;
}

void TcpServer::setOnDisconnectionCallback(OnDisconnectionCallback callback) {
    onDisconnectionCallback_ = callback;
}

void TcpServer::setOnMessageCallback(OnMessageCallback callback) {
    onMessageCallback_ = callback;
}

void TcpServer::setMessageRouter(std::shared_ptr<MessageRouter> router) {
    messageRouter_ = router;
}

void TcpServer::onNewConnection(std::weak_ptr<uv::TcpConnection> connection) {
    auto conn = connection.lock();
    if (!conn) {
        LOG_ERROR("Invalid connection in onNewConnection");
        return;
    }
    
    std::string gatewayId = connectionManager_.registerGateway(connection);
    
    LOG_INFO("New Gateway connected: %s (address: %s)", gatewayId.c_str(), conn->Name().c_str());
    
    if (onConnectionCallback_) {
        onConnectionCallback_(gatewayId);
    }
}

void TcpServer::onConnectionClose(const std::string& gatewayId) {
    LOG_INFO("Gateway disconnected: %s", gatewayId.c_str());
    
    if (onDisconnectionCallback_) {
        onDisconnectionCallback_(gatewayId);
    }
}

void TcpServer::onMessage(std::shared_ptr<uv::TcpConnection> connection, const char* buf, ssize_t size) {
    if (!connection || size <= 0) {
        return;
    }
    
    auto packetBuffer = connection->getPacketBuffer();
    if (nullptr == packetBuffer) {
        LOG_ERROR("Error: packet buffer is null for connection %s", connection->Name().c_str());
        return;
    }
    
    if (packetBuffer->append(buf, size) < 0) {
        LOG_ERROR("Error: failed to append data to buffer for connection %s", connection->Name().c_str());
        return;
    }
    
    std::string gatewayId = connection->Name();
    
    // 解析完整数据包
    uv::Packet packet;
    while (packetBuffer->readPacket(packet) == 0) {
        LOG_DEBUG("Received complete packet from %s: msgId=0x%x, dataSize=%d, payload=%s", 
                 gatewayId.c_str(), packet.MsgId(), packet.DataSize(), packet.getData());
        
        auto validationResult = validatePacketBasic(packet);
        
        if (validationResult == PacketValidationResult::Valid) {
            enqueuePacket(connection, packet, gatewayId);
        } else {
            logValidationFailure(validationResult, packet);
            proto::KickPlayerNotify notify;
            notify.set_sessionid(packet.SessionId());
            notify.set_reason(ErrorCode::INVALID_TOKEN); 
            sendToPlayer(packet.SessionId(), Protocol::SC_KICK_PLAYER_NOTIFY, notify.SerializeAsString());
        }
    }
}

PacketValidationResult TcpServer::validatePacketBasic(uv::Packet& packet) {
    uint32_t minSize = uv::Packet::PacketMinSize();
    if (packet.PacketSize() < minSize || packet.PacketSize() > (1024 * 1024)) {
        return PacketValidationResult::InvalidSize;
    }
    
    // 检查数据长度的一致性
    uint32_t expectedDataSize = packet.PacketSize() - minSize;
    if (packet.DataSize() != expectedDataSize) {
        return PacketValidationResult::InvalidLength;
    }
    
    return PacketValidationResult::Valid;
}

void TcpServer::logValidationFailure(PacketValidationResult result, uv::Packet& packet) {
    std::string reason;
    switch (result) {
        case PacketValidationResult::InvalidSize:
            reason = "Invalid packet size";
            break;
        case PacketValidationResult::InvalidLength:
            reason = "Invalid data length";
            break;
        case PacketValidationResult::Corrupted:
            reason = "Corrupted packet data";
            break;
        default:
            reason = "Unknown validation error";
    }
    
    LOG_WARN("[VALIDATION FAILED] %s - msgId=0x%x, packetSize=%d, dataSize=%d", 
             reason.c_str(), packet.MsgId(), packet.PacketSize(), packet.DataSize());
}

void TcpServer::enqueuePacket(std::shared_ptr<uv::TcpConnection> connection, 
                                    uv::Packet packet, const std::string& gatewayId) {
    if (sessionThreadPool_->getSessionQueueSize(packet.SessionId()) > maxQueueSizePerWorker_) {
        LOG_WARN("[WARNING] Session %s thread queue is full, dropping packet", packet.SessionId());
        return;
    }
    sessionThreadPool_->enqueueBySession(packet.SessionId(), [this, connection, packet, gatewayId]() mutable {
        processPacket(connection, packet, gatewayId);
    });
}

void TcpServer::processPacket(std::shared_ptr<uv::TcpConnection> connection, uv::Packet packet, const std::string& gatewayId) {
    std::string playerSessionId = packet.SessionId();
    utils::PerformanceTimer timer("[Thread Pool] Packet processing for session: " + playerSessionId, utils::PerformanceTimer::TimeUnit::MILLISECONDS);
    
    try {
        // 提取消息数据
        std::string payload(packet.getData(), packet.DataSize());
        
        // 将二进制数据转换为十六进制字符串显示
        std::string hexPayload;
        for (size_t i = 0; i < payload.size(); ++i) {
            char hexChar[3];
            sprintf(hexChar, "%02x", static_cast<unsigned char>(payload[i]));
            hexPayload += hexChar;
        }
        
        LOG_INFO("Received message from %s: msgId=0x%x, dataSize=%d, payload=%s", gatewayId.c_str(), packet.MsgId(), packet.DataSize(), hexPayload.c_str());
        // 通过消息路由器处理消息
        if (messageRouter_) {
            // 特殊处理登录请求，先创建基础PlayerSession
            if (packet.MsgId() == Protocol::CS_LOGIN_REQ) {
                if (!connectionManager_.createBasicPlayerSession(playerSessionId, gatewayId)) {
                    LOG_ERROR("Failed to create basic player session for %s", playerSessionId.c_str());
                    return;
                }
                connectionManager_.updatePlayerSessionActiveTime(playerSessionId);
                messageRouter_->routeMessage(playerSessionId, packet.MsgId(), payload);
            } 
            else {
                // 确保玩家会话已注册（除了登录请求外的其他消息）
                if (!playerSessionId.empty()) {
                    auto playerSession = connectionManager_.getPlayerSession(playerSessionId);
                    if (!playerSession) {
                        LOG_WARN("Received message for unregistered session %s, ignoring", playerSessionId.c_str());
                        return;
                    }
                    connectionManager_.updatePlayerSessionActiveTime(playerSessionId);
                    messageRouter_->routeMessage(playerSessionId, packet.MsgId(), payload);
                }
            }
        } else {
            LOG_WARN("Message router not set, cannot process message ID 0x%x", packet.MsgId());
        }
    } catch (const std::exception& e) {
        LOG_ERROR("[Thread Pool] Exception processing packet from gateway %s: %s", gatewayId.c_str(), e.what());
    }
}

bool TcpServer::sendMessageToConnection(std::weak_ptr<uv::TcpConnection> connection, 
                                      uint32_t msgId, const std::string& data, 
                                      const std::string& sessionId) {
    auto conn = connection.lock();
    if (!conn) {
        LOG_ERROR("Connection is invalid for session %s", sessionId.c_str());
        return false;
    }
    
    if (!sessionId.empty() && !utils::isValidSessionId(sessionId)) {
        LOG_ERROR("Invalid SessionId format: %s", sessionId.c_str());
        return false;
    }
    
    try {
        // 使用现有的Packet类打包消息
        auto responsePacket = std::make_shared<uv::Packet>();
        
        responsePacket->pack(data.c_str(), data.size(), msgId, sessionId.c_str());
        
        // 异步发送消息
        sendResponseAsync(conn, responsePacket, "message");
        
        if (sessionId.empty()) {
            LOG_DEBUG("Queued system message to gateway, msgId: 0x%x, size: %d", msgId, data.size());
        } else {
            LOG_DEBUG("Queued message to session %s, msgId: 0x%x, size: %d", 
                     sessionId.c_str(), msgId, data.size());
        }
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Exception sending message to session %s: %s", sessionId.c_str(), e.what());
        return false;
    }
}

void TcpServer::sendResponse(std::shared_ptr<uv::TcpConnection> connection, 
                           std::shared_ptr<uv::Packet> responsePacket, const std::string& type) {
    if (!connection || !connection->isConnected()) {
        LOG_WARN("Connection is not available for sending %s response", type.c_str());
        return;
    }
    
    connection->write(responsePacket->Buffer().c_str(), responsePacket->PacketSize(), 
        [type](uv::WriteInfo& info) {
            if (info.status) {
                LOG_ERROR("Write error for %s response: %s", 
                          type.c_str(), uv::EventLoop::GetErrorMessage(info.status));
            } else {
                LOG_DEBUG("Direct %s response sent successfully", type.c_str());
            }
        });
}

void TcpServer::sendResponseAsync(std::shared_ptr<uv::TcpConnection> connection, 
                                 std::shared_ptr<uv::Packet> responsePacket, 
                                 const std::string& type) {
    // 确保在事件循环线程中执行写操作
    loop_->runInThisLoop([connection, responsePacket, type]() {
        if (!connection || !connection->isConnected()) {
            LOG_WARN("Connection is not available for sending %s response", type.c_str());
            return;
        }
        
        connection->write(responsePacket->Buffer().c_str(), responsePacket->PacketSize(), 
            [type](uv::WriteInfo& info) {
                if (info.status) {
                    LOG_ERROR("Write error for %s response: %s", 
                              type.c_str(), uv::EventLoop::GetErrorMessage(info.status));
                } else {
                    LOG_DEBUG("Async %s response sent successfully", type.c_str());
                }
            });
    });
}