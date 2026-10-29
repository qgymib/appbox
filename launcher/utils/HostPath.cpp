#include "WinAPI.h" /* Must be first include file */
#include <vector>
#include "HostPath.hpp"

std::wstring appbox::NormalizeHostPath(const std::wstring& path)
{
    if (path.empty())
    {
        return {};
    }

    std::wstring full = path;

    /*
     * The expanded form is asked for first: a relative path depends on the
     * current directory of the process, which the caller does not control.
     */
    const DWORD length = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (length != 0)
    {
        std::vector<wchar_t> buffer(static_cast<std::size_t>(length) + 1, L'\0');
        const DWORD written = GetFullPathNameW(path.c_str(), static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
        if (written != 0 && written < buffer.size())
        {
            full.assign(buffer.data(), written);
        }
    }

    /* The drive root keeps its separator, every other path loses the trailing ones. */
    while (full.size() > 1 && (full.back() == L'\\' || full.back() == L'/'))
    {
        if (full.size() == 3 && full[1] == L':')
        {
            break;
        }

        full.pop_back();
    }

    for (auto& character : full)
    {
        if (character >= L'A' && character <= L'Z')
        {
            character = static_cast<wchar_t>(character - L'A' + L'a');
        }
    }

    return full;
}

bool appbox::SameHostPath(const std::wstring& left, const std::wstring& right)
{
    return NormalizeHostPath(left) == NormalizeHostPath(right);
}
