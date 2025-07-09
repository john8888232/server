// 示例：如何使用 MinesGame 数据落地系统
#include "games/mines_pro/infrastructure/repositories/mines_game_repository.h"
#include <iostream>
#include <vector>

// 测试示例：批量更新玩家余额
void testBatchUpdatePlayerBalances() {
    std::cout << "=== 测试批量更新玩家余额 ===" << std::endl;
    
    // 创建 repository 实例
    auto repository = std::make_shared<MinesGameRepositoryImpl>();
    
    // 准备测试数据
    std::vector<PlayerBalanceUpdate> updates;
    
    // 玩家1：下注100，中奖150，余额从1000变为1050
    PlayerBalanceUpdate update1;
    update1.loginName = "player001";
    update1.originalBalance = 1000.0;
    update1.newBalance = 1050.0;
    update1.reason = "mines_game_result_20241201_001";
    updates.push_back(update1);
    
    // 玩家2：下注200，未中奖，余额从500变为300
    PlayerBalanceUpdate update2;
    update2.loginName = "player002";
    update2.originalBalance = 500.0;
    update2.newBalance = 300.0;
    update2.reason = "mines_game_result_20241201_001";
    updates.push_back(update2);
    
    // 玩家3：下注50，中奖100，余额从800变为850
    PlayerBalanceUpdate update3;
    update3.loginName = "player003";
    update3.originalBalance = 800.0;
    update3.newBalance = 850.0;
    update3.reason = "mines_game_result_20241201_001";
    updates.push_back(update3);
    
    // 执行批量更新
    std::cout << "执行批量更新..." << std::endl;
    auto results = repository->updatePlayerBalancesBatch(updates);
    
    // 处理结果
    int successCount = 0;
    int failureCount = 0;
    
    for (const auto& result : results) {
        if (result.success) {
            successCount++;
            std::cout << "✅ 玩家 " << result.loginName 
                      << " 余额更新成功: " << result.originalBalance 
                      << " -> " << result.actualBalance << std::endl;
        } else {
            failureCount++;
            std::cout << "❌ 玩家 " << result.loginName 
                      << " 余额更新失败: " << result.errorMessage << std::endl;
        }
    }
    
    std::cout << "更新完成: " << successCount << " 成功, " 
              << failureCount << " 失败" << std::endl;
}

// 测试示例：单个玩家余额更新
void testSinglePlayerUpdate() {
    std::cout << "\n=== 测试单个玩家余额更新 ===" << std::endl;
    
    auto repository = std::make_shared<MinesGameRepositoryImpl>();
    
    std::string loginName = "test_player";
    
    // 获取当前余额
    double currentBalance = repository->getPlayerBalance(loginName);
    std::cout << "当前余额: " << currentBalance << std::endl;
    
    // 更新余额
    double newBalance = currentBalance + 100.0;
    bool success = repository->updatePlayerBalance(loginName, newBalance);
    
    if (success) {
        std::cout << "✅ 余额更新成功: " << currentBalance 
                  << " -> " << newBalance << std::endl;
    } else {
        std::cout << "❌ 余额更新失败" << std::endl;
    }
    
    // 验证更新结果
    double actualBalance = repository->getPlayerBalance(loginName);
    std::cout << "实际余额: " << actualBalance << std::endl;
}

// 测试示例：错误处理
void testErrorHandling() {
    std::cout << "\n=== 测试错误处理 ===" << std::endl;
    
    auto repository = std::make_shared<MinesGameRepositoryImpl>();
    
    // 测试不存在的玩家
    std::vector<PlayerBalanceUpdate> updates;
    
    PlayerBalanceUpdate update;
    update.loginName = "non_existent_player";
    update.originalBalance = 1000.0;
    update.newBalance = 1100.0;
    update.reason = "test_error_handling";
    updates.push_back(update);
    
    // 测试余额不匹配的情况
    PlayerBalanceUpdate update2;
    update2.loginName = "existing_player";
    update2.originalBalance = 9999.0;  // 假设的错误原始余额
    update2.newBalance = 10099.0;
    update2.reason = "test_balance_mismatch";
    updates.push_back(update2);
    
    auto results = repository->updatePlayerBalancesBatch(updates);
    
    for (const auto& result : results) {
        if (result.success) {
            std::cout << "✅ " << result.loginName << " 更新成功" << std::endl;
        } else {
            std::cout << "❌ " << result.loginName 
                      << " 更新失败: " << result.errorMessage << std::endl;
        }
    }
}

// 模拟游戏结束后的数据落地流程
void simulateGameEndDataPersistence() {
    std::cout << "\n=== 模拟游戏结束数据落地 ===" << std::endl;
    
    // 模拟游戏数据
    struct GamePlayer {
        std::string loginName;
        double currentBalance;
        double betAmount;
        double winAmount;
    };
    
    std::vector<GamePlayer> gamePlayers = {
        {"player_a", 1000.0, 100.0, 0.0},      // 下注100，未中奖
        {"player_b", 500.0, 50.0, 125.0},      // 下注50，中奖125
        {"player_c", 800.0, 200.0, 400.0},     // 下注200，中奖400
        {"player_d", 1200.0, 150.0, 0.0}       // 下注150，未中奖
    };
    
    // 构造余额更新请求
    std::vector<PlayerBalanceUpdate> updates;
    for (const auto& player : gamePlayers) {
        double balanceChange = player.winAmount - player.betAmount;
        double newBalance = player.currentBalance + balanceChange;
        
        PlayerBalanceUpdate update;
        update.loginName = player.loginName;
        update.originalBalance = player.currentBalance;
        update.newBalance = newBalance;
        update.reason = "mines_game_result_simulation";
        
        updates.push_back(update);
        
        std::cout << "准备更新 " << player.loginName 
                  << ": 下注=" << player.betAmount 
                  << ", 中奖=" << player.winAmount 
                  << ", 余额变化=" << balanceChange 
                  << " (" << player.currentBalance 
                  << " -> " << newBalance << ")" << std::endl;
    }
    
    // 执行批量更新
    auto repository = std::make_shared<MinesGameRepositoryImpl>();
    auto results = repository->updatePlayerBalancesBatch(updates);
    
    // 统计结果
    int successCount = 0;
    int failureCount = 0;
    
    for (const auto& result : results) {
        if (result.success) {
            successCount++;
            std::cout << "✅ " << result.loginName 
                      << " 余额更新成功: " << result.actualBalance << std::endl;
        } else {
            failureCount++;
            std::cout << "❌ " << result.loginName 
                      << " 余额更新失败: " << result.errorMessage << std::endl;
        }
    }
    
    std::cout << "游戏结束数据落地完成: " << successCount 
              << " 成功, " << failureCount << " 失败" << std::endl;
}

int main() {
    std::cout << "Mines Game 数据落地系统测试" << std::endl;
    std::cout << "================================" << std::endl;
    
    try {
        // 测试批量更新
        testBatchUpdatePlayerBalances();
        
        // 测试单个玩家更新
        testSinglePlayerUpdate();
        
        // 测试错误处理
        testErrorHandling();
        
        // 模拟游戏结束数据落地
        simulateGameEndDataPersistence();
        
    } catch (const std::exception& e) {
        std::cerr << "测试过程中发生异常: " << e.what() << std::endl;
        return 1;
    }
    
    std::cout << "\n测试完成！" << std::endl;
    return 0;
}

/*
编译命令示例:
g++ -std=c++17 -I/path/to/includes \
    mines_game_test_example.cpp \
    src/games/mines_pro/infrastructure/repositories/mines_game_repository.cpp \
    -lmysqlcppconn8 -o mines_game_test

运行示例:
./mines_game_test
*/