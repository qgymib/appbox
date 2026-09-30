#include "utils/WinAPI.h" /* Must be first include file */
#include "utils/FontApi.hpp"
#include "utils/Log.hpp"
#include "filesystem/Resolve.hpp"
#include "hook/NtQueryAttributesFile.hpp"
#include "hook/RtlInitUnicodeString.hpp"
#include "Sandbox.hpp"
#include "Isolation.hpp"
#include <cwctype>
#include <string>
#include <vector>

namespace
{

/**
 * @brief Deepest directory below the font directory which is searched.
 *
 * The font directory of a system is flat; the limit only keeps a pathological
 * layer from walking an endless tree.
 */
constexpr std::size_t kMaxDepth = 8;

/**
 * @brief Build the view path of the font directory of the system.
 *
 * @return The path, empty when the directory cannot be resolved.
 */
std::wstring FontsViewPath()
{
    wchar_t    windows_directory[MAX_PATH] = {};
    const UINT length = GetWindowsDirectoryW(windows_directory, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
    {
        LOG_W("the directory of the system cannot be resolved");
        return std::wstring();
    }

    /*
     * The font directory is `FOLDERID_Fonts`, which is the `Fonts` folder of
     * the Windows directory and cannot be redirected. The path is built here
     * instead of being resolved through the shell, because the module runs
     * inside the loader lock, where loading further modules is a risk.
     */
    return std::wstring(L"\\??\\") + windows_directory + L"\\Fonts";
}

/**
 * @brief Resolve the entry points the resolver of the filesystem reads a path with.
 *
 * The module runs before the hooks are attached, so the trampolines of the
 * resolver are not resolved yet. The two entry points are resolved here without
 * attaching a hook: the resolver of the sandbox stays the single place which
 * decides what the view shows, and the module reads the view through the
 * original entry points of the process like every other module does.
 */
void ResolveQueryEntryPoints()
{
    if (appbox::sys.h_ntdll == nullptr)
    {
        appbox::sys.h_ntdll = GetModuleHandleW(L"ntdll.dll");
    }

    appbox::HookRtlInitUnicodeString.load_proc_addr_fn();
    appbox::HookNtQueryAttributesFile.load_proc_addr_fn();
}

/**
 * @brief Whether two names of the view name the same file.
 * @param[in] left One name.
 * @param[in] right The other name.
 * @return true when the names are equal ignoring the case.
 */
bool IsSameName(const std::wstring& left, const std::wstring& right)
{
    if (left.size() != right.size())
    {
        return false;
    }

    for (std::size_t index = 0; index < left.size(); ++index)
    {
        if (std::towlower(left[index]) != std::towlower(right[index]))
        {
            return false;
        }
    }

    return true;
}

/**
 * @brief Collect the font files of one directory of a layer.
 *
 * The names are collected relative to the font directory and only once, so the
 * layer which comes first in the view decides the file of a name. The files are
 * not resolved here: the caller resolves every name in the view, which is what
 * honours a whiteout, an opaque marker and the isolation of a single file.
 *
 * @param[in] directory DOS path of the directory inside its layer.
 * @param[in] relative Path of the directory below the font directory.
 * @param[in] depth Depth of the directory below the font directory.
 * @param[in,out] names Names which were collected so far.
 */
void CollectFontNames(const std::wstring& directory, const std::wstring& relative, std::size_t depth,
                      std::vector<std::wstring>& names)
{
    WIN32_FIND_DATAW   entry = {};
    const std::wstring pattern = directory + L"\\*";

    const HANDLE find = FindFirstFileW(pattern.c_str(), &entry);
    if (find == INVALID_HANDLE_VALUE)
    {
        return;
    }

    do
    {
        const std::wstring name(entry.cFileName);
        if (name == L"." || name == L"..")
        {
            continue;
        }

        const std::wstring child_relative = relative.empty() ? name : relative + L"\\" + name;
        if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        {
            if (depth < kMaxDepth)
            {
                CollectFontNames(directory + L"\\" + name, child_relative, depth + 1, names);
            }
            continue;
        }

        if (!appbox::fonts::IsFontFileName(name))
        {
            continue;
        }

        bool known = false;
        for (const auto& existing : names)
        {
            if (IsSameName(existing, child_relative))
            {
                known = true;
                break;
            }
        }

        if (!known)
        {
            names.push_back(child_relative);
        }
    } while (FindNextFileW(find, &entry) != FALSE);

    FindClose(find);
}

} // namespace

NTSTATUS appbox::fonts::Isolation::Init()
{
    if (appbox::sandbox == nullptr || !appbox::sandbox->bIsolationMode)
    {
        /* Nothing to do outside isolation mode; the hooks are not attached. */
        return STATUS_SUCCESS;
    }

    if (!appbox::fonts::LoadFontModules())
    {
        /* The hook table reports the module which cannot be loaded. */
        return STATUS_SUCCESS;
    }

    ResolveQueryEntryPoints();

    const std::wstring fonts_view = FontsViewPath();
    if (fonts_view.empty())
    {
        return STATUS_SUCCESS;
    }

    /*
     * The layers which hold the font directory, the upper one first: the order
     * of the candidates is the precedence of the view, so the first layer which
     * holds a name is the layer the sandboxed process sees.
     */
    appbox::filesystem::ResolveOption option;
    option.bStopOnFirstFound = false;

    auto resolve_result =
        appbox::filesystem::ResolveFull(appbox::sandbox->fs, fonts_view, option, &appbox::sandbox->fs_isolation);
    if (resolve_result->status != appbox::filesystem::ResolveResult::Status::Exists)
    {
        LOG_D("the view holds no font directory");
        return STATUS_SUCCESS;
    }

    std::vector<std::wstring> names;
    for (const auto& layer : resolve_result->hPath)
    {
        /* The fonts of the host filesystem are installed already. */
        if (layer.layer == resolve_result->hostLayer)
        {
            continue;
        }

        std::wstring directory;
        if (!appbox::fonts::DosPathOf(layer.fPath, directory))
        {
            continue;
        }

        CollectFontNames(directory, L"", 0, names);
    }

    std::size_t loaded = 0;
    std::size_t refused = 0;

    for (const auto& name : names)
    {
        const std::wstring view_path = fonts_view + L"\\" + name;

        std::wstring layer_path;
        if (!appbox::fonts::ResolveFontFilePath(view_path, layer_path))
        {
            /* The host holds the font already, the isolation hides it or no
             * layer holds it at all. */
            continue;
        }

        std::wstring dos_path;
        if (!appbox::fonts::DosPathOf(layer_path, dos_path))
        {
            ++refused;
            continue;
        }

        /*
         * `FR_PRIVATE` adds the font to the font table of this process only:
         * the packaged application creates and enumerates it as if it were
         * installed, while the font table, the font directory and the registry
         * of the host stay untouched. The flag is the reason the sandbox never
         * passes zero, which would add the font to the font table of the
         * session and leak it to every other process of the host.
         */
        if (AddFontResourceExW(dos_path.c_str(), FR_PRIVATE, nullptr) == 0)
        {
            LOG_W(L"the font cannot be loaded: {}", dos_path);
            ++refused;
            continue;
        }

        appbox::sandbox->wFontPaths.push_back(dos_path);
        LOG_D(L"font loaded: {}", dos_path);
        ++loaded;
    }

    LOG_I("font isolation: {} of {} font files loaded, {} refused", loaded, names.size(), refused);
    return STATUS_SUCCESS;
}

void appbox::fonts::Isolation::Exit()
{
    if (appbox::sandbox == nullptr)
    {
        return;
    }

    appbox::sandbox->wFontPaths.clear();
}
