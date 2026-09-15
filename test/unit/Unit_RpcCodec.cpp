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
TEST(UnitRpcCodec, DecodeCompleteFrame)
{
    std::vector<uint8_t> buffer = MakeFrame("hello");

    std::string payload;
    std::string error;
    EXPECT_EQ(appbox::TryDecodeFrame(buffer, payload, error), appbox::FrameStatus::Ok);
    EXPECT_EQ(payload, "hello");
    EXPECT_TRUE(buffer.empty());
}

/**
 * @brief An empty payload is a valid frame.
 */
TEST(UnitRpcCodec, DecodeEmptyPayload)
{
    std::vector<uint8_t> buffer = MakeFrame("");

    std::string payload = "stale";
    std::string error;
    EXPECT_EQ(appbox::TryDecodeFrame(buffer, payload, error), appbox::FrameStatus::Ok);
    EXPECT_TRUE(payload.empty());
    EXPECT_TRUE(buffer.empty());
}

/**
 * @brief A partial header asks for more data.
 */
TEST(UnitRpcCodec, DecodePartialHeaderNeedsMoreData)
{
    std::vector<uint8_t> buffer = MakeFrame("hello");
    buffer.resize(sizeof(uint32_t)); /* Only the magic is available. */

    std::string payload;
    std::string error;
    EXPECT_EQ(appbox::TryDecodeFrame(buffer, payload, error), appbox::FrameStatus::NeedMoreData);
    EXPECT_EQ(buffer.size(), sizeof(uint32_t)); /* Nothing is consumed. */
}

/**
 * @brief A partial payload asks for more data.
 */
TEST(UnitRpcCodec, DecodePartialPayloadNeedsMoreData)
{
    std::vector<uint8_t> buffer = MakeFrame("hello");
    buffer.pop_back();

    std::string payload;
    std::string error;
    EXPECT_EQ(appbox::TryDecodeFrame(buffer, payload, error), appbox::FrameStatus::NeedMoreData);
    EXPECT_EQ(buffer.size(), MakeFrame("hello").size() - 1);
}

/**
 * @brief A wrong magic is a protocol error, the payload length is not trusted.
 */
TEST(UnitRpcCodec, DecodeInvalidMagicIsProtocolError)
{
    std::vector<uint8_t> buffer = MakeFrame("hello", kMagic + 1);

    std::string payload;
    std::string error;
    EXPECT_EQ(appbox::TryDecodeFrame(buffer, payload, error), appbox::FrameStatus::ProtocolError);
    EXPECT_FALSE(error.empty());
}

/**
 * @brief A length field above the limit is rejected before any allocation.
 */
TEST(UnitRpcCodec, DecodeOversizedLengthIsProtocolError)
{
    std::vector<uint8_t> buffer;
    AppendHeader(buffer, kMagic, 0xFFFFFFFFu);

    std::string payload;
    std::string error;
    EXPECT_EQ(appbox::TryDecodeFrame(buffer, payload, error), appbox::FrameStatus::ProtocolError);
    EXPECT_FALSE(error.empty());
}

/**
 * @brief Two frames in one buffer are decoded one after another.
 */
TEST(UnitRpcCodec, DecodeTwoFramesInOneBuffer)
{
    std::vector<uint8_t> buffer = MakeFrame("first");
    const std::vector<uint8_t> second = MakeFrame("second");
    buffer.insert(buffer.end(), second.begin(), second.end());

    std::string payload;
    std::string error;

    EXPECT_EQ(appbox::TryDecodeFrame(buffer, payload, error), appbox::FrameStatus::Ok);
    EXPECT_EQ(payload, "first");

    EXPECT_EQ(appbox::TryDecodeFrame(buffer, payload, error), appbox::FrameStatus::Ok);
    EXPECT_EQ(payload, "second");

    EXPECT_TRUE(buffer.empty());
}

/**
 * @brief The bytes of an incomplete following frame are kept in the buffer.
 */
TEST(UnitRpcCodec, DecodeKeepsIncompleteTrailingFrame)
{
    std::vector<uint8_t> buffer = MakeFrame("first");

    const std::vector<uint8_t> second = MakeFrame("second");
    buffer.insert(buffer.end(), second.begin(), second.begin() + 3); /* Partial header. */

    std::string payload;
    std::string error;

    EXPECT_EQ(appbox::TryDecodeFrame(buffer, payload, error), appbox::FrameStatus::Ok);
    EXPECT_EQ(payload, "first");

    EXPECT_EQ(appbox::TryDecodeFrame(buffer, payload, error), appbox::FrameStatus::NeedMoreData);
    EXPECT_EQ(buffer.size(), 3u); /* The partial frame is kept. */
}

/**
 * @brief The header of a maximum sized payload is accepted.
 */
TEST(UnitRpcCodec, MakeFrameHeaderAcceptsMaximumPayload)
{
    std::string header;
    std::string error;
    EXPECT_TRUE(appbox::MakeFrameHeader(appbox::kMaxRpcPayloadSize, header, error));
    EXPECT_EQ(header.size(), 2 * sizeof(uint32_t));
}

/**
 * @brief A payload above the limit is rejected instead of being truncated.
 */
TEST(UnitRpcCodec, MakeFrameHeaderRejectsOversizedPayload)
{
    std::string header;
    std::string error;
    EXPECT_FALSE(appbox::MakeFrameHeader(static_cast<size_t>(appbox::kMaxRpcPayloadSize) + 1, header, error));
    EXPECT_FALSE(error.empty());
}

/**
 * @brief The generated header can be decoded again.
 */
TEST(UnitRpcCodec, MakeFrameHeaderRoundTrip)
{
    const std::string payload = "round trip";

    std::string header;
    std::string error;
    ASSERT_TRUE(appbox::MakeFrameHeader(payload.size(), header, error));

    std::vector<uint8_t> buffer(header.begin(), header.end());
    buffer.insert(buffer.end(), payload.begin(), payload.end());

    std::string decoded;
    EXPECT_EQ(appbox::TryDecodeFrame(buffer, decoded, error), appbox::FrameStatus::Ok);
    EXPECT_EQ(decoded, payload);
}

/**
 * @brief A successful response carries an id and a result.
 */
TEST(UnitRpcCodec, ParseResponseWithResult)
{
    const nlohmann::json rsp = { { "jsonrpc", "2.0" }, { "id", 7 }, { "result", { { "value", 42 } } } };

    uint64_t    id = 0;
    appbox::RemoteResult result;
    std::string error;

    ASSERT_TRUE(appbox::ParseRpcResponse(rsp, id, result, error));
    EXPECT_EQ(id, 7u);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value()["value"].get<int>(), 42);
}

/**
 * @brief An error response carries the error code and message.
 */
TEST(UnitRpcCodec, ParseResponseWithError)
{
    const nlohmann::json rsp = { { "jsonrpc", "2.0" },
                                 { "id", 9 },
                                 { "error", { { "code", -32601 }, { "message", "Method not found" } } } };

    uint64_t             id = 0;
    appbox::RemoteResult result;
    std::string          error;

    ASSERT_TRUE(appbox::ParseRpcResponse(rsp, id, result, error));
    EXPECT_EQ(id, 9u);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, -32601);
    EXPECT_EQ(result.error().message, "Method not found");
}

/**
 * @brief A response without an id is rejected instead of throwing.
 */
TEST(UnitRpcCodec, ParseResponseWithoutId)
{
    const nlohmann::json rsp = { { "result", 1 } };

    uint64_t             id = 0;
    appbox::RemoteResult result;
    std::string          error;

    EXPECT_FALSE(appbox::ParseRpcResponse(rsp, id, result, error));
    EXPECT_FALSE(error.empty());
}

/**
 * @brief A response without a result and without an error is rejected.
 */
TEST(UnitRpcCodec, ParseResponseWithoutResultAndError)
{
    const nlohmann::json rsp = { { "id", 3 } };

    uint64_t             id = 0;
    appbox::RemoteResult result;
    std::string          error;

    EXPECT_FALSE(appbox::ParseRpcResponse(rsp, id, result, error));
    EXPECT_FALSE(error.empty());
}

/**
 * @brief A response which is not an object is rejected.
 */
TEST(UnitRpcCodec, ParseResponseNotAnObject)
{
    const nlohmann::json rsp = nlohmann::json::array();

    uint64_t             id = 0;
    appbox::RemoteResult result;
    std::string          error;

    EXPECT_FALSE(appbox::ParseRpcResponse(rsp, id, result, error));
    EXPECT_FALSE(error.empty());
}

/**
 * @brief An error field which is not an object is rejected instead of throwing.
 */
TEST(UnitRpcCodec, ParseResponseWithInvalidErrorField)
{
    const nlohmann::json rsp = { { "id", 4 }, { "error", "oops" } };

    uint64_t             id = 0;
    appbox::RemoteResult result;
    std::string          error;

    EXPECT_FALSE(appbox::ParseRpcResponse(rsp, id, result, error));
    EXPECT_FALSE(error.empty());
}
