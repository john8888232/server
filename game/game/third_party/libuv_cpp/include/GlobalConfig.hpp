#ifndef UV_GLOBAL_CONFIG_HPP
#define UV_GLOBAL_CONFIG_HPP

#include <string>
#include <functional>
#include <cstdint>

namespace uv
{
class PacketBuffer;
class Packet;
using ReadBufferStringFunc = std::function<int(PacketBuffer*, std::string&)>;
using ReadBufferPacketFunc = std::function<int(PacketBuffer*, Packet&)>;
using ReadBufferVoidFunc = std::function<int(PacketBuffer*, void*)>;

class GlobalConfig
{
public:
    enum BufferMode
    {
        NoBuffer,
        CycleBuffer,
        ListBuffer
    };
    static BufferMode BufferModeStatus;
    static uint64_t   CycleBufferSize;


    static ReadBufferStringFunc ReadBufferString;
    static ReadBufferPacketFunc ReadBufferPacket;
    static ReadBufferVoidFunc ReadBufferVoid;
};
}
#endif
