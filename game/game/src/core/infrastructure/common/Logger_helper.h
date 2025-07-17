#ifndef LOGGER_HELPER_H
#define LOGGER_HELPER_H

class LoggerHelper {
public:
    static bool Initialize();
    
    // 清理日志器资源
    static void Cleanup();
};

#endif // LOGGER_HELPER_H