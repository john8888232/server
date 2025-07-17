#ifndef DEPENDENCY_CONTAINER_H
#define DEPENDENCY_CONTAINER_H

#include <memory>
#include <unordered_map>
#include <string>
#include <typeindex>
#include <typeinfo>
#include <stdexcept>

// 依赖注入容器
class DependencyContainer {
public:
    DependencyContainer() = default;
    ~DependencyContainer() = default;
    
    // 禁止拷贝和移动
    DependencyContainer(const DependencyContainer&) = delete;
    DependencyContainer& operator=(const DependencyContainer&) = delete;
    DependencyContainer(DependencyContainer&&) = delete;
    DependencyContainer& operator=(DependencyContainer&&) = delete;

    // 注册依赖实例
    template<typename T>
    void registerDependency(std::shared_ptr<T> instance) {
        if (!instance) {
            throw std::invalid_argument("Cannot register null instance");
        }
        
        std::type_index typeId = std::type_index(typeid(T));
        dependencies_[typeId] = instance;
    }
    
    // 创建依赖实例并注册
    template<typename T, typename... Args>
    std::shared_ptr<T> create(Args&&... args) {
        auto instance = std::make_shared<T>(std::forward<Args>(args)...);
        registerDependency<T>(instance);
        return instance;
    }
    
    // 解析依赖
    template<typename T>
    std::shared_ptr<T> resolve() const {
        std::type_index typeId = std::type_index(typeid(T));
        auto it = dependencies_.find(typeId);
        if (it != dependencies_.end()) {
            return std::static_pointer_cast<T>(it->second);
        }
        return nullptr;
    }
    
    // 检查依赖是否已注册
    template<typename T>
    bool isRegistered() const {
        std::type_index typeId = std::type_index(typeid(T));
        return dependencies_.find(typeId) != dependencies_.end();
    }
    
    // 清除所有注册的依赖
    void clear() {
        dependencies_.clear();
    }

private:
    std::unordered_map<std::type_index, std::shared_ptr<void>> dependencies_;
};

#endif // DEPENDENCY_CONTAINER_H 