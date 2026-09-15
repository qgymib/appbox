#include "RpcCodec.hpp"
#include <cstring>
#include "sandbox/utils/RemoteProtocol.hpp"

appbox::FrameStatus appbox::TryDecodeFrame(std::vector<uint8_t>& buffer, std::string& out_payload,
                                           std::string& out_error)
{
    if (buffer.size() < sizeof(RemoteProtocol))
    {
        return FrameStatus::NeedMoreData;
    }

    RemoteProtocol head;
    std::memcpy(&head, buffer.data(), sizeof(head));

    if (head.magic != RemoteProtocol().magic)
    {
        out_error = "invalid frame magic";
        return FrameStatus::ProtocolError;
    }

    if (head.length > kMaxRpcPayloadSize)
    {
        out_error = "frame payload is too large";
        return FrameStatus::ProtocolError;
    }

    /*
     * head.length is limited by kMaxRpcPayloadSize above, so the addition can
     * not overflow on a 32 bit platform either.
     */
    const size_t total = static_cast<size_t>(head.length) + sizeof(head);
    if (buffer.size() < total)
    {
        return FrameStatus::NeedMoreData;
    }

    const char* p_payload = reinterpret_cast<const char*>(buffer.data() + sizeof(head));
    out_payload.assign(p_payload, head.length);

    const size_t left = buffer.size() - total;
    std::memmove(buffer.data(), buffer.data() + total, left);
    buffer.resize(left);

    return FrameStatus::Ok;
}

bool appbox::MakeFrameHeader(size_t payload_size, std::string& out_header, std::string& out_error)
{
    if (payload_size > kMaxRpcPayloadSize)
    {
        out_error = "frame payload is too large";
        return false;
    }

    RemoteProtocol head;
    head.length = static_cast<uint32_t>(payload_size);

    out_header.assign(reinterpret_cast<const char*>(&head), sizeof(head));
    return true;
}

bool appbox::ParseRpcResponse(const nlohmann::json& rsp, uint64_t& out_id, RemoteResult& out_result,
                              std::string& out_error)
{
    if (!rsp.is_object())
    {
        out_error = "response is not an object";
        return false;
    }

    const auto it_id = rsp.find("id");
    if (it_id == rsp.end() || !it_id->is_number())
    {
        out_error = "response has no valid id";
        return false;
    }

    try
    {
        out_id = it_id->get<uint64_t>();
    }
    catch (const nlohmann::json::exception& e)
    {
        out_error = std::string("invalid id: ") + e.what();
        return false;
    }

    const auto it_result = rsp.find("result");
    if (it_result != rsp.end())
    {
        out_result = *it_result;
        return true;
    }

    const auto it_error = rsp.find("error");
    if (it_error == rsp.end() || !it_error->is_object())
    {
        out_error = "response has neither a result nor an error";
        return false;
    }

    try
    {
        out_result = tl::unexpected<RemoteError>(RemoteError{ it_error->value("code", -1),
                                                              it_error->value("message", std::string()),
                                                              it_error->value("data", nlohmann::json::object()) });
    }
    catch (const nlohmann::json::exception& e)
    {
        out_error = std::string("invalid error object: ") + e.what();
        return false;
    }

    return true;
}
