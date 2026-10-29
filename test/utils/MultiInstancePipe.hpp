#ifndef APPBOX_TEST_UTILS_MULTIINSTANCEPIPE_HPP
#define APPBOX_TEST_UTILS_MULTIINSTANCEPIPE_HPP

#include <cstdint>
#include <string>
#include <nlohmann/json.hpp>

namespace appbox::test
{

/**
 * @brief Client which writes and reads the frames of a control pipe itself.
 *
 * The client of `common/RemoteClient.hpp` sends what a launcher sends and
 * refuses to build a frame which is no request, so a case which exercises the
 * robustness of the master instance against a client which is not a launcher
 * talks to the pipe directly: it writes the payload of a frame as it is and
 * reads the answer the way the server writes it.
 *
 * The pipe is opened for synchronous use, so a read which waits for the answer
 * polls the number of bytes the pipe holds and gives up after the budget of the
 * caller instead of blocking for ever.
 */
class RawControlPipe
{
public:
    /**
     * @brief Open the client end of a pipe.
     * @param[in] pipe Name of the pipe, for example `\\.\pipe\appbox_<id>`.
     */
    explicit RawControlPipe(const std::string& pipe);
    ~RawControlPipe();

    RawControlPipe(const RawControlPipe&) = delete;
    RawControlPipe& operator=(const RawControlPipe&) = delete;

    /**
     * @brief Whether the pipe was opened.
     * @return true when the handle is usable.
     */
    bool IsOpen() const;

    /**
     * @brief Write one frame with the given payload.
     * @param[in] payload Payload of the frame, which is not validated.
     * @return true when the whole frame was written.
     */
    bool SendPayload(const std::string& payload);

    /**
     * @brief Read one frame the server writes.
     * @param[out] payload Payload of the frame.
     * @param[in] timeout_ms Budget of the wait.
     * @return true when a whole frame was read.
     */
    bool ReadPayload(std::string& payload, std::uint32_t timeout_ms);

    /**
     * @brief Send one JSON-RPC request.
     * @param[in] id Id of the request.
     * @param[in] method Method to call.
     * @param[in] param Parameters of the call, which may be null.
     * @return true when the request was written.
     */
    bool SendRequest(std::uint64_t id, const std::string& method, const nlohmann::json& param);

    /**
     * @brief Read the answer of one request.
     * @param[out] response Parsed answer.
     * @param[in] timeout_ms Budget of the wait.
     * @return true when an answer was read, false when the budget was used up or
     *         the answer is no JSON object.
     */
    bool ReadResponse(nlohmann::json& response, std::uint32_t timeout_ms);

    /**
     * @brief Call one method and read its answer.
     * @param[in] id Id of the request.
     * @param[in] method Method to call.
     * @param[in] param Parameters of the call, which may be null.
     * @param[out] response Parsed answer.
     * @param[in] timeout_ms Budget of the wait.
     * @return true when an answer was read.
     */
    bool Call(std::uint64_t id, const std::string& method, const nlohmann::json& param, nlohmann::json& response,
              std::uint32_t timeout_ms);

    /**
     * @brief Get the Win32 code of the last failed operation.
     * @return The error code, zero when no operation failed.
     */
    std::uint32_t LastError() const;

private:
    void*         handle_;     /* Client end of the pipe, `INVALID_HANDLE_VALUE` when it cannot be opened. */
    std::uint32_t last_error_; /* Win32 code of the last failed operation. */
};

/**
 * @brief Get the JSON-RPC error code of an answer.
 *
 * The code of an answer of the server is the one of the JSON-RPC specification
 * for a malformed request, an unknown method or a request which is too large,
 * and the one of the method for a request the method refused.
 *
 * @param[in] response Answer of the server.
 * @return The code, zero when the answer carries no error.
 */
int RpcErrorCode(const nlohmann::json& response);

} // namespace appbox::test

#endif // APPBOX_TEST_UTILS_MULTIINSTANCEPIPE_HPP
