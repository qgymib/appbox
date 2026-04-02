#include <asio.hpp>
#include <map>
#include <utility>
#include "PipeServer.hpp"

typedef std::map<std::string, appbox::PipeServer::MethodCallback> MethodCallbackMap;

struct appbox::PipeServer::Data
{
    std::wstring      pipe_path;
    MethodCallbackMap method_callbacks;
    asio::io_context  io_context;
};

appbox::PipeServer::PipeServer()
{
    data_ = new Data();
}

appbox::PipeServer::~PipeServer()
{
    delete data_;
}

appbox::PipeServer::Ptr appbox::PipeServer::Create(const std::wstring& pipe)
{
    auto obj = Ptr(new PipeServer);
    obj->data_->pipe_path = pipe;

    return obj;
}

void appbox::PipeServer::RegisterMethod(const std::string& method_name, MethodCallback callback)
{
    data_->method_callbacks[method_name] = std::move(callback);
}
