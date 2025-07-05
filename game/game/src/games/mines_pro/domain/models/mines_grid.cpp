#include "mines_grid.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <set>
#include <third_party/libuv_cpp/include/LogWriter.hpp>
#include <third_party/nlohmann/json.hpp>

using json = nlohmann::json;

MinesGrid::MinesGrid() : revealedCount_(0), revealedStarCount_(0), hasRevealedMine_(false),
                         mineCount_(0), totalTiles_(0), starCount_(0), currentRevealIndex_(1) {
}

MinesGrid::~MinesGrid() {
}

void MinesGrid::initialize(uint32_t totalTiles, uint32_t mineCount, const std::unordered_map<uint32_t, std::pair<double, double>>& tileConfig) {
    totalTiles_ = totalTiles;
    mineCount_ = mineCount;
    starCount_ = totalTiles - mineCount;
    
    if (mineCount >= totalTiles) {
        LOG_ERROR("Invalid mine count: %d, total tiles: %d", mineCount, totalTiles);
        return;
    }
    
    if (tileConfig.size() != totalTiles) {
        LOG_ERROR("Tile config count (%d) doesn't match total tiles (%d)", 
                  tileConfig.size(), totalTiles);
        return;
    }
    
    tiles_.clear();
    tiles_.resize(totalTiles);
    
    for (uint32_t i = 0; i < totalTiles; ++i) {
        uint32_t tileIndex = i + 1; // tile index 从1开始
        auto it = tileConfig.find(tileIndex);
        if (it != tileConfig.end()) {
            tiles_[i] = Tile(tileIndex, TileType::STAR, it->second.first, it->second.second);
        } else {
            LOG_WARN("Missing config for tile %d, using default values", tileIndex);
            tiles_[i] = Tile(tileIndex, TileType::STAR, 1.0, 1.0);
        }
    }
    
    LOG_DEBUG("MinesGrid initialized: %d tiles, %d mines, %d stars", 
             totalTiles_, mineCount_, starCount_);
}

bool MinesGrid::revealTile(uint32_t index) {
    if (!isValidIndex(index)) {
        LOG_ERROR("Invalid index: %d", index);
        return false;
    }
    
    uint32_t arrayIndex = index - 1; // 转换为0-based数组索引
    if (tiles_[arrayIndex].status == TileStatus::REVEALED) {
        LOG_WARN("Tile at index %d already revealed", index);
        return false;
    }
    
    tiles_[arrayIndex].status = TileStatus::REVEALED;
    revealedCount_++;
    
    if (tiles_[arrayIndex].type == TileType::MINE) {
        hasRevealedMine_ = true;
        LOG_INFO("Mine revealed at index %d", index);
    } else {
        revealedStarCount_++;
        LOG_INFO("Star revealed at index %d, multiplier: %.2f", index, tiles_[arrayIndex].multiplier);
    }
    
    return true;
}

bool MinesGrid::isTileRevealed(uint32_t index) const {
    if (!isValidIndex(index)) {
        return false;
    }
    uint32_t arrayIndex = index - 1; // 转换为0-based数组索引
    return tiles_[arrayIndex].status == TileStatus::REVEALED;
}

TileType MinesGrid::getTileType(uint32_t index) const {
    if (!isValidIndex(index)) {
        return TileType::STAR;  // 默认返回星星
    }
    uint32_t arrayIndex = index - 1; // 转换为0-based数组索引
    return tiles_[arrayIndex].type;
}

double MinesGrid::getTileMultiplier(uint32_t index) const {
    if (!isValidIndex(index)) {
        return 1.0;
    }
    uint32_t arrayIndex = index - 1; // 转换为0-based数组索引
    return tiles_[arrayIndex].multiplier;
}

double MinesGrid::getTileWeight(uint32_t index) const {
    if (!isValidIndex(index)) {
        return 1.0;
    }
    uint32_t arrayIndex = index - 1; // 转换为0-based数组索引
    return tiles_[arrayIndex].weight;
}

const std::vector<Tile>& MinesGrid::getAllTiles() const {
    return tiles_;
}

std::vector<Tile>& MinesGrid::getAllTiles() {
    return tiles_;
}

int MinesGrid::getRevealedCount() const {
    return revealedCount_;
}

int MinesGrid::getRevealedStarCount() const {
    return revealedStarCount_;
}

bool MinesGrid::hasRevealedMine() const {
    return hasRevealedMine_;
}

bool MinesGrid::isGameOver() const {
    return hasRevealedMine_ || revealedStarCount_ == starCount_;
}

void MinesGrid::generateGrid(const std::string& seed) {
    if (totalTiles_ == 0) {
        LOG_ERROR("Grid not initialized, cannot generate");
        return;
    }
    
    seed_ = seed.empty() ? std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) : seed;
    
    reset();
    placeMinesBasedOnWeight(seed_);
    
    LOG_INFO("Generated mines grid with seed: %s", seed_.c_str());
}

void MinesGrid::reset() {
    for (auto& tile : tiles_) {
        tile.type = TileType::STAR;
        tile.status = TileStatus::HIDDEN;
    }
    revealedCount_ = 0;
    revealedStarCount_ = 0;
    hasRevealedMine_ = false;
    currentRevealIndex_ = 1;
}

void MinesGrid::placeMinesBasedOnWeight(const std::string& seed) {
    auto rng = createRandomGenerator(seed);
    
    // 按权重分布放置地雷
    std::vector<double> weights;
    weights.reserve(totalTiles_);
    
    for (const auto& tile : tiles_) {
        weights.push_back(tile.weight);
    }
    
    std::discrete_distribution<uint32_t> distribution(weights.begin(), weights.end());
    
    std::set<uint32_t> mineIndices;
    while (mineIndices.size() < static_cast<size_t>(mineCount_)) {
        uint32_t index = distribution(rng);
        mineIndices.insert(index);
    }
    
    std::vector<uint32_t> minePositions;
    for (uint32_t arrayIndex : mineIndices) {
        tiles_[arrayIndex].type = TileType::MINE;
        minePositions.push_back(tiles_[arrayIndex].index);
        LOG_DEBUG("Placed mine at index %d (weight: %.2f)", tiles_[arrayIndex].index, tiles_[arrayIndex].weight);
    }
    std::string minePositionsStr;
    for (size_t i = 0; i < minePositions.size(); ++i) {
        if (i > 0) minePositionsStr += ", ";
        minePositionsStr += std::to_string(minePositions[i]);
    }
    LOG_INFO("Placed %d mines at positions: [%s] (seed: %s)", 
             mineIndices.size(), minePositionsStr.c_str(), seed_.c_str());
}

std::mt19937 MinesGrid::createRandomGenerator(const std::string& seed) const {
    std::hash<std::string> hasher;
    return std::mt19937(hasher(seed));
}

bool MinesGrid::isValidIndex(uint32_t index) const {
    return index >= 1 && index <= static_cast<uint32_t>(totalTiles_);
}

std::vector<uint32_t> MinesGrid::getMineIndices() const {
    std::vector<uint32_t> mineIndices;
    for (uint32_t i = 0; i < static_cast<uint32_t>(totalTiles_); ++i) {
        if (tiles_[i].type == TileType::MINE) {
            mineIndices.push_back(tiles_[i].index); // 返回tile的实际index（1-based）
        }
    }
    return mineIndices;
}

bool MinesGrid::autoRevealNextTile(double& multiplier, bool& hasMine, bool& gameOver) {
    // 检查是否还有格子可以翻开
    if (currentRevealIndex_ > static_cast<uint32_t>(totalTiles_)) {
        LOG_WARN("No more tiles to reveal, reached end of grid");
        return false;
    }
    
    // 获取下一个要翻开的格子（按顺序从1到21）
    uint32_t nextIndex = currentRevealIndex_;
    currentRevealIndex_++;
    
    LOG_INFO("Auto revealing tile at index %d (sequential order: %d/%d)", 
             nextIndex, nextIndex, totalTiles_);
    
    // 翻开格子
    if (!revealTile(nextIndex)) {
        LOG_ERROR("Failed to reveal tile at index %d", nextIndex);
        return false;
    }
    
    // 检查是否是地雷
    hasMine = (getTileType(nextIndex) == TileType::MINE);
    
    if (hasMine) {
        // 踩到地雷，游戏结束
        multiplier = 0.0;
        gameOver = true;
        LOG_INFO("Mine hit at index %d during auto reveal - GAME OVER", nextIndex);
    } else {
        // 安全方块，计算当前倍数
        multiplier = getTileMultiplier(nextIndex);
        gameOver = false;  // 只有踩到雷才结束
        
        LOG_INFO("Star revealed at index %d, multiplier: %.2f, stars: %d/%d", 
                 nextIndex, multiplier, revealedStarCount_, starCount_);
    }
    
    return true;
}