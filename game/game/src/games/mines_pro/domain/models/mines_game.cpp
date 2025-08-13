#include "mines_game.h"
#include "core/domain/interfaces/i_game.h"
#include "games/mines_pro/application/dto/mines_game_result.h"
#include "third_party/libuv_cpp/include/LogWriter.hpp"
#include "core/infrastructure/common/app_context.h"
#include "core/infrastructure/network/protocol.h"
#include "core/infrastructure/common/utils.h"
#include "games/game_def.h"
#include "core/infrastructure/common/error_code.h"
#include "core/infrastructure/common/dependency_container.h"
#include "core/infrastructure/repositories/user_balance_repository_impl.h"
#include "games/mines_pro/infrastructure/repositories/mines_game_repository_impl.h"
#include "core/infrastructure/common/memory_monitor.h"

extern DependencyContainer& getDependencyContainer();

using json = nlohmann::json;

MinesGame::MinesGame() 
    : minesCount_(3), totalTiles_(24), eventLoop_(nullptr), 
      tickCounter_(0), stateTransitionTarget_(-1), targetStatus_(GameStatus::INIT),
      minBet_(10.00), maxBet_(8000.00), maxWinPerRound_(800000.00), hasPendingConfig_(false) {
    
    auto& container = getDependencyContainer();
    appContext_ = container.resolve<AppContext>();
    
    if (!appContext_) {
        LOG_ERROR("AppContext is null in MinesGame constructor");
    }

    grid_ = std::make_shared<MinesGrid>();
    setStatus(GameStatus::INIT);  // 使用封装的方法
    gameType_ = "mines_pro";  // 设置游戏类型
    
    if (appContext_) {
        eventLoop_ = appContext_->getEventLoop();
    }
    
    // 初始化用户余额仓库
    userBalanceRepository_ = std::make_shared<UserBalanceRepositoryImpl>();
    gameRepository_ = std::make_shared<MinesGameRepositoryImpl>();
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
        broadcastSnapshot(); 
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
    notify.set_hash(generateResultHash().second);  // 添加hash值
    
    // 序列化消息
    std::string messageData;
    if (!notify.SerializeToString(&messageData)) {
        LOG_ERROR("Failed to serialize MinesStartJettonNotify");
        return;
    }
    
    std::vector<std::string> sessionIds;
    {
        std::shared_lock<std::shared_mutex> playersLock(playersMutex_);
        for (const auto& [loginname, playerInGame] : players_) {
            if (playerInGame && playerInGame->isActive()) {
                auto session = playerInGame->getSession().lock();
                if (session) {
                    sessionIds.push_back(session->getSessionId());
                }
            }
        }
    }
    
    int broadcastCount = 0;
    for (const auto& sessionId : sessionIds) {
        bool success = tcpServer->sendToPlayer(sessionId, Protocol::SC_MINES_START_JETTON_NOTIFY, messageData);
        if (success) {
            broadcastCount++;
        } else {
            LOG_WARN("Failed to send start jetton notification to session %s", sessionId.c_str());
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
    
    std::vector<std::string> sessionIds;
    {
        std::shared_lock<std::shared_mutex> playersLock(playersMutex_);
        for (const auto& [loginname, playerInGame] : players_) {
            if (playerInGame && playerInGame->isActive()) {
                auto session = playerInGame->getSession().lock();
                if (session) {
                    sessionIds.push_back(session->getSessionId());
                }
            }
        }
    }
    
    int broadcastCount = 0;
    for (const auto& sessionId : sessionIds) {
        bool success = tcpServer->sendToPlayer(sessionId, Protocol::SC_GAME_STOP_JETTON_NOTIFY, messageData);
        if (success) {
            broadcastCount++;
        } else {
            LOG_WARN("Failed to send stop jetton notification to session %s", sessionId.c_str());
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
    if (starCount <= 0 || starCount > tileConfigs_.size()) {
        return 0.0;
    }
    return tileConfigs_[starCount - 1].first;  // 第1个星星对应数组索引0
}

std::pair<std::string, std::string> MinesGame::generateResultHash() const {
    std::shared_lock<std::shared_mutex> lock(gridMutex_);
    auto mineIndices = grid_->getMineIndices();
    
    std::string resultString;
    for (uint32_t i = 1; i <= totalTiles_; ++i) {
        bool isMine = std::find(mineIndices.begin(), mineIndices.end(), i) != mineIndices.end();
        resultString += isMine ? '1' : '0';
    }
    
    std::string combined = seed_ + resultString;
    std::string hash = utils::sha256(combined);
    return {resultString, hash};
}

void MinesGame::generateGameGrid() {
    // 生成30位大小写字母+数字组合的seed
    const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, chars.size() - 1);
    
    seed_.clear();
    seed_.reserve(30);
    for (int i = 0; i < 30; ++i) {
        seed_ += chars[dis(gen)];
    }
    
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

    // 插入开局记录
    bool startGameResult = gameRepository_->startGame(roundID_);
    if (!startGameResult) {
        LOG_ERROR("Failed to start game due to database issues: %s. Game will continue without database record.", roundID_.c_str());
        // 继续游戏流程，不让数据库问题阻塞游戏运行
        // return false;  // 注释掉返回false，让游戏继续
    }
    
    broadcastStatusNotify(GameStatus::START_JETTON);
    broadcastStartJettonNotify(); 
    
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
        broadcastStopJettonNotify();
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
        try {
            // 启用数据库写入进行性能测试
            LOG_INFO("Starting database performance test for round %s", roundID_.c_str());
            writeGameResultToDatabase();
            LOG_INFO("Database performance test completed for round %s", roundID_.c_str());
            
            // 批量写入内存兑现记录到数据库
            writeCashOutRecordsToDatabase();
            
            // 雷爆炸导致的游戏结束，结算所有失败订单
            settleFailedOrders();
            
            cleanupInactivePlayers();
            setStateTransition(WAIT_DURATION, GameStatus::START_JETTON);
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to write game result to database, cannot proceed with payouts: %s", e.what());
            setStateTransition(WAIT_DURATION, GameStatus::START_JETTON);
        }
    } else {
        LOG_WARN("Failed to transition to SETTLED state, current status: %d", 
                 (int)getStatus());
    }
}

void MinesGame::startNewRound() {
    // 在开始新一局前应用待生效的配置
    if (hasPendingConfig_) {
        LOG_INFO("Applying pending config at start of round %s", roundID_.c_str());
        applyConfigInternal(pendingConfig_);
        hasPendingConfig_ = false;
        pendingConfig_ = json{}; // 清空待生效配置
    }
    
    roundID_ = generateRoundId();
    generateGameGrid();
    initializeRankInfo();
    
    {
        std::unique_lock<std::shared_mutex> playersLock(playersMutex_); 
        resetAllPlayersCashOutStatus();
    }
    
    startTime_ = std::chrono::system_clock::now();
    setStatus(GameStatus::START_JETTON);

    // 插入开局记录
    bool startGameResult = gameRepository_->startGame(roundID_);
    if (!startGameResult) {
        LOG_ERROR("Failed to start game due to database issues: %s. Game will continue without database record.", roundID_.c_str());
    }

    broadcastStatusNotify(GameStatus::START_JETTON);
    broadcastStartJettonNotify();
    
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
    utils::PerformanceTimer totalTimer("Total database write operation", utils::PerformanceTimer::TimeUnit::MILLISECONDS);
    try {
        // 先获取游戏结果数据
        auto gameResults = getGameResults();
        auto [resultString, resultHash] = generateResultHash();
        
        // 创建MinesGameResult对象并填充数据
        auto minesGameResult = std::make_shared<MinesGameResult>();
        minesGameResult->roundID = roundID_;
        minesGameResult->gameType = gameType_;
        minesGameResult->startTime = startTime_;
        minesGameResult->endTime = std::chrono::system_clock::now();
        minesGameResult->mineIndex = resultString;
        
        // 设置当前倍率
        minesGameResult->multiplier = getCurrentMultiplier();
        
        // 设置hash和seed
        minesGameResult->hash = resultHash;
        minesGameResult->seed = seed_;
        
        LOG_INFO("Saving game result to database for round %s: mines=%s, multiplier=%.2f, hash=%s, seed=%s", 
                 roundID_.c_str(), resultString.c_str(), minesGameResult->multiplier, 
                 resultHash.c_str(), seed_.c_str());
        
        // 保存游戏结果到数据库
        if (!gameRepository_) {
            LOG_ERROR("Game repository is null for round %s", roundID_.c_str());
            throw std::runtime_error("Game repository is null");
        }
        
        LOG_DEBUG("About to call gameRepository_->saveGame() for round %s", roundID_.c_str());
        LOG_DEBUG("Memory status before saveGame: allocated=%zu, peak=%zu", 
                  MemoryMonitor::getInstance().getCurrentAllocated(),
                  MemoryMonitor::getInstance().getPeakAllocated());
        
        bool saveSuccess = false;
        try {
            utils::PerformanceTimer saveTimer("Game result save to database", utils::PerformanceTimer::TimeUnit::MILLISECONDS);
            LOG_DEBUG("Calling gameRepository_->saveGame() now for round %s", roundID_.c_str());
            saveSuccess = gameRepository_->saveGame(minesGameResult);
            LOG_DEBUG("gameRepository_->saveGame() returned %s for round %s", 
                      saveSuccess ? "true" : "false", roundID_.c_str());
            auto elapsed = saveTimer.elapsed();
            LOG_INFO("Database saveGame took: %lld ms for round %s", elapsed, roundID_.c_str());
            
            if (elapsed > 1000) {
                LOG_WARN("Database save operation took longer than expected: %lld ms for round %s", 
                         elapsed, roundID_.c_str());
            }
        } catch (const std::exception& e) {
            LOG_ERROR("Exception during database save for round %s: %s", roundID_.c_str(), e.what());
            throw;
        } catch (...) {
            LOG_ERROR("Unknown exception during database save for round %s", roundID_.c_str());
            throw;
        }
        
        if (!saveSuccess) {
            LOG_ERROR("Failed to save game result to database for round %s", roundID_.c_str());
            throw std::runtime_error("Failed to save game result to database");
        }
        
        LOG_INFO("Game result saved successfully to database for round %s", roundID_.c_str());

        // 总耗时输出
        LOG_INFO("Total database operation took: %lld ms", totalTimer.elapsed());
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to write game result to database for round %s: %s", 
                 roundID_.c_str(), e.what());
        LOG_INFO("Failed database operation took: %lld ms", totalTimer.elapsed());
        throw; 
    }
}

void MinesGame::initializeWithConfig(const json& config) {
    // 使用统一的配置应用方法
    applyConfigInternal(config);
    
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
    
    // 设置hash值 - 状态1、2、3时有效
    if (currentStatus == GameStatus::START_JETTON || currentStatus == GameStatus::STOP_JETTON || currentStatus == GameStatus::SETTLED) {
        snapshot->set_hash(generateResultHash().second);
    }
    
    // 设置seed值 - 仅在状态3(SETTLED)时有效
    if (currentStatus == GameStatus::SETTLED) {
        snapshot->set_seed(seed_);
    }
    
    LOG_DEBUG("Created snapshot for game %s: status=%d, remainTime=%ld, results=%d", 
             roundID_.c_str(), currentStatus, remainTime, 
             snapshot->result_size());
    
    return snapshot;
}

void MinesGame::onConfigUpdated(const json& newConfig) {
    LOG_INFO("MinesGame received config update for roundId: %s, storing for next round", roundID_.c_str());
    
    // 将新配置存储为待生效配置，不立即应用
    pendingConfig_ = newConfig;
    hasPendingConfig_ = true;
    
    LOG_INFO("Config update cached, will take effect at start of next round");
}

// 内部方法：实际应用配置（从initializeWithConfig和延迟配置应用处调用）
void MinesGame::applyConfigInternal(const json& config) {
    json oldConfig = gameConfig_;
    auto oldtileConfigs = tileConfigs_;

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
    
    LOG_INFO("Applied mines game config: minesCount=%d, totalTiles=%d, bettingTime=%d, minBet=%.2f, maxBet=%.2f, maxWinPerRound=%.2f",
             minesCount_, totalTiles_, bettingTime_, minBet_, maxBet_, maxWinPerRound_);
}


void MinesGame::initializeRankInfo() {
    roundPlayerBetRecord_.reset();
    roundPlayerBetRecord_.roundID_ = roundID_;
    roundPlayerBetRecord_.gameType_ = gameType_;
    roundPlayerRecordIndex_.clear();
    LOG_DEBUG("Initialized rank info for game %s", roundID_.c_str());
}

void MinesGame::updatePlayerBet(const PlayerBriefInfo& playerInfo, RecordDetail&& betdetail) {
    // (玩家,玩法)
    std::string playerPlayTypeKey = playerInfo.loginname_ + "_" + std::to_string(betdetail.playType_);
    auto indexIt = roundPlayerRecordIndex_.find(playerPlayTypeKey);
    
    if (indexIt == roundPlayerRecordIndex_.end()) {
        // 添加
        PlayerRoundInfo newPlayerInfo;
        
        newPlayerInfo.info_.nickname_ = playerInfo.nickname_;
        newPlayerInfo.info_.avatar_ = playerInfo.avatar_;
        newPlayerInfo.info_.loginname_ = playerInfo.loginname_;
        newPlayerInfo.info_.player_id_ = playerInfo.player_id_;
        newPlayerInfo.info_.original_balance_ = playerInfo.original_balance_;
        newPlayerInfo.info_.client_ip_ = playerInfo.client_ip_;
        
        BetRecord bet;
        bet.playType_ = betdetail.playType_;
        bet.amount_ = betdetail.amount_;
        bet.bet_time_ = std::chrono::system_clock::now();
        
        bet.details_.push_back(std::move(betdetail));
        newPlayerInfo.bets_.push_back(std::move(bet));
        
        roundPlayerBetRecord_.players_.push_back(std::move(newPlayerInfo));
        roundPlayerRecordIndex_[playerPlayTypeKey] = roundPlayerBetRecord_.players_.size() - 1;
    } else {
        // 更新
        auto& playerBetInfo = roundPlayerBetRecord_.players_[indexIt->second];
        if (!playerBetInfo.bets_.empty()) {
            auto& bet = playerBetInfo.bets_[0];
            bet.details_.push_back(std::move(betdetail));
            bet.amount_ += betdetail.amount_;
            bet.bet_time_ = betdetail.time_;
        }
    }
    
    // 更新后重新排序
    sortRankByBetAmount();
    
    LOG_DEBUG("Updated player bet for loginname %s, playType=%d, amount=%.2f", 
              playerInfo.loginname_.c_str(), betdetail.playType_, betdetail.amount_);
}

void MinesGame::updatePlayerCash(const std::string& loginname, ReckonRecord&& reckonRecord) {
    // (玩家,玩法)的唯一键
    std::string playerPlayTypeKey = loginname + "_" + std::to_string(reckonRecord.playType_);
    auto indexIt = roundPlayerRecordIndex_.find(playerPlayTypeKey);
    if (indexIt == roundPlayerRecordIndex_.end()) {
        LOG_WARN("Player loginname %s playType %d not found in rank data for cash update", 
                 loginname.c_str(), reckonRecord.playType_);
        return;
    }
    auto& playerBetInfo = roundPlayerBetRecord_.players_[indexIt->second];
    playerBetInfo.reckons_.push_back(std::move(reckonRecord));
}

void MinesGame::sortRankByBetAmount() {
    // 创建索引和下注金额的映射
    std::vector<std::pair<double, int>> betAmountIndex;
    
    for (size_t i = 0; i < roundPlayerBetRecord_.players_.size(); ++i) {
        const auto& player = roundPlayerBetRecord_.players_[i];
        double betAmount = 0.0;
        
        for (const auto& bet : player.bets_) {
            betAmount += bet.amount_;
        }
        
        betAmountIndex.push_back({betAmount, i});
    }
    
    // 按下注金额从大到小排序
    std::sort(betAmountIndex.begin(), betAmountIndex.end(), 
              [](const std::pair<double, int>& a, const std::pair<double, int>& b) {
                  return a.first > b.first;  // 从大到小
              });
    
    // 创建新的排序后的玩家列表
    MinesBetRecord sortedRecord;
    sortedRecord.reset();
    sortedRecord.roundID_ = roundPlayerBetRecord_.roundID_;
    sortedRecord.gameType_ = roundPlayerBetRecord_.gameType_;
    
    // 创建反向映射：oldIndex -> playerPlayTypeKey
    std::unordered_map<int, std::string> oldIndexToPlayerPlayTypeKey;
    for (const auto& playerPlayTypePair : roundPlayerRecordIndex_) {
        oldIndexToPlayerPlayTypeKey[playerPlayTypePair.second] = playerPlayTypePair.first;
    }
    
    // 重建playerPlayTypeToRankIndex映射
    roundPlayerRecordIndex_.clear();
    
    for (size_t newIndex = 0; newIndex < betAmountIndex.size(); ++newIndex) {
        int oldIndex = betAmountIndex[newIndex].second;
        sortedRecord.players_.push_back(roundPlayerBetRecord_.players_[oldIndex]);
        
        // 更新映射
        auto playerPlayTypeIt = oldIndexToPlayerPlayTypeKey.find(oldIndex);
        if (playerPlayTypeIt != oldIndexToPlayerPlayTypeKey.end()) {
            roundPlayerRecordIndex_[playerPlayTypeIt->second] = newIndex;
        }
    }
    
    // 替换原有的榜单数据
    roundPlayerBetRecord_ = std::move(sortedRecord);
    
    LOG_DEBUG("Sorted rank by bet amount for game %s, %d entries", 
              roundID_.c_str(), roundPlayerBetRecord_.players_.size());
}

proto::GameRankInfoNotify MinesGame::getRankInfoNotify() const {
    std::shared_lock<std::shared_mutex> lock(rankMutex_);
    
    // 创建限制为前50个的副本
    proto::GameRankInfoNotify rankInfoCopy;
    rankInfoCopy.set_roundid(roundPlayerBetRecord_.roundID_);
    rankInfoCopy.set_gametype(roundPlayerBetRecord_.gameType_);
    
    // 限制玩家数量为前50个
    int playerCount = roundPlayerBetRecord_.players_.size();
    int maxPlayers = std::min(playerCount, MAX_RANK_DISPLAY_COUNT);
    
    for (int i = 0; i < maxPlayers; ++i) {
        const auto& oldPlayer = roundPlayerBetRecord_.players_[i];
        auto* newPlayer = rankInfoCopy.add_players();

        // 转换玩家基本信息
        auto* newInfo = newPlayer->mutable_info();
        newInfo->set_nickname(oldPlayer.info_.nickname_);
        newInfo->set_avatar(oldPlayer.info_.avatar_);
        
        // 转换下注记录
        for (const auto& bet : oldPlayer.bets_) {
            auto* newBet = newPlayer->add_bets();
            newBet->set_playtype(bet.playType_);
            newBet->set_amount(bet.amount_);
        }

        // 转换派奖记录
        for (const auto& reckon : oldPlayer.reckons_) {
            auto* newReckon = newPlayer->add_reckons();
            newReckon->set_playtype(reckon.playType_);
            newReckon->set_amount(reckon.amount_);
            newReckon->set_multi(reckon.multi_);
        }
    }
    
    return rankInfoCopy; 
}

void MinesGame::broadcastRankInfo() {
    if (!appContext_) {
        LOG_ERROR("AppContext is null");
        return;
    }
    
    auto tcpServer = appContext_->getTcpServer();
    if (!tcpServer) {
        LOG_ERROR("TcpServer not available");
        return;
    }
    
    // 获取排行榜数据
    proto::GameRankInfoNotify rankInfoCopy = getRankInfoNotify();
    
    // 序列化消息
    std::string data;
    if (!rankInfoCopy.SerializeToString(&data)) {
        LOG_ERROR("Failed to serialize rank info");
        return;
    }
    
    // 先收集需要广播的会话ID，然后释放锁
    std::vector<std::string> sessionIds;
    {
        std::shared_lock<std::shared_mutex> playersLock(playersMutex_);
        for (const auto& [loginname, playerInGame] : players_) {
            if (playerInGame && playerInGame->isActive()) {
                auto session = playerInGame->getSession().lock();
                if (session) {
                    sessionIds.push_back(session->getSessionId());
                }
            }
        }
    } 
    
    int broadcastCount = 0;
    for (const auto& sessionId : sessionIds) {
        bool success = tcpServer->sendToPlayer(sessionId, Protocol::SC_GAME_RANK_INFO_NOTIFY, data);
        if (success) {
            broadcastCount++;
        } else {
            LOG_WARN("Failed to send rank info to session %s", sessionId.c_str());
        }
    }
    
    if(broadcastCount > 0) {
        LOG_DEBUG("Broadcasted rank info to %d active players", broadcastCount);
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
    
    // 先收集需要广播的会话ID，然后释放锁
    std::vector<std::string> sessionIds;
    {
        std::shared_lock<std::shared_mutex> playersLock(playersMutex_);
        for (const auto& [loginname, playerInGame] : players_) {
            if (playerInGame && playerInGame->isActive()) {
                auto session = playerInGame->getSession().lock();
                if (session) {
                    sessionIds.push_back(session->getSessionId());
                }
            }
        }
    } 
    
    int broadcastCount = 0;
    for (const auto& sessionId : sessionIds) {
        bool success = tcpServer->sendToPlayer(sessionId, Protocol::SC_GAME_SNAPSHOT_NOTIFY, data);
        if (success) {
            broadcastCount++;
        } else {
            LOG_WARN("Failed to send snapshot to session %s", sessionId.c_str());
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
        1000,
        1000, 
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
        unifiedTimer_->close([](uv::Timer*) {
            LOG_DEBUG("Unified timer closed and released");
        });
        unifiedTimer_.reset(); 
    }
}

void MinesGame::onUnifiedTick() {
    LOG_DEBUG("Timer tick started for game %s, counter: %d", roundID_.c_str(), tickCounter_);
    tickCounter_++;
    LOG_DEBUG("Checking state transition for game %s", roundID_.c_str());
    // 检查状态转换
    checkStateTransition();
    
    // 处理自动翻牌（STOP_JETTON阶段）- 先翻牌
    GameStatus currentStatus = getStatus();
    LOG_DEBUG("Current status: %d for game %s", (int)currentStatus, roundID_.c_str());
    if (currentStatus == GameStatus::STOP_JETTON) {
        LOG_DEBUG("Handling auto reveal for game %s", roundID_.c_str());
        handleAutoReveal();
    }
    
    LOG_DEBUG("Handling broadcasts for game %s", roundID_.c_str());
    // 处理广播 - 后广播（确保广播的是最新状态）
    handleBroadcasts();
}

void MinesGame::checkStateTransition() {
    LOG_DEBUG("Checking transition: target=%d, counter=%d for game %s", 
              stateTransitionTarget_, tickCounter_, roundID_.c_str());
              
    if (stateTransitionTarget_ > 0 && tickCounter_ >= stateTransitionTarget_) {
        LOG_INFO("State transition triggered for game %s: %d -> %d", 
                 roundID_.c_str(), (int)getStatus(), (int)targetStatus_);
        
        // 重置状态转换
        stateTransitionTarget_ = -1;
        
        // 执行状态转换
        switch (targetStatus_) {
            case GameStatus::STOP_JETTON:
                LOG_INFO("Transitioning to STOP_JETTON for game %s", roundID_.c_str());
                onStartJettonComplete();
                break;
            case GameStatus::START_JETTON:
                LOG_INFO("Transitioning to START_JETTON for game %s", roundID_.c_str());
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

    std::string playerPlayTypeKey = loginname + "_" + std::to_string(playType);
    auto indexIt = roundPlayerRecordIndex_.find(playerPlayTypeKey);
    if (indexIt == roundPlayerRecordIndex_.end()) {
        response.set_code(ErrorCode::PLAYER_NOT_FOUND);
        response.set_message("Player not found in rank data");
        return false;
    }
    
    int playerIndex = indexIt->second;
    if (playerIndex >= roundPlayerBetRecord_.players_.size()) {
        response.set_code(ErrorCode::PLAYER_NOT_FOUND);
        response.set_message("Player index out of range");
        return false;
    }
    
    auto& player = roundPlayerBetRecord_.players_[playerIndex];
    
    // 检查是否已经兑现过
    for (const auto& reckon : player.reckons_) {
        if (reckon.playType_ == playType) {
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
    if (!player.bets_.empty()) {
        playTypeBetAmount = player.bets_[0].amount_;
    }
    
    double payoutAmount = playTypeBetAmount * currentMultiplier;
    // 检查是否超过最大赔付限制
    if (payoutAmount > maxWinPerRound_) {
        LOG_WARN("Payout amount %.2f exceeds maximum win limit %.2f, capping at maximum", 
                payoutAmount, maxWinPerRound_);
        payoutAmount = maxWinPerRound_;
    }
    // 创建兑现记录
    ReckonRecord reckon;
    reckon.playType_ = playType;
    reckon.amount_ = payoutAmount;
    reckon.multi_ = currentMultiplier;
    reckon.reckon_time_ = std::chrono::system_clock::now();
    reckon.before_amount = user->getBalance();
    // 玩家加钱
    user->setBalance(user->getBalance() + payoutAmount);
    reckon.after_amount = user->getBalance();
    
    // 使用updatePlayerCash方法更新兑现记录
    updatePlayerCash(loginname, std::move(reckon));
    
    // 标记兑现
    playerInGame->setCashedOut(playType);
    
    // 设置成功响应
    response.set_code(0);
    response.set_message("Cash out successful");
    auto* protoReckon = response.mutable_reckon();
    protoReckon->set_playtype(reckon.playType_);
    protoReckon->set_amount(reckon.amount_);
    protoReckon->set_multi(reckon.multi_);
    response.set_balance(user->getBalance());
    
    LOG_INFO("Cash out successful for player %s: playType=%d, payout=%.2f, multi=%.2f, newBalance=%.2f", 
             loginname.c_str(), playType, payoutAmount, currentMultiplier, user->getBalance());
    
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
    if (playerIndex >= roundPlayerBetRecord_.players_.size()) {
        response.set_code(ErrorCode::PLAYER_NOT_FOUND);
        response.set_message("Player index out of range");
        return false;
    }
    
    auto& player = roundPlayerBetRecord_.players_[playerIndex];
    
    // 计算退款金额
    double totalRefund = 0.0;
    if (!player.bets_.empty()) {
        totalRefund = player.bets_[0].amount_;
    }
    
    if (totalRefund <= 0) {
        response.set_code(ErrorCode::NO_BET_TO_CANCEL);
        response.set_message("No bet to cancel");
        return false;
    }
    
    // 更新玩家余额
    double newBalance = user->getBalance() + totalRefund;
    user->setBalance(newBalance);
    
    // 使用updatePlayerCancelBet方法更新退款记录
    updatePlayerCancelBet(loginname, playType, totalRefund);

    playerInGame->cancelBetPlayType(playType);
    
    // 设置成功响应
    response.set_code(0);
    response.set_message("Cancel bet successful");
    response.set_refundamount(totalRefund);
    response.set_balance(newBalance);
    
    LOG_INFO("Cancel bet successful for player %s: playType=%d, refund=%.2f, newBalance=%.2f", 
             loginname.c_str(), playType, totalRefund, newBalance);
    
    return true;
}

// 验证下注请求
bool MinesGame::validateBet(std::shared_ptr<PlayerInGame> playerInGame_inout, const std::string& requestRoundId,
    int32_t playType, double amount, proto::MinesPlaceBetRes& response) {

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

    // 5. 余额检查
    double currentBalance = playerInGame_inout->getBalance();
    if (currentBalance < amount) {
        response.set_code(ErrorCode::INSUFFICIENT_BALANCE);
        response.set_message("Insufficient balance");
        response.set_balance(currentBalance);
        return false;
    }

    // 验证通过
    response.set_code(0);
    response.set_message("Validation passed");
    response.set_balance(currentBalance);

    return true;
}

// 同步下注信息到游戏状态（在数据库扣款成功后调用）
void MinesGame::syncPlayerBet(std::shared_ptr<PlayerInGame> playerInGame, int32_t playType, double amount) {
    std::unique_lock<std::shared_mutex> rankLock(rankMutex_);
    if(playerInGame == nullptr || playerInGame->getUser() == nullptr){
        return;
    }
    
    // 创建下注记录
    RecordDetail betRecord;
    betRecord.playType_ = playType;
    betRecord.amount_ = amount;
    betRecord.time_ = std::chrono::system_clock::now();
    betRecord.before_amount_ = playerInGame->getBalance() + amount; 
    betRecord.after_amount_ = playerInGame->getBalance();
    
    // 创建玩家信息
    PlayerBriefInfo playerInfo;
    playerInfo.loginname_ = playerInGame->getLoginname(); 
    playerInfo.nickname_ = playerInGame->getUser()->getNickName();
    playerInfo.avatar_ = playerInGame->getUser()->getAvatarUrl();
    playerInfo.player_id_ = playerInGame->getUser()->getPlayerId();
    playerInfo.original_balance_ = playerInGame->getBalance() + amount;
    playerInfo.client_ip_ = playerInGame->getUser()->getClientIp();
    playerInfo.merchant_id_ = playerInGame->getUser()->getMerchantId();
    
    // 更新榜单
    updatePlayerBet(playerInfo, std::move(betRecord));
    
    LOG_INFO("Bet synchronized for player %s: playType=%d, amount=%.2f, newBalance=%.2f", 
             playerInGame->getLoginname().c_str(), playType, amount, playerInGame->getBalance());
    
    return;
}

// 验证取消下注请求
bool MinesGame::validateCancelBet(std::shared_ptr<PlayerInGame> playerInGame_inout, const std::string& requestRoundId,
    int32_t playType, proto::MinesCancelBetRes& response,
    double& refundAmount_out) {
    // 读锁用于检查排名数据
    std::shared_lock<std::shared_mutex> rankLock(rankMutex_);

    // 设置基本响应信息
    response.set_roundid(roundID_);
    response.set_balance(0.0);
    response.set_refundamount(0.0);

    // 1. 游戏状态检查 - 只在下注阶段才能取消
    GameStatus currentStatus = getStatus();
    if (currentStatus != GameStatus::START_JETTON) {
        response.set_code(ErrorCode::BET_NOT_ALLOWED);
        response.set_message("Cancel bet not allowed in current game status");
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

    // 4. 检查玩家是否有该玩法的下注
    std::string playerPlayTypeKey = playerInGame_inout->getLoginname() + "_" + std::to_string(playType);
    auto indexIt = roundPlayerRecordIndex_.find(playerPlayTypeKey);
    if (indexIt == roundPlayerRecordIndex_.end()) {
        response.set_code(ErrorCode::NO_BET_TO_CANCEL);
        response.set_message("No bet found for this play type");
        return false;
    }

    int playerIndex = indexIt->second;
    if (playerIndex >= roundPlayerBetRecord_.players_.size()) {
        response.set_code(ErrorCode::NO_BET_TO_CANCEL);
        response.set_message("Player index out of range");
        return false;
    }

    auto& player = roundPlayerBetRecord_.players_[playerIndex];

    // 5. 计算退款金额
    double refundAmount = 0.0;
    for (const auto& bet : player.bets_) {
        if (bet.playType_ == playType) {
            refundAmount += bet.amount_;
        }
    }

    if (refundAmount <= 0) {
        response.set_code(ErrorCode::NO_BET_TO_CANCEL);
        response.set_message("No bet amount to cancel");
        return false;
    }

    // 验证通过，返回退款金额供后续使用
    refundAmount_out = refundAmount;
    response.set_code(0);
    response.set_message("Validation passed");
    response.set_balance(playerInGame_inout->getBalance());
    response.set_refundamount(refundAmount);

    return true;
}

// 同步取消下注信息到游戏状态
void MinesGame::syncPlayerCancelBet(std::shared_ptr<PlayerInGame> playerInGame, int32_t playType, double refundAmount) {
    std::unique_lock<std::shared_mutex> rankLock(rankMutex_);
    if(playerInGame == nullptr || playerInGame->getUser() == nullptr){
        LOG_ERROR("PlayerInGame or User is null");
        return;
    }
    
    const std::string& loginname = playerInGame->getLoginname();
    
    // 更新玩家状态
    playerInGame->cancelBetPlayType(playType);
    
    // 移除该玩法的记录
    std::string playerPlayTypeKey = loginname + "_" + std::to_string(playType);
    auto indexIt = roundPlayerRecordIndex_.find(playerPlayTypeKey);
    if (indexIt != roundPlayerRecordIndex_.end()) {
        int playerIndex = indexIt->second;
        if (playerIndex < roundPlayerBetRecord_.players_.size()) {
            const auto& playerToRemove = roundPlayerBetRecord_.players_[playerIndex];
            // 直接删除该(玩家+玩法)组合的记录
            roundPlayerBetRecord_.players_.erase(roundPlayerBetRecord_.players_.begin() + playerIndex);
            // 重新排序排行榜
            sortRankByBetAmount();
        } else {
            LOG_ERROR("Player index %d out of range for cancel bet", playerIndex);
        }
    } else {
        LOG_WARN("Player %s playType %d not found in rank for cancel bet", loginname.c_str(), playType);
    }
    
    LOG_INFO("Cancel bet synchronized for player %s: playType=%d, refund=%.2f, newBalance=%.2f", 
             loginname.c_str(), playType, refundAmount, playerInGame->getBalance());
}

// 验证兑现请求
bool MinesGame::validateCash(std::shared_ptr<PlayerInGame> playerInGame_inout, 
                            const std::string& requestRoundId,
                            int32_t playType, proto::MinesCashRes& response,
                            double& payoutAmount_out, double& multiplier_out) {
    std::shared_lock<std::shared_mutex> rankLock(rankMutex_);
    
    // 设置基本响应信息
    response.set_roundid(roundID_);
    response.set_playtype(playType);
    response.set_balance(0.0);
    
    // 1. 游戏状态检查 - 只在STOP_JETTON阶段才能兑现
    GameStatus currentStatus = getStatus();
    if (currentStatus != GameStatus::STOP_JETTON) {
        response.set_code(ErrorCode::CASH_NOT_ALLOWED);
        response.set_message("Cash out only allowed during STOP_JETTON phase");
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

    // 5. 检查玩家是否有该玩法的下注
    std::string playerPlayTypeKey = playerInGame_inout->getLoginname() + "_" + std::to_string(playType);
    auto indexIt = roundPlayerRecordIndex_.find(playerPlayTypeKey);
    if (indexIt == roundPlayerRecordIndex_.end()) {
        response.set_code(ErrorCode::PLAYER_NOT_FOUND);
        response.set_message("Player not found in rank data");
        return false;
    }
    
    int playerIndex = indexIt->second;
    if (playerIndex >= roundPlayerBetRecord_.players_.size()) {
        response.set_code(ErrorCode::PLAYER_NOT_FOUND);
        response.set_message("Player index out of range");
        return false;
    }
    
    auto& player = roundPlayerBetRecord_.players_[playerIndex];
    
    // 6. 检查是否已经兑现过
    if (playerInGame_inout->hasCashedOut(playType)) {
        response.set_code(ErrorCode::ALREADY_CASHED_OUT);
        response.set_message("Already cashed out for this play type");
        return false;
    }
    
    // 7. 计算兑现金额
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
    if (!player.bets_.empty()) {
        playTypeBetAmount = player.bets_[0].amount_;
    }
    
    double payoutAmount = playTypeBetAmount * currentMultiplier;
    
    // 检查是否超过最大赔付限制
    if (payoutAmount > maxWinPerRound_) {
        payoutAmount = maxWinPerRound_;
    }

    payoutAmount_out = payoutAmount;
    multiplier_out = currentMultiplier;
    response.set_code(0);
    response.set_message("Validation passed");
    response.set_balance(playerInGame_inout->getBalance());
    return true;
}

// 同步兑现信息到游戏状态（需要调用者持有rankMutex_写锁）
void MinesGame::syncPlayerCashInternal(std::shared_ptr<PlayerInGame> playerInGame, int32_t playType, double payoutAmount, double multiplier) {
    if(playerInGame == nullptr || playerInGame->getUser() == nullptr){
        LOG_ERROR("PlayerInGame or User is null");
        return;
    }
    
    const std::string& loginname = playerInGame->getLoginname();
    
    // 创建兑现记录
    ReckonRecord reckon;
    reckon.playType_ = playType;
    reckon.amount_ = payoutAmount;
    reckon.multi_ = multiplier;
    reckon.reckon_time_ = std::chrono::system_clock::now();
    reckon.before_amount = playerInGame->getUser()->getBalance() - payoutAmount;
    reckon.after_amount = playerInGame->getUser()->getBalance();
    
    // 使用updatePlayerCash方法更新兑现记录
    updatePlayerCash(loginname, std::move(reckon));
    
    // 标记为已兑现
    playerInGame->setCashedOut(playType);
    
    LOG_INFO("Cash synchronized for player %s: playType=%d, payout=%.2f, multi=%.2f, newBalance=%.2f", 
             loginname.c_str(), playType, payoutAmount, multiplier, playerInGame->getBalance());
}

// 同步兑现信息到游戏状态（外部接口，自动获取锁）
void MinesGame::syncPlayerCash(std::shared_ptr<PlayerInGame> playerInGame, int32_t playType, double payoutAmount, double multiplier) {
    std::unique_lock<std::shared_mutex> rankLock(rankMutex_);
    syncPlayerCashInternal(playerInGame, playType, payoutAmount, multiplier);
}

// 兑现执行器实现
std::unique_ptr<CashOutExecutor> MinesGame::createCashOutExecutor(bool withDatabase) {
    if (withDatabase) {
        auto minesRepo = std::dynamic_pointer_cast<MinesGameRepositoryImpl>(gameRepository_);
        return std::make_unique<CashOutExecutor>(minesRepo);
    } else {
        return std::make_unique<CashOutExecutor>(nullptr);
    }
}

CashOutExecutor::Result CashOutExecutor::execute(
    std::shared_ptr<MinesGame> game,const std::string& round_id, const std::string& loginname, int32_t playType, CashOutMode mode) {
    
    Result result;
    result.success = false;
    result.payoutAmount = 0.0;
    result.multiplier = 0.0;
    
    std::shared_ptr<PlayerInGame> playerInGame = game->getPlayer(loginname);
    if (!playerInGame) {
        result.response.set_code(ErrorCode::PLAYER_NOT_FOUND);
        result.response.set_message("Player not found in game");
        return result;
    }
    
    double payoutAmount = 0.0;
    double multiplier = 0.0;
    
    // 1. 验证阶段（所有模式都需要）
    bool valid = game->validateCash(playerInGame, round_id, playType, result.response, 
                                   payoutAmount, multiplier);
    if (!valid) {
        return result;
    }
    
    // 2. 数据库操作阶段（根据模式决定）
    if (mode == CashOutMode::IMMEDIATE_DB && repository_) {
        bool dbSuccess = repository_->updatePlayerReckon(
            playerInGame->getUser()->getPlayerId(),
            payoutAmount,
            game->roundID_,
            playType,
            multiplier,
            playerInGame->getUser()->getClientIp(),
            playerInGame
        );
        
        if (!dbSuccess) {
            result.response.set_code(ErrorCode::DATABASE_ERROR);
            result.response.set_message("Database operation failed");
            return result;
        }
        
        LOG_DEBUG("Database cash out recorded for player %s, playType %d, amount %.2f", 
                 loginname.c_str(), playType, payoutAmount);
    }
    // 3. 同步游戏状态（所有模式都需要）
    game->syncPlayerCash(playerInGame, playType, payoutAmount, multiplier);
    
    // 4. 对于MEMORY_ONLY模式，保存记录以便后续批量入库
    if (mode == CashOutMode::MEMORY_ONLY) {
        MemoryCashOutRecord memoryRecord;
        memoryRecord.loginname = loginname;
        memoryRecord.playType = playType;
        memoryRecord.payoutAmount = payoutAmount;
        memoryRecord.multiplier = multiplier;
        memoryRecord.timestamp = std::chrono::system_clock::now();
        
        game->memoryCashOutRecords_.push_back(memoryRecord);
        LOG_DEBUG("Saved cash out record to memory for batch processing: player=%s, playType=%d", 
                 loginname.c_str(), playType);
    }
    
    // 5. 构建成功结果
    result.success = true;
    result.payoutAmount = payoutAmount;
    result.multiplier = multiplier;
    result.response.set_code(ErrorCode::SUCCESS);
    result.response.set_message("Cash out successful");
    result.response.set_balance(playerInGame->getBalance());
    
    // 构建 proto::ReckonRecord
    proto::ReckonRecord protoReckon;
    protoReckon.set_playtype(playType);
    protoReckon.set_amount(payoutAmount);
    protoReckon.set_multi(multiplier);
    result.reckonRecord = protoReckon;
    
    const char* modeStr = (mode == CashOutMode::IMMEDIATE_DB) ? "IMMEDIATE_DB" : 
                         (mode == CashOutMode::MEMORY_ONLY) ? "MEMORY_ONLY" : "BATCH_DB";
    LOG_INFO("Cash out executed (%s) for player %s: playType=%d, payout=%.2f, multi=%.2f", 
             modeStr, loginname.c_str(), playType, payoutAmount, multiplier);
    
    return result;
}

void MinesGame::writeCashOutRecordsToDatabase() {
    if (memoryCashOutRecords_.empty()) {
        return;
    }
    
    LOG_INFO("Writing %zu memory-only cash out records to database at game end", 
             memoryCashOutRecords_.size());
    
    auto repository = std::dynamic_pointer_cast<MinesGameRepositoryImpl>(gameRepository_);
    int successCount = 0;
    int failCount = 0;
    
    for (const auto& record : memoryCashOutRecords_) {
        // 查找对应的玩家
        auto playerIt = players_.find(record.loginname);
        if (playerIt == players_.end() || !playerIt->second) {
            LOG_WARN("Player %s not found when writing cash out record to database", 
                     record.loginname.c_str());
            failCount++;
            continue;
        }
        
        auto playerInGame = playerIt->second;
        if (!playerInGame->getUser()) {
            LOG_WARN("User info not found for player %s when writing cash out record", 
                     record.loginname.c_str());
            failCount++;
            continue;
        }
        
        bool dbSuccess = repository->updatePlayerReckon(
            playerInGame->getUser()->getPlayerId(),
            record.payoutAmount,
            roundID_,
            record.playType,
            record.multiplier,
            playerInGame->getUser()->getClientIp(),
            playerInGame
        );
        
        if (dbSuccess) {
            successCount++;
            LOG_DEBUG("Written cash out record to database: player=%s, playType=%d, amount=%.2f", 
                     record.loginname.c_str(), record.playType, record.payoutAmount);
        } else {
            failCount++;
            LOG_WARN("Failed to write cash out record to database: player=%s, playType=%d", 
                     record.loginname.c_str(), record.playType);
        }
    }
    
    LOG_INFO("Batch write completed: %d success, %d failed out of %zu total records", 
             successCount, failCount, memoryCashOutRecords_.size());
    
    // 清空内存记录
    memoryCashOutRecords_.clear();
}

void MinesGame::settleFailedOrders() {
    LOG_INFO("Settling losed orders for round %s due to mine explosion", roundID_.c_str());
    
    auto repository = std::dynamic_pointer_cast<MinesGameRepositoryImpl>(gameRepository_);
    int64_t updatedCount = repository->updateFailedOrders(roundID_);
    
    if (updatedCount >= 0) {
        LOG_INFO("Successfully settled %ld losed orders for round %s", updatedCount, roundID_.c_str());
    } else {
        LOG_ERROR("Failed to settle orders for round %s", roundID_.c_str());
    }
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
    for (size_t i = 0; i < roundPlayerBetRecord_.players_.size(); ++i) {
        const auto& player = roundPlayerBetRecord_.players_[i];
        const std::string& loginname = player.info_.loginname_;
        
        if (!loginname.empty() && !player.bets_.empty()) {
            int32_t playType = player.bets_[0].playType_;
            std::string playerPlayTypeKey = loginname + "_" + std::to_string(playType);
            roundPlayerRecordIndex_[playerPlayTypeKey] = i;
            LOG_DEBUG("Rebuilt mapping: %s -> index %d", playerPlayTypeKey.c_str(), i);
        }
    }
    
    LOG_DEBUG("Rebuilt playerPlayType to rank mapping for game %s, %d entries", 
        roundID_.c_str(), roundPlayerRecordIndex_.size());
}

void MinesGame::updatePlayerCancelBet(const std::string& loginname, int32_t playType, double refundAmount) {
    // 创建(玩家,玩法)的唯一键
    std::string playerPlayTypeKey = loginname + "_" + std::to_string(playType);
    auto indexIt = roundPlayerRecordIndex_.find(playerPlayTypeKey);
    if (indexIt == roundPlayerRecordIndex_.end()) {
        LOG_WARN("Player loginname %s playType %d not found in rank data for cancel bet", 
                 loginname.c_str(), playType);
        return;
    }
    
    int playerIndex = indexIt->second;
    if (playerIndex >= roundPlayerBetRecord_.players_.size()) {
        LOG_ERROR("Player index %d out of range for cancel bet", playerIndex);
        return;
    }
    
    // 移除这个(玩家,玩法)条目
    roundPlayerBetRecord_.players_.erase(
        roundPlayerBetRecord_.players_.begin() + playerIndex);
    
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
    std::vector<std::pair<std::string, proto::ReckonRecord>> autoCashResults;
    int revealedStars = getRevealedStarCount();
    
    auto cashExecutor = createCashOutExecutor(false);
    
    // 收集需要自动兑现的玩家信息
    std::vector<std::tuple<std::string, int32_t, std::shared_ptr<PlayerInGame>>> autoCashPlayers;
    
    {
        std::shared_lock<std::shared_mutex> playersLock(playersMutex_);
        std::shared_lock<std::shared_mutex> rankLock(rankMutex_);
        
        for (size_t i = 0; i < roundPlayerBetRecord_.players_.size(); ++i) {
            const auto& playerBetInfo = roundPlayerBetRecord_.players_[i];
            const std::string& loginname = playerBetInfo.info_.loginname_;
            
            auto playerIt = players_.find(loginname);
            if (playerIt == players_.end()) {
                continue; 
            }
            
            auto playerInGame = playerIt->second;
            if (!playerInGame) {
                continue;
            }
            
            for (int playType = MinesProPlayType::LEFT; playType <= MinesProPlayType::RIGHT; playType++) {
                if (playerInGame->hasCashedOut(playType)) {
                    continue;
                }
                bool hasThisPlayTypeBet = false;
                for (const auto& bet : playerBetInfo.bets_) {
                    if (bet.playType_ == playType) {
                        hasThisPlayTypeBet = true;
                        break;
                    }
                }
                if (!hasThisPlayTypeBet) {
                    continue;
                }
                
                // 检查是否启用了自动兑现
                if (playerInGame->getAutoCashEnabled(playType)) {
                    int targetStars = playerInGame->getAutoCashTargetGrid(playType);
                    if (revealedStars >= targetStars) {
                        autoCashPlayers.emplace_back(loginname, playType, playerInGame);
                    }
                }
            }
        }
    }
    
    if (!autoCashPlayers.empty()) {
        for (const auto& [loginname, playType, playerInGame] : autoCashPlayers) {
            // 验证兑现
            double payoutAmount = 0.0;
            double multiplier = 0.0;
            proto::MinesCashRes response;
            
            bool valid = validateCash(playerInGame, roundID_, playType, response, 
                                    payoutAmount, multiplier);
            if (valid && playerInGame) {
                syncPlayerCashInternal(playerInGame, playType, payoutAmount, multiplier);
                // 保存到内存记录
                MemoryCashOutRecord memoryRecord;
                memoryRecord.loginname = loginname;
                memoryRecord.playType = playType;
                memoryRecord.payoutAmount = payoutAmount;
                memoryRecord.multiplier = multiplier;
                memoryRecord.timestamp = std::chrono::system_clock::now();
                memoryCashOutRecords_.push_back(memoryRecord);
                
                // 创建响应记录
                proto::ReckonRecord reckonRecord;
                reckonRecord.set_playtype(playType);
                reckonRecord.set_amount(payoutAmount);
                reckonRecord.set_multi(multiplier);
                
                autoCashResults.emplace_back(loginname, reckonRecord);
                
                LOG_INFO("Auto cash executed (memory-only) for player %s, playType %d, payout=%.2f", 
                         loginname.c_str(), playType, payoutAmount);
            } else {
                LOG_ERROR("Failed to validate auto cash for player %s, playType %d", 
                         loginname.c_str(), playType);
            }
        }
    }
    
    // 在锁外发送兑现响应给玩家
    for (const auto& [loginname, reckonRecord] : autoCashResults) {
        std::shared_lock<std::shared_mutex> playersLock(playersMutex_);
        auto playerIt = players_.find(loginname);
        if (playerIt != players_.end() && playerIt->second) {
            auto session = playerIt->second->getSession().lock();
            playersLock.unlock();
            
            if (session && appContext_) {
                auto tcpServer = appContext_->getTcpServer();
                if (tcpServer) {
                    // 创建兑现响应
                    proto::MinesCashRes cashResponse;
                    cashResponse.set_code(ErrorCode::SUCCESS);
                    cashResponse.set_roundid(roundID_);
                    cashResponse.set_playtype(reckonRecord.playtype());
                    cashResponse.set_balance(playerIt->second->getBalance());
                    cashResponse.mutable_reckon()->CopyFrom(reckonRecord);
                    
                    std::string data;
                    cashResponse.SerializeToString(&data);
                    tcpServer->sendToPlayer(session->getSessionId(), Protocol::SC_MINES_CASH_RES, data);
                    LOG_INFO("Auto cash response sent to player %s for playType %d", loginname.c_str(), reckonRecord.playtype());
                }
            }
        } else {
            playersLock.unlock();
        }
    }
}

void MinesGame::resetAllPlayersCashOutStatus() {
    for (const auto& playerPair : players_) {
        const auto& playerInGame = playerPair.second;
        if (playerInGame) {
            playerInGame->resetCashOutPlayType();
            playerInGame->resetBetPlayType(); 
        }
    }
    
    LOG_INFO("Reset cash out and bet status for all players in game %s", roundID_.c_str());
}

std::optional<proto::ReckonRecord> MinesGame::executeCashOutInternal(const std::string& loginname, int32_t playType) {

    auto playerIt = players_.find(loginname);
    if (playerIt == players_.end()) {
        LOG_ERROR("Player %s not found for internal cash out", loginname.c_str());
        return std::nullopt;
    }
    
    auto playerInGame = playerIt->second;
    if (!playerInGame) {
        LOG_ERROR("Player object not found for internal cash out");
        return std::nullopt;
    }
    
    auto user = playerInGame->getUser();
    if (!user) {
        LOG_ERROR("User object not found for internal cash out");
        return std::nullopt;
    }
    
    // 检查是否已经兑现过
    if (playerInGame->hasCashedOut(playType)) {
        LOG_WARN("Player %s playType %d already cashed out", loginname.c_str(), playType);
        return std::nullopt;
    }
    
    // 查找榜单条目
    std::string playerPlayTypeKey = loginname + "_" + std::to_string(playType);
    auto indexIt = roundPlayerRecordIndex_.find(playerPlayTypeKey);
    if (indexIt == roundPlayerRecordIndex_.end()) {
        LOG_ERROR("Player %s playType %d not found in rank data", loginname.c_str(), playType);
        return std::nullopt;
    }
    
    int playerIndex = indexIt->second;
    if (playerIndex >= roundPlayerBetRecord_.players_.size()) {
        LOG_ERROR("Player index %d out of range", playerIndex);
        return std::nullopt;
    }
    
    auto& player = roundPlayerBetRecord_.players_[playerIndex];

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
    if (!player.bets_.empty()) {
        playTypeBetAmount = player.bets_[0].amount_;
    }
    
    double payoutAmount = playTypeBetAmount * currentMultiplier;
    
    // 检查是否超过最大赔付限制
    if (payoutAmount > maxWinPerRound_) {
        LOG_WARN("Payout amount %.2f exceeds maximum win limit %.2f, capping at maximum", 
                payoutAmount, maxWinPerRound_);
        payoutAmount = maxWinPerRound_;
    }
    
    // 创建兑现记录
    ReckonRecord reckon;
    reckon.playType_ = playType;
    reckon.amount_ = payoutAmount;
    reckon.multi_ = currentMultiplier;
    reckon.reckon_time_ = std::chrono::system_clock::now();
    reckon.before_amount = user->getBalance();
    // 更新玩家余额
    double newBalance = user->getBalance() + payoutAmount;
    user->setBalance(newBalance);
    reckon.after_amount = newBalance;

    proto::ReckonRecord returnReckon;
    returnReckon.set_playtype(playType);
    returnReckon.set_amount(payoutAmount);
    returnReckon.set_multi(currentMultiplier);
    
    updatePlayerCash(loginname, std::move(reckon));
    
    // 标记为已兑现
    playerInGame->setCashedOut(playType);
    
    LOG_INFO("Internal cash out successful for player %s: playType=%d, payout=%.2f, multi=%.2f, newBalance=%.2f", 
             loginname.c_str(), playType, payoutAmount, currentMultiplier, newBalance);
    
    return returnReckon;
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
                std::string playerPlayTypeKey = loginname + "_" + std::to_string(playType);
                bool hasPlayTypeBet = false;
                {
                    std::shared_lock<std::shared_mutex> rankLock(rankMutex_);
                    auto indexIt = roundPlayerRecordIndex_.find(playerPlayTypeKey);
                    if (indexIt != roundPlayerRecordIndex_.end()) {
                        int playerIndex = indexIt->second;
                        if (playerIndex < roundPlayerBetRecord_.players_.size()) {
                            const auto& player = roundPlayerBetRecord_.players_[playerIndex];
                            if (!player.bets_.empty() && player.bets_[0].playType_ == playType) {
                                hasPlayTypeBet = true;
                            }
                        }
                    }
                }
                
                if (hasPlayTypeBet) {
                    LOG_INFO("Player %s disconnected in STOP_JETTON phase, executing immediate cash out for playType=%d", 
                             loginname.c_str(), playType);
                    
                    // 使用统一的兑现执行器（IMMEDIATE_DB模式）
                    auto cashExecutor = createCashOutExecutor(true); 
                    auto result = cashExecutor->execute(std::dynamic_pointer_cast<MinesGame>(shared_from_this()), roundID_, loginname, playType, CashOutMode::IMMEDIATE_DB);
                    
                    if (result.success) {
                        LOG_INFO("Immediate cash out successful for disconnected player %s: playType=%d, payout=%.2f", 
                                 loginname.c_str(), playType, result.payoutAmount);
                    } else {
                        LOG_WARN("Immediate cash out failed for disconnected player %s: playType=%d, reason=%s", 
                                 loginname.c_str(), playType, result.response.message().c_str());
                    }
                }
            }
        }
    }
    
    setPlayerInactive(loginname);
}
