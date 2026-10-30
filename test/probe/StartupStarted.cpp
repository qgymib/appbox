#include "WinAPI.h" /* Must be first include file */
#include <tlhelp32.h>
#include <cstdint>
#include <string>
#include <vector>
#include "WString.hpp"
#include "StartupStarted.hpp"
#include "probe/__init__.hpp"

namespace
{

/**
 * @brief Get the id of the process which created this one.
 *
 * The snapshot of the system lists the parent of every process, which is what
 * a case needs to tell whether the launcher created the process itself or
 * whether it started a program of its own in between.
 *
 * @return The parent process id, zero when the snapshot cannot be read.
 */
std::uint32_t ParentProcessId()
{
    const DWORD self = GetCurrentProcessId();

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
    {
        return 0;
    }

    PROCESSENTRY32W entry;
    ZeroMemory(&entry, sizeof(entry));
    entry.dwSize = sizeof(entry);

    std::uint32_t parent = 0;
    if (Process32FirstW(snapshot, &entry))
    {
        do
        {
            if (entry.th32ProcessID == self)
            {
                parent = entry.th32ParentProcessID;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return parent;
}

/**
 * @brief Get the path of the image this process runs.
 *
 * The path is the one the file system reports for the image, which is the file
 * of the layer the sandbox started the process from.
 *
 * @return The path in UTF-8, empty when it cannot be read.
 */
std::string ModulePath()
{
    std::vector<wchar_t> buffer(MAX_PATH);

    for (;;)
    {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0)
        {
            return {};
        }

        if (length < buffer.size())
        {
            return appbox::WideToUTF8(std::wstring(buffer.data(), length));
        }

        buffer.resize(buffer.size() * 2);
    }
}

/**
 * @brief Get the command line of this process.
 * @return The command line in UTF-8, empty when it cannot be read.
 */
std::string CommandLine()
{
    const wchar_t* command_line = GetCommandLineW();
    if (command_line == nullptr)
    {
        return {};
    }

    return appbox::WideToUTF8(command_line);
}

/**
 * @brief Get the current directory of this process.
 *
 * The directory is what the sandbox rewrites for a process which was created in
 * a directory of a layer, so the report tells whether the process works in the
 * path of the view.
 *
 * @return The directory in UTF-8, empty when it cannot be read.
 */
std::string CurrentDirectory()
{
    std::vector<wchar_t> buffer(MAX_PATH);

    for (;;)
    {
        const DWORD length = GetCurrentDirectoryW(static_cast<DWORD>(buffer.size()), buffer.data());
        if (length == 0)
        {
            return {};
        }

        if (length < buffer.size())
        {
            return appbox::WideToUTF8(std::wstring(buffer.data(), length));
        }

        buffer.resize(length + 1);
    }
}

/**
 * @brief Read the content of a file.
 *
 * The name is used as it is, so a relative name is resolved against the current
 * directory of this process: that resolution is what a case checks when it asks
 * whether the relative paths of an application are answered by the view.
 *
 * @param[in] name Name of the file.
 * @param[out] data Content of the file.
 * @return The error code of the call, zero on success.
 */
DWORD ReadFileByName(const std::string& name, std::string& data)
{
    data.clear();

    HANDLE handle = CreateFileW(appbox::UTF8ToWide(name).c_str(), GENERIC_READ,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
    {
        return GetLastError();
    }

    DWORD error = 0;
    char  buffer[4096];
    for (;;)
    {
        DWORD read = 0;
        if (!ReadFile(handle, buffer, static_cast<DWORD>(sizeof(buffer)), &read, nullptr))
        {
            error = GetLastError();
            break;
        }

        if (read == 0)
        {
            break;
        }

        data.append(buffer, read);
    }

    CloseHandle(handle);
    return error;
}

/**
 * @brief Write the content of a file and read it back.
 *
 * The file is created in the directory the process works in, so a case which
 * checks where the writes of an application land reads the layer of the view
 * and the directory of the host afterwards.
 *
 * @param[in] name Name of the file.
 * @param[in] data Content to write.
 * @param[out] readback Content which was read back after the write.
 * @return The error code of the call, zero on success.
 */
DWORD WriteFileByName(const std::string& name, const std::string& data, std::string& readback)
{
    readback.clear();

    HANDLE handle = CreateFileW(appbox::UTF8ToWide(name).c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
    {
        return GetLastError();
    }

    DWORD written = 0;
    if (!WriteFile(handle, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) || written != data.size())
    {
        const DWORD error = GetLastError();
        CloseHandle(handle);
        return error != 0 ? error : ERROR_WRITE_FAULT;
    }

    if (!FlushFileBuffers(handle) || SetFilePointer(handle, 0, nullptr, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
    {
        const DWORD error = GetLastError();
        CloseHandle(handle);
        return error != 0 ? error : ERROR_WRITE_FAULT;
    }

    DWORD error = 0;
    char  buffer[4096];
    for (;;)
    {
        DWORD read = 0;
        if (!ReadFile(handle, buffer, static_cast<DWORD>(sizeof(buffer)), &read, nullptr))
        {
            error = GetLastError();
            break;
        }

        if (read == 0)
        {
            break;
        }

        readback.append(buffer, read);
    }

    CloseHandle(handle);
    return error;
}

/**
 * @brief Delete a file by its name.
 * @param[in] name Name of the file.
 * @return The error code of the call, zero on success.
 */
DWORD DeleteFileByName(const std::string& name)
{
    return DeleteFileW(appbox::UTF8ToWide(name).c_str()) ? 0 : GetLastError();
}

/**
 * @brief Get the entries of the environment of this process.
 *
 * The block carries the current directory of every drive as an entry whose name
 * starts with `=`, which is no variable: a case reads the variables only, so
 * those entries are skipped, exactly like the request of a second instance is
 * defined to.
 *
 * @return One `name=value` text per variable, in the order of the block.
 */
std::vector<std::string> EnvironmentEntries()
{
    std::vector<std::string> entries;

    wchar_t* block = GetEnvironmentStringsW();
    if (block == nullptr)
    {
        return entries;
    }

    for (const wchar_t* cursor = block; *cursor != L'\0'; cursor += std::wstring(cursor).size() + 1)
    {
        if (*cursor == L'=')
        {
            continue;
        }

        entries.push_back(appbox::WideToUTF8(std::wstring(cursor)));
    }

    FreeEnvironmentStringsW(block);
    return entries;
}

} // namespace

appbox::test::ProtocolStartupStarted::Rsp appbox::test::ProbeStartupStarted_Entry(
    const appbox::test::ProtocolStartupStarted::Req& request)
{
    /*
     * The task data is empty for a plain report. A case which checks the
     * directory of the process names the files the process reads and writes
     * with a relative name, which is what the view has to answer.
     */
    const auto& task = request;

    appbox::test::ProtocolStartupStarted::Rsp rsp;
    rsp.marker = appbox::test::StartupMarker();
    rsp.pid = GetCurrentProcessId();
    rsp.parent_pid = ParentProcessId();
    rsp.module_path = ModulePath();
    rsp.command_line = CommandLine();
    rsp.environment = EnvironmentEntries();
    rsp.cwd = CurrentDirectory();

    for (const auto& operation : task.relative_ops)
    {
        appbox::test::ProtocolStartupStarted::OperationResult result;

        if (operation.op == "read")
        {
            result.code = ReadFileByName(operation.name, result.data);
        }
        else if (operation.op == "write")
        {
            result.code = WriteFileByName(operation.name, operation.data, result.data);
        }
        else if (operation.op == "delete")
        {
            result.code = DeleteFileByName(operation.name);
        }
        else
        {
            result.code = ERROR_INVALID_PARAMETER;
        }

        rsp.relative_ops.push_back(std::move(result));
    }

    return rsp;
}
