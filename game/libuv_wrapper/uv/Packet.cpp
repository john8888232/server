#include  "include/Packet.hpp"

using namespace uv;

Packet::DataMode Packet::Mode = Packet::DataMode::LittleEndian;

Packet::Packet()
    :buffer_(""),
    dataSize_(0),
    msgId_(0),
    sessionId_("")
{
}

uv::Packet::~Packet()
{
}

int uv::Packet::readFromBuffer(PacketBuffer* packetbuf, Packet& out)
{
    while (true)
    {
        auto size = packetbuf->readSize();
        //数据小于包头大小
        if (size < PacketMinSize())
            return -1;

        // Read length (4 bytes)
        std::string lengthData("");
        packetbuf->readBufferN(lengthData, sizeof(unsigned int));
        unsigned int length;
        UnpackNum((unsigned char*)lengthData.c_str(), length);
        
        // Check if we have enough data for the complete packet
        if (size < length)
            return -1;

        // Clear the length bytes we just read
        packetbuf->clearBufferN(sizeof(unsigned int));
        
        // Read the remaining packet data (length - 4 bytes)
        std::string remainingData("");
        packetbuf->readBufferN(remainingData, length - sizeof(unsigned int));
        
        // Clear the remaining bytes
        packetbuf->clearBufferN(length - sizeof(unsigned int));
        
        // Now remainingData contains: [msgId(4) + sessionId(32) + data(N)]
        
        // Parse msgId from the beginning of remainingData
        unsigned int msgId;
        UnpackNum((unsigned char*)remainingData.c_str(), msgId);
        
        // sessionId starts after msgId (4 bytes)
        std::string sessionId(remainingData.substr(sizeof(unsigned int), 32));
        
        // packet data starts after msgId(4) + sessionId(32) = 36 bytes
        std::string packetData(remainingData.substr(sizeof(unsigned int) + 32));
        
        // Reconstruct the complete packet buffer for compatibility
        std::string completeBuffer = lengthData + remainingData;
        
        out.buffer_ = completeBuffer;
        out.dataSize_ = packetData.size();
        out.msgId_ = msgId;
        out.sessionId_ = sessionId;
        
        break;
    }
    return 0;
}

void uv::Packet::pack(const char* data, unsigned int size, unsigned int msgid, const char* sessionid)
{
    dataSize_ = size;
    msgId_ = msgid;
    sessionId_ = sessionid;
    
    // Calculate total packet size
    unsigned int totalSize = sizeof(unsigned int) + sizeof(unsigned int) + 32 + size;
    buffer_.resize(totalSize);
    
    // Write length
    PackNum(&buffer_[0], totalSize);
    
    // Write msgid
    PackNum(&buffer_[sizeof(unsigned int)], msgid);
    
    // Write sessionid
    std::copy(sessionid, sessionid + 32, &buffer_[sizeof(unsigned int) * 2]);
    
    // Write data
    std::copy(data, data + size, &buffer_[sizeof(unsigned int) * 2 + 32]);
}

const char* uv::Packet::getData()
{
    return buffer_.c_str() + sizeof(unsigned int) * 2 + 32;
}

unsigned int uv::Packet::DataSize()
{
    return dataSize_;
}

unsigned int uv::Packet::MsgId()
{
    return msgId_;
}

const char* uv::Packet::SessionId()
{
    return sessionId_.c_str();
}

const std::string& uv::Packet::Buffer()
{
    return buffer_;
}

const unsigned int uv::Packet::PacketSize()
{
    return (unsigned int)buffer_.size();
}

void uv::Packet::swap(std::string& str)
{
    buffer_.swap(str);
    dataSize_ = (unsigned int)(buffer_.size() - PacketMinSize());
}

unsigned int uv::Packet::PacketMinSize()
{
    return sizeof(unsigned int) * 2 + 32; // length + msgid + sessionid
}
