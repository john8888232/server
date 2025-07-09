#ifndef I_GAME_H
#define I_GAME_H

#include <string>
#include <memory>
#include <unordered_map>
#include <chrono>
#include <shared_mutex>
#include <atomic>
#include <third_party/nlohmann/json.hpp>
#include "core/domain/models/player_in_game.h"
#include "core/infrastructure/proto/game.pb.h"

using json = nlohmann::json;

// 前向声明
class PlayerSession;

// 游戏状态枚举
enum class GameStatus {
	INIT,
	START_JETTON,
	STOP_JETTON,
	SETTLED,
};

class IGame : public std::enable_shared_from_this<IGame> {

public:
    virtual ~IGame() = default;
    
    // 获取游戏ID
    virtual std::string roundID() const;
    
    // 获取游戏类型
    virtual std::string gameType() const;
    
    // 获取玩家集合 - 线程安全版本，返回副本
    virtual std::unordered_map<std::string, std::shared_ptr<PlayerInGame>> getPlayers() const;
    
    // 获取游戏状态 - 无锁原子操作
    virtual GameStatus getStatus() const { return status_.load(std::memory_order_acquire); }
    
    // 检查游戏是否在进行中
    virtual bool inProgress() const = 0;
    
    // 获取游戏开始时间
    virtual std::chrono::system_clock::time_point startTime() const;
    
    // 设置游戏局号
    virtual void setRoundID(const std::string& roundID);
    
    // 设置游戏类型
    virtual void setGameType(const std::string& gameType);
    
    // 设置游戏状态 - 无锁原子操作
    virtual void setStatus(GameStatus status);
    
    // 原子性比较并交换状态 - 用于状态转换的线程安全控制
    virtual bool compareAndSwapStatus(GameStatus expected, GameStatus desired);
    
    // 设置游戏开始时间
    virtual void setStartTime(const std::chrono::system_clock::time_point& startTime);
    
    // 启动游戏
    virtual bool start() = 0;
    
    // 停止游戏
    virtual void stop() = 0;
    
    // 生成游戏快照
    virtual std::shared_ptr<proto::GameSnapshotNotify> createSnapshot() = 0;
    
    // 配置更新通知
    virtual void onConfigUpdated(const json& newConfig) {}
    
    // 添加玩家到游戏
    virtual void addPlayer(std::shared_ptr<PlayerSession> session);
    
    // 移除玩家（真正从游戏中移除）
    virtual void removePlayer(const std::string& loginname);
    
    // 设置玩家为非活跃状态（用于断线，不移除玩家数据）
    virtual void setPlayerInactive(const std::string& loginname);
    
    // 处理玩家断线（包含断线时的特殊业务逻辑）
    virtual void handlePlayerDisconnect(const std::string& loginname);
    
    // 清空所有玩家
    virtual void clearPlayers();
    
    // 获取玩家数量
    virtual size_t getPlayerCount() const;
    
    // 检查玩家是否在游戏中
    virtual bool hasPlayer(const std::string& loginname) const;
    
    // 获取指定玩家
    virtual std::shared_ptr<PlayerInGame> getPlayer(const std::string& loginname) const;
    
    // 生成局号和订单号
    static std::string generateRoundId(const std::string& gameType);
    static std::string generateBetId();
    
protected:
    std::unordered_map<std::string, std::shared_ptr<PlayerInGame>> players_; // key - loginname
	std::string roundID_;
    std::string gameType_;
	std::atomic<GameStatus> status_{GameStatus::INIT};  // 原子状态，默认初始化为INIT
	std::chrono::system_clock::time_point startTime_;
    
    // 细粒度锁设计 - 按功能模块分离
    // 1. 游戏核心状态锁（细粒度）
    mutable std::shared_mutex gameStateMutex_;  // 保护roundID_, gameType_, startTime_
    
    // 2. 玩家管理锁
    mutable std::shared_mutex playersMutex_;    // 保护players_
    
    // 锁的获取顺序规则（防止死锁）：
    // 1. gameStateMutex_
    // 2. playersMutex_
    // 3. 子类特定锁（如rankMutex_, gridMutex_）
    // 4. PlayerInGame::playerMutex_
    
    // 锁的使用原则：
    // - 读操作使用共享锁（shared_lock）
    // - 写操作使用独占锁（unique_lock）
    // - 尽量缩小锁的作用域
    // - 避免在锁内调用可能获取其他锁的函数

private:
    // 辅助方法
    static int getGameTypeId(const std::string& gameType);
};

#endif // I_GAME_H
