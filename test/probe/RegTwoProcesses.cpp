#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include <string>
#include <vector>
#include "RegTwoProcesses.hpp"
#include "RegChildProcess.hpp"
#include "utils/RegistryRootKey.hpp"
#include "common/BuildCommandLine.hpp"
#include "WString.hpp"

namespace
{

/** Longest time the parent waits for the value of the child to appear. */
constexpr DWORD kChildValueTimeoutMs = 5000;

/** Longest time the parent waits for the child process to leave. */
constexpr DWORD kChildExitTimeoutMs = 30000;

/** Interval of the poll of the parent. */
constexpr DWORD kPollIntervalMs = 50;

/**
 * @brief Path of the executable of this process.
 * @return The path, empty when it cannot be queried.
 */
std::wstring ModulePath()
{
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;)
    {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0)
        {
            return std::wstring();
        }

        if (length < buffer.size())
        {
            buffer.resize(length);
            return buffer;
        }

        buffer.resize(buffer.size() * 2);
    }
}

/**
 * @brief Read a `REG_SZ` value of a key as UTF-8 text.
 *
 * @param[in] key The key which holds the value.
 * @param[in] name Name of the value.
 * @param[out] data The text of the value.
 * @return The error code of the registry API.
 */
DWORD ReadStringValue(HKEY key, const std::wstring& name, std::string& data)
{
    data.clear();

    std::vector<wchar_t> buffer(256);
    for (;;)
    {
        DWORD       size = static_cast<DWORD>(buffer.size() * sizeof(wchar_t));
        DWORD       type = REG_NONE;
        const DWORD code =
            RegQueryValueExW(key, name.c_str(), nullptr, &type, reinterpret_cast<LPBYTE>(buffer.data()), &size);
        if (code == ERROR_SUCCESS)
        {
            std::wstring text(buffer.data(), size / sizeof(wchar_t));
            while (!text.empty() && text.back() == L'\0')
            {
                text.pop_back();
            }
            data = appbox::WideToUTF8(text);
            return ERROR_SUCCESS;
        }

        if (code != ERROR_MORE_DATA)
        {
            return code;
        }

        buffer.resize(size / sizeof(wchar_t) + 1);
    }
}

/**
 * @brief Read a `REG_SZ` value of a key with the handle of the view.
 *
 * @param[in] root The root key of the case.
 * @param[in] key_path Path of the key relative to the root key.
 * @param[in] name Name of the value.
 * @param[out] data The text of the value.
 * @return The error code of the registry API.
 */
DWORD ReadValueOfKey(HKEY root, const std::wstring& key_path, const std::wstring& name, std::string& data)
{
    data.clear();

    HKEY key = nullptr;
    if (RegOpenKeyExW(root, key_path.c_str(), 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
    {
        return ERROR_FILE_NOT_FOUND;
    }

    const DWORD code = ReadStringValue(key, name, data);
    RegCloseKey(key);
    return code;
}

/**
 * @brief Command line of the worker process of the case.
 *
 * @param[in] req The request of the case.
 * @param[in] observation Name of the value which carries what the child read.
 * @return The arguments of the worker, without the path of the executable.
 */
std::vector<std::wstring> ChildArguments(const appbox::test::ProtocolRegTwoProcesses::Req& req,
                                         const std::wstring&                               observation)
{
    std::vector<std::wstring> args = { L"regchild" };

    if (!req.Root.empty())
    {
        args.push_back(L"--root");
        args.push_back(appbox::UTF8ToWide(req.Root));
    }

    args.push_back(L"--key");
    args.push_back(appbox::UTF8ToWide(req.Key));
    args.push_back(L"--read");
    args.push_back(appbox::UTF8ToWide(req.ParentValue));
    args.push_back(L"--expect");
    args.push_back(appbox::UTF8ToWide(req.ParentData));
    args.push_back(L"--set");
    args.push_back(appbox::UTF8ToWide(req.ChildValue) + L"=" + appbox::UTF8ToWide(req.ChildData));
    args.push_back(L"--set");
    args.push_back(observation + L"={read}");
    args.push_back(L"--hold-ms");
    args.push_back(std::to_wstring(req.ChildHoldMs));
    return args;
}

/**
 * @brief Write a `REG_SZ` value into a key.
 *
 * @param[in] key The key which receives the value.
 * @param[in] name Name of the value.
 * @param[in] text The text of the value.
 * @return The error code of the registry API.
 */
DWORD WriteStringValue(HKEY key, const std::wstring& name, const std::wstring& text)
{
    return RegSetValueExW(key, name.c_str(), 0, REG_SZ, reinterpret_cast<const BYTE*>(text.c_str()),
                          static_cast<DWORD>((text.size() + 1) * sizeof(wchar_t)));
}

} // namespace

/**
 * @brief Write a value of the parent, run the worker, read the value of the
 *        worker while it is alive and collect the observations of the run.
 *
 * The value of the worker is read while the worker process is still alive, so
 * the case tells a registry view which both processes share from a state which
 * is only written back to the hive file when a process leaves.
 */
static nlohmann::json ProbeRegTwoProcesses_Entry(const nlohmann::json& data)
{
    const auto req = data.get<appbox::test::ProtocolRegTwoProcesses::Req>();

    appbox::test::ProtocolRegTwoProcesses::Rsp rsp;
    rsp.role = "parent";

    const HKEY root = appbox::test::RegistryRootHandle(req.Root);
    if (root == nullptr)
    {
        rsp.create_code = ERROR_INVALID_PARAMETER;
        return rsp;
    }

    const std::wstring key_path = appbox::UTF8ToWide(req.Key);

    HKEY key = nullptr;
    rsp.create_code =
        RegCreateKeyExW(root, key_path.c_str(), 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &key, &rsp.disposition);
    if (rsp.create_code != ERROR_SUCCESS)
    {
        return rsp;
    }

    const std::wstring parent_name = appbox::UTF8ToWide(req.ParentValue);
    rsp.set_code = WriteStringValue(key, parent_name, appbox::UTF8ToWide(req.ParentData));
    if (rsp.set_code == ERROR_SUCCESS)
    {
        std::string readback;
        if (ReadStringValue(key, parent_name, readback) == ERROR_SUCCESS)
        {
            rsp.parent_readback = readback;
        }
    }
    RegCloseKey(key);

    /* The observation of the child is a value of the same key. */
    const std::wstring observation = appbox::UTF8ToWide(appbox::test::kRegChildObservationValue);

    /*
     * The worker is created by this process, so the sandbox is injected into it
     * and it mounts the hive of the same sandbox.
     */
    std::wstring cmdline = appbox::BuildCommandLine(ModulePath(), ChildArguments(req, observation));

    STARTUPINFOW        startup_info;
    PROCESS_INFORMATION process_info;
    ZeroMemory(&startup_info, sizeof(startup_info));
    startup_info.cb = sizeof(startup_info);
    ZeroMemory(&process_info, sizeof(process_info));

    if (!CreateProcessW(nullptr, cmdline.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                        &startup_info, &process_info))
    {
        rsp.child_launch_code = GetLastError();
        return rsp;
    }
    CloseHandle(process_info.hThread);

    /* Poll the value of the child until it appears, the child leaves or the budget ends. */
    const ULONGLONG deadline = GetTickCount64() + kChildValueTimeoutMs;
    for (;;)
    {
        std::string text;
        const DWORD code = ReadValueOfKey(root, key_path, appbox::UTF8ToWide(req.ChildValue), text);
        rsp.child_value_read_code = code;
        if (code == ERROR_SUCCESS)
        {
            rsp.child_value_readback = text;
            rsp.child_alive_when_read = WaitForSingleObject(process_info.hProcess, 0) == WAIT_TIMEOUT;
            break;
        }

        if (WaitForSingleObject(process_info.hProcess, 0) != WAIT_TIMEOUT)
        {
            /* The child left: the value cannot appear any more. */
            break;
        }

        if (GetTickCount64() >= deadline)
        {
            break;
        }

        Sleep(kPollIntervalMs);
    }

    if (WaitForSingleObject(process_info.hProcess, kChildExitTimeoutMs) == WAIT_TIMEOUT)
    {
        rsp.child_timed_out = true;
        TerminateProcess(process_info.hProcess, 0);
        WaitForSingleObject(process_info.hProcess, kChildExitTimeoutMs);
    }

    DWORD exit_code = 0;
    if (GetExitCodeProcess(process_info.hProcess, &exit_code))
    {
        rsp.child_exit_code = exit_code;
    }
    CloseHandle(process_info.hProcess);

    /* The observation of the child, which is part of the merged view. */
    std::string observation_text;
    rsp.child_saw_read_code = ReadValueOfKey(root, key_path, observation, observation_text);
    rsp.child_saw_readback = observation_text;

    return rsp;
}

appbox::test::Probe appbox::test::ProbeRegTwoProcesses("RegTwoProcesses", ProbeRegTwoProcesses_Entry);
