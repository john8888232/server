﻿#ifndef UV_HTTP_CLIENT_HPP
#define UV_HTTP_CLIENT_HPP

#include "../TcpClient.hpp"
#include "Request.hpp"
#include "Response.hpp"

namespace uv
{
namespace http
{

class HttpClient
{
public:
    enum ReqResult
    {
        Success = 0,
        ConnectFail = 1,
        ParseFail = 2,
        Unknow = 3,
        WriteFail=4,
    };
    using OnRespCallback = std::function<void(ReqResult, Response*)>;
public:
    HttpClient(EventLoop* loop);
    virtual ~HttpClient();

    void Req(uv::SocketAddr& addr,Request& req);

    void setOnResp(OnRespCallback callback);

private:
    TcpClient* client_;
    OnRespCallback callback_;
    Request req_;
    std::string buffer_;

private:
    void onResp(ReqResult rst, Response* resp);
    void onConnectStatus(TcpClient::ConnectStatus status);
    void onMessage(const char* data, ssize_t size);
    bool isConnected;
};

}
}
#endif
