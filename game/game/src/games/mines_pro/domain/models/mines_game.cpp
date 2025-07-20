#include "mines_game.h"
#include <third_party/libuv_cpp/include/LogWriter.hpp>
#include <third_party/nlohmann/json.hpp>
#include <cstdint>
#include <sstream>
#include <ctime>
#include <algorithm>
#include "core/infrastructure/common/app_context.h"
#include "core/infrastructure/network/protocol.h"
#include "core/infrastructure/common/utils.h"
#include "core/infrastructure/persistence/database_factory.h"
#include "games/game_def.h"
#include "core/infrastructure/common/error_code.h"
#include <mutex>
#include "core/infrastructure/common/dependency_container.h"

extern DependencyContainer& getDependencyContainer();

using json = nlohmann::json;

MinesGame::MinesGame() 
    : minesCount_(3), totalTiles_(24), eventLoop_(nullptr), 
      tickCounter_(0), stateTransitionTarget_(-1), targetStatus_(GameStatus::INIT),
      minBet_(10.00), maxBet_(8000.00), maxWinPerRound_(800000.00) {
    
    // 从依赖容器获取依赖
    auto& container = getDependencyContainer();
    appContext_ = container.resolve<AppContext>();
    dbFactory_ = container.resolve<DatabaseFactory>();
    
    if (!appContext_) {
        LOG_ERROR("Failed to resolve AppContext from dependency container");
    }
    
    if (!dbFactory_) {
        LOG_ERROR("Failed to resolve DatabaseFactory from dependency container");
    }
    
    grid_ = std::make_shared<MinesGrid>();
    setStatus(GameStatus::INIT);  // 使用封装的方法
    gameType_ = "mines_pro";  // 设置游戏类型
    
    if (appContext_) {
        eventLoop_ = appContext_->getEventLoop();
    }
    
    // 初始化用户余额服务，使用无参构造函数
    userBalanceService_ = std::make_shared<UserBalanceService>();
}

MinesGame::~MinesGame() {
    cleanupUnifiedTimer();
}

void MinesGame::initializeMinesGrid() {
    if (!grid_) {
        grid_ = std::make_shared<MinesGrid>();
    }
    
    std::unordered_map<uint32_t, std::pair<double, double>> tileConfig;
    for (uint32_t i = 1; i <= totalTiles_; ++i) {
        double multiplier = 0;
        double weight = 0;
        if (i <= tileConfigs_.size()) {
            multiplier = tileConfigs_[i - 1].first;
            weight = tileConfigs_[i - 1].second;
        }
        
        tileConfig[i] = {multiplier, weight};
    }
    
    grid_->initialize(totalTiles_, minesCount_, tileConfig);
}

bool MinesGame::inProgress() const {
    return true;
}

void MinesGame::onAutoRevealTick() {
    std::unique_lock<std::shared_mutex> gridLock(gridMutex_);
    
    GameStatus currentStatus = getStatus();
    if (currentStatus != GameStatus::STOP_JETTON) {
        return;
    }
    
    double multiplier = 0.0;
    bool hasMine = false;
    bool gameOver = false;
    
    if (!grid_->autoRevealNextTile(multiplier, hasMine, gameOver)) {
        LOG_ERROR("Failed to auto reveal next tile for game %s", roundID_.c_str());
        return;
    }
    
    if (hasMine) {
        gridLock.unlock();
        enterSettledState();
    } else {
        gridLock.unlock();
        checkAndProcessAutoCash();
    }
}

void MinesGame::broadcastStartJettonNotify() {
    if (!appContext_) {
        LOG_ERROR("AppContext is null");
        return;
    }
    
    auto tcpServer = appContext_->getTcpServer();
    if (!tcpServer) {
        LOG_ERROR("TcpServer not available");
        return;
    }
    
    // 创建开始下注通知消息
    proto::MinesStartJettonNotify notify;
    notify.set_roundid(roundID_);
    notify.set_gametype(gameType_);
    notify.set_duration(bettingTime_);
    
    // 序列化消息
    std::string messageData;
    if (!notify.SerializeToString(&messageData)) {
        LOG_ERROR("Failed to serialize MinesStartJettonNotify");
        return;
    }
    
    // 广播给游戏中的活跃玩家
    int broadcastCount = 0;
    
    std::shared_lock<std::shared_mutex> playersLock(playersMutex_);
    for (const auto& [loginname, playerInGame] : players_) {
        if (playerInGame && playerInGame->isActive()) {
            auto session = playerInGame->getSession().lock();
            if (session) {
                bool success = tcpServer->sendToPlayer(session->getSessionId(), Protocol::SC_MINES_START_JETTON_NOTIFY, messageData);
                if (success) {
                    broadcastCount++;
                } else {
                    LOG_WARN("Failed to send start jetton notification to player %s", loginname.c_str());
                }
            }
        }
    }
    
    LOG_INFO("Broadcasted start jetton notification to %d active players", broadcastCount);
}

void MinesGame::broadcastStopJettonNotify() {
    if (!appContext_) {
        LOG_ERROR("AppContext is null");
        return;
    }
    
    auto tcpServer = appContext_->getTcpServer();
    if (!tcpServer) {
        LOG_ERROR("TcpServer not available");
        return;
    }
    
    // 创建停止下注通知消息
    proto::GameStopJettonNotify notify;
    notify.set_roundid(roundID_);
    notify.set_gametype(gameType_);
    
    // 序列化消息
    std::string messageData;
    if (!notify.SerializeToString(&messageData)) {
        LOG_ERROR("Failed to serialize GameStopJettonNotify");
        return;
    }
    
    // 广播给游戏中的活跃玩家
    int broadcastCount = 0;
    
    std::shared_lock<std::shared_mutex> playersLock(playersMutex_);
    for (const auto& [loginname, playerInGame] : players_) {
        if (playerInGame && playerInGame->isActive()) {
            auto session = playerInGame->getSession().lock();
            if (session) {
                bool success = tcpServer->sendToPlayer(session->getSessionId(), Protocol::SC_GAME_STOP_JETTON_NOTIFY, messageData);
                if (success) {
                    broadcastCount++;
                } else {
                    LOG_WARN("Failed to send stop jetton notification to player %s", loginname.c_str());
                }
            }
        }
    }
    
    LOG_INFO("Broadcasted stop jetton notification to %d active players", broadcastCount);
}

bool MinesGame::isTileRevealed(uint32_t index) const {
    std::shared_lock<std::shared_mutex> lock(gridMutex_);
    return grid_->isTileRevealed(index);
}

int MinesGame::getRevealedTileCount() const {
    std::shared_lock<std::shared_mutex> lock(gridMutex_);
    return grid_->getRevealedCount();
}

int MinesGame::getRevealedStarCount() const {
    std::shared_lock<std::shared_mutex> lock(gridMutex_);
    return grid_->getRevealedStarCount();
}

double MinesGame::getCurrentMultiplier() const {
    std::shared_lock<std::shared_mutex> lock(gridMutex_);
    int revealedStars = grid_->getRevealedStarCount();
    return getMultiplierForStarCount(revealedStars);
}

double MinesGame::getMultiplierForStarCount(int starCount) const {
    if (starCount <= 0 || starCount > static_cast<int>(tileConfigs_.size())) {
        return 1.0;
    }
    return tileConfigs_[starCount - 1].first;  // 第1个星星对应数组索引0
}

std::string MinesGame::generateResultHash() const {
    std::shared_lock<std::shared_mutex> lock(gridMutex_);
    auto mineIndices = grid_->getMineIndices();
    
    std::string resultString;
    for (uint32_t i = 1; i <= static_cast<uint32_t>(totalTiles_); ++i) {
        bool isMine = std::find(mineIndices.begin(), mineIndices.end(), i) != mineIndices.end();
        resultString += isMine ? '1' : '0';
    }
    
    std::string combined = seed_ + resultString;
    std::hash<std::string> hasher;
    size_t hashValue = hasher(combined);
    
    std::stringstream ss;
    ss << std::hex << hashValue;
    return ss.str();
}

void MinesGame::generateGameGrid() {
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    seed_ = std::to_string(timestamp);
    
    std::unique_lock<std::shared_mutex> lock(gridMutex_);
    grid_->generateGrid(seed_);
}

std::vector<proto::GameResult> MinesGame::getGameResults() const {
    std::vector<proto::GameResult> results;
    
    std::shared_lock<std::shared_mutex> lock(gridMutex_);
    const auto& tiles = grid_->getAllTiles();
    for (const auto& tile : tiles) {
        proto::GameResult result;
        result.set_index(tile.index);
        result.set_multi(tile.multiplier);
        
        int32_t resultType = 1;
        
        GameStatus currentStatus = getStatus();
        if (currentStatus == GameStatus::START_JETTON) {
            resultType = 1;
        } else if (currentStatus == GameStatus::STOP_JETTON || currentStatus == GameStatus::SETTLED) {
            if (tile.status == TileStatus::REVEALED) {
                resultType = (tile.type == TileType::STAR) ? 2 : 3;
            } else {
                resultType = 1;
            }
        }
        
        result.set_result(resultType);
        results.push_back(result);
    }
    
    return results;
}

bool MinesGame::start() {
    GameStatus expected = GameStatus::INIT;
    if (!compareAndSwapStatus(expected, GameStatus::START_JETTON)) {
        LOG_ERROR("Cannot start game, invalid status: %d", (int)expected);
        return false;
    }
    
    if (!eventLoop_) {
        LOG_ERROR("EventLoop not available for game");
        setStatus(GameStatus::INIT);
        return false;
    }
    
    roundID_ = generateRoundId();
    generateGameGrid();
    startTime_ = std::chrono::system_clock::now();
    initializeRankInfo();
    
    broadcastStatusNotify(GameStatus::START_JETTON);
    broadcastStartJettonNotify(); // 添加专用的开始下注通知
    
    tickCounter_ = 0;
    createUnifiedTimer();
    setStateTransition(bettingTime_, GameStatus::STOP_JETTON);
    
    return true;
}

void MinesGame::stop() {
    LOG_INFO("Stopping game: %s", roundID_.c_str());
    cleanupUnifiedTimer();
    setStatus(GameStatus::INIT);
}

void MinesGame::onStartJettonComplete() {
    GameStatus expected = GameStatus::START_JETTON;
    if (compareAndSwapStatus(expected, GameStatus::STOP_JETTON)) {
        broadcastStatusNotify(GameStatus::STOP_JETTON);
        broadcastStopJettonNotify(); // 添加专用的停止下注通知
    } else {
        LOG_WARN("Failed to transition from START_JETTON to STOP_JETTON, current status: %d", 
                 (int)getStatus());
    }
}

void MinesGame::onSettledComplete() {
    startNewRound();
}

void MinesGame::enterSettledState() {
    GameStatus expected = GameStatus::STOP_JETTON;
    if (compareAndSwapStatus(expected, GameStatus::SETTLED)) {
        writeGameResultToDatabase();
        cleanupInactivePlayers();
        setStateTransition(WAIT_DURATION, GameStatus::START_JETTON);
    } else {
        LOG_WARN("Failed to transition to SETTLED state, current status: %d", 
                 (int)getStatus());
    }
}

void MinesGame::startNewRound() {
    roundID_ = generateRoundId();
    generateGameGrid();
    initializeRankInfo();
    {
        std::shared_lock<std::shared_mutex> playersLock(playersMutex_);
        resetAllPlayersCashOutStatus();
    }
    
    startTime_ = std::chrono::system_clock::now();
    setStatus(GameStatus::START_JETTON);
    broadcastStatusNotify(GameStatus::START_JETTON);
    broadcastStartJettonNotify(); // 添加专用的开始下注通知
    
    tickCounter_ = 0;
    createUnifiedTimer();
    setStateTransition(bettingTime_, GameStatus::STOP_JETTON);
}

void MinesGame::broadcastStatusNotify(GameStatus status) {
    if (!appContext_) {
        LOG_ERROR("AppContext is null");
        return;
    }
    
    auto tcpServer = appContext_->getTcpServer();
    if (!tcpServer) {
        LOG_ERROR("TcpServer not available");
        return;
    }
    
    // 创建快照通知消息
    proto::GameSnapshotNotify notify;
    notify.set_roundid(roundID_);
    notify.set_gametype(gameType_);
    notify.set_status(static_cast<int32_t>(status));
    
    // 添加游戏结果数据
    auto results = getGameResults();
    for (const auto& result : results) {
        auto* gameResult = notify.add_result();
        *gameResult = result;
    }
    
    // 序列化消息
    std::string messageData;
    if (!notify.SerializeToString(&messageData)) {
        LOG_ERROR("Failed to serialize game status notification");
        return;
    }
    
    // 广播给游戏中的活跃玩家
    int broadcastCount = 0;
    
    std::shared_lock<std::shared_mutex> playersLock(playersMutex_);
    for (const auto& [loginname, playerInGame] : players_) {
        if (playerInGame && playerInGame->isActive()) {
            auto session = playerInGame->getSession().lock();
            if (session) {
                bool success = tcpServer->sendToPlayer(session->getSessionId(), Protocol::SC_GAME_SNAPSHOT_NOTIFY, messageData);
                if (success) {
                    broadcastCount++;
                } else {
                    LOG_WARN("Failed to send status notification to player %s", loginname.c_str());
                }
            }
        }
    }
    
    LOG_INFO("Broadcasted game status %d to %d active players", static_cast<int>(status), broadcastCount);
}

void MinesGame::writeGameResultToDatabase() {
    try {
        auto gameResults = getGameResults();
        auto resultHash = generateResultHash();
        
        // 收集余额更新数据
        auto balanceUpdates = collectBalanceUpdates();
        
        // 批量更新余额
        if (!balanceUpdates.empty()) {
            // 使用用户余额服务执行批量更新
            auto results = userBalanceService_->updateBalancesBatch(balanceUpdates);
            
            // 处理更新结果
            processBalanceUpdateResults(results);
        } else {
            LOG_INFO("No players to update balance for round %s", roundID_.c_str());
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to write game result to database for round %s: %s", 
                 roundID_.c_str(), e.what());
    }
}

void MinesGame::initializeWithConfig(const json& config) {
    gameConfig_ = config;
    
    // 解析基本游戏规则
        if (config.contains("rules")) {
            const auto& rules = config["rules"];
        
        if (rules.contains("betting_time")) {
            bettingTime_ = rules["betting_time"].get<uint32_t>();
        }
            
            if (rules.contains("mines_count")) {
            minesCount_ = rules["mines_count"].get<uint32_t>();
            }
            
            if (rules.contains("grid_size")) {
            totalTiles_ = rules["grid_size"].get<uint32_t>();
            }
    }
    
    // 解析下注限制和最大赔付配置
    if (config.contains("betting")) {
        const auto& betting = config["betting"];
        
        if (betting.contains("min_bet")) {
            minBet_ = betting["min_bet"].get<double>();
        }
        
        if (betting.contains("max_bet")) {
            maxBet_ = betting["max_bet"].get<double>();
        }
        
        if (betting.contains("max_win_per_round")) {
            maxWinPerRound_ = betting["max_win_per_round"].get<double>();
            }
        }
        
    // 解析倍数配置
    tileConfigs_.clear();
        if (config.contains("multipliers")) {
            const auto& multipliers = config["multipliers"];
        
        for (uint32_t i = 1; i <= totalTiles_; ++i) {
                std::string key = std::to_string(i);
                if (multipliers.contains(key) && multipliers[key].is_array() && multipliers[key].size() >= 2) {
                double multi = multipliers[key][0].get<double>();
                    double weight = multipliers[key][1].get<double>();
                tileConfigs_.push_back(std::make_pair(multi, weight));
                } else {
                tileConfigs_.push_back(std::make_pair(0.0, 0.0));
            }
        }
    }
    
    // 初始化游戏网格
    initializeMinesGrid();
    
    LOG_INFO("Initialized mines game with config: minesCount=%d, totalTiles=%d, bettingTime=%d, minBet=%.2f, maxBet=%.2f, maxWinPerRound=%.2f",
             minesCount_, totalTiles_, bettingTime_, minBet_, maxBet_, maxWinPerRound_);
}

std::shared_ptr<proto::GameSnapshotNotify> MinesGame::createSnapshot() {
    LOG_DEBUG("Creating snapshot for MinesGame: %s", roundID_.c_str());
    
    // 创建游戏快照
    auto snapshot = std::make_shared<proto::GameSnapshotNotify>();
    
    // 读操作：gameStateMutex_(读) + gridMutex_(读) - 按锁顺序获取
    std::shared_lock<std::shared_mutex> gameStateLock(gameStateMutex_);
    std::shared_lock<std::shared_mutex> gridLock(gridMutex_);
    
    GameStatus currentStatus = getStatus();
    snapshot->set_roundid(roundID_);
    snapshot->set_gametype(gameType_);
    snapshot->set_status(static_cast<int32_t>(currentStatus));
    
    // 计算剩余时间（只有下注阶段有意义）
    int32_t remainTime = 0;
    if (currentStatus == GameStatus::START_JETTON) {
        auto now = std::chrono::system_clock::now();
        auto elapsedSeconds = std::chrono::duration_cast<std::chrono::seconds>(now - startTime_).count();
        remainTime = std::max(0, bettingTime_ - static_cast<int>(elapsedSeconds));
    }
    
    snapshot->set_remaintime(remainTime);
    
    // 设置当前游戏数据
    if (currentStatus == GameStatus::STOP_JETTON || currentStatus == GameStatus::SETTLED) {
        snapshot->set_curindex(getRevealedStarCount());
        snapshot->set_curmulti(getCurrentMultiplier());
    } else {
        // 其他状态下设置为默认值
        snapshot->set_curindex(0);
        snapshot->set_curmulti(0.0);
    }
    
    // 添加游戏结果 - 根据协议，状态1、2、3时有效
    if (currentStatus == GameStatus::START_JETTON || currentStatus == GameStatus::STOP_JETTON || currentStatus == GameStatus::SETTLED) {
        auto gameResults = getGameResults();
        for (const auto& result : gameResults) {
            auto protoResult = snapshot->add_result();
            *protoResult = result;
        }
    }
    
    LOG_DEBUG("Created snapshot for game %s: status=%d, remainTime=%ld, results=%d", 
             roundID_.c_str(), static_cast<int>(currentStatus), remainTime, 
             snapshot->result_size());
    
    return snapshot;
}

void MinesGame::onConfigUpdated(const json& newConfig) {
    LOG_INFO("MinesGame received config update for roundId: %s", roundID_.c_str());
    
    // 保存旧配置
    json oldConfig = gameConfig_;
    gameConfig_ = newConfig;
    
    // 解析新配置
    int newMineCount = minesCount_;
    int newTotalTiles = totalTiles_;
    int newBettingTime = bettingTime_;
    
    try {
        if (newConfig.contains("rules")) {
            const auto& rules = newConfig["rules"];
            
            if (rules.contains("mines_count")) {
                newMineCount = rules["mines_count"].get<int>();
            }
            
            if (rules.contains("grid_size")) {
                newTotalTiles = rules["grid_size"].get<int>();
            }
            
            if (rules.contains("betting_time")) {
                newBettingTime = rules["betting_time"].get<int>();
            }
        }
        
        // 更新倍数和权重表
        if (newConfig.contains("multipliers")) {
            const auto& multipliers = newConfig["multipliers"];
            std::vector<std::pair<double, double>> newTileConfigs;
            for (int i = 1; i <= newTotalTiles; ++i) {
                std::string key = std::to_string(i);
                if (multipliers.contains(key) && multipliers[key].is_array() && multipliers[key].size() >= 2) {
                    double multiplier = multipliers[key][0].get<double>();
                    double weight = multipliers[key][1].get<double>();
                    newTileConfigs.push_back({multiplier, weight});
                }
            }
            if (newTileConfigs.size() == newTotalTiles) {
                tileConfigs_ = newTileConfigs;
                LOG_INFO("Updated tile configurations (multipliers and weights)");
            }
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("Error parsing new game config: %s", e.what());
        return;
    }
    
    // 检查关键配置是否发生变化
    bool gridConfigChanged = (newMineCount != minesCount_) || (newTotalTiles != totalTiles_);
    
    // 更新配置参数
    minesCount_ = newMineCount;
    totalTiles_ = newTotalTiles;
    bettingTime_ = newBettingTime;
    
    // 如果网格配置发生变化，重新初始化网格
    if (gridConfigChanged) {
        initializeMinesGrid();
        LOG_INFO("Grid configuration updated: %d tiles, %d mines", totalTiles_, minesCount_);
    }
    
    LOG_INFO("MinesGame config update completed");
}


void MinesGame::initializeRankInfo() {
    roundPlayerRecord_.Clear();
    roundPlayerRecord_.set_roundid(roundID_);
    roundPlayerRecord_.set_gametype(gameType_);
    roundPlayerRecordIndex_.clear();
    LOG_DEBUG("Initialized rank info for game %s", roundID_.c_str());
}

void MinesGame::updatePlayerBet(const std::string& loginname, const proto::PlayerInfo& playerInfo, 
                               const proto::BetRecord& betRecord) {
    // 注意：此方法应在rankMutex_锁保护下调用
    
    // 创建(玩家,玩法)的唯一键
    std::string playerPlayTypeKey = loginname + "_" + std::to_string(betRecord.playtype());
    auto indexIt = roundPlayerRecordIndex_.find(playerPlayTypeKey);
    proto::PlayerInfoSnap* playerSnap = nullptr;
    
    if (indexIt == roundPlayerRecordIndex_.end()) {
        // 新的(玩家,玩法)组合，添加到榜单
        playerSnap = roundPlayerRecord_.add_players();
        *playerSnap->mutable_info() = playerInfo;
        
        // 添加单个玩法的下注记录
        auto bet = playerSnap->mutable_bets()->Add();
        bet->set_playtype(betRecord.playtype());
        bet->set_amount(betRecord.amount());
        
        roundPlayerRecordIndex_[playerPlayTypeKey] = roundPlayerRecord_.players_size() - 1;
    } else {
        // 现有(玩家,玩法)组合，更新信息
        playerSnap = roundPlayerRecord_.mutable_players(indexIt->second);
        *playerSnap->mutable_info() = playerInfo;  // 更新玩家基本信息（如余额）
    
        // 累计相同玩法的下注金额
    auto& bets = *playerSnap->mutable_bets();
        if (bets.size() > 0 && bets[0].playtype() == betRecord.playtype()) {
            bets[0].set_amount(bets[0].amount() + betRecord.amount());
        }
    }
    
    // 更新后重新排序
    sortRankByBetAmount();
    
    LOG_DEBUG("Updated player bet for loginname %s, playType=%d, amount=%.2f", 
              loginname.c_str(), betRecord.playtype(), betRecord.amount());
}

void MinesGame::updatePlayerCash(const std::string& loginname, const proto::ReckonRecord& reckonRecord) {
    // 注意：此方法应在rankMutex_锁保护下调用
    
    // 创建(玩家,玩法)的唯一键
    std::string playerPlayTypeKey = loginname + "_" + std::to_string(reckonRecord.playtype());
    auto indexIt = roundPlayerRecordIndex_.find(playerPlayTypeKey);
    if (indexIt == roundPlayerRecordIndex_.end()) {
        LOG_WARN("Player loginname %s playType %d not found in rank data for cash update", 
                 loginname.c_str(), reckonRecord.playtype());
        return;
    }
    
    auto playerSnap = roundPlayerRecord_.mutable_players(indexIt->second);
    
    // 添加兑奖记录
    auto& reckons = *playerSnap->mutable_reckons();
    auto newReckon = reckons.Add();
    *newReckon = reckonRecord;
    
    LOG_DEBUG("Updated player cash for loginname %s, playType=%d, amount=%.2f, multi=%.2f", 
              loginname.c_str(), reckonRecord.playtype(), reckonRecord.amount(), reckonRecord.multi());
}

void MinesGame::sortRankByBetAmount() {
    // 注意：此方法应在rankMutex_锁保护下调用
    
    // 创建索引和下注金额的映射
    std::vector<std::pair<double, int>> betAmountIndex;
    
    for (int i = 0; i < roundPlayerRecord_.players_size(); ++i) {
        const auto& player = roundPlayerRecord_.players(i);
        double betAmount = 0.0;
        
        // 每个玩家只有一个玩法的下注记录
        if (player.bets_size() > 0) {
            betAmount = player.bets(0).amount();
        }
        
        betAmountIndex.push_back({betAmount, i});
    }
    
    // 按下注金额从大到小排序
    std::sort(betAmountIndex.begin(), betAmountIndex.end(), 
              [](const std::pair<double, int>& a, const std::pair<double, int>& b) {
                  return a.first > b.first;  // 从大到小
              });
    
    // 创建新的排序后的玩家列表
    proto::GameRankInfoNotify sortedNotify;
    sortedNotify.set_roundid(roundPlayerRecord_.roundid());
    sortedNotify.set_gametype(roundPlayerRecord_.gametype());
    
    // 创建反向映射：oldIndex -> playerPlayTypeKey
    std::unordered_map<int, std::string> oldIndexToPlayerPlayTypeKey;
    for (const auto& playerPlayTypePair : roundPlayerRecordIndex_) {
        oldIndexToPlayerPlayTypeKey[playerPlayTypePair.second] = playerPlayTypePair.first;
    }
    
    // 重建playerPlayTypeToRankIndex映射
    roundPlayerRecordIndex_.clear();
    
    for (size_t newIndex = 0; newIndex < betAmountIndex.size(); ++newIndex) {
        int oldIndex = betAmountIndex[newIndex].second;
        auto playerSnap = sortedNotify.add_players();
        *playerSnap = roundPlayerRecord_.players(oldIndex);
        
        // 更新映射
        auto playerPlayTypeIt = oldIndexToPlayerPlayTypeKey.find(oldIndex);
        if (playerPlayTypeIt != oldIndexToPlayerPlayTypeKey.end()) {
            roundPlayerRecordIndex_[playerPlayTypeIt->second] = newIndex;
        }
    }
    
    // 替换原有的榜单数据
    roundPlayerRecord_ = std::move(sortedNotify);
    
    LOG_DEBUG("Sorted rank by bet amount for game %s, %d entries", 
              roundID_.c_str(), roundPlayerRecord_.players_size());
}

proto::GameRankInfoNotify MinesGame::getRankInfoNotify() const {
    std::shared_lock<std::shared_mutex> lock(rankMutex_);
    
    // 创建限制为前50个的副本
    proto::GameRankInfoNotify rankInfoCopy;
    rankInfoCopy.set_roundid(roundPlayerRecord_.roundid());
    rankInfoCopy.set_gametype(roundPlayerRecord_.gametype());
    
    // 限制玩家数量为前50个
    int playerCount = roundPlayerRecord_.players_size();
    int maxPlayers = std::min(playerCount, MAX_RANK_DISPLAY_COUNT);
    
    for (int i = 0; i < maxPlayers; ++i) {
        auto* newPlayer = rankInfoCopy.add_players();
        *newPlayer = roundPlayerRecord_.players(i);
    }
    
    return rankInfoCopy;  // 返回限制后的副本，线程安全
}

void MinesGame::broadcastRankInfo() {
    // 使用成员变量 appContext_
    if (!appContext_) {
        LOG_ERROR("AppContext is null");
        return;
    }
    
    // 获取TCP服务器
    auto tcpServer = appContext_->getTcpServer();
    if (!tcpServer) {
        LOG_ERROR("TcpServer not available");
        return;
    }
    
    // 获取排行榜数据
    proto::GameRankInfoNotify rankInfoCopy;
    {
        std::shared_lock<std::shared_mutex> lock(rankMutex_);
        rankInfoCopy = roundPlayerRecord_;
    }
    
    // 序列化消息
    std::string data;
    if (!rankInfoCopy.SerializeToString(&data)) {
        LOG_ERROR("Failed to serialize rank info");
        return;
    }
    
    // 广播给游戏中的活跃玩家
    int broadcastCount = 0;
    
    std::shared_lock<std::shared_mutex> playersLock(playersMutex_);
    for (const auto& [loginname, playerInGame] : players_) {
        if (playerInGame && playerInGame->isActive()) {
            auto session = playerInGame->getSession().lock();
            if (session) {
                // 发送榜单信息给玩家
                bool success = tcpServer->sendToPlayer(session->getSessionId(), Protocol::SC_GAME_RANK_INFO_NOTIFY, data);
                if (success) {
                    broadcastCount++;
                } else {
                    LOG_WARN("Failed to send rank info to player %s", loginname.c_str());
                }
            }
        }
    }
    if(broadcastCount > 0) {
        LOG_INFO("Broadcasted rank info to %d active players", broadcastCount);
    }
}

void MinesGame::broadcastSnapshot() {
    // 使用成员变量 appContext_
    if (!appContext_) {
        LOG_ERROR("AppContext is null");
        return;
    }
    
    // 获取TCP服务器
    auto tcpServer = appContext_->getTcpServer();
    if (!tcpServer) {
        LOG_ERROR("TcpServer not available");
        return;
    }
    
    // 创建游戏快照
    auto snapshot = createSnapshot();
    if (!snapshot) {
        LOG_ERROR("Failed to create game snapshot");
        return;
    }
    
    // 序列化消息
    std::string data;
    if (!snapshot->SerializeToString(&data)) {
        LOG_ERROR("Failed to serialize game snapshot");
        return;
    }
    
    // 广播给游戏中的活跃玩家
    int broadcastCount = 0;
    
    std::shared_lock<std::shared_mutex> playersLock(playersMutex_);
    for (const auto& [loginname, playerInGame] : players_) {
        if (playerInGame && playerInGame->isActive()) {
            auto session = playerInGame->getSession().lock();
            if (session) {
                // 发送快照信息给玩家
                bool success = tcpServer->sendToPlayer(session->getSessionId(), Protocol::SC_GAME_SNAPSHOT_NOTIFY, data);
                if (success) {
                    broadcastCount++;
                } else {
                    LOG_WARN("Failed to send snapshot to player %s", loginname.c_str());
                }
            }
        }
    }
    
    LOG_DEBUG("Broadcasted game snapshot to %d active players", broadcastCount);
}

void MinesGame::createUnifiedTimer() {
    if (!eventLoop_) {
        LOG_ERROR("EventLoop not available for unified timer creation");
        return;
    }
    
    cleanupUnifiedTimer();
    
    // 创建每秒重复的统一定时器
    unifiedTimer_ = std::make_shared<uv::Timer>(
        eventLoop_,
        1000,  // 1秒后开始
        1000,  // 每秒重复
        [this](uv::Timer* timer) {
            onUnifiedTick();
        }
    );
    
    try {
        unifiedTimer_->start();
        LOG_INFO("Created unified timer for game %s", roundID_.c_str());
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to start unified timer for game %s: %s", roundID_.c_str(), e.what());
        unifiedTimer_.reset();
    }
}

void MinesGame::cleanupUnifiedTimer() {
    if (unifiedTimer_) {
        auto timerToClose = unifiedTimer_;
        unifiedTimer_.reset(); 
        timerToClose->close([timerToClose](uv::Timer*) {
            LOG_DEBUG("Unified timer closed and released");
        });
    }
}

void MinesGame::onUnifiedTick() {
    utils::PerformanceTimer timer(false);
    timer.setTimeUnit(utils::PerformanceTimer::TimeUnit::MICROSECONDS);
    timer.start();
    
    tickCounter_++;
    
    // 检查状态转换
    checkStateTransition();
    
    // 处理自动翻牌（STOP_JETTON阶段）- 先翻牌
    GameStatus currentStatus = getStatus();
    if (currentStatus == GameStatus::STOP_JETTON) {
        handleAutoReveal();
    }
    
    // 处理广播 - 后广播（确保广播的是最新状态）
    handleBroadcasts();
    
    auto duration = timer.stop();
    LOG_DEBUG("Timer tick completed in %ld microseconds for game %s", 
              duration, roundID_.c_str());
}

void MinesGame::checkStateTransition() {
    if (stateTransitionTarget_ > 0 && tickCounter_ >= stateTransitionTarget_) {
        LOG_INFO("State transition triggered for game %s: %d -> %d", 
                 roundID_.c_str(), (int)getStatus(), (int)targetStatus_);
        
        // 重置状态转换
        stateTransitionTarget_ = -1;
        
        // 执行状态转换
        switch (targetStatus_) {
            case GameStatus::STOP_JETTON:
                onStartJettonComplete();
                break;
            case GameStatus::START_JETTON:
                onSettledComplete();
                break;
            default:
                LOG_ERROR("Unexpected target status: %d", (int)targetStatus_);
                break;
        }
    }
}

void MinesGame::handleBroadcasts() {
    // 在 START_JETTON 和 STOP_JETTON 阶段广播榜单
    GameStatus currentStatus = getStatus();
    if (currentStatus == GameStatus::START_JETTON || currentStatus == GameStatus::STOP_JETTON) {
        broadcastRankInfo();
    }
    
    // 在 START_JETTON 和 STOP_JETTON 阶段广播快照
    if (currentStatus == GameStatus::START_JETTON || currentStatus == GameStatus::STOP_JETTON) {
        broadcastSnapshot();
    }
}

void MinesGame::handleAutoReveal() {
    onAutoRevealTick();
}

void MinesGame::setStateTransition(int seconds, GameStatus targetStatus) {
    stateTransitionTarget_ = tickCounter_ + seconds;
    targetStatus_ = targetStatus;
    LOG_INFO("Set state transition for game %s: %d seconds to %d", 
             roundID_.c_str(), seconds, (int)targetStatus);
}

bool MinesGame::processCashOut(const std::string& loginname, const std::string& requestRoundId,
                               int32_t playType, proto::MinesCashRes& response) {
    // 写操作：playersMutex_(写) + rankMutex_(写) - 按锁顺序获取
    std::unique_lock<std::shared_mutex> playersLock(playersMutex_);
    std::unique_lock<std::shared_mutex> rankLock(rankMutex_);
    
    response.set_roundid(roundID_);
    response.set_playtype(playType);
    response.set_balance(0.0);
    
    GameStatus currentStatus = getStatus();
    if (currentStatus != GameStatus::STOP_JETTON) {
        response.set_code(ErrorCode::CASH_NOT_ALLOWED);
        response.set_message("Cash out only allowed during STOP_JETTON phase");
        return false;
    }
    
    if (!requestRoundId.empty() && requestRoundId != roundID_) {
        response.set_code(ErrorCode::INVALID_ROUND_ID);
        response.set_message("Invalid round ID");
        return false;
    }
    
    if (!MinesProPlayType::isValid(playType)) {
        response.set_code(ErrorCode::INVALID_PLAY_TYPE);
        response.set_message("Invalid play type");
        return false;
    }
    
    // 通过loginname查找玩家信息
    auto playerIt = players_.find(loginname);
    if (playerIt == players_.end()) {
        response.set_code(ErrorCode::PLAYER_NOT_FOUND);
        response.set_message("Player not found in game");
        return false;
    }
    
    auto playerInGame = playerIt->second;
    if (!playerInGame) {
        response.set_code(ErrorCode::PLAYER_NOT_FOUND);
        response.set_message("Player object not found");
        return false;
    }
    
    auto user = playerInGame->getUser();
    if (!user) {
        response.set_code(ErrorCode::PLAYER_NOT_FOUND);
        response.set_message("User object not found");
        return false;
    }
    
    // 获取sessionId用于榜单操作
    std::string sessionId;
    auto session = playerInGame->getSession().lock();
    if (session) {
        sessionId = session->getSessionId();
    }
    
    // 创建(玩家,玩法)的唯一键
    std::string playerPlayTypeKey = loginname + "_" + std::to_string(playType);
    auto indexIt = roundPlayerRecordIndex_.find(playerPlayTypeKey);
    if (indexIt == roundPlayerRecordIndex_.end()) {
        response.set_code(ErrorCode::PLAYER_NOT_FOUND);
        response.set_message("Player not found in rank data");
        return false;
    }
    
    int playerIndex = indexIt->second;
    if (playerIndex >= roundPlayerRecord_.players_size()) {
        response.set_code(ErrorCode::PLAYER_NOT_FOUND);
        response.set_message("Player index out of range");
        return false;
    }
    
    auto* player = roundPlayerRecord_.mutable_players(playerIndex);
    
    // 检查是否已经兑现过
    for (const auto& reckon : player->reckons()) {
        if (reckon.playtype() == playType) {
            response.set_code(ErrorCode::ALREADY_CASHED_OUT);
            response.set_message("Already cashed out for this play type");
            return false;
        }
    }
    
    // 检查是否已经兑现过（PlayerInGame检查）
    if (playerInGame->hasCashedOut(playType)) {
        response.set_code(ErrorCode::ALREADY_CASHED_OUT);
        response.set_message("Already cashed out for this play type");
        return false;
    }
    
    // 计算兑现金额 - 需要临时获取gridMutex_读锁
    double currentMultiplier;
    bool hasMine;
    {
        std::shared_lock<std::shared_mutex> gridLock(gridMutex_);
        int revealedStars = grid_->getRevealedStarCount();
        currentMultiplier = getMultiplierForStarCount(revealedStars);
        hasMine = grid_->hasRevealedMine();
    }
    
    if (hasMine) {
        currentMultiplier = 0.0;
    }
    
    double playTypeBetAmount = 0.0;
    if (player->bets_size() > 0) {
        playTypeBetAmount = player->bets(0).amount();
    }
    
    double payoutAmount = playTypeBetAmount * currentMultiplier;
    
    // 创建兑现记录
    proto::ReckonRecord reckon;
    reckon.set_playtype(playType);
    reckon.set_amount(payoutAmount);
    reckon.set_multi(currentMultiplier);
    
    // 更新玩家余额
    double newBalance = user->getBalance() + payoutAmount;
    user->setBalance(newBalance);
    
    // 更新榜单中的余额
    player->mutable_info()->set_balance(newBalance);
    
    // 使用updatePlayerCash方法更新兑现记录
    updatePlayerCash(loginname, reckon);
    
    // 标记为已兑现
    playerInGame->setCashedOut(playType, true);
    
    // 设置成功响应
    response.set_code(0);
    response.set_message("Cash out successful");
    *response.mutable_reckon() = reckon;
    response.set_balance(newBalance);
    
    LOG_INFO("Cash out successful for player %s: playType=%d, payout=%.2f, multi=%.2f, newBalance=%.2f", 
             loginname.c_str(), playType, payoutAmount, currentMultiplier, newBalance);
    
    return true;
}

bool MinesGame::processCancelBet(const std::string& loginname, const std::string& requestRoundId, 
                                int32_t playType, proto::MinesCancelBetRes& response) {
    // 写操作：playersMutex_(写) + rankMutex_(写) - 按锁顺序获取
    std::unique_lock<std::shared_mutex> playersLock(playersMutex_);
    std::unique_lock<std::shared_mutex> rankLock(rankMutex_);
    
    // 设置基本响应信息
    response.set_roundid(roundID_);
    response.set_playtype(playType);
    response.set_refundamount(0.0);
    response.set_balance(0.0);
    
    GameStatus currentStatus = getStatus();
    if (currentStatus != GameStatus::START_JETTON) {
        response.set_code(ErrorCode::BET_NOT_ALLOWED);
        response.set_message("Cancel bet only allowed during START_JETTON phase");
        return false;
    }
    
    if (!requestRoundId.empty() && requestRoundId != roundID_) {
        response.set_code(ErrorCode::INVALID_ROUND_ID);
        response.set_message("Invalid round ID");
        return false;
    }
    
    if (!MinesProPlayType::isValid(playType)) {
        response.set_code(ErrorCode::INVALID_PLAY_TYPE);
        response.set_message("Invalid play type");
        return false;
    }
    
    auto playerIt = players_.find(loginname);
    if (playerIt == players_.end()) {
        response.set_code(ErrorCode::PLAYER_NOT_FOUND);
        response.set_message("Player not found in game");
        return false;
    }
    
    auto playerInGame = playerIt->second;
    if (!playerInGame) {
        response.set_code(ErrorCode::PLAYER_NOT_FOUND);
        response.set_message("Player object not found");
        return false;
    }
    
    auto user = playerInGame->getUser();
    if (!user) {
        response.set_code(ErrorCode::PLAYER_NOT_FOUND);
        response.set_message("User object not found");
        return false;
    }
    
    // 获取sessionId用于榜单操作
    std::string sessionId;
    auto session = playerInGame->getSession().lock();
    if (session) {
        sessionId = session->getSessionId();
    }
    
    // 创建(玩家,玩法)的唯一键
    std::string playerPlayTypeKey = loginname + "_" + std::to_string(playType);
    auto indexIt = roundPlayerRecordIndex_.find(playerPlayTypeKey);
    if (indexIt == roundPlayerRecordIndex_.end()) {
        response.set_code(ErrorCode::PLAYER_NOT_FOUND);
        response.set_message("Player not found in rank data");
        return false;
    }
    
    int playerIndex = indexIt->second;
    if (playerIndex >= roundPlayerRecord_.players_size()) {
        response.set_code(ErrorCode::PLAYER_NOT_FOUND);
        response.set_message("Player index out of range");
        return false;
    }
    
    auto* player = roundPlayerRecord_.mutable_players(playerIndex);
    
    // 计算退款金额
    double totalRefund = 0.0;
    if (player->bets_size() > 0) {
        totalRefund = player->bets(0).amount();
    }
    
    if (totalRefund <= 0) {
        response.set_code(ErrorCode::NO_BET_TO_CANCEL);
        response.set_message("No bet to cancel");
        return false;
    }
    
    // 更新玩家余额
    double newBalance = user->getBalance() + totalRefund;
    user->setBalance(newBalance);
    
    // 更新榜单中的余额
    player->mutable_info()->set_balance(newBalance);
    
    // 使用updatePlayerCancelBet方法更新退款记录
    updatePlayerCancelBet(loginname, playType, totalRefund);
    
    // 设置玩家下注状态为false（取消下注后）
    playerInGame->setBetStatus(false);
    
    // 设置成功响应
    response.set_code(0);
    response.set_message("Cancel bet successful");
    response.set_refundamount(totalRefund);
    response.set_balance(newBalance);
    
    LOG_INFO("Cancel bet successful for player %s: playType=%d, refund=%.2f, newBalance=%.2f", 
             loginname.c_str(), playType, totalRefund, newBalance);
    
    return true;
}

bool MinesGame::processPlaceBet(const std::string& loginname, const std::string& requestRoundId,
                               int32_t playType, double amount, proto::MinesPlaceBetRes& response) {
    // 写操作：playersMutex_(写) + rankMutex_(写) - 按锁顺序获取
    std::unique_lock<std::shared_mutex> playersLock(playersMutex_);
    std::unique_lock<std::shared_mutex> rankLock(rankMutex_);
    
    // 设置基本响应信息
    response.set_roundid(roundID_);
    response.set_balance(0.0);
    
    // 1. 游戏状态检查
    GameStatus currentStatus = getStatus();
    if (currentStatus != GameStatus::START_JETTON) {
        response.set_code(ErrorCode::BET_NOT_ALLOWED);
        response.set_message("Bet not allowed in current game status");
        return false;
    }
    
    // 2. roundId验证
    if (!requestRoundId.empty() && requestRoundId != roundID_) {
        response.set_code(ErrorCode::INVALID_ROUND_ID);
        response.set_message("Invalid round ID");
        return false;
    }
    
    // 3. 玩法类型检查
    if (!MinesProPlayType::isValid(playType)) {
        response.set_code(ErrorCode::INVALID_PLAY_TYPE);
        response.set_message("Invalid play type");
        return false;
    }
    
    // 4. 下注金额验证
    if (amount <= 0) {
        response.set_code(ErrorCode::INVALID_BET_AMOUNT);
        response.set_message("Invalid bet amount");
        return false;
    }
    
    // 4.1 检查最小下注金额限制
    if (amount < minBet_) {
        response.set_code(ErrorCode::BET_AMOUNT_TOO_SMALL);
        response.set_message("Bet amount is below minimum limit of " + std::to_string(minBet_));
        return false;
    }
    
    // 4.2 检查最大下注金额限制
    if (amount > maxBet_) {
        response.set_code(ErrorCode::BET_AMOUNT_TOO_LARGE);
        response.set_message("Bet amount exceeds maximum limit of " + std::to_string(maxBet_));
        return false;
    }
    
    // 5. 通过loginname查找玩家信息
    auto playerIt = players_.find(loginname);
    if (playerIt == players_.end()) {
        response.set_code(ErrorCode::PLAYER_NOT_FOUND);
        response.set_message("Player not found in game");
        return false;
    }
    
    auto playerInGame = playerIt->second;
    if (!playerInGame) {
        response.set_code(ErrorCode::PLAYER_NOT_FOUND);
        response.set_message("Player object not found");
        return false;
    }
    
    auto user = playerInGame->getUser();
    if (!user) {
        response.set_code(ErrorCode::PLAYER_NOT_FOUND);
        response.set_message("User object not found");
        return false;
    }
    
    // 6. 余额检查
    double currentBalance = user->getBalance();
    if (currentBalance < amount) {
        response.set_code(ErrorCode::INSUFFICIENT_BALANCE);
        response.set_message("Insufficient balance");
        response.set_balance(currentBalance);
        return false;
    }
    
    // 7. 扣除玩家余额
    double newBalance = currentBalance - amount;
    user->setBalance(newBalance);
    
    // 8. 创建下注记录
    proto::BetRecord betRecord;
    betRecord.set_playtype(playType);
    betRecord.set_amount(amount);
    
    // 9. 创建玩家信息
    proto::PlayerInfo playerInfo;
    playerInfo.set_loginname(loginname);  // 设置登录名用于数据库操作
    playerInfo.set_username(user->getUserName()); 
    playerInfo.set_nickname(user->getNickName());
    playerInfo.set_avatar(user->getAvatar());
    playerInfo.set_vip(user->getVipLevel());
    playerInfo.set_balance(newBalance);
    playerInfo.set_currency(user->getCurrency());
    
    // 10. 获取sessionId用于榜单更新
    std::string sessionId;
    auto session = playerInGame->getSession().lock();
    if (session) {
        sessionId = session->getSessionId();
    }
    
    // 11. 更新游戏榜单信息
    updatePlayerBet(loginname, playerInfo, betRecord);
    
    // 12. 设置玩家下注状态
    playerInGame->setBetStatus(true);
    
    // 13. 设置成功响应
    response.set_code(0);
    response.set_message("Bet placed successfully");
    response.set_balance(newBalance);
    
    LOG_INFO("Bet placed successfully for player %s: playType=%d, amount=%.2f, newBalance=%.2f", 
             loginname.c_str(), playType, amount, newBalance);
    
    return true;
}

void MinesGame::cleanupInactivePlayers() {
    std::vector<std::string> playersToRemove;
    
    {
        std::shared_lock<std::shared_mutex> lock(playersMutex_);
        for (const auto& [loginname, playerInGame] : players_) {
            if (!playerInGame || !playerInGame->isActive()) {
                // 检查是否有下注
                bool hasBet = playerInGame && playerInGame->hasBet();
                if (!hasBet) {
                    playersToRemove.push_back(loginname);
                }
            }
        }
    }
    
    if (!playersToRemove.empty()) {
        LOG_INFO("Cleaning up %d inactive players without bets", playersToRemove.size());
        
        for (const auto& loginname : playersToRemove) {
            // 使用成员变量 appContext_
            if (!appContext_) {
                LOG_ERROR("AppContext is null");
                continue;
            }
            
            auto gameManager = appContext_->getGameManager();
            if (gameManager) {
                gameManager->removePlayerFromGame(loginname);
                LOG_DEBUG("Removed player %s mapping from GameManager", loginname.c_str());
            }
            
            {
                std::unique_lock<std::shared_mutex> lock(playersMutex_);
                players_.erase(loginname);
            }
            
            LOG_INFO("Removed inactive player %s from game", loginname.c_str());
        }
    }
}

void MinesGame::rebuildPlayerPlayTypeToRankMapping() {
    roundPlayerRecordIndex_.clear();
    
    // 遍历榜单中的每个条目，重建(玩家,玩法)到索引的映射
    for (int i = 0; i < roundPlayerRecord_.players_size(); ++i) {
        const auto& player = roundPlayerRecord_.players(i);
        const std::string& loginname = player.info().loginname();
        
        if (!loginname.empty() && player.bets_size() > 0) {
            int32_t playType = player.bets(0).playtype();
            std::string playerPlayTypeKey = loginname + "_" + std::to_string(playType);
            roundPlayerRecordIndex_[playerPlayTypeKey] = i;
            LOG_DEBUG("Rebuilt mapping: %s -> index %d", playerPlayTypeKey.c_str(), i);
        }
    }
    
    LOG_DEBUG("Rebuilt playerPlayType to rank mapping for game %s, %d entries", 
              roundID_.c_str(), (int)roundPlayerRecordIndex_.size());
}

void MinesGame::updatePlayerCancelBet(const std::string& loginname, int32_t playType, double refundAmount) {
    // 此方法应在rankMutex_锁保护下调用
    // 创建(玩家,玩法)的唯一键
    std::string playerPlayTypeKey = loginname + "_" + std::to_string(playType);
    auto indexIt = roundPlayerRecordIndex_.find(playerPlayTypeKey);
    if (indexIt == roundPlayerRecordIndex_.end()) {
        LOG_WARN("Player loginname %s playType %d not found in rank data for cancel bet", 
                 loginname.c_str(), playType);
        return;
    }
    
    int playerIndex = indexIt->second;
    if (playerIndex >= roundPlayerRecord_.players_size()) {
        LOG_ERROR("Player index %d out of range for cancel bet", playerIndex);
        return;
    }
    
    // 直接从榜单中移除这个(玩家,玩法)条目
    roundPlayerRecord_.mutable_players()->erase(
        roundPlayerRecord_.mutable_players()->begin() + playerIndex);
    
    // 重建映射
    rebuildPlayerPlayTypeToRankMapping();
    
    LOG_DEBUG("Removed player %s playType %d from rank", loginname.c_str(), playType);
    LOG_DEBUG("Cancel bet updated for loginname %s, playType=%d, refund=%.2f", 
              loginname.c_str(), playType, refundAmount);
}

// 自动兑现相关方法实现
bool MinesGame::processAutoCash(const std::string& loginname, const std::string& requestRoundId,
                               int32_t playType, bool enable, int32_t targetGrid, proto::MinesAutoCashRes& response) {
    // 写操作：playersMutex_(写) - 只需要修改玩家配置
    std::unique_lock<std::shared_mutex> playersLock(playersMutex_);
    
    // 设置基本响应信息
    response.set_roundid(roundID_);
    response.set_playtype(playType);
    response.set_enable(enable ? 1 : 0);
    response.set_grid(targetGrid);
    
    if (!requestRoundId.empty() && requestRoundId != roundID_) {
        response.set_code(ErrorCode::INVALID_ROUND_ID);
        response.set_message("Invalid round ID");
        return false;
    }
    
    // 2. 玩法类型检查
    if (!MinesProPlayType::isValid(playType)) {
        response.set_code(ErrorCode::INVALID_PLAY_TYPE);
        response.set_message("Invalid play type");
        return false;
    }
    
    // 3. 查找玩家
    auto playerIt = players_.find(loginname);
    if (playerIt == players_.end()) {
        response.set_code(ErrorCode::PLAYER_NOT_FOUND);
        response.set_message("Player not found in game");
        return false;
    }
    
    auto playerInGame = playerIt->second;
    if (!playerInGame) {
        response.set_code(ErrorCode::PLAYER_NOT_FOUND);
        response.set_message("Player object not found");
        return false;
    }
    
    // 4. 检查当前自动兑现状态
    bool currentEnabled = playerInGame->getAutoCashEnabled(playType);
    
    if (enable) {
        // 启用自动兑现
        if (currentEnabled) {
            response.set_code(ErrorCode::AUTO_CASH_ALREADY_ENABLED);
            response.set_message("Auto cash already enabled for this play type");
            return false;
        }
        
        // 验证目标格子
        if (targetGrid < 1 || targetGrid > 21) {
            response.set_code(ErrorCode::INVALID_REQUEST);
            response.set_message("Invalid target grid (must be 1-21)");
            return false;
        }
    } else {
        // 取消自动兑现
        if (!currentEnabled) {
            response.set_code(ErrorCode::AUTO_CASH_NOT_ENABLED);
            response.set_message("Auto cash not enabled for this play type");
            return false;
        }
        
        // 取消时设置默认目标格子为21
        targetGrid = 21;
        response.set_grid(21);
    }
    
    // 5. 设置自动兑现配置
    playerInGame->setAutoCashConfig(playType, enable, targetGrid);
    
    // 6. 设置成功响应
    response.set_code(0);
    response.set_message(enable ? "Auto cash enabled successfully" : "Auto cash disabled successfully");
    
    LOG_INFO("Auto cash config updated for player %s: playType=%d, enable=%s, targetGrid=%d", 
             loginname.c_str(), playType, enable ? "true" : "false", targetGrid);
    
    return true;
}

void MinesGame::checkAndProcessAutoCash() {
    // 处理自动兑现逻辑
    std::vector<std::string> playersToCashOut;
    
    {
        std::shared_lock<std::shared_mutex> playersLock(playersMutex_);
        for (const auto& [loginname, playerInGame] : players_) {
            if (playerInGame && playerInGame->isActive() && playerInGame->hasBet()) {
                // 检查每种玩法的自动兑现配置
                for (int playType = 1; playType <= 2; playType++) {
                    if (playerInGame->getAutoCashEnabled(playType)) {
                        int revealedStars = getRevealedStarCount();
                        int targetStars = playerInGame->getAutoCashTargetGrid(playType);
                        
                        if (revealedStars >= targetStars) {
                            // 记录需要自动兑现的玩家和玩法
                            playersToCashOut.push_back(loginname + ":" + std::to_string(playType));
                            
                            // 发送兑现响应给玩家
                            auto session = playerInGame->getSession().lock();
                            if (session) {
                                if (!appContext_) {
                                    LOG_ERROR("AppContext is null");
                                    continue;
                                }
                                
                                auto tcpServer = appContext_->getTcpServer();
                                if (tcpServer) {
                                    // 创建兑现响应
                                    proto::MinesCashRes cashResponse;
                                    cashResponse.set_code(ErrorCode::SUCCESS);
                                    cashResponse.set_message("Auto cash success");
                                    cashResponse.set_roundid(roundID_);
                                    cashResponse.set_playtype(playType);
                                    // 注意：set_multi 不存在，使用其他字段
                                    
                                    std::string data;
                                    cashResponse.SerializeToString(&data);
                                    tcpServer->sendToPlayer(session->getSessionId(), Protocol::SC_MINES_CASH_RES, data);
                                    LOG_INFO("Auto cash response sent to player %s for playType %d", loginname.c_str(), playType);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    
    // 执行自动兑现
    for (const auto& playerInfo : playersToCashOut) {
        // 解析玩家信息 "loginname:playType"
        size_t colonPos = playerInfo.find(':');
        if (colonPos == std::string::npos) {
            LOG_ERROR("Invalid player info format: %s", playerInfo.c_str());
            continue;
        }
        
        std::string loginname = playerInfo.substr(0, colonPos);
        int playType = std::stoi(playerInfo.substr(colonPos + 1));
        
        bool success = executeCashOutInternal(loginname, playType);
        if (success) {
            LOG_INFO("Auto cash executed successfully for player %s, playType %d", loginname.c_str(), playType);
        } else {
            LOG_ERROR("Failed to execute auto cash for player %s, playType %d", loginname.c_str(), playType);
        }
    }
}

void MinesGame::resetAllPlayersCashOutStatus() {
    // 注意：此方法应在playersMutex_锁保护下调用
    for (const auto& playerPair : players_) {
        const auto& playerInGame = playerPair.second;
        if (playerInGame) {
            playerInGame->resetCashOutStatus();
            playerInGame->resetBetStatus();  // 同时重置下注状态
        }
    }
    
    LOG_INFO("Reset cash out and bet status for all players in game %s", roundID_.c_str());
}

bool MinesGame::executeCashOutInternal(const std::string& loginname, int32_t playType) {

    auto playerIt = players_.find(loginname);
    if (playerIt == players_.end()) {
        LOG_ERROR("Player %s not found for internal cash out", loginname.c_str());
        return false;
    }
    
    auto playerInGame = playerIt->second;
    if (!playerInGame) {
        LOG_ERROR("Player object not found for internal cash out");
        return false;
    }
    
    auto user = playerInGame->getUser();
    if (!user) {
        LOG_ERROR("User object not found for internal cash out");
        return false;
    }
    
    // 检查是否已经兑现过
    if (playerInGame->hasCashedOut(playType)) {
        LOG_WARN("Player %s playType %d already cashed out", loginname.c_str(), playType);
        return false;
    }
    
    // 查找榜单条目
    std::string playerPlayTypeKey = loginname + "_" + std::to_string(playType);
    auto indexIt = roundPlayerRecordIndex_.find(playerPlayTypeKey);
    if (indexIt == roundPlayerRecordIndex_.end()) {
        LOG_ERROR("Player %s playType %d not found in rank data", loginname.c_str(), playType);
        return false;
    }
    
    int playerIndex = indexIt->second;
    if (playerIndex >= roundPlayerRecord_.players_size()) {
        LOG_ERROR("Player index %d out of range", playerIndex);
        return false;
    }
    
    auto* player = roundPlayerRecord_.mutable_players(playerIndex);
    
    // 检查是否已经兑现过（榜单检查）
    for (const auto& reckon : player->reckons()) {
        if (reckon.playtype() == playType) {
            LOG_WARN("Player %s playType %d already cashed out in rank data", loginname.c_str(), playType);
            return false;
        }
    }
    
    // 计算兑现金额 - 需要临时获取gridMutex_读锁
    double currentMultiplier;
    bool hasMine;
    {
        std::shared_lock<std::shared_mutex> gridLock(gridMutex_);
        int revealedStars = grid_->getRevealedStarCount();
        currentMultiplier = getMultiplierForStarCount(revealedStars);
        hasMine = grid_->hasRevealedMine();
    }
    
    if (hasMine) {
        currentMultiplier = 0.0;
    }
    
    double playTypeBetAmount = 0.0;
    if (player->bets_size() > 0) {
        playTypeBetAmount = player->bets(0).amount();
    }
    
    double payoutAmount = playTypeBetAmount * currentMultiplier;
    
    // 检查是否超过最大赔付限制
    if (payoutAmount > maxWinPerRound_) {
        LOG_WARN("Payout amount %.2f exceeds maximum win limit %.2f, capping at maximum", 
                payoutAmount, maxWinPerRound_);
        payoutAmount = maxWinPerRound_;
    }
    
    // 创建兑现记录
    proto::ReckonRecord reckon;
    reckon.set_playtype(playType);
    reckon.set_amount(payoutAmount);
    reckon.set_multi(currentMultiplier);
    
    // 更新玩家余额
    double newBalance = user->getBalance() + payoutAmount;
    user->setBalance(newBalance);
    
    // 更新榜单中的余额
    player->mutable_info()->set_balance(newBalance);
    
    // 使用updatePlayerCash方法更新兑现记录
    updatePlayerCash(loginname, reckon);
    
    LOG_INFO("Internal cash out successful for player %s: playType=%d, payout=%.2f, multi=%.2f, newBalance=%.2f", 
             loginname.c_str(), playType, payoutAmount, currentMultiplier, newBalance);
    
    return true;
}

void MinesGame::handlePlayerDisconnect(const std::string& loginname) {
    LOG_INFO("Handling player disconnect for %s", loginname.c_str());
    
    // 先获取玩家信息和游戏状态
    std::shared_ptr<PlayerInGame> playerInGame;
    GameStatus currentStatus;
    
    {
        std::shared_lock<std::shared_mutex> lock(playersMutex_);
        auto it = players_.find(loginname);
        if (it == players_.end()) {
            LOG_WARN("Player %s not found in game during disconnect handling", loginname.c_str());
            return;
        }
        playerInGame = it->second;
        currentStatus = getStatus();
    }
    
    // 检查是否需要执行断线兑现
    if (currentStatus == GameStatus::STOP_JETTON && playerInGame) {
        // 检查玩家是否有需要立即兑现的玩法
        for (int32_t playType = MinesProPlayType::LEFT; playType <= MinesProPlayType::RIGHT; ++playType) {
            if (!playerInGame->getAutoCashEnabled(playType) && !playerInGame->hasCashedOut(playType)) {
                // 检查是否有该玩法的下注
                std::string playerPlayTypeKey = loginname + "_" + std::to_string(playType);
                bool hasPlayTypeBet = false;
                {
                    std::shared_lock<std::shared_mutex> rankLock(rankMutex_);
                    auto indexIt = roundPlayerRecordIndex_.find(playerPlayTypeKey);
                    if (indexIt != roundPlayerRecordIndex_.end()) {
                        int playerIndex = indexIt->second;
                        if (playerIndex < roundPlayerRecord_.players_size()) {
                            const auto& player = roundPlayerRecord_.players(playerIndex);
                            if (player.bets_size() > 0 && player.bets(0).playtype() == playType) {
                                hasPlayTypeBet = true;
                            }
                        }
                    }
                }
                
                if (hasPlayTypeBet) {
                    // 执行立即兑现
                    LOG_INFO("Player %s disconnected in STOP_JETTON phase, executing immediate cash out for playType=%d", 
                             loginname.c_str(), playType);
                    
                    // 调用游戏的兑现逻辑
                    proto::MinesCashRes response;
                    bool success = processCashOut(loginname, roundID_, playType, response);
                    
                    if (success) {
                        LOG_INFO("Immediate cash out successful for disconnected player %s: playType=%d, payout=%.2f", 
                                 loginname.c_str(), playType, 
                                 response.has_reckon() ? response.reckon().amount() : 0.0);
                    } else {
                        LOG_WARN("Immediate cash out failed for disconnected player %s: playType=%d, reason=%s", 
                                 loginname.c_str(), playType, response.message().c_str());
                    }
                }
            }
        }
    }
    
    // 最后设置玩家为非活跃状态
    setPlayerInactive(loginname);
}
 
// 收集余额更新数据
std::vector<PlayerBalanceUpdate> MinesGame::collectBalanceUpdates() {
    std::vector<PlayerBalanceUpdate> updates;
    std::shared_lock<std::shared_mutex> rankLock(rankMutex_);
    
    LOG_INFO("Collecting balance updates for game %s", roundID_.c_str());
    
    for (const auto& player : roundPlayerRecord_.players()) {
        // 从player.info()获取基本信息
        const auto& playerInfo = player.info();
        
        // 尝试从玩家下注和派奖记录中获取信息
        double betAmount = 0.0;
        double profitAmount = 0.0;
        int32_t playType = 0;
        
        // 获取下注信息
        for (const auto& bet : player.bets()) {
            betAmount += bet.amount();
            playType = bet.playtype();
        }
        
        // 获取派奖信息
        for (const auto& reckon : player.reckons()) {
            profitAmount += reckon.amount();
        }
        
        // 跳过无效记录
        if (playType == 0 || betAmount <= 0.0) {
            continue;
        }
        
        // 处理所有下过注的玩家，不管是否有派奖
        PlayerBalanceUpdate update;
        update.loginName = playerInfo.loginname();
        
        // 正确计算原始余额: 当前余额 - 派奖金额 + 下注金额
        update.originalBalance = playerInfo.balance() - profitAmount + betAmount;
        update.newBalance = playerInfo.balance();
        update.reason = "mines_game_result_" + roundID_;
        
        updates.push_back(update);
        
        LOG_INFO("Player %s balance update: original=%.2f, new=%.2f, bet=%.2f, profit=%.2f", 
                 playerInfo.loginname().c_str(), 
                 update.originalBalance,
                 update.newBalance,
                 betAmount,
                 profitAmount);
    }
    
    LOG_INFO("Collected %lu balance updates", updates.size());
    return updates;
}

// 处理余额更新结果
void MinesGame::processBalanceUpdateResults(const std::vector<PlayerBalanceUpdateResult>& results) {
    for (const auto& result : results) {
        if (result.success) {
            LOG_INFO("Balance update successful for player %s: %.2f -> %.2f", 
                     result.loginName.c_str(), result.originalBalance, result.actualBalance);
            
            // 更新游戏中的玩家余额
            std::shared_lock<std::shared_mutex> playersLock(playersMutex_);
            auto playerIt = players_.find(result.loginName);
            if (playerIt != players_.end() && playerIt->second) {
                auto user = playerIt->second->getUser();
                if (user) {
                    user->setBalance(result.actualBalance);
                    LOG_DEBUG("Updated player %s balance in game to %.2f", 
                             result.loginName.c_str(), result.actualBalance);
                }
            }
        } else {
            LOG_ERROR("Balance update failed for player %s: %s", 
                     result.loginName.c_str(), result.errorMessage.c_str());
            
            // 处理余额更新失败的情况
            if (result.errorMessage.find("Original balance mismatch") != std::string::npos) {
                // 余额不匹配，需要刷新玩家余额
                // 使用成员变量 dbFactory_
                if (!dbFactory_) {
                    LOG_ERROR("DatabaseFactory is null");
                    continue;
                }
                
                auto mysqlClient = dbFactory_->getMySQLClient();
                if (mysqlClient) {
                    try {
                        std::string sql = "SELECT CAST(amount AS CHAR) FROM sys_player WHERE login_name = ?";
                        std::string amountStr;
                        
                        mysqlClient->queryWithCallback(sql, {result.loginName}, [&](const mysqlx::Row& row) {
                            amountStr = row[0].get<std::string>();
                        });
                        
                        if (!amountStr.empty()) {
                            double actualBalance = std::stod(amountStr);
                            
                            // 更新游戏中的玩家余额
                            std::shared_lock<std::shared_mutex> playersLock(playersMutex_);
                            auto playerIt = players_.find(result.loginName);
                            if (playerIt != players_.end() && playerIt->second) {
                                auto user = playerIt->second->getUser();
                                if (user) {
                                    user->setBalance(actualBalance);
                                    LOG_INFO("Updated player %s balance from database: %.2f", 
                                             result.loginName.c_str(), actualBalance);
                                }
                            }
                        }
                    } catch (const std::exception& e) {
                        LOG_ERROR("Exception while refreshing player balance: %s", e.what());
                    }
                }
            }
        }
    }
}
 