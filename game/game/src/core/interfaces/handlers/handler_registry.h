#ifndef HANDLER_REGISTRY_H
#define HANDLER_REGISTRY_H

#include <memory>
#include <string>
#include <functional>
#include "../message_router.h"
#include "../../infrastructure/network/protocol.h"

// 消息处理器注册器 - 负责注册和管理系统中的基础消息处理器
class HandlerRegistry {
public:
    // 响应回调函数类型
    using ResponseCallback = std::function<void(const std::string&, uint32_t, const std::string&)>;
    
    HandlerRegistry(std::shared_ptr<MessageRouter> messageRouter, ResponseCallback responseCallback);
    
    // 注册所有系统级消息处理器（心跳、系统状态等）
    bool registerSystemHandlers();
    
    // 注册所有用户相关的消息处理器（登录、注册、用户信息等）
    bool registerUserHandlers();
    
    // 注册所有游戏相关的基础消息处理器（游戏列表、房间管理等通用功能）
    bool registerGameBaseHandlers();
    
    // 注册所有游戏特定的消息处理器（由各游戏工厂提供）
    bool registerGameSpecificHandlers();
    

private:
    // 注册单个处理器的辅助方法
    template<typename HandlerType>
    bool registerHandler(uint32_t protocolId);
    
    std::shared_ptr<MessageRouter> messageRouter_;
    ResponseCallback responseCallback_;
};

#endif // HANDLER_REGISTRY_H 