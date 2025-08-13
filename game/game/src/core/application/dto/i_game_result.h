#ifndef I_GAME_RESULT_H
#define I_GAME_RESULT_H

#include <string>
#include <chrono>

// 游戏结果基类 - 用于层间数据传输
class IGameResult {
public:
    IGameResult() = default;
    virtual ~IGameResult() = default;
    
public:
    std::string roundID;
    std::chrono::system_clock::time_point startTime;
    std::chrono::system_clock::time_point endTime;
    std::string gameType;
};

#endif // I_GAME_RESULT_H 