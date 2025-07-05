#ifndef GAME_REGISTRY_H
#define GAME_REGISTRY_H

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <functional>
#include <mutex>
#include "game_factory.h"
#include "core/interfaces/message_router.h"

// 游戏注册表 - 负责管理所有可用的游戏类型和对应的工厂
class GameRegistry {
public:
    // 游戏工厂变更回调已移除，配置更新现在直接处理

    GameRegistry();
    ~GameRegistry();
    
    // 初始化游戏注册表
    bool initialize();
    
    // 注册游戏类型
    bool registerGame(const std::string& gameType, std::shared_ptr<GameFactory> factory);
    
    // 取消注册游戏类型
    bool unregisterGame(const std::string& gameType);
    
    // 根据类型获取游戏工厂
    std::shared_ptr<GameFactory> getGameFactory(const std::string& gameType);
    
    // 获取所有已注册的游戏类型
    std::vector<std::string> getAllGameTypes() const;
    
    // 获取所有已注册的游戏工厂
    std::unordered_map<std::string, std::shared_ptr<GameFactory>> getAllGameFactories() const;
    
    // 向消息路由器注册消息处理器
    bool registerMessageHandlers(std::shared_ptr<MessageRouter> router);
    
    // 设置游戏类型启用状态
    bool setGameEnabled(const std::string& gameType, bool enabled);
    
    // 检查游戏类型是否启用
    bool isGameEnabled(const std::string& gameType) const;
    
    // 获取所有已启用的游戏类型
    std::vector<std::string> getEnabledGameTypes() const;
    
    // 启动所有已启用的游戏
    bool startAllGames();
    
    // 停止所有游戏
    void stopAllGames();
    
private:
    std::unordered_map<std::string, std::shared_ptr<GameFactory>> gameFactories_;
    mutable std::mutex gameFactoriesMutex_;  // 保护gameFactories_的互斥锁
    
    // 初始化内置游戏类型
    bool initializeBuiltInGames();
};

#endif // GAME_REGISTRY_H
