#ifndef MINES_GAME_RESULT_H
#define MINES_GAME_RESULT_H

#include "core/application/dto/i_game_result.h"

// 扫雷游戏结果类 - 用于层间数据传输
class MinesGameResult : public IGameResult {
public:
    MinesGameResult() = default;
    ~MinesGameResult() override = default;
    
    // 扫雷游戏特有属性
    std::string mineIndex;  // 地雷索引
    double multiplier;      // 倍率
    std::string hash;       // 地雷坐标+seed的sha256的值
    std::string seed;       // 随机种子
};

#endif // MINES_GAME_RESULT_H 

