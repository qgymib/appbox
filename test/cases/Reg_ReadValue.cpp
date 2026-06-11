#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include <gtest/gtest.h>
#include <spdlog/spdlog.h>
#include <CLI/Encoding.hpp>
#include "probe/__init__.hpp"
#include "utils/WinHandle.hpp"
#include "utils/CommonFixture.hpp"
#include "WString.hpp"

typedef appbox::test::CommonFixture Reg;

struct ProtocolReadRegVal
{
    struct Req
    {
        std::string path;
        std::string key;
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Req, path, key)
    };

    struct Rsp
    {
        DWORD       code = 0;
        std::string value;
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Rsp, code, value)
    };
};

/**
 * @brief Probe function to read a registry value.
 * Reads HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\ProductName
 * using RegGetValueW API.
 */
static nlohmann::json ProbeReadRegValue(const nlohmann::json& data)
{
    ProtocolReadRegVal::Rsp rsp;

    auto req = data.get<ProtocolReadRegVal::Req>();
    auto wPath = CLI::widen(req.path);
    auto wKey = CLI::widen(req.key);

    appbox::test::WinHandle<HKEY> hKey(RegCloseKey);
    LONG                          lRet = RegOpenKeyExW(HKEY_LOCAL_MACHINE, wPath.c_str(), 0, KEY_READ, &hKey);
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

/**
 * @brief Register the probe.
 */
static appbox::test::Probe probe_ReadRegistryValue("ReadRegistryValue", ProbeReadRegValue);

/**
 * @brief Test reading a registry value inside the sandbox.
 *
 * Condition:
 * 1. Sandbox is active.
 * 2. Read HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\ProductName.
 *
 * Expected:
 * 1. The registry value can be read successfully.
 * 2. The returned value is a non-empty string.
 */
TEST_F(Reg, ReadValue)
{
    ProtocolReadRegVal::Req req;
    req.path = "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
    req.key = "ProductName";
    auto rsp = probe_ReadRegistryValue.Call(req, GetCWDString(), {});
    auto ret = rsp.get<ProtocolReadRegVal::Rsp>();

    ASSERT_EQ(ret.code, (DWORD)ERROR_SUCCESS) << "Failed to read registry value";
    ASSERT_FALSE(ret.value.empty()) << "Registry value should not be empty";
    SPDLOG_INFO("Read registry value: {}", rsp.dump());
}
