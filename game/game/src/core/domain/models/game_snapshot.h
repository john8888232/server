#ifndef GAME_SNAPSHOT_H
#define GAME_SNAPSHOT_H

#include <string>
#include <vector>
#include <memory>
#include <chrono>
#include "core/infrastructure/proto/game.pb.h"
#include "core/domain/interfaces/i_game.h"

// 游戏快照 - 用于保存游戏状态并传递给客户端
class GameSnapshot {
public:
    // 构造函数
    explicit GameSnapshot(std::shared_ptr<IGame> game);
    
    // 析构函数
    virtual ~GameSnapshot() = default;
    
    // 将快照转换为proto消息
    virtual std::shared_ptr<proto::GameSnapshotNotify> toProto() const;
    
    // 添加玩家信息到proto消息
    virtual void addPlayersToProto(proto::GameSnapshotNotify* notify) const;
    
    // 添加游戏特定数据到proto消息
    virtual void addGameDataToProto(proto::GameSnapshotNotify* notify) const;
    
    // 获取游戏实例
    std::shared_ptr<IGame> getGame() const { return game_; }
    
protected:
    std::shared_ptr<IGame> game_;  // 游戏实例
};

#endif // GAME_SNAPSHOT_H 