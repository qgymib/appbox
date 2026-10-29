#ifndef APPBOX_SANDBOX_FILESYSTEM_VIEW_CURRENT_DIRECTORY_HPP
#define APPBOX_SANDBOX_FILESYSTEM_VIEW_CURRENT_DIRECTORY_HPP

#include <cstddef>
#include <string>

namespace appbox::filesystem
{

/**
 * @brief Namespace which opens a drive in a DOS style NT path.
 *
 * The paths of the layers and the paths of the view are compared in this form,
 * see `common/MappingAsDosNtPath.hpp`, so a DOS path is turned into it before
 * it is handed to the view.
 */
inline constexpr const wchar_t* kDosNtPathNamespace = L"\\??\\";

/** @brief Namespace the object manager spells out for the global DOS namespace. */
inline constexpr const wchar_t* kGlobalDosNtPathNamespace = L"\\GLOBAL??\\";

/**
 * @brief Translation of a path of a layer into the path of its view.
 *
 * The signature is the one of `RebaseLayerPathToView()`, which the sandbox
 * implements on top of the layers of the run. The translation is handed to
 * `RebaseCurrentDirectory()` as a parameter, so the path forms of the call are
 * covered by a unit test which needs no layer, no sandbox and no system call.
 */
using RebaseLayerPathFn = bool (*)(const std::wstring& layer_path, std::wstring& view_path);

namespace detail
{

/**
 * @brief Fold one character of a path prefix to lower case.
 *
 * Only the letters of the ASCII range are folded: a path prefix of the object
 * manager is ASCII, and the call must not depend on the locale of the process,
 * which the sandbox runs under the locale of the application.
 *
 * @param[in] character Character to fold.
 * @return The lower case character.
 */
inline wchar_t ViewCurrentDirectoryFold(wchar_t character)
{
    if (character >= L'A' && character <= L'Z')
    {
        return static_cast<wchar_t>(character - L'A' + L'a');
    }

    return character;
}

/**
 * @brief Whether a path starts with a prefix, ignoring case.
 * @param[in] path Path to inspect.
 * @param[in] prefix Prefix to look for, null terminated.
 * @return true when the path starts with the prefix.
 */
inline bool ViewCurrentDirectoryStartsWith(const std::wstring& path, const wchar_t* prefix)
{
    std::size_t length = 0;
    while (prefix[length] != L'\0')
    {
        ++length;
    }

    if (path.size() < length)
    {
        return false;
    }

    for (std::size_t index = 0; index < length; ++index)
    {
        if (ViewCurrentDirectoryFold(path[index]) != ViewCurrentDirectoryFold(prefix[index]))
        {
            return false;
        }
    }

    return true;
}

/**
 * @brief Whether a path is the full path of a drive, for example `C:\dir`.
 * @param[in] path Path to inspect.
 * @return true when the path names a directory below the root of a drive.
 */
inline bool ViewCurrentDirectoryIsDrivePath(const std::wstring& path)
{
    const bool letter = (path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z');
    return path.size() >= 3 && letter && path[1] == L':' && path[2] == L'\\';
}

} // namespace detail

/**
 * @brief Translate a DOS path into the DOS style NT form of the object manager.
 *
 * Two shapes are translated: the path of a drive (`C:\dir`, which becomes
 * `\??\C:\dir`) and the path of a share (`\\server\share\dir`, which becomes
 * `\??\UNC\server\share\dir`). Every other shape names no entry of a local file
 * system the view maps - a device, a named pipe, a mailslot, a path which is
 * relative to the current directory of a drive, a path which already carries a
 * namespace - and is refused, so the caller keeps the directory as it is.
 *
 * @param[in] dos_path Path to translate.
 * @param[out] nt_path The DOS style NT path, only set when the call succeeds.
 * @return true when the path has a shape the view can name.
 */
inline bool DosPathToNtPath(const std::wstring& dos_path, std::wstring& nt_path)
{
    nt_path.clear();

    if (dos_path.empty())
    {
        return false;
    }

    /* A path which carries a namespace already is no DOS path. */
    if (detail::ViewCurrentDirectoryStartsWith(dos_path, kDosNtPathNamespace) ||
        detail::ViewCurrentDirectoryStartsWith(dos_path, kGlobalDosNtPathNamespace))
    {
        return false;
    }

    if (detail::ViewCurrentDirectoryIsDrivePath(dos_path))
    {
        nt_path = kDosNtPathNamespace + dos_path;
        return true;
    }

    if (!detail::ViewCurrentDirectoryStartsWith(dos_path, L"\\\\"))
    {
        return false;
    }

    /*
     * The device namespace spells a device with the same two leading
     * separators the UNC form uses, so it is refused before the share is read.
     */
    if (detail::ViewCurrentDirectoryStartsWith(dos_path, L"\\\\.\\") ||
        detail::ViewCurrentDirectoryStartsWith(dos_path, L"\\\\?\\"))
    {
        return false;
    }

    /* A share is named by a server and a share name, both of which are present. */
    const std::wstring share = dos_path.substr(2);
    const auto         separator = share.find(L'\\');
    if (separator == std::wstring::npos || separator == 0 || separator + 1 >= share.size() ||
        share[separator + 1] == L'\\')
    {
        return false;
    }

    nt_path = std::wstring(kDosNtPathNamespace) + L"UNC\\" + share;
    return true;
}

/**
 * @brief Translate a DOS style NT path back into a DOS path.
 *
 * The call is the counterpart of `DosPathToNtPath()`: `\??\C:\dir` becomes
 * `C:\dir` and `\??\UNC\server\share\dir` becomes `\\server\share\dir`. The
 * `\GLOBAL??\` spelling of the namespace is accepted as well.
 *
 * @param[in] nt_path Path to translate.
 * @param[out] dos_path The DOS path, only set when the call succeeds.
 * @return true when the path carries a namespace and names a drive or a share.
 */
inline bool NtPathToDosPath(const std::wstring& nt_path, std::wstring& dos_path)
{
    dos_path.clear();

    std::size_t offset = 0;
    if (detail::ViewCurrentDirectoryStartsWith(nt_path, kDosNtPathNamespace))
    {
        offset = 4;
    }
    else if (detail::ViewCurrentDirectoryStartsWith(nt_path, kGlobalDosNtPathNamespace))
    {
        offset = 10;
    }
    else
    {
        return false;
    }

    const std::wstring remainder = nt_path.substr(offset);

    if (detail::ViewCurrentDirectoryStartsWith(remainder, L"UNC\\"))
    {
        /* `\\server\share`: the share of the remainder is named by a server and a name. */
        const std::wstring share = remainder.substr(4);
        const auto         separator = share.find(L'\\');
        if (share.empty() || separator == 0)
        {
            return false;
        }

        dos_path = L"\\\\" + share;
        return true;
    }

    if (!detail::ViewCurrentDirectoryIsDrivePath(remainder))
    {
        return false;
    }

    dos_path = remainder;
    return true;
}

/**
 * @brief Translate the current directory of a process into the path of its view.
 *
 * The directory is translated into the DOS style NT form, handed to the
 * translation of the layers and translated back, which is the path a sandboxed
 * process has to work in: a relative path is resolved against it, so the
 * relative paths of the application keep being answered by the view instead of
 * reaching the directory of a layer directly.
 *
 * A directory which belongs to no layer - the directory of the host, the root
 * of a share - keeps its path. The rewritten path belongs to no layer either,
 * so running the call on its own result answers the same path, which makes the
 * call idempotent.
 *
 * @param[in] dos_path Current directory of the process.
 * @param[in] rebase Translation of a path of a layer, see `RebaseLayerPathFn`.
 * @param[out] view_dos_path Directory the process has to work in, only set when
 *                           the call succeeds.
 * @return true when the directory has a shape the view can name, false when it
 *         has not and the process keeps the directory it was created with.
 */
inline bool RebaseCurrentDirectory(const std::wstring& dos_path, RebaseLayerPathFn rebase, std::wstring& view_dos_path)
{
    view_dos_path.clear();

    std::wstring nt_path;
    if (rebase == nullptr || !DosPathToNtPath(dos_path, nt_path))
    {
        return false;
    }

    std::wstring view_nt_path;
    if (!rebase(nt_path, view_nt_path) || view_nt_path.empty() || view_nt_path == nt_path)
    {
        /* The directory belongs to no layer of the view. */
        view_dos_path = dos_path;
        return true;
    }

    std::wstring converted;
    if (!NtPathToDosPath(view_nt_path, converted))
    {
        view_dos_path = dos_path;
        return true;
    }

    view_dos_path = std::move(converted);
    return true;
}

/**
 * @brief Rewrite the current directory of this sandboxed process to the view.
 *
 * The call runs once, after the modules of the sandbox are initialized and
 * before the application runs: the hooks of the file system are attached by
 * then, so the directory of the view is opened through the view and the process
 * keeps a handle of the layer which holds it, exactly like a directory the
 * application opened itself.
 *
 * A directory which belongs to no layer keeps its path, and a step which fails
 * is reported and leaves the process with the directory it was created with:
 * the application then works in a directory of the host, which the hooks still
 * answer as the view, instead of not starting at all.
 */
void SetViewCurrentDirectory();

} // namespace appbox::filesystem

#endif // APPBOX_SANDBOX_FILESYSTEM_VIEW_CURRENT_DIRECTORY_HPP
