// src/core/infrastructure/common/config_manager.cpp
#include "config_manager.h"
#include <fstream>
#include <filesystem>
#include <iostream>
#include "../persistence/database_factory.h"
#include "../persistence/redis_client.h"
#include "app_context.h"
#include "../../../games/game_registry.h"

namespace fs = std::filesystem;

bool ConfigManager::loadServerConfig() {
    try {
        std::string serverConfigPath = "server_config.json";
        std::ifstream configFile(serverConfigPath);
        if (!configFile.is_open()) {
            LOG_ERROR("Failed to open config file: %s", serverConfigPath.c_str());
            return false;
        }
        
        configFile >> serverConfig_;
        configFile.close();
        
        LOG_DEBUG("System configuration initialized successfully");
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Error initializing config manager: %s", e.what());
        return false;
    }
}

bool ConfigManager::loadGameConfig() {
    try {
        if (!serverConfig_.contains("server") || !serverConfig_["server"].contains("game_type")) {
            LOG_ERROR("Server config missing game_type field");
            return false;
        }
        
        const auto& gameTypes = serverConfig_["server"]["game_type"];
        if (!gameTypes.is_array()) {
            LOG_ERROR("game_type is not an array in server config");
            return false;
        }
        
        auto& dbFactory = DatabaseFactory::getInstance();
        auto redisClient = dbFactory.getRedisClient();
        
        if (!redisClient) {
            LOG_ERROR("Failed to get Redis client from DatabaseFactory");
            return false;
        }
        
        for (const auto& gameType : gameTypes) {
            if (!gameType.is_string()) {
                continue;
            }
            
            std::string gameId = gameType.get<std::string>();
            std::string configKey = "game_config:" + gameId;
            LOG_DEBUG("configKey: %s", configKey.c_str());
            auto gameConfigStr = redisClient->get(configKey);
            if (gameConfigStr) {
                try {
                    json gameConfig = json::parse(*gameConfigStr);
                    gameConfigs_[gameId] = gameConfig;
                    LOG_DEBUG("Loaded game config for %s from Redis", gameId.c_str());
                } catch (const json::parse_error& e) {
                    LOG_ERROR("Error parsing game config JSON from Redis for %s: %s", gameId.c_str(), e.what());
                }
            } else {
                LOG_ERROR("Game config not found in Redis for %s", gameId.c_str());
            }
        }
        
        return !gameConfigs_.empty();
    } catch (const std::exception& e) {
        LOG_ERROR("Error loading game configs from Redis: %s", e.what());
        return false;
    }
}

const json& ConfigManager::getServerConfig() const {
    return serverConfig_;
}

json ConfigManager::getGameConfig(const std::string& gameId) const {
    auto it = gameConfigs_.find(gameId);
    if (it != gameConfigs_.end()) {
        return it->second;
    }
    return json::object();
}

json ConfigManager::getValueFromPath(const json& root, const std::string& path) const {
    std::stringstream ss(path);
    std::string item;
    json current = root;
    
    while (std::getline(ss, item, '.')) {
        if (current.is_object() && current.contains(item)) {
            current = current[item];
        } else {
            return json();
        }
    }
    
    return current;
}

bool ConfigManager::hasPath(const std::string& path) const {
    return !getValueFromPath(serverConfig_, path).is_null();
}

bool ConfigManager::reloadGameConfigFromRedis(const std::string& gameType) {
    try {
        LOG_INFO("Reloading config for game type: %s from Redis", gameType.c_str());
        
        if (!serverConfig_.contains("server") || !serverConfig_["server"].contains("game_type")) {
            LOG_ERROR("Server config missing game_type field");
            return false;
        }
        
        const auto& gameTypes = serverConfig_["server"]["game_type"];
        if (!gameTypes.is_array()) {
            LOG_ERROR("game_type is not an array in server config");
            return false;
        }
        
        bool gameTypeFound = false;
        for (const auto& configuredGameType : gameTypes) {
            if (configuredGameType.is_string() && configuredGameType.get<std::string>() == gameType) {
                gameTypeFound = true;
                break;
            }
        }
        
        if (!gameTypeFound) {
            LOG_ERROR("Game type '%s' is not configured in server_config.json. Configured game types: %s", 
                     gameType.c_str(), gameTypes.dump().c_str());
            return false;
        }
        
        auto& dbFactory = DatabaseFactory::getInstance();
        auto redisClient = dbFactory.getRedisClient();
        
        if (!redisClient) {
            LOG_ERROR("Failed to get Redis client for reloading config");
            return false;
        }
        
        std::string configKey = "game_config:" + gameType;
        auto gameConfigStr = redisClient->get(configKey);
        
        if (!gameConfigStr) {
            LOG_ERROR("Game config not found in Redis for %s", gameType.c_str());
            return false;
        }
        
        json newConfig;
        try {
            newConfig = json::parse(*gameConfigStr);
        } catch (const json::parse_error& e) {
            LOG_ERROR("Error parsing game config JSON from Redis for %s: %s", gameType.c_str(), e.what());
            return false;
        }
        
        gameConfigs_[gameType] = newConfig;
        LOG_INFO("Updated game config for %s in memory", gameType.c_str());
        
        auto& appContext = AppContext::getInstance();
        auto gameManager = appContext.getGameManager();
        
        if (!gameManager) {
            LOG_ERROR("GameManager not available");
            return false;
        }
        
        auto gameRegistry = gameManager->getGameRegistry();
        if (!gameRegistry) {
            LOG_ERROR("GameRegistry not available");
            return false;
        }
        
        auto gameFactory = gameRegistry->getGameFactory(gameType);
        if (!gameFactory) {
            LOG_ERROR("Game factory not found for type: %s", gameType.c_str());
            return false;
        }
        
        gameFactory->setConfig(newConfig);
        LOG_INFO("Successfully updated config for game type: %s", gameType.c_str());
        
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Error reloading game config for %s: %s", gameType.c_str(), e.what());
        return false;
    }
}
