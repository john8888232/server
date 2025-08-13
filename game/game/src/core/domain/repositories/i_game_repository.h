#ifndef I_GAME_REPOSITORY_H
#define I_GAME_REPOSITORY_H
#include <memory>
#include "core/application/dto/i_game_result.h"

// 游戏仓库接口
class IGameRepository {
public:
    IGameRepository() = default;
    virtual ~IGameRepository() = default;
    virtual bool startGame(const std::string& roundID) = 0;
    virtual bool saveGame(std::shared_ptr<IGameResult> pGame) = 0;
};

#endif