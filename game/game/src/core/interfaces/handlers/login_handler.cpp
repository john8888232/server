#include "login_handler.h"
#include <third_party/libuv_cpp/include/LogWriter.hpp>
#include <chrono>
#include <thread>
#include <ctime>
#include "core/infrastructure/proto/game.pb.h"
#include "core/infrastructure/common/error_code.h"
#include "core/infrastructure/network/protocol.h"
#include "core/infrastructure/proto/game.pb.h"
#include "core/application/services/user_service.h"
#include "core/infrastructure/common/app_context.h"
#include "core/domain/models/game_manager.h"
#include "core/domain/models/player_session.h"
#include "core/domain/models/user.h"
#include "src/games/game_registry.h"
#include <memory>

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
        
        // 检查玩家是否已在游戏中
        auto& appContext = AppContext::getInstance();
        auto gameManager = appContext.getGameManager();
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
                
                // 发送失败响应并清理会话
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
        
        // 构建成功响应
        response.set_code(ErrorCode::SUCCESS);
        response.set_message(ErrorCode::getErrorMessage(ErrorCode::SUCCESS));
        auto playerInfo = response.mutable_info();
        playerInfo->set_username(finalUser->getUserName());
        playerInfo->set_loginname(finalUser->getLoginName());  // 设置登录名
        playerInfo->set_nickname(finalUser->getNickName());
        playerInfo->set_avatar(finalUser->getAvatar());
        playerInfo->set_balance(finalUser->getBalance()); // 使用游戏中的余额或数据库余额
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
            
            // 检查玩家是否在游戏中，恢复游戏状态
            auto gameManager = AppContext::getInstance().getGameManager();
            auto currentGame = gameManager->getPlayerGame(loginname);
            if (currentGame) {
                // 重连逻辑：直接恢复到当前游戏
                LOG_INFO("Player %s reconnecting to existing game %s", 
                         loginname.c_str(), currentGame->roundID().c_str());
                
                // 更新GameManager映射关系
                gameManager->addPlayerToGame(playerSession, currentGame);
                
                // 重新激活玩家到当前游戏
                currentGame->addPlayer(playerSession);
                
                sendGameSnapshot(playerSessionId, currentGame);
                LOG_INFO("Player %s successfully reconnected to game %s", 
                         loginname.c_str(), currentGame->gameType().c_str());
            } else {
                if (!gameType.empty()) {
                    LOG_INFO("Player %s requested game of type %s", loginname.c_str(), gameType.c_str());
                    
                    auto gameRegistry = gameManager->getGameRegistry();
                    if (!gameRegistry) {
                        LOG_ERROR("Game registry not initialized");
                        return false;
                    }
                    auto factory = gameRegistry->getGameFactory(gameType);
                    if (!factory) {
                        LOG_ERROR("No factory found for game type: %s", gameType.c_str());
                        return false;
                    }
                    
                    auto creationMode = factory->getGameCreationMode();
                    std::shared_ptr<IGame> targetGame = nullptr;
                    
                    if (creationMode == GameFactory::GameCreationMode::GLOBAL_SHARED) {
                        // 全局共享模式
                        auto existingGames = gameManager->getGamesByType(gameType);
                        if (!existingGames.empty()) {
                            targetGame = existingGames[0];
                            LOG_INFO("Player %s joining existing global game %s of type %s", 
                                     loginname.c_str(), targetGame->roundID().c_str(), gameType.c_str());
                        } else {
                            LOG_ERROR("No global game instance found for type %s (should be created in factory start)", gameType.c_str());
                            return false;
                        }
                    } else if (creationMode == GameFactory::GameCreationMode::ON_DEMAND) {
                        // 按需创建模式
                        LOG_INFO("Creating new game instance of type %s for player %s", gameType.c_str(), loginname.c_str());
                        
                        auto gameService = factory->getGameService();
                        if (!gameService) {
                            LOG_ERROR("Failed to create game service for type: %s", gameType.c_str());
                            return false;
                        }
                        auto newGame = gameService->createGame();
                        if (!newGame) {
                            LOG_ERROR("Failed to create game instance for type: %s", gameType.c_str());
                            return false;
                        }
                        
                        gameManager->addGame(newGame);
                        targetGame = newGame;
                        
                        LOG_INFO("Created new game %s of type %s for player %s", 
                                 newGame->roundID().c_str(), gameType.c_str(), loginname.c_str());
                    }
                    if (targetGame) {
                        // 更新GameManager映射关系
                        gameManager->addPlayerToGame(playerSession, targetGame);
                        
                        // 添加玩家到目标游戏
                        targetGame->addPlayer(playerSession);
                        
                        sendGameSnapshot(playerSessionId, targetGame);
                        LOG_INFO("Player %s successfully joined game %s of type %s", 
                                 loginname.c_str(), targetGame->roundID().c_str(), gameType.c_str());
                    }
                } else {
                    LOG_INFO("Player %s did not request a specific game type", loginname.c_str());
                }
            }
            
            LOG_INFO("Player session %s fully registered with player object for %s", 
                     playerSessionId.c_str(), loginname.c_str());
            return true;
        }
        
        LOG_ERROR("Could not find player session after registration");
        return false;
    } catch (const std::exception& e) {
        LOG_ERROR("Exception in handlePlayerAuthenticated: %s", e.what());
        return false;
    }
}

void LoginHandler::sendGameSnapshot(const std::string& sessionId, std::shared_ptr<IGame> game) {
    if (!game) {
        LOG_ERROR("Cannot send snapshot for null game");
        return;
    }
    
    try {
        auto& appContext = AppContext::getInstance();
        auto gameManager = appContext.getGameManager();
        auto gameRegistry = gameManager->getGameRegistry();
        if (!gameRegistry) {
            LOG_ERROR("Game registry not initialized");
            return;
        }
        
        auto factory = gameRegistry->getGameFactory(game->gameType());
        if (!factory) {
            LOG_ERROR("No factory found for game type: %s", game->gameType().c_str());
            return;
        }
        
        auto gameService = factory->getGameService();
        if (!gameService) {
            LOG_ERROR("Failed to create game service for type: %s", game->gameType().c_str());
            return;
        }
        
        auto snapshot = game->createSnapshot();
        if (!snapshot) {
            LOG_ERROR("Failed to create game snapshot for game %s", game->roundID().c_str());
            return;
        }
        
        std::string responseData;
        if (snapshot->SerializeToString(&responseData)) {
            responseCallback_(sessionId, Protocol::SC_GAME_SNAPSHOT_NOTIFY, responseData);
            LOG_INFO("Sent game snapshot for game %s to session %s", 
                     game->roundID().c_str(), sessionId.c_str());
        } else {
            LOG_ERROR("Failed to serialize game snapshot");
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Exception in sendGameSnapshot: %s", e.what());
    }
} 