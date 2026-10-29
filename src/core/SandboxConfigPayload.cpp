#include "PeResourcePatch.hpp" /* Must be first include file: it brings <windows.h> in. */
#include <detours.h>
#include <nlohmann/json.hpp>
#include "sandbox/Config.hpp"
#include "sandbox/utils/Defines.hpp"
#include "SandboxConfigPayload.hpp"
#include <cstddef>
#include <exception>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace
{

using appbox::pe_resource::kUpdateAttempts;
using appbox::pe_resource::ReadFileBytes;
using appbox::pe_resource::TemporaryFile;
using appbox::pe_resource::TemporaryPath;
using appbox::pe_resource::WaitForResourceUpdate;
using appbox::pe_resource::WriteFileBytes;

/**
 * @brief Whether a failure of the binary API may pass on a second attempt.
 *
 * A filter driver of the machine scans a freshly written image and holds it for
 * a moment, which the binary API reports as a denied or a shared access; the
 * same wait the resource update uses covers it.
 *
 * @param[in] code Error code of the failed attempt.
 * @return true when the attempt is worth repeating.
 */
bool IsRetryable(DWORD code)
{
    return code == ERROR_ACCESS_DENIED || code == ERROR_SHARING_VIOLATION || code == ERROR_USER_MAPPED_FILE;
}

/**
 * @brief RAII owner of a file handle.
 */
class FileHandle
{
public:
    /**
     * @brief Take ownership of a handle.
     * @param[in] handle Handle to close, `INVALID_HANDLE_VALUE` when the call failed.
     */
    explicit FileHandle(HANDLE handle) : handle_(handle)
    {
    }

    ~FileHandle()
    {
        if (handle_ != INVALID_HANDLE_VALUE && handle_ != nullptr)
        {
            CloseHandle(handle_);
        }
    }

    FileHandle(const FileHandle&) = delete;
    FileHandle& operator=(const FileHandle&) = delete;

    /**
     * @brief Whether the handle is usable.
     * @return true when the system opened the file.
     */
    explicit operator bool() const
    {
        return handle_ != INVALID_HANDLE_VALUE && handle_ != nullptr;
    }

    /**
     * @brief Get the handle.
     * @return The handle, `INVALID_HANDLE_VALUE` when the call failed.
     */
    HANDLE Get() const
    {
        return handle_;
    }

private:
    HANDLE handle_;
};

/**
 * @brief RAII owner of a binary image of the binary API of Detours.
 *
 * The image maps the file it was opened from, so it has to be released before
 * the file is opened for writing again.
 */
class BinaryImage
{
public:
    /**
     * @brief Take ownership of a binary image.
     * @param[in] binary Image returned by `DetourBinaryOpen()`, may be null.
     */
    explicit BinaryImage(PDETOUR_BINARY binary) : binary_(binary)
    {
    }

    ~BinaryImage()
    {
        if (binary_ != nullptr)
        {
            DetourBinaryClose(binary_);
        }
    }

    BinaryImage(const BinaryImage&) = delete;
    BinaryImage& operator=(const BinaryImage&) = delete;

    /**
     * @brief Whether the image was opened.
     * @return true when the image is usable.
     */
    explicit operator bool() const
    {
        return binary_ != nullptr;
    }

    /**
     * @brief Get the image.
     * @return The image, null when the call failed.
     */
    PDETOUR_BINARY Get() const
    {
        return binary_;
    }

private:
    PDETOUR_BINARY binary_;
};

/**
 * @brief Build the configuration a launcher carries in its own image.
 *
 * The document turns the hooks off and leaves every other member at its default:
 * the payload only answers the question whether the module works in this
 * process, while the configuration of a run is written into the process it
 * creates.
 *
 * @return The UTF-8 JSON document of the configuration.
 */
std::string BuildLauncherConfigPayload()
{
    appbox::SandboxConfig config;
    config.hook_enabled = false;
    return nlohmann::json(config).dump();
}

/**
 * @brief Write a copy of an image which carries the configuration payload.
 *
 * The patched image is written to a file of its own instead of the file which
 * was read, because the binary API maps the source while it writes and the
 * packer keeps the payload it started from untouched.
 *
 * @param[in] source Image to read.
 * @param[in] target File the patched image is written to.
 * @param[in] payload Configuration document to store.
 * @param[out] error Description of the failure, empty on success.
 * @param[out] code Error code of the binary API on failure.
 * @return true when the patched image was written.
 */
bool WritePatchedImage(const std::filesystem::path& source, const std::filesystem::path& target,
                       const std::string& payload, std::string& error, DWORD& code)
{
    code = 0;

    const FileHandle input(CreateFileW(source.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!input)
    {
        code = GetLastError();
        error = "failed to open '" + appbox::WideToUTF8(source.wstring()) + "' for the configuration (error " +
                std::to_string(code) + ")";
        return false;
    }

    const BinaryImage binary(DetourBinaryOpen(input.Get()));
    if (!binary)
    {
        code = GetLastError();
        error = "failed to read '" + appbox::WideToUTF8(source.wstring()) + "' as a PE image (error " +
                std::to_string(code) + ")";
        return false;
    }

    const GUID guid = APPBOX_SANDBOX_GUID;
    if (DetourBinarySetPayload(binary.Get(), guid, const_cast<char*>(payload.data()),
                               static_cast<DWORD>(payload.size())) == nullptr)
    {
        code = GetLastError();
        error = "failed to add the sandbox configuration to '" + appbox::WideToUTF8(source.wstring()) + "' (error " +
                std::to_string(code) + ")";
        return false;
    }

    const FileHandle output(CreateFileW(target.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                        FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!output)
    {
        code = GetLastError();
        error = "failed to create '" + appbox::WideToUTF8(target.wstring()) + "' for the patched image (error " +
                std::to_string(code) + ")";
        return false;
    }

    if (DetourBinaryWrite(binary.Get(), output.Get()) == FALSE)
    {
        code = GetLastError();
        error = "failed to write the patched image to '" + appbox::WideToUTF8(target.wstring()) + "' (error " +
                std::to_string(code) + ")";
        return false;
    }

    return true;
}

} // namespace

std::vector<char> appbox::EmbedSandboxConfigPayload(const void* image_bytes, std::size_t image_size, std::string& error)
{
    error.clear();

    if (image_bytes == nullptr || image_size == 0)
    {
        error = "the launcher payload is empty";
        return {};
    }
    if (!appbox::pe_resource::LooksLikePeImage(image_bytes, image_size))
    {
        error = "the launcher payload is not a PE image";
        return {};
    }

    try
    {
        const auto          source = TemporaryPath(L"AppBox-SandboxConfigPayload");
        const auto          target = TemporaryPath(L"AppBox-SandboxConfigPayloadPatched");
        const TemporaryFile source_file(source);
        const TemporaryFile target_file(target);

        if (!WriteFileBytes(source, image_bytes, image_size, error))
        {
            return {};
        }

        const std::string payload = BuildLauncherConfigPayload();

        for (int attempt = 0; attempt < kUpdateAttempts; ++attempt)
        {
            if (attempt > 0)
            {
                WaitForResourceUpdate(attempt);
            }

            std::string attempt_error;
            DWORD       code = 0;
            if (WritePatchedImage(source, target, payload, attempt_error, code))
            {
                /* An attempt which succeeded clears the reason of an earlier one. */
                error.clear();

                std::vector<char> patched;
                if (!ReadFileBytes(target, patched, error))
                {
                    return {};
                }

                return patched;
            }

            error = std::move(attempt_error);
            if (!IsRetryable(code))
            {
                break;
            }
        }

        return {};
    }
    catch (const std::exception& e)
    {
        error = std::string("failed to embed the sandbox configuration: ") + e.what();
        return {};
    }
}
