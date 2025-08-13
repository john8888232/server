#include "user.h"

User::User(uint64_t playerId, 
           const std::string& avatarUrl, 
           const std::string& loginName, 
           const std::string& username,
           const std::string& nickName, 
           double amount, 
           const std::string& currency,
           Status playerStatus,
           int merchantId)
    : playerId_(playerId),
      avatarUrl_(avatarUrl),
      loginName_(loginName),
      username_(username),
      nickName_(nickName),
      amount_(amount),
      currency_(currency),
      playerStatus_(playerStatus),
      merchantId_(merchantId) {
}
