// src/core/infrastructure/common/config_manager.h
#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <string>
#include <memory>
#include <unordered_map>
#include <functional>
#include <third_party/nlohmann/json.hpp>

using json = nlohmann::json;

class ConfigManager {
public:
    ConfigManager() = default;
    ~ConfigManager() = default;

    // 初始化系统配置（不加载游戏配置）
    bool loadServerConfig();
    
    // 从Redis加载游戏配置（在DB初始化后调用）
    bool loadGameConfig();
    
    // 获取服务器配置
    const json& getServerConfig() const;
    
    // 获取特定游戏配置
    json getGameConfig(const std::string& gameId) const;
    
    // 获取所有游戏配置
    const std::unordered_map<std::string, json>& getGameConfigs() const { return gameConfigs_; }
    
    // 从Redis重新加载特定游戏配置并通知游戏更新 - 这是唯一的配置重载入口
    bool reloadGameConfigFromRedis(const std::string& gameType);
    
    // 获取配置值的辅助方法
    template<typename T>
    T getValue(const std::string& path, const T& defaultValue = T()) const;
    
    // 检查配置路径是否存在
    bool hasPath(const std::string& path) const;
    
    // 移除拷贝构造和赋值
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;

private:
    // 解析路径字符串 ("server.network.port")
    json getValueFromPath(const json& root, const std::string& path) const;
    
    json serverConfig_;
    std::unordered_map<std::string, json> gameConfigs_;
};

// 模板方法实现
template<typename T>
T ConfigManager::getValue(const std::string& path, const T& defaultValue) const {
    json value = getValueFromPath(serverConfig_, path);
    if (value.is_null()) {
        return defaultValue;
    }
    
    try {
        return value.get<T>();
    } catch (const std::exception&) {
        return defaultValue;
    }
}

#endif // CONFIG_MANAGER_H
