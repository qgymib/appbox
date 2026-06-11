#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#define WIN32_NO_STATUS
#include <windows.h>
#include <ntstatus.h>

#include <bcrypt.h>
#include <sddl.h>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "WString.hpp"
#include "RegCompile.hpp"

#pragma comment(lib, "bcrypt.lib")

/* ========================================================================= */
/*  Internal Helpers                                                         */
/* ========================================================================= */

namespace
{

/* ------------------------------------------------------------------------ */
/*  SHA-256                                                                 */
/* ------------------------------------------------------------------------ */

struct Sha256Hash
{
    static constexpr DWORD kHashLen = 32;
    BYTE                  data[kHashLen];

    bool operator==(const Sha256Hash& other) const { return memcmp(data, other.data, kHashLen) == 0; }
    bool operator!=(const Sha256Hash& other) const { return !(*this == other); }

    bool IsZero() const
    {
        for (DWORD i = 0; i < kHashLen; ++i)
            if (data[i] != 0)
                return false;
        return true;
    }

    static Sha256Hash Zero()
    {
        Sha256Hash h{};
        memset(h.data, 0, kHashLen);
        return h;
    }
};

static Sha256Hash ComputeFileSha256(const std::wstring& file_path)
{
    std::ifstream f(file_path, std::ios::binary);
    if (!f)
        return Sha256Hash::Zero();

    BCRYPT_ALG_HANDLE  hAlg = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;
    Sha256Hash         result{};

    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0)
        return Sha256Hash::Zero();

    if (BCryptCreateHash(hAlg, &hHash, nullptr, 0, nullptr, 0, 0) != 0)
    {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return Sha256Hash::Zero();
    }

    char buf[65536];
    while (f.read(buf, sizeof(buf)) || f.gcount() > 0)
    {
        BCryptHashData(hHash, reinterpret_cast<PUCHAR>(buf), static_cast<ULONG>(f.gcount()), 0);
    }

    BCryptFinishHash(hHash, result.data, Sha256Hash::kHashLen, 0);

    BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return result;
}

/* ------------------------------------------------------------------------ */
/*  SID Helper                                                              */
/* ------------------------------------------------------------------------ */

static std::wstring GetCurrentUserSid()
{
    HANDLE hToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken))
        return L"";

    DWORD bufLen = 0;
    GetTokenInformation(hToken, TokenUser, nullptr, 0, &bufLen);
    if (bufLen == 0)
    {
        CloseHandle(hToken);
        return L"";
    }

    auto buf = std::make_unique<BYTE[]>(bufLen);
    if (!GetTokenInformation(hToken, TokenUser, buf.get(), bufLen, &bufLen))
    {
        CloseHandle(hToken);
        return L"";
    }
    CloseHandle(hToken);

    auto*  tu     = reinterpret_cast<TOKEN_USER*>(buf.get());
    LPWSTR sidStr = nullptr;
    if (!ConvertSidToStringSidW(tu->User.Sid, &sidStr))
        return L"";

    std::wstring result(sidStr);
    LocalFree(sidStr);
    return result;
}

/* ------------------------------------------------------------------------ */
/*  Hive File Lock                                                          */
/* ------------------------------------------------------------------------ */

class HiveLock
{
public:
    explicit HiveLock(const std::wstring& hive_path)
        : lock_path_(hive_path + L".lock"), handle_(INVALID_HANDLE_VALUE)
    {
    }
    ~HiveLock() { Release(); }

    bool Acquire(DWORD timeout_ms = 5000)
    {
        ULONGLONG deadline = GetTickCount64() + timeout_ms;
        do
        {
            handle_ = CreateFileW(lock_path_.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
            if (handle_ != INVALID_HANDLE_VALUE)
                return true;

            if (GetLastError() != ERROR_SHARING_VIOLATION)
                return false;

            Sleep(50);
        } while (GetTickCount64() < deadline);

        return false;
    }

    void Release()
    {
        if (handle_ != INVALID_HANDLE_VALUE)
        {
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
        }
    }

private:
    std::wstring lock_path_;
    HANDLE       handle_;
};

/* ------------------------------------------------------------------------ */
/*  Registry Hive Wrapper                                                   */
/* ------------------------------------------------------------------------ */

/**
 * @brief RAII wrapper around a registry hive loaded via RegLoadAppKeyW.
 *
 * Creates or loads a hive file, provides helpers for key/value manipulation.
 */
class RegHive
{
public:
    RegHive() : root_(nullptr), is_new_(false) {}
    ~RegHive() { Close(); }

    RegHive(const RegHive&) = delete;
    RegHive& operator=(const RegHive&) = delete;

    bool OpenOrCreate(const std::wstring& path)
    {
        file_path_ = path;

        /* Ensure parent directory exists */
        auto parent = std::filesystem::path(path).parent_path();
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);

        if (std::filesystem::exists(path, ec))
        {
            /* File exists — try to load it */
            if (LoadHive(path))
            {
                is_new_ = false;
                return true;
            }
            return false;
        }

        /* File does not exist — create a minimal empty hive */
        if (!CreateEmptyHiveFile(path))
        {
            SPDLOG_ERROR("Failed to create empty hive file '{}'", appbox::WideToUTF8(path));
            return false;
        }

        if (!LoadHive(path))
            return false;

        is_new_ = true;
        return true;
    }

    void Close()
    {
        if (root_ != nullptr)
        {
            RegCloseKey(root_);
            root_ = nullptr;
        }
    }

    void Flush()
    {
        if (root_ != nullptr)
            RegFlushKey(root_);
    }

    bool IsNew() const { return is_new_; }
    bool IsOpen() const { return root_ != nullptr; }

    /* ---- Key Management ---- */

    bool EnsureKey(const std::wstring& relative_path, HKEY& out_key)
    {
        std::vector<HKEY>    opened;
        std::vector<wchar_t*> segments;
        HKEY                 current = root_;

        auto components = appbox::Split(relative_path, L"\\");
        bool first = true;

        for (const auto& comp : components)
        {
            if (comp.empty())
                continue;

            HKEY parent  = current;
            HKEY new_key = nullptr;
            LONG ret     = RegCreateKeyExW(parent, comp.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS,
                                            nullptr, &new_key, nullptr);

            if (ret != ERROR_SUCCESS)
            {
                for (auto& k : opened)
                    RegCloseKey(k);
                out_key = nullptr;
                return false;
            }

            if (!first)
                opened.push_back(parent);

            current = new_key;
            first   = false;
        }

        /* Close all intermediate keys except root and final */
        for (auto& k : opened)
        {
            if (k != root_ && k != current)
                RegCloseKey(k);
        }

        out_key = current;
        return true;
    }

    bool OpenKey(const std::wstring& relative_path, HKEY& out_key) const
    {
        LONG ret = RegOpenKeyExW(root_, relative_path.c_str(), 0, KEY_READ | KEY_WRITE, &out_key);
        return ret == ERROR_SUCCESS;
    }

    bool KeyExists(const std::wstring& relative_path) const
    {
        HKEY hKey = nullptr;
        LONG ret  = RegOpenKeyExW(root_, relative_path.c_str(), 0, KEY_READ, &hKey);
        if (ret == ERROR_SUCCESS)
            RegCloseKey(hKey);
        return ret == ERROR_SUCCESS;
    }

    bool DeleteKey(const std::wstring& relative_path)
    {
        LONG ret = ::RegDeleteTreeW(root_, relative_path.c_str());
        return ret == ERROR_SUCCESS || ret == ERROR_FILE_NOT_FOUND;
    }

    std::vector<std::wstring> EnumSubKeys(const std::wstring& relative_path) const
    {
        std::vector<std::wstring> result;
        HKEY hKey = nullptr;
        if (RegOpenKeyExW(root_, relative_path.c_str(), 0, KEY_READ, &hKey) != ERROR_SUCCESS)
            return result;

        DWORD index   = 0;
        WCHAR name[256] = {};
        DWORD nameLen = 256;
        while (RegEnumKeyExW(hKey, index, name, &nameLen, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS)
        {
            result.emplace_back(name, nameLen);
            nameLen = 256;
            ++index;
        }
        RegCloseKey(hKey);
        return result;
    }

    /* ---- Value Management ---- */

    bool SetString(HKEY hKey, const std::wstring& name, const std::wstring& value)
    {
        const auto& w = value;
        LONG ret = RegSetValueExW(hKey, name.empty() ? nullptr : name.c_str(), 0, REG_SZ,
                                  reinterpret_cast<const BYTE*>(w.c_str()),
                                  static_cast<DWORD>((w.size() + 1) * sizeof(wchar_t)));
        return ret == ERROR_SUCCESS;
    }

    bool SetDword(HKEY hKey, const std::wstring& name, DWORD value)
    {
        LONG ret = RegSetValueExW(hKey, name.empty() ? nullptr : name.c_str(), 0, REG_DWORD,
                                  reinterpret_cast<const BYTE*>(&value), sizeof(value));
        return ret == ERROR_SUCCESS;
    }

    bool SetQword(HKEY hKey, const std::wstring& name, ULONGLONG value)
    {
        LONG ret = RegSetValueExW(hKey, name.empty() ? nullptr : name.c_str(), 0, REG_QWORD,
                                  reinterpret_cast<const BYTE*>(&value), sizeof(value));
        return ret == ERROR_SUCCESS;
    }

    bool SetBinary(HKEY hKey, const std::wstring& name, const void* data, DWORD size)
    {
        LONG ret = RegSetValueExW(hKey, name.empty() ? nullptr : name.c_str(), 0, REG_BINARY,
                                  static_cast<const BYTE*>(data), size);
        return ret == ERROR_SUCCESS;
    }

    bool SetNone(HKEY hKey, const std::wstring& name)
    {
        LONG ret = RegSetValueExW(hKey, name.empty() ? nullptr : name.c_str(), 0, REG_NONE, nullptr, 0);
        return ret == ERROR_SUCCESS;
    }

    /* ---- Skeleton ---- */

    bool CreateSkeleton()
    {
        if (!is_new_)
            return true;

        struct SkeletonKey
        {
            const wchar_t* path;
        };

        const SkeletonKey skeleton[] = {
            {L"DATA"},
            {L"DATA\\Machine"},
            {L"DATA\\User"},
            {L"__sbx__"},
            {L"__sbx__\\Meta"},
            {L"__sbx__\\Meta\\LowerFSHashes"},
            {L"__sbx__\\Modified"},
            {L"__sbx__\\Deleted"},
            {L"__sbx__\\Whiteout"},
            {L"__sbx__\\Opaque"},
        };

        for (const auto& sk : skeleton)
        {
            HKEY hKey = nullptr;
            if (!EnsureKey(sk.path, hKey))
            {
                SPDLOG_ERROR("Failed to create skeleton key '{}'", appbox::WideToUTF8(sk.path));
                return false;
            }
            RegCloseKey(hKey);
        }
        return true;
    }

    HKEY Root() const { return root_; }

private:
    HKEY        root_;
    bool        is_new_;
    std::wstring file_path_;

    /**
     * @brief Load an existing hive file from disk using RegLoadAppKeyW.
     *
     * RegLoadAppKeyW is loaded dynamically from advapi32.dll to support
     * all Windows 7+ SDK versions.
     */
    bool LoadHive(const std::wstring& path)
    {
        HMODULE hAdvApi = GetModuleHandleW(L"advapi32.dll");
        if (!hAdvApi)
            return false;

        typedef LSTATUS(WINAPI* RegLoadAppKeyWFn)(HKEY, LPCWSTR, REGSAM, DWORD, PHKEY);
        auto pFn = reinterpret_cast<RegLoadAppKeyWFn>(GetProcAddress(hAdvApi, "RegLoadAppKeyW"));
        if (!pFn)
        {
            SPDLOG_ERROR("RegLoadAppKeyW not available (requires Windows 7+)");
            return false;
        }

        LONG ret = pFn(HKEY_LOCAL_MACHINE, path.c_str(), KEY_ALL_ACCESS, 0, &root_);
        if (ret != ERROR_SUCCESS)
        {
            SPDLOG_ERROR("RegLoadAppKeyW failed for '{}': error={}", appbox::WideToUTF8(path), ret);
            return false;
        }

        return true;
    }

    /**
     * @brief Create a minimal, empty registry hive on disk.
     *
     * Strategy: create a temporary key in HKCU, set a dummy value, save it
     * as a hive file via RegSaveKeyW, then delete the temporary key. The
     * resulting file is a valid (though small) hive that can be loaded by
     * RegLoadAppKeyW.
     */
    static bool CreateEmptyHiveFile(const std::wstring& path)
    {
        /* Create file first so the path exists */
        HANDLE hFile = CreateFileW(path.c_str(), GENERIC_WRITE | GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile == INVALID_HANDLE_VALUE)
        {
            SPDLOG_ERROR("CreateFileW failed for new hive '{}': error={}", appbox::WideToUTF8(path), GetLastError());
            return false;
        }
        CloseHandle(hFile);

        /* Generate a unique temp key name */
        auto tempName = L"__AppBoxTempHive__" + std::to_wstring(GetTickCount64()) + L"_" + std::to_wstring(rand());

        /* Create temporary key in HKCU */
        HKEY hkTemp = nullptr;
        LONG ret    = RegCreateKeyExW(HKEY_CURRENT_USER, tempName.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE,
                                      KEY_ALL_ACCESS | KEY_WOW64_64KEY, nullptr, &hkTemp, nullptr);
        if (ret != ERROR_SUCCESS)
        {
            DeleteFileW(path.c_str());
            SPDLOG_ERROR("Failed to create temporary reg key: error={}", ret);
            return false;
        }

        /* Write a dummy value so the key has content */
        DWORD dummy = 0;
        RegSetValueExW(hkTemp, L"_", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&dummy), sizeof(dummy));
        RegCloseKey(hkTemp);

        /* Save the temp key as a hive file using RegSaveKeyW */
        /* RegSaveKeyW(HKEY hKey, LPCWSTR lpFile, LPSECURITY_ATTRIBUTES pSecurityAttributes) */
        ret = RegSaveKeyW(HKEY_CURRENT_USER, path.c_str(), nullptr);
        if (ret != ERROR_SUCCESS)
        {
            SPDLOG_ERROR("RegSaveKeyW failed: error={}", ret);
            /* Cleanup temp key */
            RegDeleteTreeW(HKEY_CURRENT_USER, tempName.c_str());
            RegDeleteKeyW(HKEY_CURRENT_USER, tempName.c_str());
            DeleteFileW(path.c_str());
            return false;
        }

        /* Remove the temp key from the real registry */
        RegDeleteTreeW(HKEY_CURRENT_USER, tempName.c_str());
        RegDeleteKeyW(HKEY_CURRENT_USER, tempName.c_str());

        /* Now we have a valid hive file at 'path'.
         * After loading with RegLoadAppKeyW, we will create the skeleton. */
        return true;
    }
};

/* ------------------------------------------------------------------------ */
/*  .reg File Parser                                                        */
/* ------------------------------------------------------------------------ */

struct RegValueEntry
{
    DWORD             type;
    std::vector<BYTE> data;
};

struct ParsedRegLayer
{
    /* key_path (hive-relative, e.g. "DATA\\Machine\\SOFTWARE\\Foo") → values */
    std::map<std::wstring, std::map<std::wstring, RegValueEntry>> data;

    /* Physical paths that are path-whiteout */
    std::set<std::wstring> path_whiteouts;

    /* Physical paths that are opaque */
    std::set<std::wstring> opaques;

    /* (key_path, value_name) pairs for value whiteout */
    std::set<std::pair<std::wstring, std::wstring>> value_whiteouts;
};

static const wchar_t* kWhiteoutMarker = L".$APPBOX_WHITEOUT$";
static const wchar_t* kOpaqueMarker   = L".$APPBOX_OPAQUE$";

/**
 * @brief Translate a registry.reg-style key path to a hive-relative DATA path.
 *
 * Examples:
 *   HKEY_LOCAL_MACHINE\SOFTWARE\Foo  →  DATA\Machine\SOFTWARE\Foo
 *   HKEY_CURRENT_USER\Software\Bar   →  DATA\User\S-1-5-...\Software\Bar
 *   HKEY_CLASSES_ROOT\.txt           →  DATA\Machine\SOFTWARE\Classes\.txt
 */
static std::wstring TranslateRegKeyPath(const std::wstring& reg_path, const std::wstring& user_sid)
{
    std::wstring p = reg_path;

    if (_wcsnicmp(p.c_str(), L"HKEY_LOCAL_MACHINE\\", 19) == 0)
        return L"DATA\\Machine" + p.substr(18);
    if (_wcsnicmp(p.c_str(), L"HKLM\\", 5) == 0)
        return L"DATA\\Machine" + p.substr(4);
    if (_wcsnicmp(p.c_str(), L"HKEY_CURRENT_USER\\", 18) == 0)
    {
        if (user_sid.empty())
            return L"";
        return L"DATA\\User\\" + user_sid + p.substr(17);
    }
    if (_wcsnicmp(p.c_str(), L"HKCU\\", 5) == 0)
    {
        if (user_sid.empty())
            return L"";
        return L"DATA\\User\\" + user_sid + p.substr(4);
    }
    if (_wcsnicmp(p.c_str(), L"HKEY_CLASSES_ROOT\\", 18) == 0)
        return L"DATA\\Machine\\SOFTWARE\\Classes" + p.substr(17);
    if (_wcsnicmp(p.c_str(), L"HKCR\\", 5) == 0)
        return L"DATA\\Machine\\SOFTWARE\\Classes" + p.substr(4);
    if (_wcsnicmp(p.c_str(), L"HKEY_USERS\\", 11) == 0)
        return L"DATA\\User" + p.substr(10);
    if (_wcsnicmp(p.c_str(), L"HKU\\", 4) == 0)
        return L"DATA\\User" + p.substr(3);
    if (_wcsnicmp(p.c_str(), L"HKEY_CURRENT_CONFIG\\", 15) == 0)
        return L"DATA\\Machine\\System\\CurrentControlSet" + p.substr(14);
    if (_wcsnicmp(p.c_str(), L"HKCC\\", 5) == 0)
        return L"DATA\\Machine\\System\\CurrentControlSet" + p.substr(4);
    if (_wcsnicmp(p.c_str(), L"DATA\\", 5) == 0 || _wcsnicmp(p.c_str(), L"__sbx__\\", 8) == 0)
        return p;

    return L"";
}

static std::vector<BYTE> ParseHexData(const std::wstring& hex_str)
{
    std::vector<BYTE>     result;
    std::wistringstream iss(hex_str);
    std::wstring         token;

    while (std::getline(iss, token, L','))
    {
        token.erase(0, token.find_first_not_of(L" \t"));
        token.erase(token.find_last_not_of(L" \t") + 1);
        if (token.empty())
            continue;

        wchar_t* end = nullptr;
        unsigned long val = wcstoul(token.c_str(), &end, 16);
        if (end != token.c_str() && val <= 0xFF)
            result.push_back(static_cast<BYTE>(val));
    }
    return result;
}

static bool ParseHexType(const std::wstring& line, DWORD& type, std::wstring& hex_str)
{
    if (line.find(L"hex:") == 0)
    {
        type    = REG_BINARY;
        hex_str = line.substr(4);
        return true;
    }
    if (line.find(L"hex(") == 0)
    {
        auto close_paren = line.find(L')');
        if (close_paren == std::wstring::npos || close_paren + 1 >= line.size() || line[close_paren + 1] != L':')
            return false;

        std::wstring type_str = line.substr(4, close_paren - 4);
        wchar_t*     end      = nullptr;
        unsigned long t       = wcstoul(type_str.c_str(), &end, 16);
        if (end == type_str.c_str())
            return false;

        type    = static_cast<DWORD>(t);
        hex_str = line.substr(close_paren + 2);
        return true;
    }
    return false;
}

/**
 * @brief Parse a single .reg file into a ParsedRegLayer.
 */
static bool ParseRegFile(const std::wstring& file_path, const std::wstring& user_sid, ParsedRegLayer& layer)
{
    std::ifstream f(file_path);
    if (!f)
        return true; /* File does not exist — not an error */

    std::wstring current_reg_path;
    std::wstring current_translated_path;
    std::string  line_utf8;

    auto add_value = [&](const std::wstring& value_name, DWORD type, const std::vector<BYTE>& data) {
        if (current_translated_path.empty())
            return;

        /* Check for path-level markers */
        if (type == REG_DWORD && data.size() == sizeof(DWORD))
        {
            DWORD val = *reinterpret_cast<const DWORD*>(data.data());
            if (value_name == kWhiteoutMarker && val == 1)
            {
                layer.path_whiteouts.insert(current_translated_path);
                return;
            }
            if (value_name == kOpaqueMarker && val == 1)
            {
                layer.opaques.insert(current_translated_path);
                return;
            }
        }

        /* Check for value-level whiteout ("ValueName"=".$APPBOX_WHITEOUT$") */
        if (type == REG_SZ && data.size() >= sizeof(wchar_t))
        {
            /* data includes null terminator */
            size_t char_count = data.size() / sizeof(wchar_t);
            if (char_count > 0)
            {
                /* Ensure null-terminated */
                std::wstring val_str(reinterpret_cast<const wchar_t*>(data.data()), char_count - 1);
                if (val_str == kWhiteoutMarker)
                {
                    layer.value_whiteouts.insert({current_translated_path, value_name});
                    return;
                }
            }
        }

        /* Normal value */
        layer.data[current_translated_path][value_name] = {type, data};
    };

    while (std::getline(f, line_utf8))
    {
        std::wstring line = appbox::UTF8ToWide(line_utf8);

        /* Trim trailing \r */
        while (!line.empty() && (line.back() == L'\r' || line.back() == L'\n'))
            line.pop_back();
        /* Trim leading whitespace */
        while (!line.empty() && (line[0] == L' ' || line[0] == L'\t'))
            line.erase(line.begin());

        if (line.empty() || line[0] == L';')
            continue;

        /* Section header */
        if (line[0] == L'[')
        {
            auto close = line.find(L']');
            if (close == std::wstring::npos)
                continue;
            current_reg_path        = line.substr(1, close - 1);
            current_translated_path = TranslateRegKeyPath(current_reg_path, user_sid);
            continue;
        }

        if (current_reg_path.empty())
            continue;

        /* Value line */
        size_t eq_pos = line.find(L'=');
        if (eq_pos == std::wstring::npos)
            continue;

        std::wstring value_name_part = line.substr(0, eq_pos);
        std::wstring value_data_part = line.substr(eq_pos + 1);

        /* Trim trailing spaces from name */
        while (!value_name_part.empty() && value_name_part.back() == L' ')
            value_name_part.pop_back();

        std::wstring value_name;
        if (value_name_part == L"@")
        {
            value_name = L"";
        }
        else if (value_name_part.size() >= 2 && value_name_part[0] == L'"' && value_name_part.back() == L'"')
        {
            value_name = value_name_part.substr(1, value_name_part.size() - 2);
        }
        else
        {
            continue;
        }

        if (value_data_part.empty())
            continue;

        if (value_data_part[0] == L'"')
        {
            size_t end_quote = value_data_part.find(L'"', 1);
            if (end_quote == std::wstring::npos)
                continue;

            std::wstring str_val = value_data_part.substr(1, end_quote - 1);

            /* Unescape \r, \n, \\, \" */
            std::wstring unescaped;
            for (size_t i = 0; i < str_val.size(); ++i)
            {
                if (str_val[i] == L'\\' && i + 1 < str_val.size())
                {
                    switch (str_val[i + 1])
                    {
                    case L'n':
                        unescaped += L'\n';
                        ++i;
                        continue;
                    case L'r':
                        unescaped += L'\r';
                        ++i;
                        continue;
                    case L'\\':
                        unescaped += L'\\';
                        ++i;
                        continue;
                    case L'"':
                        unescaped += L'"';
                        ++i;
                        continue;
                    case L'0':
                        unescaped += L'\0';
                        ++i;
                        continue;
                    default:
                        unescaped += L'\\';
                        continue;
                    }
                }
                unescaped += str_val[i];
            }

            std::vector<BYTE> data_vec(
                reinterpret_cast<const BYTE*>(unescaped.c_str()),
                reinterpret_cast<const BYTE*>(unescaped.c_str() + (unescaped.size() + 1) * sizeof(wchar_t)));
            add_value(value_name, REG_SZ, data_vec);
        }
        else if (value_data_part.find(L"dword:") == 0)
        {
            std::wstring hex_val = value_data_part.substr(6);
            wchar_t*     end     = nullptr;
            unsigned long val    = wcstoul(hex_val.c_str(), &end, 16);
            if (end == hex_val.c_str())
                continue;

            DWORD dword_val = static_cast<DWORD>(val);
            std::vector<BYTE> data_vec(reinterpret_cast<const BYTE*>(&dword_val),
                                       reinterpret_cast<const BYTE*>(&dword_val) + sizeof(dword_val));
            add_value(value_name, REG_DWORD, data_vec);
        }
        else if (value_data_part.find(L"hex(") == 0 || value_data_part.find(L"hex:") == 0)
        {
            DWORD       type    = REG_BINARY;
            std::wstring hex_str;
            if (!ParseHexType(value_data_part, type, hex_str))
                continue;

            auto data_vec = ParseHexData(hex_str);
            add_value(value_name, type, data_vec);
        }
    }

    return true;
}

/* ------------------------------------------------------------------------ */
/*  Merge Layers                                                            */
/* ------------------------------------------------------------------------ */

struct MergedRegData
{
    std::map<std::wstring, std::map<std::wstring, RegValueEntry>> data;
    std::set<std::wstring>                                        deleted_paths;
    std::set<std::wstring>                                        opaque_paths;
    std::set<std::pair<std::wstring, std::wstring>>               value_whiteouts;
};

static MergedRegData MergeLayers(const std::vector<ParsedRegLayer>& layers)
{
    MergedRegData merged;

    for (const auto& layer : layers)
    {
        for (const auto& [path, values] : layer.data)
        {
            auto& dest = merged.data[path];
            for (const auto& [val_name, val_entry] : values)
                dest[val_name] = val_entry;
        }

        for (const auto& path : layer.path_whiteouts)
            merged.deleted_paths.insert(path);
        for (const auto& path : layer.opaques)
            merged.opaque_paths.insert(path);
        for (const auto& pair : layer.value_whiteouts)
            merged.value_whiteouts.insert(pair);
    }

    /* Data takes precedence over whiteout/opaque markers */
    for (const auto& [path, values] : merged.data)
    {
        merged.deleted_paths.erase(path);
        for (const auto& [val_name, val_entry] : values)
            merged.value_whiteouts.erase({path, val_name});
    }

    return merged;
}

/* ------------------------------------------------------------------------ */
/*  Write merged data to hive                                               */
/* ------------------------------------------------------------------------ */

static bool WriteMergedDataToHive(RegHive& hive, const MergedRegData& merged)
{
    /* Write DATA entries */
    for (const auto& [path, values] : merged.data)
    {
        HKEY hKey = nullptr;
        if (!hive.EnsureKey(path, hKey))
        {
            SPDLOG_ERROR("Failed to create key '{}' in hive", appbox::WideToUTF8(path));
            continue;
        }

        for (const auto& [val_name, entry] : values)
        {
            RegSetValueExW(hKey, val_name.empty() ? nullptr : val_name.c_str(), 0, entry.type,
                           entry.data.empty() ? nullptr : entry.data.data(),
                           static_cast<DWORD>(entry.data.size()));
        }
        RegCloseKey(hKey);
    }

    /* Write value whiteouts: __sbx__\Whiteout\[path]\[value_name] = REG_NONE */
    for (const auto& [key_path, val_name] : merged.value_whiteouts)
    {
        std::wstring wp = L"__sbx__\\Whiteout\\" + key_path;
        HKEY hKey = nullptr;
        if (hive.EnsureKey(wp, hKey))
        {
            hive.SetNone(hKey, val_name);
            RegCloseKey(hKey);
        }
    }

    /* Write path whiteouts: __sbx__\Deleted\[path] = REG_NONE */
    for (const auto& path : merged.deleted_paths)
    {
        std::wstring dp = L"__sbx__\\Deleted\\" + path;
        HKEY hKey = nullptr;
        if (hive.EnsureKey(dp, hKey))
        {
            hive.SetNone(hKey, L"");
            RegCloseKey(hKey);
        }
    }

    /* Write opaques: __sbx__\Opaque\[path] = REG_NONE */
    for (const auto& path : merged.opaque_paths)
    {
        std::wstring op = L"__sbx__\\Opaque\\" + path;
        HKEY hKey = nullptr;
        if (hive.EnsureKey(op, hKey))
        {
            hive.SetNone(hKey, L"");
            RegCloseKey(hKey);
        }
    }

    return true;
}

/* ------------------------------------------------------------------------ */
/*  Hash helpers                                                            */
/* ------------------------------------------------------------------------ */

static std::vector<Sha256Hash> ReadStoredHashes(RegHive& hive, size_t layer_count)
{
    std::vector<Sha256Hash> hashes;
    for (size_t i = 0; i < layer_count; ++i)
    {
        auto key_path = L"__sbx__\\Meta\\LowerFSHashes\\" + std::to_wstring(i);
        HKEY hKey = nullptr;
        if (!hive.OpenKey(key_path, hKey))
        {
            hashes.push_back(Sha256Hash::Zero());
            continue;
        }

        DWORD      type = 0;
        Sha256Hash hash_val{};
        DWORD      data_size = Sha256Hash::kHashLen;
        LONG ret = RegQueryValueExW(hKey, L"", nullptr, &type, hash_val.data, &data_size);
        if (ret != ERROR_SUCCESS || type != REG_BINARY || data_size != Sha256Hash::kHashLen)
            hashes.push_back(Sha256Hash::Zero());
        else
            hashes.push_back(hash_val);

        RegCloseKey(hKey);
    }
    return hashes;
}

static bool WriteLayerHashes(RegHive& hive, const std::vector<Sha256Hash>& hashes)
{
    for (size_t i = 0; i < hashes.size(); ++i)
    {
        auto key_path = L"__sbx__\\Meta\\LowerFSHashes\\" + std::to_wstring(i);
        HKEY hKey = nullptr;
        if (!hive.EnsureKey(key_path, hKey))
            continue;
        hive.SetBinary(hKey, L"", hashes[i].data, Sha256Hash::kHashLen);
        RegCloseKey(hKey);
    }
    return true;
}

static bool WriteCompileMetadata(RegHive& hive)
{
    HKEY hMeta = nullptr;
    if (!hive.EnsureKey(L"__sbx__\\Meta", hMeta))
        return false;

    /* Read and increment version */
    DWORD version = 0;
    {
        DWORD type = 0, size = sizeof(version);
        LONG ret = RegQueryValueExW(hMeta, L"Version", nullptr, &type, reinterpret_cast<BYTE*>(&version), &size);
        if (ret == ERROR_SUCCESS && type == REG_DWORD)
            ++version;
    }
    hive.SetDword(hMeta, L"Version", version);

    /* Write compile time */
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULONGLONG ft_val = (static_cast<ULONGLONG>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    hive.SetQword(hMeta, L"CompileTime", ft_val);

    RegCloseKey(hMeta);
    return true;
}

/* ------------------------------------------------------------------------ */
/*  Update Flow (Section 5)                                                 */
/* ------------------------------------------------------------------------ */

static bool PerformUpdate(RegHive& hive, const std::vector<ParsedRegLayer>& layers, const std::wstring& /* user_sid */)
{
    SPDLOG_INFO("Performing registry hive update (registry.reg has changed)");

    /* 1. Merge new layers */
    MergedRegData new_merged = MergeLayers(layers);

    /* 2. Read existing state from hive */
    std::set<std::wstring> modified_set;
    {
        /* Walk __sbx__\Modified recursively */
        std::function<void(const std::wstring&, const std::wstring&)> enum_recursive = [&](const std::wstring& key_path,
                                                                                            const std::wstring& prefix) {
            if (prefix.empty())
            {
                auto subs = hive.EnumSubKeys(key_path);
                for (const auto& sub : subs)
                    enum_recursive(key_path + L"\\" + sub, sub);
            }
            else
            {
                modified_set.insert(prefix);
                auto subs = hive.EnumSubKeys(key_path);
                for (const auto& sub : subs)
                    enum_recursive(key_path + L"\\" + sub, prefix + L"\\" + sub);
            }
        };
        enum_recursive(L"__sbx__\\Modified", L"");
    }

    std::set<std::wstring> deleted_set;
    {
        std::function<void(const std::wstring&, const std::wstring&)> enum_recursive = [&](const std::wstring& key_path,
                                                                                            const std::wstring& prefix) {
            if (prefix.empty())
            {
                auto subs = hive.EnumSubKeys(key_path);
                for (const auto& sub : subs)
                    enum_recursive(key_path + L"\\" + sub, sub);
            }
            else
            {
                deleted_set.insert(prefix);
                auto subs = hive.EnumSubKeys(key_path);
                for (const auto& sub : subs)
                    enum_recursive(key_path + L"\\" + sub, prefix + L"\\" + sub);
            }
        };
        enum_recursive(L"__sbx__\\Deleted", L"");
    }

    /* Read old value whiteouts */
    std::set<std::pair<std::wstring, std::wstring>> old_whiteouts;
    {
        auto key_paths = hive.EnumSubKeys(L"__sbx__\\Whiteout");
        for (const auto& kp : key_paths)
        {
            auto wp = L"__sbx__\\Whiteout\\" + kp;
            HKEY hKey = nullptr;
            if (hive.OpenKey(wp, hKey))
            {
                DWORD index   = 0;
                WCHAR valName[256] = {};
                DWORD nameLen = 256;
                while (RegEnumValueW(hKey, index, valName, &nameLen, nullptr, nullptr, nullptr, nullptr) ==
                       ERROR_SUCCESS)
                {
                    old_whiteouts.insert({kp, std::wstring(valName, nameLen)});
                    nameLen = 256;
                    ++index;
                }
                RegCloseKey(hKey);
            }
        }
    }

    /* Read old opaques */
    std::set<std::wstring> old_opaque_set;
    {
        std::function<void(const std::wstring&, const std::wstring&)> enum_recursive = [&](const std::wstring& key_path,
                                                                                            const std::wstring& prefix) {
            if (prefix.empty())
            {
                auto subs = hive.EnumSubKeys(key_path);
                for (const auto& sub : subs)
                    enum_recursive(key_path + L"\\" + sub, sub);
            }
            else
            {
                old_opaque_set.insert(prefix);
                auto subs = hive.EnumSubKeys(key_path);
                for (const auto& sub : subs)
                    enum_recursive(key_path + L"\\" + sub, prefix + L"\\" + sub);
            }
        };
        enum_recursive(L"__sbx__\\Opaque", L"");
    }

    /* 3. For each entry in new_merged.data, upsert into DATA (skip if modified or deleted by app) */
    for (const auto& [path, values] : new_merged.data)
    {
        if (modified_set.count(path) > 0)
            continue;
        if (deleted_set.count(path) > 0)
            continue;

        HKEY hKey = nullptr;
        if (!hive.EnsureKey(path, hKey))
            continue;

        for (const auto& [val_name, entry] : values)
        {
            RegSetValueExW(hKey, val_name.empty() ? nullptr : val_name.c_str(), 0, entry.type,
                           entry.data.empty() ? nullptr : entry.data.data(),
                           static_cast<DWORD>(entry.data.size()));
        }
        RegCloseKey(hKey);
    }

    /* 5. Update __sbx__\Deleted */
    for (const auto& path : new_merged.deleted_paths)
    {
        if (modified_set.count(path) == 0)
        {
            auto dp = L"__sbx__\\Deleted\\" + path;
            HKEY hKey = nullptr;
            if (hive.EnsureKey(dp, hKey))
            {
                hive.SetNone(hKey, L"");
                RegCloseKey(hKey);
            }
        }
    }
    for (const auto& path : deleted_set)
    {
        if (new_merged.deleted_paths.count(path) == 0 && modified_set.count(path) == 0)
            hive.DeleteKey(L"__sbx__\\Deleted\\" + path);
    }

    /* 6. Update __sbx__\Whiteout */
    for (const auto& [path, val_name] : new_merged.value_whiteouts)
    {
        if (modified_set.count(path) > 0)
        {
            HKEY hKey = nullptr;
            if (hive.OpenKey(path, hKey))
            {
                DWORD type = 0;
                BYTE  buf[4];
                DWORD size = sizeof(buf);
                LONG  ret  = RegQueryValueExW(hKey, val_name.c_str(), nullptr, &type, buf, &size);
                RegCloseKey(hKey);
                if (ret == ERROR_SUCCESS)
                    continue;
            }
        }

        auto wp = L"__sbx__\\Whiteout\\" + path;
        HKEY hKey = nullptr;
        if (hive.EnsureKey(wp, hKey))
        {
            hive.SetNone(hKey, val_name);
            RegCloseKey(hKey);
        }
    }
    for (const auto& [path, val_name] : old_whiteouts)
    {
        if (new_merged.value_whiteouts.count({path, val_name}) == 0)
        {
            if (modified_set.count(path) > 0)
                continue;

            auto wp = L"__sbx__\\Whiteout\\" + path;
            HKEY hKey = nullptr;
            if (hive.OpenKey(wp, hKey))
            {
                RegDeleteValueW(hKey, val_name.c_str());
                RegCloseKey(hKey);
            }

            /* Clean up empty whiteout subkey */
            auto empty_check = L"__sbx__\\Whiteout\\" + path;
            HKEY hKey2 = nullptr;
            if (hive.OpenKey(empty_check, hKey2))
            {
                DWORD subKeys = 0, values = 0;
                RegQueryInfoKeyW(hKey2, nullptr, nullptr, nullptr, &subKeys, nullptr, nullptr, &values, nullptr, nullptr,
                                 nullptr, nullptr);
                RegCloseKey(hKey2);
                if (subKeys == 0 && values == 0)
                    hive.DeleteKey(empty_check);
            }
        }
    }

    /* 7. Update __sbx__\Opaque */
    for (const auto& path : new_merged.opaque_paths)
    {
        if (modified_set.count(path) == 0 && deleted_set.count(path) == 0)
        {
            auto op = L"__sbx__\\Opaque\\" + path;
            HKEY hKey = nullptr;
            if (hive.EnsureKey(op, hKey))
            {
                hive.SetNone(hKey, L"");
                RegCloseKey(hKey);
            }
        }
    }
    for (const auto& path : old_opaque_set)
    {
        if (new_merged.opaque_paths.count(path) == 0 && modified_set.count(path) == 0)
            hive.DeleteKey(L"__sbx__\\Opaque\\" + path);
    }

    return true;
}

/* ------------------------------------------------------------------------ */
/*  Find registry.reg files in base_fs directories                          */
/* ------------------------------------------------------------------------ */

/**
 * @brief Collect registry.reg paths from each LowerFS subdirectory.
 *
 * For each base_fs directory, enumerates subdirectories and checks each
 * for a registry.reg file. Results are collected in the order encountered.
 *
 * @return Number of .reg files found.
 */
static size_t FindRegFiles(const std::vector<std::string>& base_fs_dirs, std::vector<std::wstring>& reg_file_paths)
{
    for (const auto& dir : base_fs_dirs)
    {
        auto dir_w = appbox::UTF8ToWide(dir);
        std::error_code ec;

        if (!std::filesystem::is_directory(dir_w, ec))
            continue;

        for (const auto& entry : std::filesystem::directory_iterator(dir_w, ec))
        {
            if (!entry.is_directory(ec))
                continue;

            auto reg_path = entry.path().wstring() + L"\\registry.reg";
            if (std::filesystem::exists(reg_path, ec))
                reg_file_paths.push_back(reg_path);
        }
    }

    return reg_file_paths.size();
}

/**
 * @brief Count the total number of LowerFS layers (subdirectories under base_fs).
 */
static size_t CountLowerFSLayers(const std::vector<std::string>& base_fs_dirs)
{
    size_t count = 0;
    for (const auto& dir : base_fs_dirs)
    {
        auto dir_w = appbox::UTF8ToWide(dir);
        std::error_code ec;
        if (!std::filesystem::is_directory(dir_w, ec))
            continue;
        for (auto it = std::filesystem::directory_iterator(dir_w, ec); it != std::filesystem::end(it); ++it)
        {
            if (it->is_directory(ec))
                ++count;
        }
    }
    return count;
}

} // anonymous namespace

/* ========================================================================= */
/*  Public API                                                               */
/* ========================================================================= */

namespace appbox
{

RegCompileResult InitializeRegistryHive(const std::wstring& hive_path, const std::vector<std::string>& base_fs_dirs)
{
    RegCompileResult result{false, ERROR_SUCCESS};

    /* ── Acquire file-level lock ── */
    HiveLock lock(hive_path);
    if (!lock.Acquire())
    {
        SPDLOG_ERROR("Failed to acquire hive lock for '{}'", WideToUTF8(hive_path));
        result.error_code = ERROR_SHARING_VIOLATION;
        return result;
    }

    /* ── Load or create the hive ── */
    RegHive hive;
    if (!hive.OpenOrCreate(hive_path))
    {
        result.error_code = GetLastError();
        return result;
    }

    /* ── Ensure skeleton exists ── */
    if (hive.IsNew())
    {
        if (!hive.CreateSkeleton())
        {
            result.error_code = ERROR_INTERNAL_ERROR;
            return result;
        }
        SPDLOG_INFO("Created new registry hive skeleton at '{}'", WideToUTF8(hive_path));
    }

    /* ── Collect registry.reg file paths ── */
    std::vector<std::wstring> reg_file_paths;
    FindRegFiles(base_fs_dirs, reg_file_paths);

    /* ── Compute current hashes ── */
    std::vector<Sha256Hash> current_hashes;
    for (const auto& rp : reg_file_paths)
        current_hashes.push_back(ComputeFileSha256(rp));

    /* ── Read stored hashes ── */
    size_t total_layers = CountLowerFSLayers(base_fs_dirs);
    auto   stored_hashes = ReadStoredHashes(hive, total_layers);

    /* ── Detect changes ── */
    bool needs_update = (stored_hashes.size() != total_layers);
    if (!needs_update)
    {
        for (size_t i = 0; i < total_layers; ++i)
        {
            Sha256Hash current = (i < current_hashes.size()) ? current_hashes[i] : Sha256Hash::Zero();
            if (!(stored_hashes[i] == current))
            {
                needs_update = true;
                break;
            }
        }
    }

    if (!needs_update)
    {
        SPDLOG_DEBUG("Registry hive '{}' is up to date", WideToUTF8(hive_path));
        result.success = true;
        return result;
    }

    /* ── Parse all registry.reg files ── */
    std::wstring user_sid = GetCurrentUserSid();
    if (user_sid.empty())
        SPDLOG_WARN("Could not determine current user SID; HKCU entries may not work correctly");

    std::vector<ParsedRegLayer> parsed_layers;
    for (const auto& reg_path : reg_file_paths)
    {
        ParsedRegLayer layer;
        if (!ParseRegFile(reg_path, user_sid, layer))
        {
            SPDLOG_ERROR("Failed to parse registry.reg file '{}'", WideToUTF8(reg_path));
            result.error_code = ERROR_INVALID_DATA;
            return result;
        }
        parsed_layers.push_back(std::move(layer));
    }

    /* ── Fresh compile or update ── */
    if (hive.IsNew())
    {
        auto merged = MergeLayers(parsed_layers);
        if (!WriteMergedDataToHive(hive, merged))
        {
            result.error_code = ERROR_INTERNAL_ERROR;
            return result;
        }
        SPDLOG_INFO("Compiled {} entries from registry.reg into '{}'", merged.data.size(), WideToUTF8(hive_path));
    }
    else
    {
        if (!PerformUpdate(hive, parsed_layers, user_sid))
        {
            result.error_code = ERROR_INTERNAL_ERROR;
            return result;
        }
        SPDLOG_INFO("Updated registry hive '{}' from changed registry.reg files", WideToUTF8(hive_path));
    }

    /* ── Write updated hashes and metadata ── */
    /* Build full hash vector for all layers (zero-hash for layers without .reg) */
    std::vector<Sha256Hash> final_hashes;
    size_t reg_idx = 0;
    std::error_code ec;
    for (const auto& dir : base_fs_dirs)
    {
        auto dir_w = UTF8ToWide(dir);
        if (!std::filesystem::is_directory(dir_w, ec))
            continue;
        for (auto it = std::filesystem::directory_iterator(dir_w, ec); it != std::filesystem::end(it); ++it)
        {
            if (!it->is_directory(ec))
                continue;
            if (reg_idx < current_hashes.size())
                final_hashes.push_back(current_hashes[reg_idx]);
            else
                final_hashes.push_back(Sha256Hash::Zero());
            ++reg_idx;
        }
    }

    WriteLayerHashes(hive, final_hashes);
    WriteCompileMetadata(hive);

    /* ── Flush to disk ── */
    hive.Flush();

    result.success = true;
    return result;
}

} // namespace appbox