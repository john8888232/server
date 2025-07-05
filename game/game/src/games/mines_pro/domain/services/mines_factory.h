#ifndef MINES_FACTORY_H
#define MINES_FACTORY_H

#include "games/game_factory.h"

class MinesFactory : public GameFactory {
public:
    MinesFactory();
    virtual ~MinesFactory();
    
    // 获取游戏服务实例（作为成员变量）
    std::shared_ptr<IGameService> getGameService() override;
    
    // 向消息路由器注册消息处理器
    bool registerMessageHandlers(MessageRouter* router) override;
    
    // 获取游戏类型
    std::string getGameType() const override;
    
    // 启动游戏服务
    bool start() override;
    
    // 停止游戏服务
    void stop() override;
    
    // 获取游戏创建模式
    GameCreationMode getGameCreationMode() const override;
    
    // 配置更新通知
    void onConfigUpdated(const json& oldConfig, const json& newConfig) override;
    
private:
};

#endif // MINES_FACTORY_H 