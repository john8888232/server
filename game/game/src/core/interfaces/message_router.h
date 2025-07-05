#ifndef MESSAGE_ROUTER_H
#define MESSAGE_ROUTER_H

#include <functional>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <string>
#include <cstdint>
#include <vector>

// 定义消息处理器函数类型
using MessageHandler = std::function<void(const std::string&, const std::string&)>;

// 前向声明
class IMessageHandler;

class MessageRouter {
public:
    MessageRouter();
    ~MessageRouter();
    
    // 注册消息处理器函数
    void registerHandler(uint32_t msgId, MessageHandler handler);
    
    // 注销消息处理器函数
    void unregisterHandler(uint32_t msgId);
    
    // 注册消息处理器对象
    void registerHandlerObject(std::shared_ptr<IMessageHandler> handler);
    
    // 注销消息处理器对象
    void unregisterHandlerObject(uint32_t msgId);
    
    // 将消息路由到相应的处理器
    void routeMessage(const std::string& sessionId, uint32_t msgId, const std::string& data);
    
private:
    std::unordered_map<uint32_t, MessageHandler> handlers_;
    std::unordered_map<uint32_t, std::shared_ptr<IMessageHandler>> handlerObjects_;
    std::mutex handlersMutex_;
};

// 消息处理器接口
class IMessageHandler {
public:
    virtual ~IMessageHandler() = default;
    
    // 处理消息的接口方法
    virtual void handleMessage(const std::string& sessionId, const std::string& data) = 0;
    
    // 获取处理器支持的消息ID
    virtual uint32_t getMsgId() const = 0;
};

#endif // MESSAGE_ROUTER_H
