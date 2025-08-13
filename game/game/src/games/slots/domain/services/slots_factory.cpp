#include "slots_factory.h"
#include "third_party/libuv_cpp/include/LogWriter.hpp"
#include "games/slots/application/slots_game_service.h"
#include "third_party/libuv_cpp/include/LogWriter.hpp"

SlotsFactory::SlotsFactory() {
    gameService_ = std::make_shared<SlotsGameService>(this);
}

SlotsFactory::~SlotsFactory() {
}

std::shared_ptr<IGameService> SlotsFactory::getGameService() {
    return gameService_;
}

std::shared_ptr<IBetService> SlotsFactory::createBetService() {
    LOG_INFO("Creating SlotsBetService instance");
    // TODO: 实现SlotsGameService后取消注释
    // return std::make_shared<SlotsBetService>();
    return nullptr;
}

bool SlotsFactory::registerMessageHandlers(MessageRouter* router) {
    if (!router) {
        LOG_ERROR("Message router is null");
        return false;
    }
    
    // TODO: 注册slots游戏的消息处理器
    LOG_INFO("Registered message handlers for slots game");
    return true;
}

std::string SlotsFactory::getGameType() const {
    return "slots";
}

bool SlotsFactory::start() {
    LOG_INFO("Starting slots game service");
    // 对于ON_DEMAND模式，不需要在启动时创建游戏实例
    return true;
}

void SlotsFactory::stop() {
    LOG_INFO("Stopping slots game service");
    // 清理资源
}

GameFactory::GameCreationMode SlotsFactory::getGameCreationMode() const {
    return GameFactory::GameCreationMode::ON_DEMAND;
}