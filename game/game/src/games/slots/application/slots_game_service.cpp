#include "slots_game_service.h"
#include "games/game_factory.h"
#include "games/game_def.h"
#include <third_party/libuv_cpp/include/LogWriter.hpp>
#include <random>
#include <sstream>
#include <chrono>
#include "core/infrastructure/common/app_context.h"
#include <iomanip>
#include <ctime>

SlotsGameService::SlotsGameService(GameFactory* factory) : factory_(factory) {
    LOG_INFO("SlotsGameService initialized");
}

SlotsGameService::~SlotsGameService() {
}

std::shared_ptr<IGame> SlotsGameService::createGame() {
    if (!factory_) {
        LOG_ERROR("Factory is null");
        return nullptr;
    }
    
    // 新的游戏ID格式: 4位游戏类型ID + 4位服务器ID + 12位时间戳(年月日时分秒) + 5位随机数
    // 例如: 0002000126062723402712345
    
    std::string gameType = factory_->getGameType();
    
    // 获取游戏类型对应的数字ID (4位，不足补0)
    int gameTypeId = GameDef::getGameTypeId(gameType);
    std::stringstream gameTypeStream;
    gameTypeStream << std::setfill('0') << std::setw(4) << gameTypeId;
    std::string gameTypePart = gameTypeStream.str();
    
    // 获取服务器ID（4位，不足补0）
    auto& appContext = AppContext::getInstance();
    auto gameManager = appContext.getGameManager();
    std::string serverId = gameManager->getServerId();
    
    // 从服务器ID中提取数字部分，例如从"game_server_1"中提取"1"
    std::string serverIdNum = "0";
    size_t underscorePos = serverId.find_last_of('_');
    if (underscorePos != std::string::npos && underscorePos + 1 < serverId.length()) {
        serverIdNum = serverId.substr(underscorePos + 1);
    }
    
    // 服务器ID部分固定4位，不足补0
    std::stringstream serverIdStream;
    try {
        int serverIdInt = std::stoi(serverIdNum);
        serverIdStream << std::setfill('0') << std::setw(4) << serverIdInt;
    } catch (const std::exception& e) {
        // 如果解析失败，使用默认值0001
        serverIdStream << "0001";
    }
    std::string serverIdPart = serverIdStream.str();
    
    // 获取当前时间并格式化为 年月日时分秒 (12位)
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto tm = *std::localtime(&time_t);
    
    std::stringstream timeStream;
    timeStream << std::setfill('0') 
               << std::setw(2) << (tm.tm_year % 100)  // 年份取后2位
               << std::setw(2) << (tm.tm_mon + 1)      // 月份
               << std::setw(2) << tm.tm_mday           // 日期
               << std::setw(2) << tm.tm_hour           // 小时
               << std::setw(2) << tm.tm_min            // 分钟
               << std::setw(2) << tm.tm_sec;           // 秒钟
    std::string timePart = timeStream.str();
    
    // 生成5位随机数
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(10000, 99999);
    int randomNum = dis(gen);
    
    // 组合生成最终ID: 4+4+12+5 = 25位固定长度
    std::stringstream ss;
    ss << gameTypePart << serverIdPart << timePart << randomNum;
    std::string roundId = ss.str();
    
    LOG_INFO("Creating slots game with roundId: %s (gameType=%s->%d, serverId=%s->%s, time=%s, random=%d)", 
             roundId.c_str(), gameType.c_str(), gameTypeId, serverId.c_str(), serverIdPart.c_str(), timePart.c_str(), randomNum);
    
    // TODO: 创建SlotsGame实例
    // 对于slots这种按需创建的游戏，在createGame时只创建实例并设置配置
    // 不生成roundID（在玩家触发start时生成）
    LOG_INFO("Created slots game instance in INIT status (roundID will be generated on start)");
    // 这里需要实现具体的SlotsGame类
    return nullptr;
}



bool SlotsGameService::destroyGame(std::shared_ptr<IGame> game) {
    if (!game) {
        LOG_ERROR("Invalid game pointer in destroyGame");
        return false;
    }
    
    LOG_INFO("Destroying slots game: %s", game->roundID().c_str());
    
    // 停止游戏
    game->stop();
    
    // 从GameManager中移除
    auto& appContext = AppContext::getInstance();
    auto gameManager = appContext.getGameManager();
    if (!gameManager->removeGame(game)) {
        LOG_ERROR("Failed to remove slots game from manager");
        return false;
    }
    
    LOG_INFO("Slots game destroyed and removed from manager");
    return true;
}

 