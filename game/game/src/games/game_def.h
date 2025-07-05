#pragma once

#include <string>
#include <unordered_map>
#include <vector>

// 游戏类型ID常量
constexpr int MINES_PRO_ID = 1;
constexpr int SLOTS_ID = 2;

// 游戏类型字符串常量
constexpr const char* MINES_PRO_TYPE = "mines_pro";
constexpr const char* SLOTS_TYPE = "slots";

// mines_pro 玩法类型常量
namespace MinesProPlayType {
    constexpr int LEFT = 1;   // 左边玩法
    constexpr int RIGHT = 2;  // 右边玩法
    
    // 检查playType是否有效
    inline bool isValid(int playType) {
        return playType == LEFT || playType == RIGHT;
    }
}

// 游戏类型到ID的映射
inline const std::unordered_map<std::string, int> GAME_TYPE_TO_ID = {
    {MINES_PRO_TYPE, MINES_PRO_ID},
    {SLOTS_TYPE, SLOTS_ID}
};

// ID到游戏类型的映射
inline const std::unordered_map<int, std::string> ID_TO_GAME_TYPE = {
    {MINES_PRO_ID, MINES_PRO_TYPE},
    {SLOTS_ID, SLOTS_TYPE}
};

inline int getGameTypeId(const std::string& gameType) {
    auto it = GAME_TYPE_TO_ID.find(gameType);
    return (it != GAME_TYPE_TO_ID.end()) ? it->second : 0;
}

inline std::string getGameTypeFromId(int gameId) {
    auto it = ID_TO_GAME_TYPE.find(gameId);
    return (it != ID_TO_GAME_TYPE.end()) ? it->second : "";
}

// 检查游戏类型是否有效
inline bool isValidGameType(const std::string& gameType) {
    return GAME_TYPE_TO_ID.find(gameType) != GAME_TYPE_TO_ID.end();
}

// 检查游戏ID是否有效
inline bool isValidGameId(int gameId) {
    return ID_TO_GAME_TYPE.find(gameId) != ID_TO_GAME_TYPE.end();
}

inline std::vector<std::string> getAllGameTypes() {
    std::vector<std::string> types;
    for (const auto& pair : GAME_TYPE_TO_ID) {
        types.push_back(pair.first);
    }
    return types;
}

inline std::vector<int> getAllGameIds() {
    std::vector<int> ids;
    for (const auto& pair : ID_TO_GAME_TYPE) {
        ids.push_back(pair.first);
    }
    return ids;
}