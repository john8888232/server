#ifndef MYSQL_DATA_DEF_H
#define MYSQL_DATA_DEF_H

#include <variant>
#include <string>
#include <vector>
#include <chrono>
#include <cstdint>
#include <optional>

// 为 MySQL DATETIME/DATE/TIME 类型定义结构
struct MySQLDateTime {
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
    int microsecond;
    
    MySQLDateTime(int y = 0, int mo = 0, int d = 0, int h = 0, int mi = 0, int s = 0, int ms = 0)
        : year(y), month(mo), day(d), hour(h), minute(mi), second(s), microsecond(ms) {}
};

// 为 BLOB/BINARY 数据定义类型
using BinaryData = std::vector<std::uint8_t>;

// 为 NULL 值定义类型
struct MySQLNull {};

// 定义可以对应 MySQL 所有常见数据类型的 variant
using MySQLParamValue = std::variant<
    // 整数类型
    int8_t,              // 微小整型
    int16_t,             // 小整型
    int32_t,             // 整型、中整型
    int64_t,             // 大整型
    uint8_t,             // 无符号微小整型
    uint16_t,            // 无符号小整型
    uint32_t,            // 无符号整型、无符号中整型
    uint64_t,            // 无符号大整型
    
    // 浮点类型
    float,               // 单精度浮点型
    double,              // 双精度浮点型、实数型
    
    // 字符串类型
    std::string,         // 变长字符串、字符串、文本
    
    // 二进制类型
    BinaryData,          // 二进制大对象、二进制、可变二进制
    
    // 布尔类型
    bool,                // 布尔型、微小整型(1)
    
    // 日期和时间类型
    MySQLDateTime,       // 日期时间、日期、时间、时间戳
    
    // NULL 值
    MySQLNull,
    
    // 可选类型（用于可空字段）
    std::optional<std::string>,
    std::optional<int32_t>,
    std::optional<int64_t>,
    std::optional<double>
>;

#endif // MYSQL_DATA_DEF_H