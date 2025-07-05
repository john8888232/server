#include "user.h"
#include <third_party/libuv_cpp/include/LogWriter.hpp>
#include "core/infrastructure/persistence/database_factory.h"
#include "core/infrastructure/persistence/mysql_client.h"
#include "core/infrastructure/common/config_manager.h"
#include <sstream>

User::User(int64_t playerId, 
           int avatarId, 
           const std::string& loginName, 
           const std::string& nickName, 
           double amount, 
           int vipLevel, 
           const std::string& currency,
           Status playerStatus)
    : playerId_(playerId),
      avatarId_(avatarId),
      loginName_(loginName),
      nickName_(nickName),
      amount_(amount),
      vipLevel_(vipLevel),
      currency_(currency),
      playerStatus_(playerStatus) {
}
