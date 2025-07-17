#include <iostream>
#include <cstring>
#include "uv/include/uv11.hpp"

// Heartbeat protocol ID
#define HEARTBEAT_PROTOCOL_ID 0x10001

class EchoServer
{
public:
    EchoServer(uv::EventLoop* loop)
        : server_(loop)
    {
        server_.setMessageCallback([this](uv::TcpConnectionPtr conn, const char* data, ssize_t size) {
            // Check if enough data for packet header (at least 8 bytes for length + msgid)
            if (size < 8) {
                std::cout << "Received incomplete packet" << std::endl;
                return;
            }
            
            // Parse packet header
            unsigned int length;
            unsigned int msgId;
            uv::Packet::UnpackNum((const unsigned char*)data, length);
            uv::Packet::UnpackNum((const unsigned char*)(data + 4), msgId);
            
            std::cout << "Received packet: length=" << length << ", msgId=0x" 
                      << std::hex << msgId << std::dec << std::endl;
            
            if (msgId == HEARTBEAT_PROTOCOL_ID) {
                // Handle heartbeat packet
                std::cout << "Received heartbeat packet" << std::endl;
                
                // Create response heartbeat
                char sessionId[32];
                std::memset(sessionId, 0, sizeof(sessionId));
                
                // Create heartbeat response (empty data)
                uv::Packet packet;
                packet.pack("", 0, HEARTBEAT_PROTOCOL_ID, sessionId);
                
                // Send the heartbeat response
                conn->write(packet.Buffer().c_str(), packet.PacketSize(), [](uv::WriteInfo& info) {
                    if (info.status) {
                        std::cout << "Write error: " << uv::EventLoop::GetErrorMessage(info.status) << std::endl;
                    }
                });
            } else {
                // Regular echo for non-heartbeat packets
                std::string str(data, size);
                std::cout << "Received: " << str << std::endl;
                
                // Echo back
                conn->write(data, size, [](uv::WriteInfo& info) {
                    if (info.status) {
                        std::cout << "Write error: " << uv::EventLoop::GetErrorMessage(info.status) << std::endl;
                    }
                });
            }
        });
    }

    int bindAndListen(const std::string& ip, int port)
    {
        uv::SocketAddr addr(ip, port);
        return server_.bindAndListen(addr);
    }

private:
    uv::TcpServer server_;
};

int main(int argc, char** argv)
{
    uv::EventLoop loop;
    EchoServer server(&loop);
    
    std::string ip = "0.0.0.0";
    int port = 10000;
    
    if (argc > 2) {
        ip = argv[1];
        port = std::atoi(argv[2]);
    }
    
    int result = server.bindAndListen(ip, port);
    if (result < 0) {
        std::cerr << "Error binding to " << ip << ":" << port 
                  << " - " << uv::EventLoop::GetErrorMessage(result) << std::endl;
        return 1;
    }
    
    std::cout << "Echo server listening on " << ip << ":" << port << std::endl;
    loop.run();
    
    return 0;
} 