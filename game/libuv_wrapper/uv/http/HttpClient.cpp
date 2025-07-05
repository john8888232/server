﻿/*
   Copyright © 2017-2020, orcaer@yeah.net  All rights reserved.

   Author: orcaer@yeah.net

   Last modified: 2020-3-30

   Description: https://github.com/wlgq2/uv-cpp
*/

#include "../include/http/HttpClient.hpp"

using namespace uv;
using namespace uv::http;

HttpClient::HttpClient(EventLoop* loop)
    :client_(new TcpClient(loop)),
    callback_(nullptr),
    isConnected(false)
{
    buffer_.resize(20480);
}

HttpClient::~HttpClient()
{
    if (isConnected)
    {
        client_->close([](uv::TcpClient* client)
        {
            delete client;
        });
    }
    else
    {
        delete client_;
    }
}


void HttpClient::Req(uv::SocketAddr& addr,Request& req)
{
    req_ = req;
    buffer_.clear();
    client_->setConnectStatusCallback(std::bind(&HttpClient::onConnectStatus,this,std::placeholders::_1));
    client_->setMessageCallback(std::bind(&HttpClient::onMessage, this, std::placeholders::_1, std::placeholders::_2));
    client_->connect(addr);
}

void HttpClient::setOnResp(OnRespCallback callback)
{
    callback_ = callback;
}

void HttpClient::onResp(ReqResult rst, Response* resp)
{
    if (nullptr != callback_)
    {
        callback_(rst, resp);
    }
}

void HttpClient::onConnectStatus(TcpClient::ConnectStatus status)
{
    if (status == uv::TcpClient::ConnectStatus::OnConnectSuccess)
    {
        isConnected = true;
        std::string str;
        req_.pack(str);
        client_->write(str.c_str(), (unsigned int)str.size(),[this](WriteInfo& info)
        {
            if (0 != info.status)
            {
                onResp(WriteFail, nullptr);
                LOG_ERROR("Http Write error %s", std::string(uv_strerror(info.status)));
            }
        });
    }
    else if (status == uv::TcpClient::ConnectStatus::OnConnectFail)
    {
        isConnected = false;
        onResp(ConnectFail, nullptr);
    }
    else
    {
        isConnected = false;
        Response resp;
        if (0 == resp.unpack(buffer_))
        {
            onResp(Success, &resp);
        }
        else
        {
            onResp(ParseFail, nullptr);
        }
    }
}

void HttpClient::onMessage(const char* data, ssize_t size)
{
    // Handle potential buffer overflow more safely
    if (buffer_.size() + size > 1024 * 1024) // 1MB limit
    {
        LOG_ERROR("HTTP response too large");
        buffer_.clear();
        return;
    }

    unsigned bufsize = (unsigned)buffer_.size();
    buffer_.resize(bufsize + size);
    char* out = (char*)buffer_.c_str() + bufsize;
    std::copy(data, data + size, out);

    Response resp;
    auto rst = resp.unpackAndCompleted(buffer_);
    if (rst == ParseResult::Success)
    {
        onResp(Success, &resp);
    }
    else if (rst == ParseResult::Error)
    {
        LOG_ERROR("parse http's response error.");
        buffer_.clear();
    }
}
