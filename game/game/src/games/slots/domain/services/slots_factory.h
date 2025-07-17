#ifndef SLOTS_FACTORY_H
#define SLOTS_FACTORY_H

#include "games/game_factory.h"

class SlotsFactory : public GameFactory {
public:
    SlotsFactory();
    virtual ~SlotsFactory();
    
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
};

#endif // SLOTS_FACTORY_H