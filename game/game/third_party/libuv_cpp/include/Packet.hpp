#ifndef  UV_PACKET_HPP
#define  UV_PACKET_HPP

#include <string>
#include "PacketBuffer.hpp"
//Packet:
//------------------------------------------------
//  length   |  msgid   | sessionid  |  data   |
//  4 bytes  |  4 bytes |  32 bytes  | N bytes |
//------------------------------------------------

namespace uv
{

class Packet
{
public:
    Packet();
    ~Packet();

    void pack(const char* data, unsigned int size, unsigned int msgid, const char* sessionid);
    const char* getData();
    unsigned int DataSize();
    unsigned int MsgId();
    const char* SessionId();
    const std::string& Buffer();
    const unsigned int PacketSize();

    void swap(std::string& str);
    
    static int readFromBuffer(PacketBuffer*, Packet&);
    
    template<typename NumType>
    static void UnpackNum(const unsigned char* data, NumType& num);

    template<typename NumType>
    static void PackNum(char* data, NumType num);

    static unsigned int PacketMinSize();

public:
    enum DataMode
    {
        BigEndian,
        LittleEndian
    };

    static DataMode Mode;

protected:
    std::string buffer_;
    unsigned int dataSize_;
    unsigned int msgId_;
    std::string sessionId_;
};

template<typename NumType>
inline void Packet::UnpackNum(const unsigned char* data, NumType& num)
{
    num = 0;
    auto size = static_cast<int>(sizeof(NumType));
    if (Packet::DataMode::BigEndian == Packet::Mode)
    {
        for (int i = 0; i < size; i++)
        {
            num <<= 8;
            num |= data[i];

        }
    }
    else
    {
        for (int i = size-1; i >= 0; i--)
        {
            num <<= 8;
            num |= data[i];
        }
    }
}

template<typename NumType>
inline void Packet::PackNum(char* data, NumType num)
{
    int size = static_cast<int>(sizeof(NumType));
    if (Packet::DataMode::BigEndian == Packet::Mode)
    {
        for (int i = size-1; i >= 0; i--)
        {
            data[i] = num & 0xff;
            num >>= 8;
        }
    }
    else
    {
        for (int i = 0; i < size; i++)
        {
            data[i] = num & 0xff;
            num >>= 8;
        }
    }
}
}
#endif
