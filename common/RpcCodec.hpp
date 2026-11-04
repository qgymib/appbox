#ifndef APPBOX_COMMON_RPC_CODEC_HPP
#define APPBOX_COMMON_RPC_CODEC_HPP

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include <tl/expected.hpp>
#include "ErrorCode.hpp"
#include "RemoteProtocol.hpp"

namespace appbox
{

/**
 * @brief Error of a failed RPC call.
 *
 * The structure is the error object of the wire protocol of JSON-RPC and not
 * the description of a failure of an interface of `common/`: it is what one
 * peer tells the other about a method it could not serve, so it carries the
 * text and the data the peer wrote. A failure of an interface of `common/` is
 * described by a code of `common/ErrorCode.hpp` alone; the error a caller
 * receives from its peer belongs to the protocol and keeps its three members.
 * `docs/Launcher.md` records that reason.
 */
struct RemoteError
{
    int            code;    /* Error code. */
    std::string    message; /* Error message. */
    nlohmann::json data;    /* Error data. */
};

/**
 * @brief Result of an RPC call: a JSON result value or a RemoteError.
 *
 * The expected carries the two ends of one answer: a value is the result the
 * peer produced, and the error is the error object the peer sent or the one
 * this end composes for a request it could not deliver. A failure of the
 * session itself is therefore reported on the same channel, which is why the
 * calls of the session keep this answer instead of a code.
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
    Ok,           /* One frame was decoded and removed from the buffer. */
    NeedMoreData, /* The buffer holds an incomplete frame. */
};

/**
 * @brief Outcome of one decoding attempt.
 *
 * A buffer which holds no complete frame is no failure: the caller asks again
 * once more bytes arrived, which the status tells apart from a decoded frame.
 * The two failures of the decoding carry a code instead, so the outcome of the
 * call is an `expected` of this structure.
 */
struct FrameOutcome
{
    FrameStatus status;  /* Decoded, or an incomplete buffer. */
    std::string payload; /* Payload of the decoded frame, empty otherwise. */
};

/**
 * @brief Try to decode one frame from the front of the buffer.
 *
 * When a frame was decoded it is removed from the buffer, together with the
 * bytes of an incomplete following frame which stay in place.
 *
 * @param[in,out] buffer Receive buffer.
 * @return The outcome of the decoding, or the code of a frame which is no frame
 *         of this protocol (`kRpcFrameMagicInvalid`, `kRpcFrameTooLarge`). A
 *         buffer which holds no complete frame is a value of status
 *         `NeedMoreData`, not a failure.
 */
inline tl::expected<FrameOutcome, ErrorCode> TryDecodeFrame(std::vector<std::uint8_t>& buffer) noexcept
{
    if (buffer.size() < sizeof(RemoteProtocol))
    {
        return FrameOutcome{ FrameStatus::NeedMoreData, std::string() };
    }

    RemoteProtocol head;
    std::memcpy(&head, buffer.data(), sizeof(head));

    if (head.magic != RemoteProtocol().magic)
    {
        return tl::unexpected<ErrorCode>(ErrorCode::kRpcFrameMagicInvalid);
    }

    if (head.length > kMaxRpcPayloadSize)
    {
        return tl::unexpected<ErrorCode>(ErrorCode::kRpcFrameTooLarge);
    }

    /*
     * head.length is limited by kMaxRpcPayloadSize above, so the addition can
     * not overflow on a 32 bit platform either.
     */
    const std::size_t total = static_cast<std::size_t>(head.length) + sizeof(head);
    if (buffer.size() < total)
    {
        return FrameOutcome{ FrameStatus::NeedMoreData, std::string() };
    }

    const char* p_payload = reinterpret_cast<const char*>(buffer.data() + sizeof(head));
    std::string payload(p_payload, head.length);

    const std::size_t left = buffer.size() - total;
    std::memmove(buffer.data(), buffer.data() + total, left);
    buffer.resize(left);

    return FrameOutcome{ FrameStatus::Ok, std::move(payload) };
}

/**
 * @brief Build the header of a frame which carries a payload of the given size.
 * @param[in] payload_size Payload size in bytes.
 * @return The serialized frame header, or `kRpcFrameTooLarge` when the payload
 *         is above the limit of the protocol.
 */
inline tl::expected<std::string, ErrorCode> MakeFrameHeader(std::size_t payload_size) noexcept
{
    if (payload_size > kMaxRpcPayloadSize)
    {
        return tl::unexpected<ErrorCode>(ErrorCode::kRpcFrameTooLarge);
    }

    RemoteProtocol head;
    head.length = static_cast<uint32_t>(payload_size);

    return std::string(reinterpret_cast<const char*>(&head), sizeof(head));
}

/**
 * @brief Id and result of one JSON-RPC response object.
 */
struct RpcResponse
{
    std::uint64_t id;     /* Request id the answer carries. */
    RemoteResult  result; /* Result value, or the error object of the peer. */
};

/**
 * @brief Parse a JSON-RPC response message.
 *
 * The parse reads the members without the exception reporting of
 * `nlohmann::json`, so a value of another type is a code and not a throw.
 *
 * @param[in] rsp Response object.
 * @return The id and the result of the response, or the code of a response
 *         this protocol cannot read.
 */
inline tl::expected<RpcResponse, ErrorCode> ParseRpcResponse(const nlohmann::json& rsp) noexcept
{
    if (!rsp.is_object())
    {
        return tl::unexpected<ErrorCode>(ErrorCode::kRpcResponseNotObject);
    }

    const auto it_id = rsp.find("id");
    if (it_id == rsp.end())
    {
        return tl::unexpected<ErrorCode>(ErrorCode::kRpcResponseIdMissing);
    }

    std::uint64_t id = 0;
    if (it_id->is_number_unsigned())
    {
        id = it_id->get<std::uint64_t>();
    }
    else if (it_id->is_number_integer())
    {
        /* A negative id names no request this session ever sent. */
        const auto signed_id = it_id->get<std::int64_t>();
        if (signed_id < 0)
        {
            return tl::unexpected<ErrorCode>(ErrorCode::kRpcResponseInvalid);
        }
        id = static_cast<std::uint64_t>(signed_id);
    }
    else
    {
        return tl::unexpected<ErrorCode>(ErrorCode::kRpcResponseIdMissing);
    }

    const auto it_result = rsp.find("result");
    if (it_result != rsp.end())
    {
        return RpcResponse{ id, RemoteResult(*it_result) };
    }

    const auto it_error = rsp.find("error");
    if (it_error == rsp.end())
    {
        return tl::unexpected<ErrorCode>(ErrorCode::kRpcResponseEmpty);
    }

    if (!it_error->is_object())
    {
        return tl::unexpected<ErrorCode>(ErrorCode::kRpcResponseInvalid);
    }

    /*
     * The two members the protocol requires are read with a default, so a peer
     * which omits one is still answered: the error object it sent is what the
     * caller has to see, and the missing member names nothing to act on.
     */
    RemoteError error{ -1, std::string(), nlohmann::json::object() };

    const auto it_code = it_error->find("code");
    if (it_code != it_error->end() && it_code->is_number_integer())
    {
        error.code = it_code->get<int>();
    }

    const auto it_message = it_error->find("message");
    if (it_message != it_error->end() && it_message->is_string())
    {
        error.message = it_message->get<std::string>();
    }

    const auto it_data = it_error->find("data");
    if (it_data != it_error->end())
    {
        error.data = *it_data;
    }

    return RpcResponse{ id, tl::unexpected<RemoteError>(std::move(error)) };
}

} // namespace appbox

#endif // APPBOX_COMMON_RPC_CODEC_HPP
