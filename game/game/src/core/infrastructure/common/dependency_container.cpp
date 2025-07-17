#include "dependency_container.h"

// 全局依赖容器实例
DependencyContainer g_dependencyContainer;

// 获取全局依赖容器的函数
DependencyContainer& getDependencyContainer() {
    return g_dependencyContainer;
} 