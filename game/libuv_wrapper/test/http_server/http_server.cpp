#include <iostream>
#include "uv/include/uv11.hpp"

class SimpleHttpServer
{
public:
    SimpleHttpServer(uv::EventLoop* loop)
        : server_(loop)
    {
        // Register handlers for different endpoints
        server_.Get("/", [](uv::http::Request& req, uv::http::Response* resp) {
            std::string content = "<html><body><h1>Hello from uv-cpp HTTP Server!</h1>"
                                  "<p>Try these endpoints:</p>"
                                  "<ul>"
                                  "<li><a href='/json'>/json</a> - Returns JSON data</li>"
                                  "<li><a href='/text'>/text</a> - Returns plain text</li>"
                                  "<li><a href='/echo?message=hello'>/echo?message=hello</a> - Echoes your message</li>"
                                  "</ul>"
                                  "</body></html>";
            
            resp->setStatus(uv::http::Response::StatusCode::OK, "OK");
            resp->appendHead("Content-Type", "text/html");
            resp->swapContent(content);
        });

        server_.Get("/json", [](uv::http::Request& req, uv::http::Response* resp) {
            std::string content = "{\"status\":\"success\",\"message\":\"This is JSON data\"}";
            
            resp->setStatus(uv::http::Response::StatusCode::OK, "OK");
            resp->appendHead("Content-Type", "application/json");
            resp->swapContent(content);
        });

        server_.Get("/text", [](uv::http::Request& req, uv::http::Response* resp) {
            std::string content = "This is plain text from uv-cpp HTTP server";
            
            resp->setStatus(uv::http::Response::StatusCode::OK, "OK");
            resp->appendHead("Content-Type", "text/plain");
            resp->swapContent(content);
        });
        
        server_.Get("/echo", [](uv::http::Request& req, uv::http::Response* resp) {
            std::string message = req.getUrlParam("message");
            if (message.empty()) {
                message = "No message provided. Try /echo?message=hello";
            }
            
            std::string content = "Echo: " + message;
            
            resp->setStatus(uv::http::Response::StatusCode::OK, "OK");
            resp->appendHead("Content-Type", "text/plain");
            resp->swapContent(content);
        });
    }

    int bindAndListen(const std::string& ip, int port)
    {
        uv::SocketAddr addr(ip, port);
        return server_.bindAndListen(addr);
    }

private:
    uv::http::HttpServer server_;
};

int main(int argc, char** argv)
{
    // Set buffer mode for HTTP server to work properly
    uv::TcpServer::SetBufferMode(uv::GlobalConfig::ListBuffer);
    
    uv::EventLoop loop;
    SimpleHttpServer server(&loop);
    
    std::string ip = "0.0.0.0";
    int port = 8080;
    
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
    
    std::cout << "HTTP server listening on http://" << ip << ":" << port << std::endl;
    std::cout << "Try visiting: http://127.0.0.1:" << port << "/" << std::endl;
    loop.run();
    
    return 0;
} 