#include "probe/RegReadValue.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/HiveBuilder.hpp"
#include "Random.hpp"
#include "WString.hpp"
#include <filesystem>
#include <string>
#include <vector>

typedef appbox::test::CommonFixture E2E_Reg;
using namespace appbox::test;

namespace
{

/**
 * @brief Delete the key of the test from the real HKCU.
 */
struct RealKeyGuard
{
    std::wstring subkey;

    ~RealKeyGuard()
    {
        RegDeleteTreeW(HKEY_CURRENT_USER, subkey.c_str());
    }
};

/**
 * @brief Build the raw data of a `REG_SZ` value.
 * @param[in] text Text of the value.
 * @return The UTF-16 data with its trailing terminator.
 */
std::vector<BYTE> StringData(const std::wstring& text)
{
    std::vector<BYTE> data((text.size() + 1) * sizeof(wchar_t), 0);
    memcpy(data.data(), text.c_str(), text.size() * sizeof(wchar_t));
    return data;
}

/**
 * @brief Create a key of the real HKCU and write a `REG_SZ` value into it.
 * @param[in] subkey Path of the key below HKCU.
 * @param[in] name Name of the value.
 * @param[in] text Text of the value.
 * @return true on success.
 */
bool WriteRealValue(const std::wstring& subkey, const std::wstring& name, const std::wstring& text)
{
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) !=
        ERROR_SUCCESS)
    {
        return false;
    }

    const auto code = RegSetValueExW(key, name.c_str(), 0, REG_SZ, reinterpret_cast<const BYTE*>(text.c_str()),
                                     static_cast<DWORD>((text.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return code == ERROR_SUCCESS;
}

/**
 * @brief Read a `REG_SZ` value of the real HKCU.
 * @param[in] subkey Path of the key below HKCU.
 * @param[in] name Name of the value.
 * @return The text, empty when the value is not there.
 */
std::wstring ReadRealValue(const std::wstring& subkey, const std::wstring& name)
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
    {
        return std::wstring();
    }

    wchar_t    buffer[128] = {};
    DWORD      size = sizeof(buffer);
    const auto code = RegQueryValueExW(key, name.c_str(), nullptr, nullptr, reinterpret_cast<LPBYTE>(buffer), &size);
    RegCloseKey(key);
    return code == ERROR_SUCCESS ? std::wstring(buffer) : std::wstring();
}

/**
 * @brief Read a value of a key below HKCU inside the sandbox.
 * @param[in] subkey Path of the key below HKCU.
 * @param[in] name Name of the value.
 * @param[in] cwd Working directory of the case.
 * @param[in] config Loader configuration of the sandbox.
 * @return The response of the probe.
 */
ProtocolRegReadValue::Rsp ReadSandboxValue(const std::wstring& subkey, const std::wstring& name,
                                           const std::filesystem::path& cwd, const appbox::LoaderConfig& config)
{
    ProtocolRegReadValue::Req req;
    req.Key = appbox::WideToUTF8(subkey);
    req.Value = appbox::WideToUTF8(name);
    return ProbeRegReadValue.Call(req, cwd, config).get<ProtocolRegReadValue::Rsp>();
}

} // namespace

/**
 * Condition:
 * 1. The real HKCU holds the key with a value and its sub key `Child` with a
 *    value of its own.
 * 2. The isolation file marks the parent key `Full` and the child key carries
 *    no mode of its own; the sandbox hive holds the child key with a value.
 * 3. The isolation file is written again with a mode for the child key which
 *    keeps the host visible.
 *
 * Expected:
 * 1. The mode of the parent key reaches the child key: the host value of the
 *    child key is not readable, while the value of the hive is.
 * 2. The mode of the child key overrides the mode of its parent, so the host
 *    value of the child key is readable again.
 * 3. The real registry keeps its own values.
 */
TEST_F(E2E_Reg, IsolationInheritance_KeyModeReachesChildKey)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data")
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto subkey = L"Software\\AppBoxTest\\IsolationInheritance_" + appbox::UTF8ToWide(appbox::RandomString(8));
    const auto child = subkey + L"\\Child";

    RealKeyGuard guard{ subkey };
    ASSERT_TRUE(WriteRealValue(subkey, L"HostValue", L"host"));
    ASSERT_TRUE(WriteRealValue(child, L"ChildHost", L"child-host"));

    HiveBuilder builder(GetCWD());
    builder.SetValue(L"HKEY_CURRENT_USER\\" + child, L"ChildSandbox", REG_SZ, StringData(L"child-sandbox"));
    builder.SetKeyIsolation(L"HKEY_CURRENT_USER\\" + subkey, appbox::RegistryIsolation::Full);

    std::string error;
    ASSERT_TRUE(builder.Write(error)) << error;

    /* The mode of the ancestor hides the host value of the child key. */
    {
        const auto rsp = ReadSandboxValue(child, L"ChildHost", GetCWD(), config);
        ASSERT_EQ(rsp.open_code, static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(rsp.query_code, static_cast<DWORD>(ERROR_FILE_NOT_FOUND));
    }

    /* The value of the hive stays visible. */
    {
        const auto rsp = ReadSandboxValue(child, L"ChildSandbox", GetCWD(), config);
        ASSERT_EQ(rsp.open_code, static_cast<DWORD>(ERROR_SUCCESS));
        ASSERT_EQ(rsp.query_code, static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(rsp.data, "child-sandbox");
    }

    /* A mode of the child key overrides the mode of the ancestor. */
    HiveBuilder overriding(GetCWD());
    overriding.SetValue(L"HKEY_CURRENT_USER\\" + child, L"ChildSandbox", REG_SZ, StringData(L"child-sandbox"));
    overriding.SetKeyIsolation(L"HKEY_CURRENT_USER\\" + subkey, appbox::RegistryIsolation::Full);
    overriding.SetKeyIsolation(L"HKEY_CURRENT_USER\\" + child, appbox::RegistryIsolation::WriteCopy);
    ASSERT_TRUE(overriding.Write(error)) << error;

    {
        const auto rsp = ReadSandboxValue(child, L"ChildHost", GetCWD(), config);
        ASSERT_EQ(rsp.open_code, static_cast<DWORD>(ERROR_SUCCESS));
        ASSERT_EQ(rsp.query_code, static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(rsp.data, "child-host");
    }

    /* The real registry keeps its own values. */
    EXPECT_EQ(ReadRealValue(child, L"ChildHost"), L"child-host");
}

/**
 * Condition:
 * 1. The real HKCU holds the key with the values `HiddenByValue` and
 *    `Visible`, the sandbox hive holds the same key with the value
 *    `SandboxValue`.
 * 2. The isolation file marks the value `HiddenByValue` as `Hide` and leaves
 *    the mode of the key at its default.
 *
 * Expected:
 * 1. The mode of a single value hides the value of the host without touching
 *    the other values of the key: `Visible` keeps its read through and the
 *    value of the hive is visible as well.
 * 2. The real registry keeps its own values.
 */
TEST_F(E2E_Reg, IsolationInheritance_ValueModeHidesTheValue)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data")
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto subkey = L"Software\\AppBoxTest\\IsolationValueMode_" + appbox::UTF8ToWide(appbox::RandomString(8));

    RealKeyGuard guard{ subkey };
    ASSERT_TRUE(WriteRealValue(subkey, L"HiddenByValue", L"hidden"));
    ASSERT_TRUE(WriteRealValue(subkey, L"Visible", L"visible"));

    HiveBuilder builder(GetCWD());
    builder.SetValue(L"HKEY_CURRENT_USER\\" + subkey, L"SandboxValue", REG_SZ, StringData(L"sandbox"));
    builder.SetValueIsolation(L"HKEY_CURRENT_USER\\" + subkey, L"HiddenByValue", appbox::RegistryIsolation::Hide);

    std::string error;
    ASSERT_TRUE(builder.Write(error)) << error;

    /* The mode of the value hides the value of the host. */
    {
        const auto rsp = ReadSandboxValue(subkey, L"HiddenByValue", GetCWD(), config);
        ASSERT_EQ(rsp.open_code, static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(rsp.query_code, static_cast<DWORD>(ERROR_FILE_NOT_FOUND));
    }

    /* The other values of the key keep their read through. */
    {
        const auto rsp = ReadSandboxValue(subkey, L"Visible", GetCWD(), config);
        ASSERT_EQ(rsp.open_code, static_cast<DWORD>(ERROR_SUCCESS));
        ASSERT_EQ(rsp.query_code, static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(rsp.data, "visible");
    }

    /* The value of the hive is visible. */
    {
        const auto rsp = ReadSandboxValue(subkey, L"SandboxValue", GetCWD(), config);
        ASSERT_EQ(rsp.open_code, static_cast<DWORD>(ERROR_SUCCESS));
        ASSERT_EQ(rsp.query_code, static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(rsp.data, "sandbox");
    }

    /* The real registry keeps its own values. */
    EXPECT_EQ(ReadRealValue(subkey, L"HiddenByValue"), L"hidden");
    EXPECT_EQ(ReadRealValue(subkey, L"Visible"), L"visible");
}
