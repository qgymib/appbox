#include "WinAPI.h" /* Must be first include file */
#include <string>
#include <utility>
#include "utils/Log.hpp"
#include "Sandbox.hpp"
#include "WString.hpp"
#include "LayerPath.hpp"
#include "ViewCurrentDirectory.hpp"

namespace
{

/**
 * @brief RtlSetCurrentDirectory_U() of ntdll.
 *
 * The call is the entry point `SetCurrentDirectoryW()` runs on: it opens the
 * directory, keeps the handle as the directory of the process and stores the
 * path it was given, which is what makes the three of them agree. It is not
 * documented, so it is resolved by name and a machine which does not offer it
 * keeps the directory the process was created with.
 *
 * @see https://ntdoc.m417z.com/rtlsetcurrentdirectoryu
 */
typedef NTSTATUS(NTAPI* T_RtlSetCurrentDirectory_U)(const UNICODE_STRING* Directory);

/**
 * @brief Read the current directory of this process.
 *
 * The call grows the buffer until the directory fits: the directory of a
 * process may be longer than `MAX_PATH`, and the length the call reports when
 * the buffer is too small is the length the directory needs.
 *
 * @param[out] cwd The directory as a DOS path.
 * @return true when the directory was read.
 */
bool ReadCurrentDirectory(std::wstring& cwd)
{
    std::wstring buffer(MAX_PATH, L'\0');

    for (;;)
    {
        const DWORD length = ::GetCurrentDirectoryW(static_cast<DWORD>(buffer.size()), buffer.data());
        if (length == 0)
        {
            return false;
        }

        if (length < buffer.size())
        {
            buffer.resize(length);
            cwd = std::move(buffer);
            return true;
        }

        buffer.resize(length + 1);
    }
}

/**
 * @brief Resolve RtlSetCurrentDirectory_U() of ntdll.
 *
 * The modules of the sandbox are initialized before the hooks are attached, so
 * the export is resolved through the loader of this process.
 *
 * @return The entry point, null when ntdll does not offer it.
 */
T_RtlSetCurrentDirectory_U ResolveSetCurrentDirectory()
{
    const HMODULE ntdll = ::GetModuleHandleW(L"ntdll.dll");
    if (ntdll == nullptr)
    {
        return nullptr;
    }

    return reinterpret_cast<T_RtlSetCurrentDirectory_U>(::GetProcAddress(ntdll, "RtlSetCurrentDirectory_U"));
}

} // namespace

void appbox::filesystem::SetViewCurrentDirectory()
{
    if (appbox::sandbox == nullptr || !appbox::sandbox->bIsolationMode)
    {
        return;
    }

    std::wstring cwd;
    if (!ReadCurrentDirectory(cwd))
    {
        LOG_W("the current directory of the process cannot be read: {}", ::GetLastError());
        return;
    }

    std::wstring view;
    if (!RebaseCurrentDirectory(cwd, RebaseLayerPathToView, view))
    {
        LOG_W("the current directory '{}' is not a path the view can name", appbox::WideToUTF8(cwd));
        return;
    }

    if (view == cwd)
    {
        LOG_D("the current directory '{}' belongs to no layer of the view", appbox::WideToUTF8(cwd));
        return;
    }

    /* The counted form of the entry point stores the length in a USHORT. */
    if (view.size() * sizeof(wchar_t) > 0xFFFFu)
    {
        LOG_W("the path of the view is too long for the directory of the process: {}", appbox::WideToUTF8(view));
        return;
    }

    const auto set_current_directory = ResolveSetCurrentDirectory();
    if (set_current_directory == nullptr)
    {
        LOG_W("the current directory of the process is not rewritten: RtlSetCurrentDirectory_U is missing");
        return;
    }

    /*
     * The path is spelled by a string of this call, and the counted form the
     * entry point reads is built on it: the buffer is not copied, so it has to
     * outlive the call, which it does.
     */
    UNICODE_STRING directory;
    directory.Buffer = view.data();
    directory.Length = static_cast<USHORT>(view.size() * sizeof(wchar_t));
    directory.MaximumLength = static_cast<USHORT>(directory.Length + sizeof(wchar_t));

    const NTSTATUS status = set_current_directory(&directory);
    if (!NT_SUCCESS(status))
    {
        LOG_W("the current directory of the process stays '{}': RtlSetCurrentDirectory_U failed with {:#x}",
              appbox::WideToUTF8(cwd), status);
        return;
    }

    LOG_I("the current directory of the process is the view path '{}' instead of '{}'", appbox::WideToUTF8(view),
          appbox::WideToUTF8(cwd));
}
