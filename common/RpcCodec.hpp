#ifndef APPBOX_COMMON_RPC_CODEC_HPP
#define APPBOX_COMMON_RPC_CODEC_HPP

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include <tl/expected.hpp>
#include "RemoteProtocol.hpp"

namespace appbox
{

/**
 * @brief Error of a failed RPC call.
 */
struct RemoteError
{
    int            code;    /* Error code. */
    std::string    message; /* Error message. */
    nlohmann::json data;    /* Error data. */
};

/**
 * @brief Result of an RPC call: a JSON result value or a RemoteError.
 */
typedef tl::expected<nlohmann::json, RemoteError> RemoteResult;

/**
 * @brief Maximum size of the payload of one RPC frame.
 *
 * The length field of a frame is a 32 bit value. This limit keeps the frame
 * size computation free of overflows on 32 bit platforms and prevents a
 * corrupted or malicious length from triggering a huge allocation.
 */
constexpr uint32_t kMaxRpcPayloadSize = 64u * 1024u * 1024u; /* 64 MiB */

/**
 * @brief Result of decoding one frame.
 */
enum class FrameStatus
{
    Ok,            /* One frame was decoded and removed from the buffer. */
    NeedMoreData,  /* The buffer holds an incomplete frame. */
    ProtocolError, /* The frame header is invalid, the stream has to be dropped. */
};

/**
 * @brief Try to decode one frame from the front of the buffer.
 *
 * When a frame was decoded it is removed from the buffer, together with the
 * bytes of an incomplete following frame which stay in place.
 *
 * @param[in,out] buffer Receive buffer.
 * @param[out] out_payload Decoded payload.
 * @param[out] out_error Error description, set when the status is ProtocolError.
 * @return The decoding status.
 */
inline FrameStatus TryDecodeFrame(std::vector<uint8_t>& buffer, std::string& out_payload, std::string& out_error)
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

/**
 * @brief Build the header of a frame which carries a payload of the given size.
 * @param[in] payload_size Payload size in bytes.
 * @param[out] out_header Serialized frame header.
 * @param[out] out_error Error description when the function fails.
 * @return true on success, otherwise false.
 */
inline bool MakeFrameHeader(size_t payload_size, std::string& out_header, std::string& out_error)
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

/**
 * @brief Parse a JSON-RPC response message.
 * @param[in] rsp Response object.
 * @param[out] out_id Request id of the response.
 * @param[out] out_result Parsed result or error.
 * @param[out] out_error Error description when the function fails.
 * @return true on success, otherwise false.
 */
inline bool ParseRpcResponse(const nlohmann::json& rsp, uint64_t& out_id, RemoteResult& out_result,
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

} // namespace appbox

#endif // APPBOX_COMMON_RPC_CODEC_HPP
