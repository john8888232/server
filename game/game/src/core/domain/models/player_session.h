#ifndef PLAYER_SESSION_H
#define PLAYER_SESSION_H

#include <string>
#include <memory>
#include <chrono>
#include "core/domain/models/user.h"

class PlayerSession {
public:
    PlayerSession(const std::string& sessionId, const std::string& gwId, const std::string& name = "")
        : playerSessionId(sessionId), gatewayId(gwId), loginname(name),
          lastActiveTime(std::chrono::system_clock::now()), player(nullptr) {}

    std::string getSessionId() const { return playerSessionId; }
    std::string getGatewayId() const { return gatewayId; }
    std::string getLoginname() const { return loginname; }
    void setLoginname(const std::string& name) { loginname = name; }
    std::chrono::system_clock::time_point getLastActiveTime() const { return lastActiveTime; }
    void updateActiveTime() { lastActiveTime = std::chrono::system_clock::now(); }
    void setPlayer(std::shared_ptr<User> user) { player = user; }
    std::shared_ptr<User> getPlayer() const { return player; }

    // 检查会话是否已超时
    bool isExpired(int timeoutSeconds) const {
        auto now = std::chrono::system_clock::now();
        auto diff = std::chrono::duration_cast<std::chrono::seconds>(now - lastActiveTime).count();
        return diff > timeoutSeconds;
    }

private:
    std::string playerSessionId;     // 玩家会话ID
    std::string gatewayId;           // 所属Gateway连接ID
    std::string loginname;           // 登录名
    std::chrono::system_clock::time_point lastActiveTime;  // 最后活跃时间
    std::shared_ptr<User> player;    // 指向玩家对象的指针
};

#endif // PLAYER_SESSION_H 