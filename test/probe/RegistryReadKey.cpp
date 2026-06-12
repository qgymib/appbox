#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include "utils/WinHandle.hpp"
#include "RegistryReadKey.hpp"
#include <CLI/Encoding.hpp>

using namespace appbox::test;

struct RegistryKeyMap
{
    const wchar_t* name; /* Name of the registry root */
    HKEY           hKey; /* Predefined registry root */
};

static const RegistryKeyMap s_key_map[] = {
    { L"HKEY_CLASSES_ROOT",   HKEY_CLASSES_ROOT   },
    { L"HKEY_CURRENT_USER",   HKEY_CURRENT_USER   },
    { L"HKEY_CURRENT_CONFIG", HKEY_CURRENT_CONFIG },
    { L"HKEY_LOCAL_MACHINE",  HKEY_LOCAL_MACHINE  },
    { L"HKEY_USERS",          HKEY_USERS          },
};

static DWORD SplitRegistryPath(const std::wstring& path, HKEY* pKey, std::wstring& subPath)
{
    auto pos = path.find(L'\\');
    if (pos == std::wstring::npos)
    {
        return ERROR_INVALID_PARAMETER;
    }

    auto key = path.substr(0, pos);
    bool bKeyFound = false;
    for (auto& item : s_key_map)
    {
        if (key == item.name)
        {
            *pKey = item.hKey;
            bKeyFound = true;
            break;
        }
    }
    if (!bKeyFound)
    {
        return ERROR_INVALID_PARAMETER;
    }

    subPath = path.substr(pos + 1);
    return 0;
}

/**
 * @brief Probe function to read a registry value.
 * Reads HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\ProductName
 * using RegGetValueW API.
 */
static nlohmann::json ProbeReadRegValue(const nlohmann::json& data)
{
    ProtocolRegistryReadKey::Rsp rsp;

    auto req = data.get<ProtocolRegistryReadKey::Req>();
    auto wPath = CLI::widen(req.path);
    auto wKey = CLI::widen(req.key);

    HKEY         root = nullptr;
    std::wstring subPath;
    if ((rsp.code = SplitRegistryPath(wPath, &root, subPath)) != 0)
    {
        return rsp;
    }

    appbox::test::WinHandle<HKEY> hKey(RegCloseKey);
    LONG                          lRet = RegOpenKeyExW(root, subPath.c_str(), 0, KEY_READ, &hKey);
    if (lRet != ERROR_SUCCESS)
    {
        rsp.code = lRet;
        return rsp;
    }

    WCHAR szValue[256];
    DWORD dwSize = sizeof(szValue);
    DWORD dwType = 0;

    lRet = RegQueryValueExW(hKey.Ref(), wKey.c_str(), nullptr, &dwType, (LPBYTE)szValue, &dwSize);
    if (lRet != ERROR_SUCCESS)
    {
        rsp.code = lRet;
        return rsp;
    }

    rsp.value = CLI::narrow(szValue);
    return rsp;
}

appbox::test::Probe appbox::test::ProbeRegistryReadKey("ReadRegistryValue", ProbeReadRegValue);
