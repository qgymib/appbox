#include "LauncherConfigResource.hpp"
#include "PeResourcePatch.hpp"
#include "WString.hpp"
#include "launcher/EmbeddedResourceIds.h"
#include <cstddef>
#include <exception>
#include <filesystem>
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
 * @brief Write the configuration resource of one image.
 *
 * The image already carries the resource of an earlier pack run when the
 * payload was patched before, so the write replaces it instead of appending a
 * second copy: the launcher looks the resource up by its identifier and would
 * read whichever copy the resource directory returns first.
 *
 * @param[in] path Host path of the image to patch.
 * @param[in] json UTF-8 JSON document of the configuration.
 * @param[out] error Description of the failure.
 * @param[out] code Error code of the resource API on failure.
 * @return true when the resource was written.
 */
bool WriteConfigResource(const std::filesystem::path& path, std::string_view json, std::string& error, DWORD& code)
{
    code = 0;

    const HANDLE update = BeginUpdateResourceW(path.c_str(), FALSE);
    if (update == nullptr)
    {
        code = GetLastError();
        error = "failed to open '" + appbox::WideToUTF8(path.wstring()) + "' for the configuration update (error " +
                std::to_string(code) + ")";
        return false;
    }

    UpdateSession session(update);
    if (!UpdateResourceW(session.Get(), RT_RCDATA, MAKEINTRESOURCEW(IDR_APPBOX_LAUNCHER_CONFIG),
                         MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL), const_cast<char*>(json.data()),
                         static_cast<DWORD>(json.size())))
    {
        code = GetLastError();
        error = "failed to write the launcher configuration resource (error " + std::to_string(code) + ")";
        return false;
    }

    if (!session.Commit())
    {
        code = GetLastError();
        error = "failed to write the launcher configuration resource of '" + appbox::WideToUTF8(path.wstring()) +
                "' (error " + std::to_string(code) + ")";
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

std::vector<char> appbox::EmbedLauncherConfig(const void* launcher_bytes, std::size_t launcher_size,
                                              std::string_view json, std::string& error)
{
    error.clear();

    if (launcher_bytes == nullptr || launcher_size == 0)
    {
        error = "the launcher payload is empty";
        return {};
    }
    if (json.empty())
    {
        error = "the launcher configuration is empty";
        return {};
    }
    if (!LooksLikePeImage(launcher_bytes, launcher_size))
    {
        error = "the launcher payload is not a PE image";
        return {};
    }

    try
    {
        const auto          temporary_path = TemporaryPath(L"AppBox-LauncherConfig");
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
            if (WriteConfigResource(temporary_path, json, attempt_error, code))
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
        error = std::string("failed to embed the launcher configuration: ") + e.what();
        return {};
    }
}
