#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include "RegSaveKey.hpp"
#include "utils/RegistryRootKey.hpp"
#include "WString.hpp"
#include <algorithm>
#include <vector>

namespace
{

/**
 * @brief Enable the backup privilege of the current process.
 *
 * `RegSaveKeyW` needs `SeBackupPrivilege`; the privilege exists in the token of
 * an elevated process but is disabled by default, so it is enabled here.
 *
 * @return The error code of the call, `ERROR_SUCCESS` when the privilege is on.
 */
DWORD EnableBackupPrivilege()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
    {
        return GetLastError();
    }

    TOKEN_PRIVILEGES privileges = {};
    privileges.PrivilegeCount   = 1;
    /* The name of the privilege; the W entry point is used explicitly, because
     * the probe does not rely on the character set of the build. */
    if (!LookupPrivilegeValueW(nullptr, L"SeBackupPrivilege", &privileges.Privileges[0].Luid))
    {
        const DWORD error = GetLastError();
        CloseHandle(token);
        return error;
    }

    privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    const BOOL enabled = AdjustTokenPrivileges(token, FALSE, &privileges, 0, nullptr, nullptr);
    const DWORD error  = GetLastError();
    CloseHandle(token);

    if (!enabled)
    {
        return error != ERROR_SUCCESS ? error : ERROR_ACCESS_DENIED;
    }
    return error;
}

/**
 * @brief Whether the bytes of a hive file hold a text.
 *
 * The names of keys and values are stored as single byte text when they are
 * ASCII, while the data of a `REG_SZ` value is stored as UTF-16, so both
 * encodings are searched.
 *
 * @param[in] data The bytes of the file.
 * @param[in] text The text to search, UTF-8.
 * @return true when the text is part of the file.
 */
bool ContainsText(const std::vector<BYTE>& data, const std::string& text)
{
    if (text.empty())
    {
        return true;
    }

    const auto* narrow = reinterpret_cast<const BYTE*>(text.data());
    if (std::search(data.begin(), data.end(), narrow, narrow + text.size()) != data.end())
    {
        return true;
    }

    const std::wstring wide = appbox::UTF8ToWide(text);
    const auto*        wide_bytes = reinterpret_cast<const BYTE*>(wide.c_str());
    const std::size_t  wide_size = wide.size() * sizeof(wchar_t);
    return std::search(data.begin(), data.end(), wide_bytes, wide_bytes + wide_size) != data.end();
}

} // namespace

/**
 * @brief Save a key of the view and inspect the bytes of the written file.
 *
 * The call runs inside the sandbox, so the registry isolation has to export the
 * merged view of the key instead of the raw hive layer.
 */
static nlohmann::json ProbeRegSaveKey_Entry(const nlohmann::json& data)
{
    auto req = data.get<appbox::test::ProtocolRegSaveKey::Req>();

    appbox::test::ProtocolRegSaveKey::Rsp rsp;

    rsp.privilege_code = EnableBackupPrivilege();
    if (rsp.privilege_code != ERROR_SUCCESS)
    {
        return rsp;
    }

    const auto root = appbox::test::RegistryRootHandle(req.Root);
    if (root == nullptr)
    {
        rsp.open_code = ERROR_INVALID_PARAMETER;
        return rsp;
    }

    const auto path = appbox::UTF8ToWide(req.Path);

    HKEY key = nullptr;
    rsp.open_code = RegOpenKeyExW(root, appbox::UTF8ToWide(req.Key).c_str(), 0, KEY_READ, &key);
    if (rsp.open_code != ERROR_SUCCESS)
    {
        return rsp;
    }

    /* An earlier run may have left the file behind; the save expects a fresh one. */
    DeleteFileW(path.c_str());

    rsp.save_code = RegSaveKeyW(key, path.c_str(), nullptr);
    RegCloseKey(key);

    if (rsp.save_code == ERROR_SUCCESS)
    {
        /*
         * Read the file the way the view resolves it: the file the save wrote
         * is the one the filesystem isolation created.
         */
        HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE)
        {
            LARGE_INTEGER size = {};
            if (GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart <= 16 * 1024 * 1024)
            {
                std::vector<BYTE> bytes(static_cast<std::size_t>(size.QuadPart));
                DWORD             read = 0;
                if (ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr))
                {
                    bytes.resize(read);
                    rsp.size = read;
                    rsp.hive_signature = bytes.size() >= 4 && memcmp(bytes.data(), "regf", 4) == 0;

                    for (const auto& text : req.Expect)
                    {
                        if (!ContainsText(bytes, text))
                        {
                            rsp.missing.push_back(text);
                        }
                    }
                    for (const auto& text : req.Reject)
                    {
                        if (ContainsText(bytes, text))
                        {
                            rsp.unexpected.push_back(text);
                        }
                    }
                }
            }

            CloseHandle(file);
        }

        /* The file and the transaction logs of a mount are only for the probe. */
        DeleteFileW(path.c_str());
        DeleteFileW((path + L".LOG1").c_str());
        DeleteFileW((path + L".LOG2").c_str());
    }

    return rsp;
}

appbox::test::Probe appbox::test::ProbeRegSaveKey("RegSaveKey", ProbeRegSaveKey_Entry);
