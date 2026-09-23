#include "tracer/TargetProgram.hpp"
#include <windows.h>
#include <string>
#include <vector>

namespace appbox::tracer
{

std::filesystem::path ResolveTargetProgram(const std::filesystem::path& target)
{
    if (target.empty())
    {
        return {};
    }

    std::error_code error;
    if (std::filesystem::is_regular_file(target, error) && !error)
    {
        const std::filesystem::path absolute = std::filesystem::absolute(target, error);
        return error ? target : absolute;
    }

    /*
     * SearchPathW applies the search order of CreateProcess. A name which
     * carries a directory is only looked up at that place, which is what makes
     * a typo in a relative path a hard error.
     */
    std::vector<wchar_t> buffer(MAX_PATH);
    const DWORD length = ::SearchPathW(nullptr, target.c_str(), L".exe",
                                       static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
    if (length == 0)
    {
        return {};
    }

    if (length >= buffer.size())
    {
        /* The buffer was too small: the call reports the required size. */
        buffer.resize(static_cast<std::size_t>(length) + 1);
        const DWORD retry = ::SearchPathW(nullptr, target.c_str(), L".exe",
                                          static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
        if (retry == 0 || retry >= buffer.size())
        {
            return {};
        }

        return std::filesystem::path(std::wstring(buffer.data(), retry));
    }

    return std::filesystem::path(std::wstring(buffer.data(), length));
}

} // namespace appbox::tracer
