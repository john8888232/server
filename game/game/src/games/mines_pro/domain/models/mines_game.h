#ifndef MINES_GAME_H
#define MINES_GAME_H

#include <string>
#include <vector>
#include <chrono>
#include <memory>
#include "mines_grid.h"
#include "src/core/domain/interfaces/i_game.h"
#include "core/infrastructure/proto/game.pb.h"
#include "games/game_def.h"
#include <third_party/libuv_cpp/include/Timer.hpp>
#include <third_party/libuv_cpp/include/EventLoop.hpp>
#include <third_party/libuv_cpp/include/Async.hpp>
#include <third_party/nlohmann/json.hpp>
#include <unordered_map>
#include <mutex>

using json = nlohmann::json;

class MinesGame : public IGame {
public:
    MinesGame();
    ~MinesGame();

    // IGame接口实现
    bool inProgress() const override;
    bool start() override;  // 启动游戏，生成roundID，启动定时器
    void stop() override;   // 停止游戏，清理定时器
    std::shared_ptr<proto::GameSnapshotNotify> createSnapshot() override;
    
    // 配置更新通知
    void onConfigUpdated(const json& newConfig) override;
    
    // 初始化方法
    void initializeWithConfig(const json& config);
    
    // 网格操作
    bool isTileRevealed(uint32_t index) const;
    int getRevealedTileCount() const;
    int getRevealedStarCount() const;
    
    // 倍数计算
    double getCurrentMultiplier() const;
    double getMultiplierForStarCount(int starCount) const;
    
    // 游戏配置访问
    int getMinesCount() const { return minesCount_; }
    int getTotalTiles() const { return totalTiles_; }
    
    // 公平性验证
    void setSeed(const std::string& seed) { seed_ = seed; }
    const std::string& getSeed() const { return seed_; }
    std::string generateResultHash() const;
    
    // 网格访问
    std::shared_ptr<MinesGrid> getGrid() const { return grid_; }
    
    // 获取游戏结果数据（用于proto序列化）
    std::vector<proto::GameResult> getGameResults() const;
    
    // 榜单相关方法 - 简化版本
    void updatePlayerBet(const std::string& loginname, const proto::PlayerInfo& playerInfo, 
                        const proto::BetRecord& betRecord);
    void updatePlayerCancelBet(const std::string& loginname, int32_t playType, double refundAmount);
    void updatePlayerCash(const std::string& loginname, const proto::ReckonRecord& reckonRecord);
    void sortRankByBetAmount();  // 按下注金额排序
    void initializeRankInfo();   // 初始化榜单信息（设置roundId和gameType）
    void clearRankInfo();        // 清空榜单信息
    proto::GameRankInfoNotify getRankInfoNotify() const;  // 线程安全版本，返回副本
    
    // 状态转换方法
    void onStartJettonComplete();                   // START_JETTON阶段完成
    void onSettledComplete();                       // SETTLED阶段完成
    void enterSettledState();                       // 进入SETTLED状态
    void startNewRound();                           // 开始新一轮游戏
    
    // 自动翻牌方法
    void onAutoRevealTick();                        // 每秒自动翻牌
    
    // 兑现处理
    bool processCashOut(const std::string& loginname, const std::string& roundId, 
                       int32_t playType, proto::MinesCashRes& response);
    
    // 取消下注处理
    bool processCancelBet(const std::string& loginname, const std::string& requestRoundId, 
                         int32_t playType, proto::MinesCancelBetRes& response);
    
    // 下注处理
    bool processPlaceBet(const std::string& loginname, const std::string& requestRoundId,
                        int32_t playType, double amount, proto::MinesPlaceBetRes& response);
    
    // 自动兑现处理
    bool processAutoCash(const std::string& loginname, const std::string& requestRoundId,
                        int32_t playType, bool enable, int32_t targetGrid, proto::MinesAutoCashRes& response);
    
    // 数据库操作
    void writeGameResultToDatabase();
    
    // 清理inactive玩家
    void cleanupInactivePlayers();
    
private:
    // 游戏网格
    std::shared_ptr<MinesGrid> grid_;
    
    // 游戏配置
    uint32_t minesCount_;
    uint32_t totalTiles_;
    uint32_t bettingTime_;
    std::vector<std::pair<double, double>> tileConfigs_;  // 格子配置
    json gameConfig_;  // 存储完整的游戏配置
    
    // 公平性
    std::string seed_;
    
    // 定时器控制
    std::shared_ptr<uv::Timer> unifiedTimer_;       // 统一定时器
    uv::EventLoop* eventLoop_;
    
    // 定时器状态
    int tickCounter_;                               // 秒计数器
    int stateTransitionTarget_;                     // 状态转换目标时间（-1表示无目标）
    GameStatus targetStatus_;                       // 目标状态
    
    // 榜单数据结构 - 改为按(玩家,玩法)分开的榜单
    proto::GameRankInfoNotify rankInfoNotify_;
    std::unordered_map<std::string, int> playerPlayTypeToRankIndex_;  // "loginname_playtype" -> index in rankInfoNotify_.players()
    
    // 线程安全：使用继承自IGame的gameMutex_，保护游戏状态、玩家列表和榜单数据
    
    static constexpr int WAIT_DURATION = 3;          // 写完数据库后等待 3秒,给前端的动画时间
    static constexpr int AUTO_REVEAL_INTERVAL = 1;   // 自动翻牌间隔 1秒
    
    // 内部方法
    void initializeMinesGrid();
    void generateGameGrid();
    
    // 统一定时器相关方法
    void createUnifiedTimer();                      // 创建统一定时器
    void cleanupUnifiedTimer();                     // 清理统一定时器
    void onUnifiedTick();                           // 统一的每秒回调
    void checkStateTransition();                    // 检查状态转换
    void handleBroadcasts();                        // 处理广播
    void handleAutoReveal();                        // 处理自动翻牌
    void setStateTransition(int seconds, GameStatus targetStatus); // 设置状态转换
    
    void broadcastStatusNotify(GameStatus status);
    void broadcastRankInfo();                       // 广播榜单信息
    void broadcastSnapshot();                       // 广播快照信息
    
    // 重建playerPlayTypeToRankIndex映射
    void rebuildPlayerPlayTypeToRankMapping();
    
    // 自动兑现相关方法
    void checkAndProcessAutoCash();  // 检查并处理自动兑现
    void resetAllPlayersCashOutStatus();  // 重置所有玩家的兑现状态
    bool executeCashOutInternal(const std::string& loginname, int32_t playType);  // 内部兑现方法（无锁保护）
};

#endif // MINES_GAME_H
