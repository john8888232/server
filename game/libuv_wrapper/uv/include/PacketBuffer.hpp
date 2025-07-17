#ifndef    UV_PACKET_BUFFER_HPP
#define    UV_PACKET_BUFFER_HPP

#include   <functional>
#include   <string>
#include   <memory>

#include   "LogWriter.hpp"
#include   "GlobalConfig.hpp"

namespace uv
{
class Packet;
class PacketBuffer
{

public:
    PacketBuffer() {}
    virtual ~PacketBuffer(){}

    virtual int append(const char* data, uint64_t size) = 0;
    virtual int readBufferN(std::string& data, uint64_t N) = 0;
    virtual int clearBufferN(uint64_t N) = 0;
    virtual int clear() = 0;
    virtual uint64_t readSize() = 0;

    int readString(std::string& out)
    {
        if (nullptr != GlobalConfig::ReadBufferString)
            return GlobalConfig::ReadBufferString(this, out);
        LOG_ERROR("not defined packet parse func.");
        return -1;
    }

    int readPacket(Packet& out)
    {
        if (nullptr != GlobalConfig::ReadBufferPacket)
            return GlobalConfig::ReadBufferPacket(this, out);
        LOG_ERROR("not defined packet parse func.");
        return -1;
    }

    int readGeneric(void* out)
    {
        if (nullptr != GlobalConfig::ReadBufferVoid)
            return GlobalConfig::ReadBufferVoid(this, out);
        LOG_ERROR("not defined packet parse func.");
        return -1;
    }
};

using PacketBufferPtr = std::shared_ptr<PacketBuffer>;
}

#endif

