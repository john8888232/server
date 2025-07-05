#include "third_party/libuv_cpp/include/LogWriter.hpp"
#include "config_manager.h"

class LoggerHelper {
public:
    static bool Initialize();
    
    // 清理日志器资源
    static void Cleanup();
};