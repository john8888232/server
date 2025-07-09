# Mines Game 数据落地方法设计和实现

## 概述

本文档描述了为 mines_game 实现的数据落地方法，主要功能是更新 MySQL 中 `sys_player` 表的余额，并确保单个玩家更新失败不影响其他玩家的余额更新。

## 设计思路

### 1. 核心架构

- **Repository 模式**: 使用 `MinesGameRepository` 抽象类和 `MinesGameRepositoryImpl` 实现类
- **批量处理**: 支持批量更新多个玩家的余额，每个玩家使用独立的数据库事务
- **错误隔离**: 单个玩家更新失败不会影响其他玩家的余额更新
- **余额一致性**: 使用数据库事务和乐观锁确保余额更新的一致性

### 2. 数据结构设计

#### PlayerBalanceUpdate
```cpp
struct PlayerBalanceUpdate {
    std::string loginName;      // 玩家登录名
    double originalBalance;     // 原始余额
    double newBalance;          // 新余额
    std::string reason;         // 更新原因
};
```

#### PlayerBalanceUpdateResult
```cpp
struct PlayerBalanceUpdateResult {
    std::string loginName;      // 玩家登录名
    double originalBalance;     // 原始余额
    double newBalance;          // 期望的新余额
    double actualBalance;       // 实际数据库中的余额
    bool success;              // 是否更新成功
    std::string errorMessage;   // 错误信息
};
```

## 实现细节

### 1. 数据库操作类 (MinesGameRepositoryImpl)

#### 核心方法

**updatePlayerBalance**: 更新单个玩家余额
```cpp
bool updatePlayerBalance(const std::string& loginName, double newBalance);
```

**getPlayerBalance**: 获取玩家当前余额
```cpp
double getPlayerBalance(const std::string& loginName);
```

**updatePlayerBalancesBatch**: 批量更新玩家余额
```cpp
std::vector<PlayerBalanceUpdateResult> updatePlayerBalancesBatch(
    const std::vector<PlayerBalanceUpdate>& updates);
```

#### 事务处理特点

- 每个玩家使用独立的数据库事务
- 使用 `FOR UPDATE` 锁定玩家记录
- 验证原始余额匹配性（防止并发修改）
- 更新 `sys_player` 表的 `amount` 字段和 `update_time` 字段

### 2. 游戏数据落地流程 (writeGameResultToDatabase)

#### 处理步骤

1. **数据收集阶段**:
   - 遍历所有游戏中的玩家
   - 计算每个玩家的下注总额
   - 从榜单信息中获取玩家的兑现总额
   - 计算余额变化量

2. **余额更新阶段**:
   - 批量调用 `updatePlayerBalancesBatch` 更新数据库
   - 每个玩家使用独立事务，确保错误隔离

3. **结果处理阶段**:
   - 更新游戏内玩家的余额状态
   - 记录更新成功和失败的统计信息
   - 对失败的玩家记录错误日志

4. **游戏状态保存**:
   - 保存游戏数据到 mines_game 表

#### 关键代码片段

```cpp
void MinesGame::writeGameResultToDatabase() {
    try {
        auto repository = std::make_shared<MinesGameRepositoryImpl>();
        std::vector<PlayerBalanceUpdate> balanceUpdates;
        
        // 收集玩家余额更新数据
        {
            std::shared_lock<std::shared_mutex> playersLock(playersMutex_);
            for (const auto& playerPair : players_) {
                // 计算余额变化
                double balanceChange = totalWinAmount - totalBetAmount;
                double expectedBalance = currentBalance + balanceChange;
                
                // 创建更新请求
                PlayerBalanceUpdate update;
                update.loginName = loginName;
                update.originalBalance = currentBalance;
                update.newBalance = expectedBalance;
                update.reason = "mines_game_result_" + roundID_;
                
                balanceUpdates.push_back(update);
            }
        }
        
        // 批量更新余额
        auto results = repository->updatePlayerBalancesBatch(balanceUpdates);
        
        // 处理更新结果
        for (const auto& result : results) {
            if (result.success) {
                // 更新游戏内玩家余额
                user->setBalance(result.actualBalance);
            } else {
                // 记录错误日志
                LOG_ERROR("Failed to update balance for player %s: %s", 
                         result.loginName.c_str(), result.errorMessage.c_str());
            }
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to write game result to database: %s", e.what());
    }
}
```

## 错误处理和容错机制

### 1. 数据库连接失败
- 记录错误日志
- 继续处理其他玩家

### 2. 余额验证失败
- 检查原始余额与数据库当前余额是否匹配
- 允许小的浮点数误差（0.001）
- 验证失败时回滚事务

### 3. 事务执行失败
- 每个玩家使用独立事务
- 单个玩家失败不影响其他玩家
- 记录详细的错误信息

### 4. 并发控制
- 使用 `FOR UPDATE` 锁定玩家记录
- 验证余额一致性
- 防止并发修改导致的数据不一致

## 性能优化

### 1. 批量处理
- 一次性收集所有玩家的余额更新需求
- 减少数据库连接次数

### 2. 锁粒度控制
- 使用读写锁保护共享数据
- 最小化锁持有时间

### 3. 异常处理
- 捕获并记录所有异常
- 确保不会因为单个玩家的问题影响整个游戏

## 数据库 Schema 要求

### sys_player 表
- `player_id`: 主键
- `login_name`: 玩家登录名（唯一键）
- `amount`: 玩家余额 (decimal(32,2))
- `update_time`: 更新时间戳

### SQL 更新语句
```sql
UPDATE sys_player SET amount = ?, update_time = NOW() WHERE login_name = ?
```

## 日志记录

### 成功日志
- 记录每个玩家的余额更新详情
- 包括原始余额、新余额、下注金额、中奖金额

### 错误日志
- 数据库连接失败
- 余额验证失败
- 事务执行失败
- 各类异常信息

### 统计日志
- 更新成功和失败的玩家数量
- 处理时间和性能指标

## 部署和使用说明

### 文件结构
```
src/games/mines_pro/infrastructure/repositories/
├── mines_game_repository.h      // 接口定义
└── mines_game_repository.cpp    // 实现代码
```

### 编译依赖
- MySQL C++ Connector
- nlohmann/json
- 项目基础设施类

### 使用方式
游戏结束时自动调用 `writeGameResultToDatabase()` 方法进行数据落地。

## 测试建议

### 单元测试
- 测试余额更新的正确性
- 测试事务回滚机制
- 测试并发更新场景

### 集成测试
- 测试与实际数据库的交互
- 测试大量玩家的批量更新
- 测试网络异常情况

### 压力测试
- 模拟高并发游戏场景
- 测试数据库连接池的性能
- 验证错误隔离机制

## 总结

本实现方案成功解决了以下核心需求：

1. ✅ 更新 MySQL 中 `sys_player` 的余额
2. ✅ 更新成功后将余额返回更新玩家身上的余额
3. ✅ 更新失败需要记录该玩家的错误日志
4. ✅ 单个玩家更新失败不影响其他玩家更新余额

该方案具有良好的扩展性、可维护性和错误处理能力，能够满足生产环境的需求。