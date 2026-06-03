#include "utils/WinAPI.h"
#include "PipeClient.hpp"
#include "utils/RemoteProtocol.hpp"
#include <atomic>
#include <mutex>

struct appbox::PipeClient::Data
{
    Data(const std::wstring& path);
    ~Data();
    std::atomic_uint64_t id_cnt_;
    std::wstring         path_;
    std::mutex           mutex_;
    HANDLE               pipe_;
};

static bool ReadData(HANDLE pipe, void* data, size_t expect_size)
{
    size_t total_read = 0;
    auto   p_data = static_cast<uint8_t*>(data);

    while (total_read < expect_size)
    {
        DWORD read = 0;
        DWORD left_sz = static_cast<DWORD>(expect_size - total_read);
        if (!ReadFile(pipe, p_data + total_read, left_sz, &read, nullptr))
        {
            return false;
        }

        total_read += read;
    }

    return true;
}

static bool SendRequest(HANDLE pipe, const std::string& method, const nlohmann::json& params, uint64_t id)
{
    nlohmann::json req_json;
    req_json["jsonrpc"] = "2.0";
    req_json["method"] = method;
    req_json["params"] = params;
    req_json["id"] = id;

    auto                   req_data = req_json.dump();
    appbox::RemoteProtocol hdr;
    hdr.length = static_cast<uint32_t>(req_data.size());

    DWORD written = 0;
    if (!WriteFile(pipe, &hdr, sizeof(hdr), &written, nullptr) || written != sizeof(hdr))
    {
        return false;
    }

    auto need_write_sz = static_cast<DWORD>(req_data.size());
    if (!WriteFile(pipe, req_data.c_str(), need_write_sz, &written, nullptr) || written != need_write_sz)
    {
        return false;
    }

    return true;
}

static bool RecvResponse(HANDLE pipe, uint64_t id, nlohmann::json& rsp)
{
    appbox::RemoteProtocol hdr;
    if (!ReadData(pipe, &hdr, sizeof(hdr)))
    {
        return false;
    }
    if (hdr.magic != APPBOX_REMOTE_MAGIC)
    {
        return false;
    }

    std::string rsp_data(hdr.length, '\0');
    if (!ReadData(pipe, rsp_data.data(), hdr.length))
    {
        return false;
    }

    nlohmann::json json_rsp = nlohmann::json::parse(rsp_data);
    if (json_rsp["id"].get<uint64_t>() != id)
    {
        return false;
    }

    auto it = json_rsp.find("result");
    if (it == json_rsp.end())
    {
        return false;
    }

    rsp = *it;
    return true;
}

appbox::PipeClient::Data::Data(const std::wstring& path) : id_cnt_(0), path_(path), pipe_(INVALID_HANDLE_VALUE)
{
}

appbox::PipeClient::Data::~Data()
{
    if (pipe_ != INVALID_HANDLE_VALUE)
    {
        CloseHandle(pipe_);
        pipe_ = INVALID_HANDLE_VALUE;
    }
}

appbox::PipeClient::PipeClient(const std::wstring& path)
{
    data_ = new Data(path);
}

appbox::PipeClient::~PipeClient()
{
    delete data_;
}

bool appbox::PipeClient::Start()
{
    for (size_t i = 0; i < 5; ++i)
    {
        data_->pipe_ =
            CreateFileW(data_->path_.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (data_->pipe_ != INVALID_HANDLE_VALUE)
        {
            break;
        }

        DWORD err = GetLastError();
        if (err != ERROR_PIPE_BUSY)
        {
            return false;
        }

        WaitNamedPipeW(data_->path_.c_str(), 1 * 1000);
    }
    if (data_->pipe_ == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    DWORD mode = PIPE_READMODE_BYTE;
    if (!SetNamedPipeHandleState(data_->pipe_, &mode, nullptr, nullptr))
    {
        return false;
    }

    return true;
}

bool appbox::PipeClient::Call(const std::string& method, const nlohmann::json& params, nlohmann::json& rsp)
{
    const uint64_t              id = data_->id_cnt_++;
    std::lock_guard<std::mutex> lock(data_->mutex_);

    if (!SendRequest(data_->pipe_, method, params, id))
    {
        return false;
    }

    return RecvResponse(data_->pipe_, id, rsp);
}
