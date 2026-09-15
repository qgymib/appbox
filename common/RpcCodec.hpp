#ifndef APPBOX_COMMON_RPC_CODEC_HPP
#define APPBOX_COMMON_RPC_CODEC_HPP

#include <cstdint>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "RemoteServer.hpp"

namespace appbox
{

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
FrameStatus TryDecodeFrame(std::vector<uint8_t>& buffer, std::string& out_payload, std::string& out_error);

/**
 * @brief Build the header of a frame which carries a payload of the given size.
 * @param[in] payload_size Payload size in bytes.
 * @param[out] out_header Serialized frame header.
 * @param[out] out_error Error description when the function fails.
 * @return true on success, otherwise false.
 */
bool MakeFrameHeader(size_t payload_size, std::string& out_header, std::string& out_error);

/**
 * @brief Parse a JSON-RPC response message.
 * @param[in] rsp Response object.
 * @param[out] out_id Request id of the response.
 * @param[out] out_result Parsed result or error.
 * @param[out] out_error Error description when the function fails.
 * @return true on success, otherwise false.
 */
bool ParseRpcResponse(const nlohmann::json& rsp, uint64_t& out_id, RemoteResult& out_result, std::string& out_error);

} // namespace appbox

#endif // APPBOX_COMMON_RPC_CODEC_HPP
