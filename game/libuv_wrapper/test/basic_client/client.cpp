#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <cstring>
#include "uv/include/uv11.hpp"

// Heartbeat protocol ID
#define HEARTBEAT_PROTOCOL_ID 0x10001

class EchoClient
{
public:
    EchoClient(uv::EventLoop* loop)
        : client_(loop)
    {
        client_.setConnectStatusCallback([this](uv::TcpClient::ConnectStatus status) {
            if (status == uv::TcpClient::ConnectStatus::OnConnectSuccess) {
                std::cout << "Connected to server successfully!" << std::endl;
                connected_ = true;
                
                // Start sending heartbeats
                startHeartbeat();
            }
            else {
                std::cout << "Failed to connect to server!" << std::endl;
                connected_ = false;
            }
        });

        client_.setMessageCallback([](const char* data, ssize_t size) {
            // Check if enough data for packet header
            if (size < 8) {
                std::cout << "Received incomplete packet" << std::endl;
                return;
            }
            
            // Parse packet header
            unsigned int length;
            unsigned int msgId;
            uv::Packet::UnpackNum((const unsigned char*)data, length);
            uv::Packet::UnpackNum((const unsigned char*)(data + 4), msgId);
            
            if (msgId == HEARTBEAT_PROTOCOL_ID) {
                std::cout << "Received heartbeat response from server" << std::endl;
            } else {
                std::string response(data, size);
                std::cout << "Server response: " << response << std::endl;
            }
        });
    }

    void connect(const std::string& ip, int port)
    {
        uv::SocketAddr addr(ip, port);
        client_.connect(addr);
    }

    void sendMessage(const std::string& message)
    {
        if (connected_) {
            client_.write(message.c_str(), message.length(), [](uv::WriteInfo& info) {
                if (info.status) {
                    std::cout << "Write error: " << uv::EventLoop::GetErrorMessage(info.status) << std::endl;
                }
            });
        }
        else {
            std::cout << "Not connected to server, can't send message" << std::endl;
        }
    }
    
    void sendHeartbeat()
    {
        if (connected_) {
            char sessionId[32];
            std::memset(sessionId, 0, sizeof(sessionId));
            
            // Create heartbeat packet (empty data)
            uv::Packet packet;
            packet.pack("", 0, HEARTBEAT_PROTOCOL_ID, sessionId);
            
            // Send the heartbeat
            client_.write(packet.Buffer().c_str(), packet.PacketSize(), [](uv::WriteInfo& info) {
                if (info.status) {
                    std::cout << "Heartbeat write error: " << uv::EventLoop::GetErrorMessage(info.status) << std::endl;
                } else {
                    std::cout << "Heartbeat sent" << std::endl;
                }
            });
        }
    }
    
    void startHeartbeat()
    {
        // Send initial heartbeat
        sendHeartbeat();
        
        // Set up timer for periodic heartbeats (every 5 seconds)
        heartbeatTimer_ = std::make_shared<std::thread>([this]() {
            while (connected_) {
                std::this_thread::sleep_for(std::chrono::seconds(5));
                if (connected_) {
                    sendHeartbeat();
                }
            }
        });
        heartbeatTimer_->detach();
    }

private:
    uv::TcpClient client_;
    bool connected_ = false;
    std::shared_ptr<std::thread> heartbeatTimer_;
};

int main(int argc, char** argv)
{
    uv::EventLoop loop;
    EchoClient client(&loop);
    
    std::string ip = "127.0.0.1";
    int port = 10000;
    
    if (argc > 2) {
        ip = argv[1];
        port = std::atoi(argv[2]);
    }
    
    std::cout << "Connecting to " << ip << ":" << port << std::endl;
    client.connect(ip, port);
    
    // Start event loop in a separate thread
    std::thread loopThread([&loop]() {
        loop.run();
    });
    
    // Give time for connection to establish
    std::this_thread::sleep_for(std::chrono::seconds(1));
    static int c = 0;
    // Send messages
    while(1){
        std::string message = "Hello from client, message #" + std::to_string(c);
        std::cout << "Sending: " << message << std::endl;
        client.sendMessage(message);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        c++;
    }
    
    // Wait a bit to receive responses
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    // Clean shutdown
    loop.stop();
    loopThread.join();
    
    return 0;
} 