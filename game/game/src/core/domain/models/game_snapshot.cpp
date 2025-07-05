#include "game_snapshot.h"
#include "core/domain/models/player_in_game.h"
#include "core/domain/models/player_session.h"
#include <third_party/libuv_cpp/include/LogWriter.hpp>

GameSnapshot::GameSnapshot(std::shared_ptr<IGame> game)
    : game_(game) {
}

std::shared_ptr<proto::GameSnapshotNotify> GameSnapshot::toProto() const {
    if (!game_) {
        LOG_ERROR("Cannot create snapshot for null game");
        return nullptr;
    }
    
    auto notify = std::make_shared<proto::GameSnapshotNotify>();
    
    // 设置基本游戏信息
    notify->set_roundid(game_->roundID());
    notify->set_gametype(game_->gameType());
    notify->set_status(static_cast<int>(game_->gameStatus()));
    
    // 计算游戏已进行时间（秒）
    auto startTimePoint = game_->startTime();
    auto now = std::chrono::system_clock::now();
    auto elapsedSeconds = std::chrono::duration_cast<std::chrono::seconds>(
        now - startTimePoint).count();
    notify->set_remaintime(elapsedSeconds);
    
    // 不再在GameSnapshotNotify中添加玩家信息
    // 玩家榜单信息通过单独的GameRankInfoNotify协议发送
    // addPlayersToProto(notify.get());
    
    // 添加游戏特定数据
    addGameDataToProto(notify.get());
    
    return notify;
}

void GameSnapshot::addPlayersToProto(proto::GameSnapshotNotify* notify) const {
    // 不再在GameSnapshotNotify中添加玩家信息
    // 玩家榜单信息通过单独的GameRankInfoNotify协议发送
    // 这个方法保留但不执行任何操作，以保持接口兼容性
}

void GameSnapshot::addGameDataToProto(proto::GameSnapshotNotify* notify) const {
    // 基类中不添加特定游戏数据，由子类实现
    // 可以在这里添加通用游戏数据
} 