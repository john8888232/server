#include "login_handler.h"
#include "third_party/libuv_cpp/include/LogWriter.hpp"
#include <ctime>
#include "core/infrastructure/protogen/game.pb.h"
#include "core/infrastructure/common/error_code.h"
#include "core/infrastructure/network/protocol.h"
#include "core/infrastructure/protogen/game.pb.h"
#include "core/application/services/user_service.h"
#include "core/infrastructure/common/app_context.h"
#include "core/infrastructure/common/dependency_container.h"
#include "core/domain/models/game_manager.h"
#include "core/domain/models/user.h"
#include "src/games/game_factory.h"
#include "src/games/game_registry.h"
#include <memory>

extern DependencyContainer& getDependencyContainer();

LoginHandler::LoginHandler(ResponseCallback responseCallback, std::shared_ptr<TcpServer> tcpServer)
    : responseCallback_(std::move(responseCallback)), 
      userService_(std::make_unique<UserService>()), 
      tcpServer_(std::move(tcpServer)) {
}

void LoginHandler::handleMessage(const std::string& sessionId, const std::string& data) {
    try {
        proto::LoginReq request;
        if (!request.ParseFromString(data)) {
            LOG_ERROR("Failed to parse login request from session %s", sessionId.c_str());
            proto::LoginResp response;
            response.set_code(ErrorCode::INVALID_REQUEST);
            response.set_message(ErrorCode::getErrorMessage(ErrorCode::INVALID_REQUEST));
            std::string responseData;
            if (response.SerializeToString(&responseData)) {
                responseCallback_(sessionId, Protocol::SC_LOGIN_RES, responseData);
            }
            return;
        }
        
        LOG_INFO("Processing login request from %s (session: %s)", request.loginname().c_str(), sessionId.c_str());
        
        proto::LoginResp response;
        response.set_loginname(request.loginname());
        
        auto& container = getDependencyContainer();
        auto appContext = container.resolve<AppContext>();
        if (!appContext) {
            LOG_ERROR("AppContext not available in dependency container");
            response.set_code(ErrorCode::SYSTEM_ERROR);
            response.set_message("System error: AppContext not available");
            std::string responseData;
            if (response.SerializeToString(&responseData)) {
                responseCallback_(sessionId, Protocol::SC_LOGIN_RES, responseData);
            }
            return;
        }
        
        auto gameManager = appContext->getGameManager();
        std::shared_ptr<User> finalUser = nullptr;
        bool userFromGame = false;
        
        if (gameManager) {
            auto currentGame = gameManager->getPlayerGame(request.loginname());
            if (currentGame) {
                // 玩家在游戏中，尝试获取游戏中的 User 对象
                const auto& players = currentGame->getPlayers();
                auto playerIt = players.find(request.loginname());
                if (playerIt != players.end() && playerIt->second) {
                    auto gameUser = playerIt->second->getUser();
                    if (gameUser) {
                        finalUser = gameUser;
                        userFromGame = true;
                        LOG_INFO("Player %s found in game %s, using game User object (balance: %.2f)", 
                                 request.loginname().c_str(), currentGame->roundID().c_str(), 
                                 gameUser->getBalance());
                    }
                }
            }
        }
        
        // 如果玩家不在游戏中，或游戏中没有有效的 User 对象，进行正常登录验证
        if (!finalUser) {
            auto loginResult = userService_->processLogin(request, sessionId);
            
            if (!loginResult.success) {
                response.set_code(loginResult.errorCode);
                response.set_message(loginResult.errorMessage);
                LOG_WARN("Login failed for %s: %s (code: %d)", 
                         request.loginname().c_str(), loginResult.errorMessage.c_str(), loginResult.errorCode);
                
                std::string responseData;
                if (response.SerializeToString(&responseData)) {
                    responseCallback_(sessionId, Protocol::SC_LOGIN_RES, responseData);
                    LOG_INFO("Sent login response to session %s, code: %d", sessionId.c_str(), response.code());
                }
                tcpServer_->getConnectionManager()->unregisterPlayerSession(sessionId);
                LOG_INFO("Removed basic player session for failed authentication: %s", sessionId.c_str());
                return;
            }
            
            finalUser = loginResult.user;
            finalUser->setClientIp(request.client_ip());
            LOG_INFO("Player %s authenticated from database (balance: %.2f)", 
                     request.loginname().c_str(), finalUser->getBalance());
        } else {
            // 即使使用游戏中的 User 对象，仍需验证 token
            auto loginResult = userService_->processLogin(request, sessionId);
            if (!loginResult.success) {
                response.set_code(loginResult.errorCode);
                response.set_message(loginResult.errorMessage);
                LOG_WARN("Token validation failed for %s: %s (code: %d)", 
                         request.loginname().c_str(), loginResult.errorMessage.c_str(), loginResult.errorCode);
                
                // 发送失败响应并清理会话
                std::string responseData;
                if (response.SerializeToString(&responseData)) {
                    responseCallback_(sessionId, Protocol::SC_LOGIN_RES, responseData);
                }
                tcpServer_->getConnectionManager()->unregisterPlayerSession(sessionId);
                return;
            }
        }
        
        response.set_code(ErrorCode::SUCCESS);
        response.set_message(ErrorCode::getErrorMessage(ErrorCode::SUCCESS));
        auto playerInfo = response.mutable_info();
        playerInfo->set_username(finalUser->getUserName());
        playerInfo->set_loginname(finalUser->getLoginName()); 
        playerInfo->set_nickname(finalUser->getNickName());
        playerInfo->set_avatar(finalUser->getAvatarUrl());
        playerInfo->set_balance(finalUser->getBalance()); 
        playerInfo->set_currency(finalUser->getCurrency());
            
        bool completed = tcpServer_->getConnectionManager()->completePlayerSession(
            sessionId, request.loginname(), finalUser);
            
        if (!completed) {
            LOG_ERROR("Failed to complete player session after successful authentication");
                response.set_code(ErrorCode::SYSTEM_ERROR);
                response.set_message("Failed to complete player session");
        } else {
            // 只有在使用数据库数据时才更新登录信息
            if (!userFromGame) {
                userService_->updateLastLogin(request.loginname(), request.client_ip());
                userService_->updateUserRedis(request.loginname());
            }
        }
        
        // 发送登录响应
        std::string responseData;
        if (response.SerializeToString(&responseData)) {
            responseCallback_(sessionId, Protocol::SC_LOGIN_RES, responseData);
            LOG_INFO("Sent login response to session %s, code: %d, balance: %.2f (from %s)", 
                     sessionId.c_str(), response.code(), finalUser->getBalance(),
                     userFromGame ? "game" : "database");
        } else {
            LOG_ERROR("Failed to serialize login response");
            return;
        }
        
        // 如果登录成功，处理游戏相关逻辑
        if (response.code() == ErrorCode::SUCCESS) {
            bool processed = handlePlayerAuthenticated(
                sessionId, request.loginname(), finalUser, request.game_type());
            
            if (!processed) {
                LOG_ERROR("Failed to process player authentication logic for %s", request.loginname().c_str());
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Exception in login handler: %s", e.what());
        
        proto::LoginResp response;
        response.set_code(ErrorCode::SYSTEM_ERROR);
        response.set_message(ErrorCode::getErrorMessage(ErrorCode::SYSTEM_ERROR));
        
        std::string responseData;
        if (response.SerializeToString(&responseData)) {
            responseCallback_(sessionId, Protocol::SC_LOGIN_RES, responseData);
        }
    }
}

uint32_t LoginHandler::getMsgId() const {
    return Protocol::CS_LOGIN_REQ;
}

bool LoginHandler::handlePlayerAuthenticated(const std::string& playerSessionId, 
                                         const std::string& loginname, std::shared_ptr<User> player, const std::string& gameType) {
    try {
        auto playerSession = tcpServer_->getConnectionManager()->getPlayerSession(playerSessionId);
        if (playerSession) {
            auto& container = getDependencyContainer();
            auto appContext = container.resolve<AppContext>();
            if (!appContext) {
                LOG_ERROR("AppContext not available in dependency container");
                return false;
            }
            
            auto gameManager = appContext->getGameManager();
            if (!gameManager) {
                LOG_ERROR("GameManager is null");
                return false;
            }
            
            // 检查玩家是否已在游戏中
            auto currentGame = gameManager->getPlayerGame(loginname);
            if (currentGame) {
                LOG_INFO("Player %s already in game %s, adding player session", 
                         loginname.c_str(), currentGame->roundID().c_str());
                
                // 将玩家会话添加到游戏中
                currentGame->addPlayer(playerSession);
                
                // 发送游戏快照
                sendGameSnapshot(playerSessionId, currentGame);
                
                return true;
            }
            
            // 如果没有指定游戏类型，不创建新游戏
            if (gameType.empty()) {
                LOG_INFO("No game type specified for player %s, skipping game creation", loginname.c_str());
                return true;
            }
            
            // 获取游戏注册表
            auto gameRegistry = gameManager->getGameRegistry();
            if (!gameRegistry) {
                LOG_ERROR("GameRegistry is null");
                return false;
            }
            
            // 获取指定类型的游戏工厂
            auto factory = gameRegistry->getFactory(gameType);
            if (!factory) {
                LOG_ERROR("Game factory not found for type: %s", gameType.c_str());
                return false;
            }
            
            // 获取游戏服务
            auto gameService = factory->getGameService();
            if (!gameService) {
                LOG_ERROR("Game service not available for type: %s", gameType.c_str());
                return false;
            }
            
            // 根据游戏工厂的创建模式处理
            auto creationMode = factory->getGameCreationMode();
            
            if (creationMode == GameFactory::GameCreationMode::ON_DEMAND) {
                // 按需创建模式：为每个玩家创建新游戏
                LOG_INFO("Creating new ON_DEMAND game for player %s (type: %s)", loginname.c_str(), gameType.c_str());
                
                auto newGame = gameService->createGame();
                if (!newGame) {
                    LOG_ERROR("Failed to create new game for player %s", loginname.c_str());
                    return false;
                }
                
                // 添加玩家到新游戏
                newGame->addPlayer(playerSession);
                
                // 将游戏添加到管理器
                gameManager->addGame(newGame);
                
                // 将玩家映射到游戏
                gameManager->addPlayerToGame(playerSession, newGame);
                
                // 发送游戏快照
                sendGameSnapshot(playerSessionId, newGame);
                
                LOG_INFO("Created new game %s for player %s", newGame->roundID().c_str(), loginname.c_str());
                
            } else if (creationMode == GameFactory::GameCreationMode::GLOBAL_SHARED) {
                // 全局共享模式：使用已有的全局游戏
                auto games = gameManager->getGamesByType(gameType);
                std::shared_ptr<IGame> globalGame;
                
                if (!games.empty()) {
                    // 使用第一个可用的全局游戏
                    globalGame = games[0];
                    LOG_INFO("Using existing global game %s for player %s", 
                             globalGame->roundID().c_str(), loginname.c_str());
                } else {
                    // 如果没有全局游戏，创建一个
                    LOG_INFO("No global game found, creating new GLOBAL_SHARED game for type: %s", gameType.c_str());
                    globalGame = gameService->createGame();
                    if (!globalGame) {
                        LOG_ERROR("Failed to create global game");
                        return false;
                    }
                    gameManager->addGame(globalGame);
                    LOG_INFO("Created new global game %s", globalGame->roundID().c_str());
                }
                
                // 添加玩家到全局游戏
                globalGame->addPlayer(playerSession);
                
                // 将玩家映射到游戏
                gameManager->addPlayerToGame(playerSession, globalGame);
                
                // 发送游戏快照
                sendGameSnapshot(playerSessionId, globalGame);
                
                LOG_INFO("Added player %s to global game %s", loginname.c_str(), globalGame->roundID().c_str());
            }
            
            return true;
        } else {
            LOG_ERROR("Player session %s not found", playerSessionId.c_str());
            return false;
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Exception in handlePlayerAuthenticated: %s", e.what());
        return false;
    }
}

void LoginHandler::sendGameSnapshot(const std::string& sessionId, std::shared_ptr<IGame> game) {
    try {
        if (!game) {
            LOG_ERROR("Cannot send snapshot for null game");
            return;
        }
        
        // 使用游戏的createSnapshot方法获取快照
        auto snapshot = game->createSnapshot();
        if (!snapshot) {
            LOG_ERROR("Failed to create game snapshot for game %s", game->roundID().c_str());
            return;
        }
        
        std::string responseData;
        if (snapshot->SerializeToString(&responseData)) {
            responseCallback_(sessionId, Protocol::SC_GAME_SNAPSHOT_NOTIFY, responseData);
            LOG_INFO("Sent game snapshot for game %s to session %s", game->roundID().c_str(), sessionId.c_str());
        } else {
            LOG_ERROR("Failed to serialize game snapshot");
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Exception in sendGameSnapshot: %s", e.what());
    }
} 