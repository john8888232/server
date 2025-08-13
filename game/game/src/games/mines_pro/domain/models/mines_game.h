#ifndef MINES_GAME_H
#define MINES_GAME_H

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <shared_mutex>
#include <optional>
#include "mines_grid.h"
#include "core/domain/interfaces/i_game.h"
#include "core/infrastructure/protogen/game.pb.h"
#include "core/domain/repositories/i_user_balance_repository.h"
#include "core/infrastructure/common/app_context.h"
#include "core/domain/repositories/i_game_repository.h"
#include "third_party/libuv_cpp/include/Timer.hpp"
#include "third_party/libuv_cpp/include/EventLoop.hpp"
#include "third_party/nlohmann/json.hpp"
#include "games/mines_pro/application/dto/mines_bet_record.h"

using json = nlohmann::json;

// 前向声明
class MinesGame;
class MinesGameRepositoryImpl;

// 兑现模式枚举
enum class CashOutMode {
    IMMEDIATE_DB,    // 立即入库（Handler、断线）
    MEMORY_ONLY,     // 仅内存操作（自动兑现）
    BATCH_DB         // 批量入库（游戏结束时）
};

// 内存兑现记录结构
struct MemoryCashOutRecord {
    std::string loginname;
    int32_t playType;
    double payoutAmount;
    double multiplier;
    std::chrono::time_point<std::chrono::system_clock> timestamp;
};

// 兑现执行器类
class CashOutExecutor {
public:
    struct Result {
        bool success = false;
        double payoutAmount = 0.0;
        double multiplier = 0.0;
        proto::MinesCashRes response;
        std::optional<proto::ReckonRecord> reckonRecord;
    };

    explicit CashOutExecutor(std::shared_ptr<MinesGameRepositoryImpl> repository)
        : repository_(repository) {}

    // 执行兑现操作
    Result execute(std::shared_ptr<MinesGame> game, const std::string& round_id, const std::string& loginname, 
                  int32_t playType, CashOutMode mode);

private:
    std::shared_ptr<MinesGameRepositoryImpl> repository_;
};

class MinesGame : public IGame {
    friend class CashOutExecutor;  // 允许CashOutExecutor访问私有成员
public:
    MinesGame();
    ~MinesGame();
    
    //  IGame接口实现 
    bool inProgress() const override;
    bool start() override;  // 启动游戏，生成roundID，启动定时器
    void stop() override;   // 停止游戏，清理定时器
    std::shared_ptr<proto::GameSnapshotNotify> createSnapshot() override;
    void onConfigUpdated(const json& newConfig) override;  // 配置更新通知
    void handlePlayerDisconnect(const std::string& loginname) override;  // 处理玩家断线
    
    //  游戏初始化方法 
    void initializeWithConfig(const json& config);
    void applyConfigInternal(const json& config); // 内部配置应用方法
    
    //  网格操作方法 
    bool isTileRevealed(uint32_t index) const;
    int getRevealedTileCount() const;
    int getRevealedStarCount() const;
    std::shared_ptr<MinesGrid> getGrid() const { return grid_; }
    
    //  倍数计算方法 
    double getCurrentMultiplier() const;
    double getMultiplierForStarCount(int starCount) const;
    
    //  游戏配置访问方法 
    int getMinesCount() const { return minesCount_; }
    int getTotalTiles() const { return totalTiles_; }
    
    //  公平性验证方法 
    void setSeed(const std::string& seed) { seed_ = seed; }
    const std::string& getSeed() const { return seed_; }
    std::pair<std::string, std::string> generateResultHash() const;
    
    //  游戏结果方法 
    std::vector<proto::GameResult> getGameResults() const;
    
    //  榜单相关方法 
    void updatePlayerBet(const PlayerBriefInfo& playerInfo, RecordDetail&& betdetail);
    void updatePlayerCancelBet(const std::string& loginname, int32_t playType, double refundAmount);
    void updatePlayerCash(const std::string& loginname, ReckonRecord&& reckonRecord);
    void sortRankByBetAmount();  // 按下注金额排序
    void initializeRankInfo();   // 初始化榜单信息（设置roundId和gameType）
    proto::GameRankInfoNotify getRankInfoNotify() const;  // 线程安全版本，返回副本
    
    //  状态转换方法 
    void onStartJettonComplete();                   // START_JETTON阶段完成
    void onSettledComplete();                       // SETTLED阶段完成
    void enterSettledState();                       // 进入SETTLED状态
    void startNewRound();                           // 开始新一轮游戏
    
    //  广播方法 
    void broadcastStatusNotify(GameStatus status);
    void broadcastStartJettonNotify();             // 广播开始下注通知
    void broadcastStopJettonNotify();              // 广播停止下注通知
    void broadcastSnapshot();
    void broadcastRankInfo();
    
    //  自动翻牌方法 
    void onAutoRevealTick();                        // 每秒自动翻牌
    
    //  游戏业务处理方法 
    bool processCashOut(const std::string& loginname, const std::string& roundId, 
                       int32_t playType, proto::MinesCashRes& response);  // 兑现处理
    bool processCancelBet(const std::string& loginname, const std::string& requestRoundId, 
                         int32_t playType, proto::MinesCancelBetRes& response);  // 取消下注处理
    bool processPlaceBet(const std::string& loginname, const std::string& requestRoundId,
                        int32_t playType, double amount, proto::MinesPlaceBetRes& response);  // 下注处理
    
    // 验证下注请求（不实际扣款）
    bool validateBet(std::shared_ptr<PlayerInGame> playerInGame_inout, const std::string& requestRoundId,
        int32_t playType, double amount, proto::MinesPlaceBetRes& response);
    
    // 同步下注信息到游戏状态（在数据库扣款成功后调用）
    void syncPlayerBet(std::shared_ptr<PlayerInGame> playerInGame, int32_t playType, double amount);
    
    // 验证取消下注请求（不实际退款）
    bool validateCancelBet(std::shared_ptr<PlayerInGame> playerInGame_inout, const std::string& requestRoundId,
                           int32_t playType, proto::MinesCancelBetRes& response,
                           double& refundAmount_out);
                           
    // 同步取消下注信息到游戏状态
    void syncPlayerCancelBet(std::shared_ptr<PlayerInGame> playerInGame, int32_t playType, double refundAmount);
    
    // 验证兑现请求
    bool validateCash(std::shared_ptr<PlayerInGame> playerInGame_inout, 
                    const std::string& requestRoundId,
                    int32_t playType, proto::MinesCashRes& response,
                    double& payoutAmount_out, double& multiplier_out);
    
    // 同步兑现信息到游戏状态（在数据库派奖成功后调用）
    void syncPlayerCash(std::shared_ptr<PlayerInGame> playerInGame, int32_t playType, double payoutAmount, double multiplier);
    
    // 创建兑现执行器的工厂方法
    std::unique_ptr<CashOutExecutor> createCashOutExecutor(bool withDatabase = true);
    
    // 批量写入内存兑现记录到数据库
    void writeCashOutRecordsToDatabase();
    
    // 批量结算失败订单（雷爆炸时）
    void settleFailedOrders();
    
    bool processAutoCash(const std::string& loginname, const std::string& requestRoundId,
                        int32_t playType, bool enable, int32_t targetGrid, proto::MinesAutoCashRes& response);  // 自动兑现处理
    
    //  数据库操作方法 
    void writeGameResultToDatabase();
    
    //  玩家管理方法 
    void cleanupInactivePlayers();  // 清理inactive玩家

private:
    //  常量定义 
    static constexpr int WAIT_DURATION = 3;           // 写完数据库后等待 3秒,给前端的动画时间
    static constexpr int AUTO_REVEAL_INTERVAL = 1;    // 自动翻牌间隔 1秒
    static constexpr int MAX_RANK_DISPLAY_COUNT = 50; // 榜单最大显示数量
    
    //  游戏核心数据 
    std::shared_ptr<MinesGrid> grid_;  // 游戏网格
    std::string seed_;  // 公平性种子
    
    //  游戏配置变量 
    uint32_t minesCount_;  // 地雷数量
    uint32_t totalTiles_;  // 总格子数
    int32_t bettingTime_;  // 下注时间
    std::vector<std::pair<double, double>> tileConfigs_;  // 格子配置
    json gameConfig_;       // 当前生效的配置
    json pendingConfig_;    // 待生效的配置（下局开始前生效）
    bool hasPendingConfig_; // 是否有待生效的配置
    
    //  下注限制配置变量 
    double minBet_;    // 最小下注金额
    double maxBet_;    // 最大下注金额
    double maxWinPerRound_;  // 单局最大赔付金额
    
    //  定时器相关变量 
    std::shared_ptr<uv::Timer> unifiedTimer_;       // 统一定时器
    uv::EventLoop* eventLoop_;  // 事件循环
    int tickCounter_;                               // 秒计数器
    int stateTransitionTarget_;                     // 状态转换目标时间（-1表示无目标）
    GameStatus targetStatus_;                       // 目标状态
    
    //  榜单数据变量 
    MinesBetRecord roundPlayerBetRecord_;           // 下注派奖数据结构
    std::unordered_map<std::string, int> roundPlayerRecordIndex_;  // "loginname_playtype" -> index in roundPlayerRecord_.players()
    
    //  兑现数据变量
    std::vector<MemoryCashOutRecord> memoryCashOutRecords_;  // 内存兑现记录（用于批量入库）
    
    //  线程安全变量 
    mutable std::shared_mutex rankMutex_;           // 保护roundPlayerRecord_, roundPlayerRecordIndex_
    mutable std::shared_mutex gridMutex_;           // 保护grid_相关操作
    
    //  依赖注入变量 
    std::shared_ptr<IUserBalanceRepository> userBalanceRepository_;  // 用户余额服务
    std::shared_ptr<IGameRepository> gameRepository_;  // 游戏结果仓库
    std::shared_ptr<AppContext> appContext_;  // 应用上下文
    
    //  内部初始化方法 
    void initializeMinesGrid();
    void generateGameGrid();
    
    //  统一定时器相关方法 
    void createUnifiedTimer();                      // 创建统一定时器
    void cleanupUnifiedTimer();                     // 清理统一定时器
    void onUnifiedTick();                           // 统一的每秒回调
    void checkStateTransition();                    // 检查状态转换
    void handleBroadcasts();                        // 处理广播
    void handleAutoReveal();                        // 处理自动翻牌
    void setStateTransition(int seconds, GameStatus targetStatus); // 设置状态转换
    
    //  榜单内部方法 
    void rebuildPlayerPlayTypeToRankMapping();  // 重建playerPlayTypeToRankIndex映射
    
    //  自动兑现内部方法 
    void checkAndProcessAutoCash();  // 检查并处理自动兑现
    void resetAllPlayersCashOutStatus();  // 重置所有玩家的兑现状态
    std::optional<proto::ReckonRecord> executeCashOutInternal(const std::string& loginname, int32_t playType);  // 内部兑现方法（无锁保护）
    // 同步兑现信息到游戏状态的内部版本（需要调用者持有rankMutex_写锁）
    void syncPlayerCashInternal(std::shared_ptr<PlayerInGame> playerInGame, int32_t playType, double payoutAmount, double multiplier);
};

#endif // MINES_GAME_H
