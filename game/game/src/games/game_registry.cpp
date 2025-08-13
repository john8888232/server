#include "game_registry.h"
#include "third_party/libuv_cpp/include/LogWriter.hpp"
#include "core/infrastructure/common/dependency_container.h"
#include "mines_pro/domain/services/mines_factory.h"

extern DependencyContainer& getDependencyContainer();

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

std::shared_ptr<GameFactory> GameRegistry::getFactory(const std::string& gameType) const {
    std::lock_guard<std::mutex> lock(gameFactoriesMutex_);
    auto it = gameFactories_.find(gameType);
    if (it != gameFactories_.end()) {
        return it->second;
    }
    return nullptr;
}

std::vector<std::pair<std::string, std::shared_ptr<GameFactory>>> GameRegistry::getAllFactories() const {
    std::vector<std::pair<std::string, std::shared_ptr<GameFactory>>> result;
    std::lock_guard<std::mutex> lock(gameFactoriesMutex_);
    
    for (const auto& entry : gameFactories_) {
        result.push_back(entry);
    }
    
    return result;
}

bool GameRegistry::registerMessageHandlers(std::shared_ptr<MessageRouter> router) {
    if (!router) {
        LOG_ERROR("Message router is null");
        return false;
    }
    
    LOG_INFO("Registering message handlers for all game types");
    bool success = true;
    
    auto factories = getAllFactories();
    for (const auto& pair : factories) {
        const auto& gameType = pair.first;
        auto factory = pair.second;
        
        if (factory && factory->isEnabled()) {
            LOG_DEBUG("Registering message handlers for game type: %s", gameType.c_str());
            if (!factory->registerMessageHandlers(router.get())) {
                LOG_ERROR("Failed to register message handlers for game type: %s", gameType.c_str());
                success = false;
            } else {
                LOG_INFO("Successfully registered message handlers for game type: %s", gameType.c_str());
            }
        } else {
            LOG_DEBUG("Game type %s is disabled, skipping message handler registration", gameType.c_str());
        }
    }
    
    return success;
}

bool GameRegistry::startGame(const std::string& gameType) {
    auto factory = getFactory(gameType);
    if (!factory) {
        LOG_ERROR("Game type %s not registered", gameType.c_str());
        return false;
    }
    
    LOG_INFO("Starting game service: %s", gameType.c_str());
    return factory->start();
}

bool GameRegistry::startAllGames() {
    LOG_INFO("Starting all game services");
    bool success = true;
    
    auto factories = getAllFactories();
    for (const auto& pair : factories) {
        const auto& gameType = pair.first;
        auto factory = pair.second;
        
        if (factory && factory->isEnabled()) {
            LOG_DEBUG("Starting game type: %s", gameType.c_str());
            if (!factory->start()) {
                LOG_ERROR("Failed to start game type: %s", gameType.c_str());
                success = false;
            } else {
                LOG_INFO("Game type %s started successfully", gameType.c_str());
            }
        } else {
            LOG_DEBUG("Game type %s is disabled, skipping", gameType.c_str());
        }
    }
    
    return success;
}

void GameRegistry::stopGame(const std::string& gameType) {
    auto factory = getFactory(gameType);
    if (factory) {
        LOG_INFO("Stopping game service: %s", gameType.c_str());
        factory->stop();
    } else {
        LOG_WARN("Cannot stop unknown game type: %s", gameType.c_str());
    }
}

void GameRegistry::stopAllGames() {
    LOG_INFO("Stopping all game services");
    
    auto factories = getAllFactories();
    for (const auto& pair : factories) {
        const auto& gameType = pair.first;
        auto factory = pair.second;
        
        if (factory) {
            LOG_DEBUG("Stopping game type: %s", gameType.c_str());
            factory->stop();
        }
    }
    
    LOG_INFO("All game services stopped");
}

void GameRegistry::onGameConfigUpdated(const std::string& gameType) {
    auto factory = getFactory(gameType);
    if (!factory) {
        LOG_WARN("Cannot update config for unknown game type: %s", gameType.c_str());
        return;
    }
    
    auto& container = getDependencyContainer();
    auto appContext = container.resolve<AppContext>();
    if (!appContext) {
        LOG_ERROR("AppContext not available in dependency container");
        return;
    }
    
    auto configManager = appContext->getConfigManager();
    if (!configManager) {
        LOG_ERROR("ConfigManager not available");
        return;
    }
    
    const auto& gameConfigs = configManager->getGameConfigs();
    auto configIt = gameConfigs.find(gameType);
    if (configIt == gameConfigs.end()) {
        LOG_WARN("No config found for game type: %s", gameType.c_str());
        return;
    }
    
    LOG_INFO("Updating config for game type: %s", gameType.c_str());
    
    // 获取旧配置
    const auto& oldConfig = factory->getConfig();
    
    // 设置新配置
    factory->setConfig(configIt->second);
    
    // 触发配置更新通知
    factory->onConfigUpdated(configIt->second);
    
    LOG_INFO("Game config update completed for: %s", gameType.c_str());
}

bool GameRegistry::initializeBuiltInGames() {
    try {
        auto& container = getDependencyContainer();
        auto appContext = container.resolve<AppContext>();
        if (!appContext) {
            LOG_ERROR("AppContext not available in dependency container");
            return false;
        }
        
        auto configManager = appContext->getConfigManager();
        if (!configManager) {
            LOG_ERROR("ConfigManager not available");
            return false;
        }
        
        // 获取数据库工厂
        auto dbFactory = container.resolve<DatabaseFactory>();
        if (!dbFactory) {
            LOG_ERROR("DatabaseFactory not available in dependency container");
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
                // 创建MinesFactory时使用依赖容器
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
        LOG_ERROR("Exception in initializeBuiltInGames: %s", e.what());
        return false;
    }
}
