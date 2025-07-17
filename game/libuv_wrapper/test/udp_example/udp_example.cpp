#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include "uv/include/uv11.hpp"

// A simple UDP echo server and client example

class UdpEchoServer 
{
public:
    UdpEchoServer(uv::EventLoop* loop, const std::string& ip, int port)
        : loop_(loop), udp_(loop)
    {
        // Set up message callback
        udp_.setMessageCallback([this](uv::SocketAddr& from, const char* data, unsigned size) {
            std::string message(data, size);
            std::cout << "Server received: " << message << " from " << from.toStr() << std::endl;
            
            // Echo the message back
            udp_.send(from, data, size);
        });
        
        // Bind to address
        uv::SocketAddr addr(ip, port);
        int result = udp_.bindAndRead(addr);
        
        if (result) {
            std::cerr << "Server bind error: " << uv::EventLoop::GetErrorMessage(result) << std::endl;
        } else {
            std::cout << "UDP server listening on " << ip << ":" << port << std::endl;
        }
    }
    
    void close() {
        udp_.close([](){
            // Empty callback
        });
    }

private:
    uv::EventLoop* loop_;
    uv::Udp udp_;
};

class UdpClient
{
public:
    UdpClient(uv::EventLoop* loop, const std::string& serverIp, int serverPort)
        : loop_(loop), udp_(loop), serverAddr_(serverIp, serverPort), running_(true)
    {
        // Set up message callback to receive responses
        udp_.setMessageCallback([this](uv::SocketAddr& from, const char* data, unsigned size) {
            std::string message(data, size);
            std::cout << "Client received response: " << message << " from " << from.toStr() << std::endl;
        });
        
        // Bind to any available port
        uv::SocketAddr bindAddr("0.0.0.0", 0);
        int result = udp_.bindAndRead(bindAddr);
        
        if (result) {
            std::cerr << "Client bind error: " << uv::EventLoop::GetErrorMessage(result) << std::endl;
            running_ = false;
        }
    }
    
    void sendMessage(const std::string& message) {
        if (running_) {
            std::cout << "Client sending: " << message << std::endl;
            udp_.send(serverAddr_, message.c_str(), message.size());
        }
    }
    
    void close() {
        running_ = false;
        udp_.close([](){
            // Empty callback
        });
    }
    
    bool isRunning() const {
        return running_;
    }

private:
    uv::EventLoop* loop_;
    uv::Udp udp_;
    uv::SocketAddr serverAddr_;
    std::atomic<bool> running_;
};

int main(int argc, char** argv)
{
    uv::EventLoop serverLoop;
    uv::EventLoop clientLoop;
    
    std::string ip = "127.0.0.1";
    int port = 9000;
    
    if (argc > 2) {
        ip = argv[1];
        port = std::atoi(argv[2]);
    }
    
    // Create server
    UdpEchoServer server(&serverLoop, ip, port);
    
    // Start server event loop in separate thread
    std::thread serverThread([&serverLoop]() {
        serverLoop.run();
    });
    
    // Wait for server to start
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // Create client
    UdpClient client(&clientLoop, ip, port);
    
    // Start client event loop in separate thread
    std::thread clientThread([&clientLoop]() {
        clientLoop.run();
    });
    
    // Send messages if client started successfully
    if (client.isRunning()) {
        for (int i = 0; i < 5; i++) {
            std::string message = "UDP message #" + std::to_string(i);
            client.sendMessage(message);
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }
    
    // Wait for responses
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // Clean shutdown
    client.close();
    server.close();
    
    clientLoop.stop();
    serverLoop.stop();
    
    clientThread.join();
    serverThread.join();
    
    return 0;
} 