#include "game_factory.h"
#include <third_party/libuv_cpp/include/LogWriter.hpp>

GameFactory::GameFactory() : enabled_(true) {
}

GameFactory::~GameFactory() {
}

bool GameFactory::isEnabled() const {
    return enabled_;
}

void GameFactory::setEnabled(bool enabled) {
    bool oldState = enabled_;
    enabled_ = enabled;
    
    if (oldState != enabled) {
        LOG_INFO("Game %s %s", getGameType().c_str(), enabled ? "enabled" : "disabled");
    }
}

void GameFactory::setConfig(const json& config) {
    json oldConfig = config_;
    config_ = config;
    LOG_INFO("Game %s config updated", getGameType().c_str());
    
    onConfigUpdated(oldConfig, config);
}


