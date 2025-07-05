#ifndef ERROR_CODE_H
#define ERROR_CODE_H

namespace ErrorCode {
    // 系统错误码 (0-999)
    const int SUCCESS = 0;
    const int SYSTEM_ERROR = 1;
    const int DATABASE_ERROR = 2;
    const int NETWORK_ERROR = 3;
    const int INVALID_REQUEST = 4;
    const int SERVICE_UNAVAILABLE = 5;
    
    // 用户相关错误码 (1000-1999)
    const int NOT_FOUND = 1000;
    const int INVALID_CREDENTIALS = 1001;
    const int DISABLED = 1002;
    const int ALREADY_EXISTS = 1003;
    const int INVALID_USER_ID = 1004;
    const int INVALID_TOKEN = 1005;
    const int INACTIVE = 1006;
    const int OTHER_LOGIN = 1007;
    const int IN_GAME = 1008;
    
    // 游戏相关错误码 (2000-2999)
    const int GAME_NOT_FOUND = 2000;
    const int GAME_DISABLED = 2001;
    const int INSUFFICIENT_BALANCE = 2002;
    const int INVALID_BET = 2003;
    const int GAME_SESSION_EXPIRED = 2004;
    
    // 通用游戏操作错误码 (2005-2099)
    const int INVALID_REQUEST_FORMAT = 2005;
    const int NO_ACTIVE_GAME = 2006;
    const int INVALID_GAME_PHASE = 2007;
    const int INVALID_ROUND_ID = 2008;
    const int GAME_SERVER_ERROR = 2009;
    const int SESSION_NOT_FOUND = 2010;
    const int PLAYER_NOT_FOUND = 2011;
    const int INVALID_BET_AMOUNT = 2012;
    const int INVALID_PLAY_TYPE = 2013;
    const int GAME_INTERNAL_ERROR = 2014;
    const int CASH_NOT_ALLOWED = 2015;
    const int BET_NOT_ALLOWED = 2016;
    const int ALREADY_CASHED_OUT = 2017;
    const int NO_BET_TO_CANCEL = 2018;
    const int AUTO_CASH_ALREADY_ENABLED = 2019;
    const int AUTO_CASH_NOT_ENABLED = 2020;
    
    // 获取错误消息
    inline const char* getErrorMessage(int errorCode) {
        switch (errorCode) {
            case SUCCESS: return "success";
            case SYSTEM_ERROR: return "system error";
            case DATABASE_ERROR: return "database error";
            case NETWORK_ERROR: return "network error";
            case INVALID_REQUEST: return "invalid request";
            case SERVICE_UNAVAILABLE: return "service unavailable";
            
            case NOT_FOUND: return "user not found";
            case INVALID_CREDENTIALS: return "invalid credentials";
            case DISABLED: return "user disabled";
            case ALREADY_EXISTS: return "user already exists";
            case INVALID_USER_ID: return "invalid user id";
            case INVALID_TOKEN: return "invalid token";
            
            case GAME_NOT_FOUND: return "game not found";
            case GAME_DISABLED: return "game disabled";
            case INSUFFICIENT_BALANCE: return "insufficient balance";
            case INVALID_BET: return "invalid bet";
            case GAME_SESSION_EXPIRED: return "game session expired";
            
            case INVALID_REQUEST_FORMAT: return "invalid request format";
            case NO_ACTIVE_GAME: return "no active game";
            case INVALID_GAME_PHASE: return "invalid game phase";
            case INVALID_ROUND_ID: return "invalid round id";
            case GAME_SERVER_ERROR: return "game server error";
            case SESSION_NOT_FOUND: return "session not found";
            case PLAYER_NOT_FOUND: return "player not found";
            case INVALID_BET_AMOUNT: return "invalid bet amount";
            case INVALID_PLAY_TYPE: return "invalid play type";
            case GAME_INTERNAL_ERROR: return "game internal error";
            case CASH_NOT_ALLOWED: return "cash not allowed";
            case BET_NOT_ALLOWED: return "bet not allowed";
            case ALREADY_CASHED_OUT: return "already cashed out";
            case NO_BET_TO_CANCEL: return "no bet to cancel";
            case AUTO_CASH_ALREADY_ENABLED: return "auto cash already enabled";
            case AUTO_CASH_NOT_ENABLED: return "auto cash not enabled";
            
            default: return "unknown error";
        }
    }
}

#endif // ERROR_CODE_H 