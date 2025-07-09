#ifndef GAME_MANAGER_H
#define GAME_MANAGER_H

#include <memory>
#include <string>
#include <unordered_map>
#include <mutex>
#include <vector>
#include <chrono>
#include <functional>
#include "core/domain/interfaces/i_game.h"
#include "core/domain/models/player_session.h"

class GameRegistry;
class MessageRouter;

// 游戏管理器 - 负责管理所有游戏实例和玩家与游戏的映射关系
class GameManager {
public:
    // 游戏状态变更回调函数类型
    using GameStatusCallback = std::function<void(std::shared_ptr<IGame>, GameStatus)>;
    
    // 构造函数
    GameManager();
    ~GameManager();

    // 初始化游戏管理器
    bool initialize(const std::string& serverId, std::shared_ptr<MessageRouter> messageRouter);
    
    // 添加游戏到管理器
    bool addGame(std::shared_ptr<IGame> game);

    // 根据游戏局号查找游戏
    std::shared_ptr<IGame> findGameByRoundId(const std::string& roundId);

    // 移除游戏
    bool removeGame(std::shared_ptr<IGame> game);

    // 玩家加入游戏
    bool addPlayerToGame(std::shared_ptr<PlayerSession> player, std::shared_ptr<IGame> game);

    // 玩家离开游戏
    bool removePlayerFromGame(const std::string& loginname);

    // 获取玩家当前所在的游戏
    std::shared_ptr<IGame> getPlayerGame(const std::string& loginname);

    // 获取所有游戏
    std::vector<std::shared_ptr<IGame>> getAllGames();

    // 获取特定类型的所有游戏
    std::vector<std::shared_ptr<IGame>> getGamesByType(const std::string& gameType);

    // 清理已结束的游戏
    void cleanupFinishedGames();
    
    // 获取游戏注册表
    std::shared_ptr<GameRegistry> getGameRegistry() { return gameRegistry_; }
    
    // 获取服务器ID
    const std::string& getServerId() const { return serverId_; }
    
    // 注册游戏状态变更回调
    void registerGameStatusCallback(GameStatusCallback callback);
    
    // 触发游戏状态变更回调
    void onGameStatusChanged(std::shared_ptr<IGame> game, GameStatus newStatus);
    
    // 移除拷贝构造和赋值
    GameManager(const GameManager&) = delete;
    GameManager& operator=(const GameManager&) = delete;

private:

    std::mutex mutex_;
    std::vector<std::shared_ptr<IGame>> games_;  // 所有游戏列表
    std::unordered_map<std::string, std::shared_ptr<IGame>> playerToGame_;  // loginname -> Game
    std::shared_ptr<GameRegistry> gameRegistry_;
    std::string serverId_;
    
    // 游戏状态变更回调函数列表
    std::vector<GameStatusCallback> statusCallbacks_;
    std::mutex callbackMutex_;
};

#endif // GAME_MANAGER_H 