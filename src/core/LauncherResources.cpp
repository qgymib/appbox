#include "LauncherResources.hpp"
#include "PeResourcePatch.hpp"
#include "WString.hpp"
#include "launcher/EmbeddedResourceIds.h"
#include <cstddef>
#include <exception>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{

using appbox::pe_resource::kUpdateAttempts;
using appbox::pe_resource::LooksLikePeImage;
using appbox::pe_resource::ReadFileBytes;
using appbox::pe_resource::TemporaryFile;
using appbox::pe_resource::TemporaryPath;
using appbox::pe_resource::UpdateSession;
using appbox::pe_resource::WaitForResourceUpdate;
using appbox::pe_resource::WriteFileBytes;

/**
 * @brief Write the resources of one image.
 *
 * The image already carries the resources of an earlier pack run when the
 * payload was patched before, so the writes replace them instead of appending a
 * second copy: the launcher looks a resource up by its identifier and would read
 * whichever copy the resource directory returns first. Both resources are
 * written in one session, so the image is opened and committed once.
 *
 * @param[in] path Host path of the image to patch.
 * @param[in] config_json UTF-8 JSON document of the configuration.
 * @param[in] startup_image Bytes of the startup image, empty while the session
 *                          sets no image.
 * @param[out] error Description of the failure.
 * @param[out] code Error code of the resource API on failure.
 * @return true when the resources were written.
 */
bool WriteResources(const std::filesystem::path& path, std::string_view config_json, std::string_view startup_image,
                    std::string& error, DWORD& code)
{
    code = 0;

    const HANDLE update = BeginUpdateResourceW(path.c_str(), FALSE);
    if (update == nullptr)
    {
        code = GetLastError();
        error = "failed to open '" + appbox::WideToUTF8(path.wstring()) + "' for the resource update (error " +
                std::to_string(code) + ")";
        return false;
    }

    UpdateSession session(update);
    if (!UpdateResourceW(session.Get(), RT_RCDATA, MAKEINTRESOURCEW(IDR_APPBOX_LAUNCHER_CONFIG),
                         MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL), const_cast<char*>(config_json.data()),
                         static_cast<DWORD>(config_json.size())))
    {
        code = GetLastError();
        error = "failed to write the launcher configuration resource (error " + std::to_string(code) + ")";
        return false;
    }

    /*
     * A session without a startup image writes no image resource: the payload
     * the packer starts from is the launcher as it was built, which carries no
     * image resource, so there is nothing to replace or drop.
     */
    if (!startup_image.empty() &&
        !UpdateResourceW(session.Get(), RT_RCDATA, MAKEINTRESOURCEW(IDR_APPBOX_STARTUP_IMAGE),
                         MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL), const_cast<char*>(startup_image.data()),
                         static_cast<DWORD>(startup_image.size())))
    {
        code = GetLastError();
        error = "failed to write the startup image resource (error " + std::to_string(code) + ")";
        return false;
    }

    if (!session.Commit())
    {
        code = GetLastError();
        error = "failed to write the launcher resources of '" + appbox::WideToUTF8(path.wstring()) + "' (error " +
                std::to_string(code) + ")";
        return false;
    }

    return true;
}

} // namespace

std::string appbox::BuildLauncherConfigJson(const LauncherConfig& config)
{
    /*
     * The third argument turns the ASCII escaping off: the resource has to be
     * UTF-8, so a character which is not ASCII is written as its UTF-8 bytes
     * instead of a `\uXXXX` escape.
     */
    return nlohmann::json(config).dump(2, ' ', false);
}

std::vector<char> appbox::EmbedLauncherResources(const void* launcher_bytes, std::size_t launcher_size,
                                                 std::string_view config_json, std::string_view startup_image,
                                                 std::string& error)
{
    error.clear();

    if (launcher_bytes == nullptr || launcher_size == 0)
    {
        error = "the launcher payload is empty";
        return {};
    }
    if (config_json.empty())
    {
        error = "the launcher configuration is empty";
        return {};
    }
    /*
     * The resource API takes the size of a payload as a DWORD, so a payload
     * which does not fit that is refused instead of being truncated. The
     * parentheses keep the `max` macro of `<windows.h>` out of the call.
     */
    if (config_json.size() > (std::numeric_limits<DWORD>::max)())
    {
        error = "the launcher configuration is too large for the resource update";
        return {};
    }
    if (startup_image.size() > (std::numeric_limits<DWORD>::max)())
    {
        error = "the startup image is too large for the resource update";
        return {};
    }
    if (!LooksLikePeImage(launcher_bytes, launcher_size))
    {
        error = "the launcher payload is not a PE image";
        return {};
    }

    try
    {
        const auto          temporary_path = TemporaryPath(L"AppBox-LauncherResources");
        const TemporaryFile payload(temporary_path);
        if (!WriteFileBytes(temporary_path, launcher_bytes, launcher_size, error))
        {
            return {};
        }

        for (int attempt = 0; attempt < kUpdateAttempts; ++attempt)
        {
            if (attempt > 0)
            {
                WaitForResourceUpdate(attempt);
            }

            std::string attempt_error;
            DWORD       code = 0;
            if (WriteResources(temporary_path, config_json, startup_image, attempt_error, code))
            {
                /* An attempt which succeeded clears the reason of an earlier one. */
                error.clear();

                std::vector<char> patched;
                if (!ReadFileBytes(temporary_path, patched, error))
                {
                    return {};
                }

                return patched;
            }

            error = std::move(attempt_error);
            if (code != ERROR_ACCESS_DENIED && code != ERROR_SHARING_VIOLATION)
            {
                break;
            }
        }

        return {};
    }
    catch (const std::exception& e)
    {
        error = std::string("failed to embed the launcher resources: ") + e.what();
        return {};
    }
}
