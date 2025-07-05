#include "game_registry.h"
#include <third_party/libuv_cpp/include/LogWriter.hpp>
#include <algorithm>
#include "core/infrastructure/common/app_context.h"

// 包含内置游戏的游戏工厂
#include "mines_pro/domain/services/mines_factory.h"

GameRegistry::GameRegistry() {
}

GameRegistry::~GameRegistry() {
    // 清理所有注册的游戏工厂
    std::lock_guard<std::mutex> lock(gameFactoriesMutex_);
    gameFactories_.clear();
}

bool GameRegistry::initialize() {
    LOG_INFO("Initializing game registry");
    
    // 初始化内置游戏
    if (!initializeBuiltInGames()) {
        LOG_ERROR("Failed to initialize built-in games");
        return false;
    }
    
    std::lock_guard<std::mutex> lock(gameFactoriesMutex_);
    LOG_INFO("Game registry initialized with %d game types", gameFactories_.size());
    return true;
}

bool GameRegistry::registerGame(const std::string& gameType, std::shared_ptr<GameFactory> factory) {
    if (!factory) {
        LOG_ERROR("Cannot register null factory for game type %s", gameType.c_str());
        return false;
    }
    
    {
        std::lock_guard<std::mutex> lock(gameFactoriesMutex_);
    auto existingIt = gameFactories_.find(gameType);
    if (existingIt != gameFactories_.end()) {
        LOG_WARN("Game type %s already registered, overwriting", gameType.c_str());
    }
    
    gameFactories_[gameType] = factory;
    }
    
    LOG_DEBUG("Registered game type: %s", gameType.c_str());
    
    return true;
}

bool GameRegistry::unregisterGame(const std::string& gameType) {
    std::shared_ptr<GameFactory> factory;
    
    {
        std::lock_guard<std::mutex> lock(gameFactoriesMutex_);
    auto it = gameFactories_.find(gameType);
    if (it == gameFactories_.end()) {
        LOG_WARN("Game type %s not found for unregistration", gameType.c_str());
        return false;
    }
    
        factory = it->second;
    gameFactories_.erase(it);
    }
    
    LOG_INFO("Unregistered game type: %s", gameType.c_str());
    
    return true;
}

std::shared_ptr<GameFactory> GameRegistry::getGameFactory(const std::string& gameType) {
    std::lock_guard<std::mutex> lock(gameFactoriesMutex_);
    auto it = gameFactories_.find(gameType);
    if (it == gameFactories_.end()) {
        LOG_ERROR("Game type %s not found in registry", gameType.c_str());
        return nullptr;
    }
    
    return it->second;
}

std::vector<std::string> GameRegistry::getAllGameTypes() const {
    std::lock_guard<std::mutex> lock(gameFactoriesMutex_);
    std::vector<std::string> gameTypes;
    gameTypes.reserve(gameFactories_.size());
    
    for (const auto& entry : gameFactories_) {
        gameTypes.push_back(entry.first);
    }
    
    return gameTypes;
}

std::unordered_map<std::string, std::shared_ptr<GameFactory>> GameRegistry::getAllGameFactories() const {
    std::lock_guard<std::mutex> lock(gameFactoriesMutex_);
    return gameFactories_;
}

bool GameRegistry::registerMessageHandlers(std::shared_ptr<MessageRouter> router) {
    if (!router) {
        LOG_ERROR("Invalid message router");
        return false;
    }
    
    try {
        std::unordered_map<std::string, std::shared_ptr<GameFactory>> factories;
        {
            std::lock_guard<std::mutex> lock(gameFactoriesMutex_);
            factories = gameFactories_;
        }
        
        // 注册每种游戏类型的消息处理器
        for (const auto& entry : factories) {
            const auto& gameType = entry.first;
            const auto& factory = entry.second;
            
            // 跳过未启用的游戏
            if (!factory->isEnabled()) {
                LOG_INFO("Skipping message handlers for disabled game type: %s", gameType.c_str());
                continue;
            }
            
            LOG_DEBUG("Registering message handlers for game type: %s", gameType.c_str());
            if (!factory->registerMessageHandlers(router.get())) {
                LOG_ERROR("Failed to register message handlers for game type: %s", gameType.c_str());
                return false;
            }
        }
        
        LOG_INFO("All game message handlers registered successfully");
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Exception while registering message handlers: %s", e.what());
        return false;
    }
}

// 回调相关方法已移除，配置更新现在直接处理

bool GameRegistry::setGameEnabled(const std::string& gameType, bool enabled) {
    std::shared_ptr<GameFactory> factory;
    
    {
        std::lock_guard<std::mutex> lock(gameFactoriesMutex_);
    auto it = gameFactories_.find(gameType);
    if (it == gameFactories_.end()) {
        LOG_ERROR("Game type %s not found for enabling/disabling", gameType.c_str());
        return false;
    }
    
        factory = it->second;
    }
    
    factory->setEnabled(enabled);
    LOG_INFO("Game type %s %s", gameType.c_str(), enabled ? "enabled" : "disabled");
    
    return true;
}

bool GameRegistry::isGameEnabled(const std::string& gameType) const {
    std::lock_guard<std::mutex> lock(gameFactoriesMutex_);
    auto it = gameFactories_.find(gameType);
    if (it == gameFactories_.end()) {
        return false;
    }
    
    return it->second->isEnabled();
}

std::vector<std::string> GameRegistry::getEnabledGameTypes() const {
    std::lock_guard<std::mutex> lock(gameFactoriesMutex_);
    std::vector<std::string> enabledTypes;
    
    for (const auto& entry : gameFactories_) {
        if (entry.second->isEnabled()) {
            enabledTypes.push_back(entry.first);
        }
    }
    
    return enabledTypes;
}

bool GameRegistry::initializeBuiltInGames() {
    try {
        // 从AppContext获取ConfigManager
        auto& appContext = AppContext::getInstance();
        auto configManager = appContext.getConfigManager();
        if (!configManager) {
            LOG_ERROR("ConfigManager not available");
            return false;
        }
        const auto& gameConfigs = configManager->getGameConfigs();
        
        if (gameConfigs.empty()) {
            LOG_ERROR("No game configurations found in ConfigManager");
            return false;
        }
        
        // 遍历所有游戏配置
        for (const auto& [gameType, config] : gameConfigs) {
            LOG_DEBUG("Initializing game type: %s", gameType.c_str());
            
            // 根据游戏类型创建对应的工厂
            std::shared_ptr<GameFactory> factory;
            
            if (gameType == "mines_pro") {
                auto minesFactory = std::make_shared<MinesFactory>();
                // 将配置传递给工厂
                minesFactory->setConfig(config);
                factory = minesFactory;
            }
            // 在这里添加其他游戏类型
            // else if (gameType == "slots") {
            //     auto slotsFactory = std::make_shared<SlotsFactory>();
            //     slotsFactory->setConfig(config);
            //     factory = slotsFactory;
            // }
            else {
                LOG_WARN("Unknown game type: %s, skipping", gameType.c_str());
                continue;
            }
            
            // 注册游戏工厂
            if (!registerGame(gameType, factory)) {
                LOG_ERROR("Failed to register game type: %s", gameType.c_str());
                return false;
            }
            
            LOG_DEBUG("Successfully registered game type: %s", gameType.c_str());
        }
        
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Exception while initializing built-in games: %s", e.what());
        return false;
    }
}

bool GameRegistry::startAllGames() {
    LOG_INFO("Starting all enabled games...");
    
    try {
        std::unordered_map<std::string, std::shared_ptr<GameFactory>> factories;
        {
            std::lock_guard<std::mutex> lock(gameFactoriesMutex_);
            factories = gameFactories_;
        }
        
        for (const auto& entry : factories) {
            const auto& gameType = entry.first;
            const auto& factory = entry.second;
            
            if (!factory->isEnabled()) {
                LOG_INFO("Skipping disabled game type: %s", gameType.c_str());
                continue;
            }
            
            LOG_DEBUG("Starting game type: %s", gameType.c_str());
            if (!factory->start()) {
                LOG_ERROR("Failed to start game type: %s", gameType.c_str());
                return false;
            }
            
            LOG_DEBUG("Successfully started game type: %s", gameType.c_str());
        }
        
        LOG_INFO("All enabled games started successfully");
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Exception while starting games: %s", e.what());
        return false;
    }
}

void GameRegistry::stopAllGames() {
    LOG_INFO("Stopping all games...");
    
    try {
        std::unordered_map<std::string, std::shared_ptr<GameFactory>> factories;
        {
            std::lock_guard<std::mutex> lock(gameFactoriesMutex_);
            factories = gameFactories_;
        }
        
        for (const auto& entry : factories) {
            const auto& gameType = entry.first;
            const auto& factory = entry.second;
            
            try {
                LOG_DEBUG("Stopping game type: %s", gameType.c_str());
                factory->stop();
                LOG_DEBUG("Successfully stopped game type: %s", gameType.c_str());
            } catch (const std::exception& e) {
                LOG_ERROR("Exception while stopping game type %s: %s", gameType.c_str(), e.what());
                // 继续停止其他游戏，不因为一个游戏停止失败而中断
            }
        }
        
        LOG_INFO("All games stopped");
    } catch (const std::exception& e) {
        LOG_ERROR("Exception while stopping games: %s", e.what());
    }
}
