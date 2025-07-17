#ifndef MINES_GRID_H
#define MINES_GRID_H

#include <vector>
#include <string>
#include <random>
#include <unordered_map>

enum class TileType {
    STAR,    // 星星
    MINE     // 地雷
};

enum class TileStatus {
    HIDDEN,     // 未翻开
    REVEALED    // 已翻开
};

struct Tile {
    uint32_t index;
    TileType type;
    TileStatus status;
    double multiplier;
    double weight;
    
    Tile(uint32_t idx = 0, TileType t = TileType::STAR, double mult = 1.0, double w = 1.0) 
        : index(idx), type(t), status(TileStatus::HIDDEN), multiplier(mult), weight(w) {}
};

class MinesGrid {
public:
    MinesGrid();
    ~MinesGrid();

    // 初始化网格配置 - 传入总格子数、总雷数、以及每个格子的倍数和权重
    void initialize(uint32_t totalTiles, uint32_t mineCount, const std::unordered_map<uint32_t, std::pair<double, double>>& tileConfig);

    // 基于 index 的网格操作
    bool revealTile(uint32_t index);
    bool isTileRevealed(uint32_t index) const;
    TileType getTileType(uint32_t index) const;
    double getTileMultiplier(uint32_t index) const;
    double getTileWeight(uint32_t index) const;
    
    // 自动翻牌操作 - 由MinesGrid内部管理翻牌顺序
    bool autoRevealNextTile(double& multiplier, bool& hasMine, bool& gameOver);
    
    // 获取全部 tile 的集合
    const std::vector<Tile>& getAllTiles() const;
    std::vector<Tile>& getAllTiles();
    
    int getRevealedCount() const;
    int getRevealedStarCount() const;
    bool hasRevealedMine() const;
    bool isGameOver() const;
    
    // 网格生成 - 根据权重进行地雷设置
    void generateGrid(const std::string& seed = "");
    
    // 获取地雷位置
    std::vector<uint32_t> getMineIndices() const;
    
    // 获取网格配置信息
    int getMineCount() const { return mineCount_; }
    int getTotalTiles() const { return totalTiles_; }
    int getStarCount() const { return starCount_; }

private:
    std::vector<Tile> tiles_;
    std::string seed_;
    int revealedCount_;
    int revealedStarCount_;
    bool hasRevealedMine_;
    
    // 网格配置
    int mineCount_;
    int totalTiles_;
    int starCount_;
    
    // 自动翻牌状态
    uint32_t currentRevealIndex_;        // 当前翻牌位置(从1开始)
private:
    void reset();
    void placeMinesBasedOnWeight(const std::string& seed);
    std::mt19937 createRandomGenerator(const std::string& seed) const;
    bool isValidIndex(uint32_t index) const;
};

#endif // MINES_GRID_H





