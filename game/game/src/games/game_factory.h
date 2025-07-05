#ifndef GAME_FACTORY_H
#define GAME_FACTORY_H

#include <memory>
#include <string>
#include <unordered_map>
#include <third_party/nlohmann/json.hpp>
#include "core/interfaces/message_router.h"
#include "core/application/interfaces/i_game_service.h"

using json = nlohmann::json;

// 游戏工厂的抽象基类 - 负责创建特定类型游戏的服务和处理器
class GameFactory {
public:
    GameFactory();
    virtual ~GameFactory();
    
    // 获取游戏服务实例
    virtual std::shared_ptr<IGameService> getGameService() = 0;
    
    // 向消息路由器注册消息处理器
    virtual bool registerMessageHandlers(MessageRouter* router) = 0;
    
    // 获取游戏类型
    virtual std::string getGameType() const = 0;
    
    // 检查游戏是否启用
    virtual bool isEnabled() const;
    
    // 设置游戏启用状态
    virtual void setEnabled(bool enabled);
    
    // 设置游戏配置
    virtual void setConfig(const json& config);
    
    // 获取游戏配置
    const json& getConfig() const { return config_; }
    
    // 配置更新通知 - 子类可以重写此方法来处理配置更新
    virtual void onConfigUpdated(const json& oldConfig, const json& newConfig) {}
    
    // 启动游戏服务 - 在服务器启动时调用，用于初始化游戏相关的服务
    virtual bool start() = 0;
    
    // 停止游戏服务 - 在服务器关闭时调用，用于清理游戏相关的资源
    virtual void stop() = 0;
    
    // 游戏创建模式枚举
    enum class GameCreationMode {
        GLOBAL_SHARED,    // 全局共享模式：服务启动时创建，玩家共享同一个游戏实例 (如mines_pro)
        ON_DEMAND         // 按需创建模式：玩家进入时创建新的游戏实例 (如slots)
    };
    
    // 获取游戏创建模式
    virtual GameCreationMode getGameCreationMode() const = 0;
    
protected:
    bool enabled_;  // 游戏是否启用
    json config_;   // 游戏配置
    std::shared_ptr<IGameService> gameService_;  // 游戏服务实例
};

#endif // GAME_FACTORY_H
