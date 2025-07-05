#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <cstdint>
#include <unordered_set>

namespace Protocol {
    // 网关协议
    const uint32_t GW_CONN_CLOSE = 0x10001; // 网关连接关闭通知
    
    // 系统协议
    const uint32_t HEARTBEAT_PROTOCOL_ID = 0x11000; // 心跳
    
    // 玩家协议
    const uint32_t CS_LOGIN_REQ = 0x11001; // 登录请求，在close tab或者断网后需要重新发送LOGIN_REQ到服务器
    const uint32_t SC_LOGIN_RES = 0x11002; // 登录响应
    const uint32_t CS_LOGOUT_REQ = 0x11003; // 正常退出请求，至于退到哪里，由于目前是没有大厅，所以看产品需求
    const uint32_t CS_LOGOUT_RES = 0x11004; // 正常退出响应
    const uint32_t SC_KICK_PLAYER_NOTIFY = 0x11006; // 一直收不到包更新玩家时间戳的用户180s后会被踢出，或者被顶号的玩家也会被踢出
    const uint32_t SC_GAME_SNAPSHOT_NOTIFY = 0x11008; // 游戏快照统一格式，进入游戏会收到，开牌阶段每秒会收到
    
    // 扫雷游戏协议
    const uint32_t CS_MINES_PLACE_BET_REQ = 0x20001; // 下注请求
    const uint32_t SC_MINES_PLACE_BET_RES = 0x20002; // 下注回包
    const uint32_t CS_MINES_AUTO_CASH_REQ = 0x20003; // 设置自动cash请求
    const uint32_t SC_MINES_AUTO_CASH_RES = 0x20004; // 设置自动cash回包
    const uint32_t CS_MINES_CASH_REQ = 0x20005; // 手动cash请求
    const uint32_t SC_MINES_CASH_RES = 0x20006; // 手动cash回包
    const uint32_t CS_MINES_CANCEL_BET_REQ = 0x20007; // 取消下注请求
    const uint32_t SC_MINES_CANCEL_BET_RES = 0x20008; // 取消下注回包
    const uint32_t SC_MINES_REVEAL_TILE_RESULT = 0x20009; // 扫雷的游戏结果
    const uint32_t SC_MINES_START_JETTON_NOTIFY = 0x2000A; // 开始下注通知
    const uint32_t SC_GAME_STOP_JETTON_NOTIFY = 0x2000C; // 停止下注通知
    const uint32_t SC_GAME_RANK_INFO_NOTIFY = 0x2000E; // 游戏榜单信息通知
}

#endif // PROTOCOL_H
