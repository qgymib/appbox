#include <gtest/gtest.h>
#include <cstdint>
#include <string>
#include <vector>
#include "RpcCodec.hpp"

namespace
{

/* Magic of the frame protocol, kept in sync with RemoteProtocol. */
constexpr uint32_t kMagic = 0x424F583A;

/**
 * @brief Append a frame header to a buffer.
 * @param[in,out] buffer Target buffer.
 * @param[in] magic Frame magic.
 * @param[in] length Payload length field.
 */
void AppendHeader(std::vector<uint8_t>& buffer, uint32_t magic, uint32_t length)
{
    const auto* p_magic = reinterpret_cast<const uint8_t*>(&magic);
    buffer.insert(buffer.end(), p_magic, p_magic + sizeof(magic));

    const auto* p_length = reinterpret_cast<const uint8_t*>(&length);
    buffer.insert(buffer.end(), p_length, p_length + sizeof(length));
}

/**
 * @brief Build a complete frame.
 * @param[in] payload Frame payload.
 * @param[in] magic Frame magic.
 * @return The serialized frame.
 */
std::vector<uint8_t> MakeFrame(const std::string& payload, uint32_t magic = kMagic)
{
    std::vector<uint8_t> buffer;
    AppendHeader(buffer, magic, static_cast<uint32_t>(payload.size()));
    buffer.insert(buffer.end(), payload.begin(), payload.end());

    return buffer;
}

} // namespace

/**
 * @brief A complete frame is decoded and removed from the buffer.
 */
TEST(Unit_RpcCodec, DecodeCompleteFrame)
{
    std::vector<uint8_t> buffer = MakeFrame("hello");

    const auto decoded = appbox::TryDecodeFrame(buffer);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->status, appbox::FrameStatus::Ok);
    EXPECT_EQ(decoded->payload, "hello");
    EXPECT_TRUE(buffer.empty());
}

/**
 * @brief An empty payload is a valid frame.
 */
TEST(Unit_RpcCodec, DecodeEmptyPayload)
{
    std::vector<uint8_t> buffer = MakeFrame("");

    const auto decoded = appbox::TryDecodeFrame(buffer);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->status, appbox::FrameStatus::Ok);
    EXPECT_TRUE(decoded->payload.empty());
    EXPECT_TRUE(buffer.empty());
}

/**
 * @brief A partial header asks for more data.
 */
TEST(Unit_RpcCodec, DecodePartialHeaderNeedsMoreData)
{
    std::vector<uint8_t> buffer = MakeFrame("hello");
    buffer.resize(sizeof(uint32_t)); /* Only the magic is available. */

    const auto decoded = appbox::TryDecodeFrame(buffer);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->status, appbox::FrameStatus::NeedMoreData);
    EXPECT_EQ(buffer.size(), sizeof(uint32_t)); /* Nothing is consumed. */
}

/**
 * @brief A partial payload asks for more data.
 */
TEST(Unit_RpcCodec, DecodePartialPayloadNeedsMoreData)
{
    std::vector<uint8_t> buffer = MakeFrame("hello");
    buffer.pop_back();

    const auto decoded = appbox::TryDecodeFrame(buffer);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->status, appbox::FrameStatus::NeedMoreData);
    EXPECT_EQ(buffer.size(), MakeFrame("hello").size() - 1);
}

/**
 * @brief A wrong magic is a protocol error, the payload length is not trusted.
 */
TEST(Unit_RpcCodec, DecodeInvalidMagicIsProtocolError)
{
    std::vector<uint8_t> buffer = MakeFrame("hello", kMagic + 1);

    const auto decoded = appbox::TryDecodeFrame(buffer);
    ASSERT_FALSE(decoded.has_value());
    EXPECT_EQ(decoded.error(), appbox::ErrorCode::kRpcFrameMagicInvalid);
}

/**
 * @brief A length field above the limit is rejected before any allocation.
 */
TEST(Unit_RpcCodec, DecodeOversizedLengthIsProtocolError)
{
    std::vector<uint8_t> buffer;
    AppendHeader(buffer, kMagic, 0xFFFFFFFFu);

    const auto decoded = appbox::TryDecodeFrame(buffer);
    ASSERT_FALSE(decoded.has_value());
    EXPECT_EQ(decoded.error(), appbox::ErrorCode::kRpcFrameTooLarge);
}

/**
 * @brief Two frames in one buffer are decoded one after another.
 */
TEST(Unit_RpcCodec, DecodeTwoFramesInOneBuffer)
{
    std::vector<uint8_t>       buffer = MakeFrame("first");
    const std::vector<uint8_t> second = MakeFrame("second");
    buffer.insert(buffer.end(), second.begin(), second.end());

    const auto first = appbox::TryDecodeFrame(buffer);
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->status, appbox::FrameStatus::Ok);
    EXPECT_EQ(first->payload, "first");

    const auto next = appbox::TryDecodeFrame(buffer);
    ASSERT_TRUE(next.has_value());
    EXPECT_EQ(next->status, appbox::FrameStatus::Ok);
    EXPECT_EQ(next->payload, "second");

    EXPECT_TRUE(buffer.empty());
}

/**
 * @brief The bytes of an incomplete following frame are kept in the buffer.
 */
TEST(Unit_RpcCodec, DecodeKeepsIncompleteTrailingFrame)
{
    std::vector<uint8_t> buffer = MakeFrame("first");

    const std::vector<uint8_t> second = MakeFrame("second");
    buffer.insert(buffer.end(), second.begin(), second.begin() + 3); /* Partial header. */

    const auto first = appbox::TryDecodeFrame(buffer);
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->status, appbox::FrameStatus::Ok);
    EXPECT_EQ(first->payload, "first");

    const auto next = appbox::TryDecodeFrame(buffer);
    ASSERT_TRUE(next.has_value());
    EXPECT_EQ(next->status, appbox::FrameStatus::NeedMoreData);
    EXPECT_EQ(buffer.size(), 3u); /* The partial frame is kept. */
}

/**
 * @brief The header of a maximum sized payload is accepted.
 */
TEST(Unit_RpcCodec, MakeFrameHeaderAcceptsMaximumPayload)
{
    const auto header = appbox::MakeFrameHeader(appbox::kMaxRpcPayloadSize);
    ASSERT_TRUE(header.has_value());
    EXPECT_EQ(header->size(), 2 * sizeof(uint32_t));
}

/**
 * @brief A payload above the limit is rejected instead of being truncated.
 */
TEST(Unit_RpcCodec, MakeFrameHeaderRejectsOversizedPayload)
{
    const auto header = appbox::MakeFrameHeader(static_cast<size_t>(appbox::kMaxRpcPayloadSize) + 1);
    ASSERT_FALSE(header.has_value());
    EXPECT_EQ(header.error(), appbox::ErrorCode::kRpcFrameTooLarge);
}

/**
 * @brief The generated header can be decoded again.
 */
TEST(Unit_RpcCodec, MakeFrameHeaderRoundTrip)
{
    const std::string payload = "round trip";

    const auto header = appbox::MakeFrameHeader(payload.size());
    ASSERT_TRUE(header.has_value());

    std::vector<uint8_t> buffer(header->begin(), header->end());
    buffer.insert(buffer.end(), payload.begin(), payload.end());

    const auto decoded = appbox::TryDecodeFrame(buffer);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->status, appbox::FrameStatus::Ok);
    EXPECT_EQ(decoded->payload, payload);
}

/**
 * @brief A successful response carries an id and a result.
 */
TEST(Unit_RpcCodec, ParseResponseWithResult)
{
    const nlohmann::json rsp = {
        { "jsonrpc", "2.0"               },
        { "id",      7                   },
        { "result",  { { "value", 42 } } }
    };

    const auto parsed = appbox::ParseRpcResponse(rsp);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->id, 7u);
    ASSERT_TRUE(parsed->result.has_value());
    EXPECT_EQ(parsed->result.value()["value"].get<int>(), 42);
}

/**
 * @brief An error response carries the error code and message.
 */
TEST(Unit_RpcCodec, ParseResponseWithError)
{
    const nlohmann::json rsp = {
        { "jsonrpc", "2.0"                                                     },
        { "id",      9                                                         },
        { "error",   { { "code", -32601 }, { "message", "Method not found" } } }
    };

    const auto parsed = appbox::ParseRpcResponse(rsp);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->id, 9u);
    ASSERT_FALSE(parsed->result.has_value());
    EXPECT_EQ(parsed->result.error().code, -32601);
    EXPECT_EQ(parsed->result.error().message, "Method not found");
}

/**
 * @brief A response without an id is rejected instead of throwing.
 */
TEST(Unit_RpcCodec, ParseResponseWithoutId)
{
    const nlohmann::json rsp = {
        { "result", 1 }
    };

    const auto parsed = appbox::ParseRpcResponse(rsp);
    ASSERT_FALSE(parsed.has_value());
    EXPECT_EQ(parsed.error(), appbox::ErrorCode::kRpcResponseIdMissing);
}

/**
 * @brief A response without a result and without an error is rejected.
 */
TEST(Unit_RpcCodec, ParseResponseWithoutResultAndError)
{
    const nlohmann::json rsp = {
        { "id", 3 }
    };

    const auto parsed = appbox::ParseRpcResponse(rsp);
    ASSERT_FALSE(parsed.has_value());
    EXPECT_EQ(parsed.error(), appbox::ErrorCode::kRpcResponseEmpty);
}

/**
 * @brief A response which is not an object is rejected.
 */
TEST(Unit_RpcCodec, ParseResponseNotAnObject)
{
    const nlohmann::json rsp = nlohmann::json::array();

    const auto parsed = appbox::ParseRpcResponse(rsp);
    ASSERT_FALSE(parsed.has_value());
    EXPECT_EQ(parsed.error(), appbox::ErrorCode::kRpcResponseNotObject);
}

/**
 * @brief An error field which is not an object is rejected instead of throwing.
 */
TEST(Unit_RpcCodec, ParseResponseWithInvalidErrorField)
{
    const nlohmann::json rsp = {
        { "id",    4      },
        { "error", "oops" }
    };

    const auto parsed = appbox::ParseRpcResponse(rsp);
    ASSERT_FALSE(parsed.has_value());
    EXPECT_EQ(parsed.error(), appbox::ErrorCode::kRpcResponseInvalid);
}
