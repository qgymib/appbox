#include "utils/WinAPI.h" /* Must be first include file */
#include "utils/Log.hpp"
#include "utils/MappingAsDosNtPath.hpp"
#include "filesystem/Resolve.hpp"
#include "hook/__init__.hpp"
#include "FontApi.hpp"
#include <algorithm>
#include <cwctype>

namespace
{

/**
 * @brief Extensions the font loader of the system reads as a font resource.
 *
 * The list is the one the documentation of `AddFontResource` names: a font
 * resource file, a raw bitmap font, a TrueType file and its collection, a
 * TrueType resource file, a PostScript OpenType font and the Type 1 files.
 */
const wchar_t* const s_font_extensions[] = {
    L".fon", L".fnt", L".fot", L".mmm", L".otc", L".otf", L".pfb", L".pfm", L".ttc", L".ttf",
};

/**
 * @brief Whether a text ends with a suffix, ignoring the case.
 * @param[in] text Text to look at.
 * @param[in] suffix Suffix to look for.
 * @return true when the text ends with the suffix.
 */
bool EndsWithIgnoreCase(const std::wstring& text, const std::wstring& suffix)
{
    if (suffix.size() > text.size())
    {
        return false;
    }

    const std::size_t offset = text.size() - suffix.size();
    for (std::size_t index = 0; index < suffix.size(); ++index)
    {
        if (std::towlower(text[offset + index]) != std::towlower(suffix[index]))
        {
            return false;
        }
    }

    return true;
}

} // namespace

bool appbox::fonts::LoadFontModules()
{
    if (appbox::sys.h_gdi32 == nullptr)
    {
        appbox::sys.h_gdi32 = LoadLibraryW(L"gdi32.dll");
    }
    if (appbox::sys.h_win32u == nullptr)
    {
        appbox::sys.h_win32u = LoadLibraryW(L"win32u.dll");
    }

    if (appbox::sys.h_gdi32 == nullptr || appbox::sys.h_win32u == nullptr)
    {
        LOG_W("the modules of the font resources are not available");
        return false;
    }

    return true;
}

bool appbox::fonts::ResolveFontFilePath(const std::wstring& path, std::wstring& resolved)
{
    /*
     * The caller may spell the path with the namespace of the object manager,
     * which is the form the font entry points of the system receive.
     */
    std::wstring view_path;
    if (!appbox::MappingAsDosNtPath(path, view_path))
    {
        return false;
    }

    auto resolve_result = appbox::filesystem::Resolve(view_path);
    if (resolve_result->status != appbox::filesystem::ResolveResult::Status::Exists || resolve_result->hPath.empty())
    {
        return false;
    }

    /*
     * A path which only the host filesystem holds is left to the caller: the
     * kernel reaches that file without the sandbox, so the call already names
     * the file the view shows.
     */
    if (!resolve_result->bSandboxHolds)
    {
        return false;
    }

    resolved = resolve_result->hPath.front().fPath;
    return true;
}

bool appbox::fonts::RewriteFontFileBuffer(const wchar_t* files, ULONG cwc, std::wstring& rewritten,
                                          ULONG& rewritten_cwc)
{
    if (files == nullptr || cwc == 0)
    {
        return false;
    }

    /*
     * The buffer is read within the length the caller declared and never
     * beyond it: a hook runs inside a kernel call, so a buffer which does not
     * carry a terminator inside its length is left to the caller.
     */
    const wchar_t* const end = files + cwc;
    const wchar_t* const terminator = std::find(files, end, L'\0');
    if (terminator == end)
    {
        return false;
    }

    const std::size_t  length = static_cast<std::size_t>(terminator - files);
    const std::wstring view_path(files, length);

    std::wstring layer_path;
    if (!ResolveFontFilePath(view_path, layer_path))
    {
        return false;
    }

    rewritten.assign(layer_path);
    rewritten.push_back(L'\0');

    /*
     * The count of the caller covers the buffer including the terminating NUL,
     * so the difference of the path lengths is the difference of the counts.
     */
    rewritten_cwc = static_cast<ULONG>(cwc - length + layer_path.size());
    return true;
}

bool appbox::fonts::IsFontFileName(const std::wstring& name)
{
    for (const auto* extension : s_font_extensions)
    {
        if (EndsWithIgnoreCase(name, extension))
        {
            return true;
        }
    }

    return false;
}

bool appbox::fonts::DosPathOf(const std::wstring& nt_path, std::wstring& dos_path)
{
    constexpr const wchar_t* kPrefix = L"\\??\\";
    constexpr std::size_t    kPrefixSize = 4;

    if (nt_path.size() <= kPrefixSize || nt_path.compare(0, kPrefixSize, kPrefix) != 0)
    {
        return false;
    }

    /* The remainder has to be an absolute path of a drive, see MappingAsDosNtPath(). */
    const std::wstring remainder = nt_path.substr(kPrefixSize);
    if (remainder.size() < 3 || remainder[1] != L':' || remainder[2] != L'\\')
    {
        return false;
    }

    dos_path = remainder;
    return true;
}
