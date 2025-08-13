#ifndef GAME_REGISTRY_H
#define GAME_REGISTRY_H

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include "core/infrastructure/common/app_context.h"
#include "core/interfaces/message_router.h"

// 前向声明
class GameFactory;
class IGameService;

// 游戏注册表 - 管理所有支持的游戏类型
class GameRegistry {
public:
    GameRegistry();
    ~GameRegistry();
    
    // 初始化注册表
    bool initialize();
    
    // 注册游戏
    bool registerGame(const std::string& gameType, std::shared_ptr<GameFactory> factory);
    
    // 获取指定类型的游戏工厂
    std::shared_ptr<GameFactory> getFactory(const std::string& gameType) const;
    
    // 获取所有游戏工厂
    std::vector<std::pair<std::string, std::shared_ptr<GameFactory>>> getAllFactories() const;
    
    // 启动指定类型的游戏服务
    bool startGame(const std::string& gameType);
    
    // 启动所有游戏服务
    bool startAllGames();
    
    // 停止指定类型的游戏服务
    void stopGame(const std::string& gameType);
    
    // 停止所有游戏服务
    void stopAllGames();
    
    // 游戏配置更新通知
    void onGameConfigUpdated(const std::string& gameType);
    
    // 向消息路由器注册消息处理器
    bool registerMessageHandlers(std::shared_ptr<MessageRouter> router);
    
private:
    // 初始化内置游戏
    bool initializeBuiltInGames();
    
    // 游戏工厂映射表
    std::unordered_map<std::string, std::shared_ptr<GameFactory>> gameFactories_;
    mutable std::mutex gameFactoriesMutex_;
    
    // 应用上下文 - 通过依赖容器获取，不再需要存储
};

#endif // GAME_REGISTRY_H
