#include "probe/RegEnumValue.hpp"
#include "probe/RegWriteValue.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "Random.hpp"
#include "WString.hpp"
#include <set>
#include <string>

typedef appbox::test::CommonFixture Reg;
using namespace appbox::test;

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
 * @brief Create a key of the real registry and write a REG_SZ value into it.
 *
 * The key is created outside the sandbox, so it exists in the real HKCU only.
 *
 * @param[in] subkey The key path relative to HKCU.
 * @param[in] value The value name, empty for the default value of the key.
 * @param[in] data The value data.
 */
static void WriteRealValue(const std::wstring& subkey, const wchar_t* value, const std::string& data)
{
    HKEY key = nullptr;
    ASSERT_EQ(RegCreateKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr),
              ERROR_SUCCESS);

    auto wdata = appbox::UTF8ToWide(data);
    ASSERT_EQ(RegSetValueExW(key, value, 0, REG_SZ, reinterpret_cast<const BYTE*>(wdata.c_str()),
                             static_cast<DWORD>((wdata.size() + 1) * sizeof(wchar_t))),
              ERROR_SUCCESS);
    RegCloseKey(key);
}

/**
 * @brief Read a REG_SZ value of a real key outside the sandbox.
 *
 * @param[in] subkey The key path relative to HKCU.
 * @param[in] value The value name, empty for the default value of the key.
 * @return The value data.
 */
static std::string ReadRealValue(const std::wstring& subkey, const wchar_t* value)
{
    HKEY key = nullptr;
    EXPECT_EQ(RegOpenKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0, KEY_READ, &key), ERROR_SUCCESS);

    wchar_t buf[128] = {};
    DWORD   buf_size = sizeof(buf);
    EXPECT_EQ(RegQueryValueExW(key, value, nullptr, nullptr, reinterpret_cast<LPBYTE>(buf), &buf_size), ERROR_SUCCESS);
    RegCloseKey(key);
    return appbox::WideToUTF8(buf);
}

/**
 * @brief Write a REG_SZ value of a key below HKCU inside the sandbox.
 *
 * The key is created inside the hive — a shadow of the real key when the host
 * holds it — so the value lands in the sandbox and the real registry is never
 * modified.
 *
 * @param[in] subkey The key path relative to HKCU.
 * @param[in] value The value name, empty for the default value of the key.
 * @param[in] data The value data.
 * @param[in] cwd The working directory of the test.
 * @param[in] config The loader configuration of the sandbox.
 */
static void WriteSandboxValue(const std::wstring& subkey, const std::string& value, const std::string& data,
                              const std::filesystem::path& cwd, const appbox::LoaderConfig& config)
{
    ProtocolRegWriteValue::Req req;
    req.Key   = appbox::WideToUTF8(subkey);
    req.Value = value;
    req.Data  = data;
    auto rsp  = ProbeRegWriteValue.Call(req, cwd, config).get<ProtocolRegWriteValue::Rsp>();
    ASSERT_EQ(rsp.create_code, 0u);
    ASSERT_EQ(rsp.set_code, 0u);
    ASSERT_EQ(rsp.readback, data);
}

/**
 * @brief Enumerate the values of a key below HKCU inside the sandbox.
 *
 * The enumeration reports the merged two layer view of the key together with
 * the value count of RegQueryInfoKeyW(). The default value of the key is
 * enumerated with an empty name.
 *
 * @param[in] subkey The key path relative to HKCU.
 * @param[in] cwd The working directory of the test.
 * @param[in] config The loader configuration of the sandbox.
 * @return The response of the probe.
 */
static ProtocolRegEnumValue::Rsp EnumerateSandboxValues(const std::wstring& subkey, const std::filesystem::path& cwd,
                                                        const appbox::LoaderConfig& config)
{
    ProtocolRegEnumValue::Req req;
    req.Key = appbox::WideToUTF8(subkey);
    return ProbeRegEnumValue.Call(req, cwd, config).get<ProtocolRegEnumValue::Rsp>();
}

/**
 * Condition:
 * 1. Inside the sandbox the key is created (a shadow in the hive) and its
 *    default value plus the two named values "First" and "Second" are written
 *    into it. The real HKCU does not hold the key at all.
 * 2. The values of the key are enumerated inside the sandbox.
 *
 * Expected:
 * 1. The enumeration reports every value exactly once, the default value with
 *    its empty name included, and every value keeps its own data.
 * 2. The value count of RegQueryInfoKeyW() is the count of the merged view.
 * 3. The real registry does not hold the key, so the whole view comes from the
 *    hive layer.
 */
TEST_F(Reg, EnumValue_DefaultValueInHive)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"Upper")
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto subkey = L"Software\\AppBoxTest\\EnumValue_DefaultValueInHive_"
                        + appbox::UTF8ToWide(appbox::RandomString(8));

    const std::string default_data = "DefaultData";
    const std::string first_data   = "FirstData";
    const std::string second_data  = "SecondData";

    RealKeyGuard guard{ subkey };

    /* The default value is written first, so it is enumerated first. */
    WriteSandboxValue(subkey, "", default_data, GetCWD(), config);
    WriteSandboxValue(subkey, "First", first_data, GetCWD(), config);
    WriteSandboxValue(subkey, "Second", second_data, GetCWD(), config);

    auto rsp = EnumerateSandboxValues(subkey, GetCWD(), config);
    ASSERT_EQ(rsp.open_code, 0u);
    ASSERT_EQ(rsp.count_code, 0u);
    ASSERT_EQ(rsp.enum_code, 0u);

    /* Every value is reported exactly once, the default value included. */
    ASSERT_EQ(rsp.names.size(), 3u);
    std::set<std::string> names(rsp.names.begin(), rsp.names.end());
    ASSERT_EQ(names.size(), 3u);
    ASSERT_TRUE(names.count("") > 0);
    ASSERT_TRUE(names.count("First") > 0);
    ASSERT_TRUE(names.count("Second") > 0);

    ASSERT_EQ(rsp.value_count, 3u);
    ASSERT_EQ(rsp.values.size(), 3u);
    ASSERT_EQ(rsp.values[""], default_data);
    ASSERT_EQ(rsp.values["First"], first_data);
    ASSERT_EQ(rsp.values["Second"], second_data);

    /* The view comes from the hive alone. */
    HKEY key = nullptr;
    ASSERT_EQ(RegOpenKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0, KEY_READ, &key), ERROR_FILE_NOT_FOUND);
}

/**
 * Condition:
 * 1. The real HKCU holds the key with the default value "HostDefault" and the
 *    REG_SZ value "HostValue".
 * 2. Inside the sandbox the key is created (a shadow in the hive) and the
 *    REG_SZ value "SandboxValue" is written into the shadow.
 * 3. The values of the key are enumerated inside the sandbox.
 *
 * Expected:
 * 1. The merged view holds the default value of the real key (the empty name),
 *    "HostValue" of the real key and "SandboxValue" of the hive layer.
 * 2. The value count of RegQueryInfoKeyW() counts all three, so the count
 *    agrees with the enumeration.
 * 3. The real registry keeps its own default value and its own "HostValue".
 */
TEST_F(Reg, EnumValue_DefaultValueOfRealKey)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"Upper")
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto subkey = L"Software\\AppBoxTest\\EnumValue_DefaultValueOfRealKey_"
                        + appbox::UTF8ToWide(appbox::RandomString(8));

    const std::string host_default = "HostDefault";
    const std::string host_data    = "HostData";
    const std::string sandbox_data = "SandboxData";

    /* The real key holds a default value and a named value. */
    WriteRealValue(subkey, L"", host_default);
    WriteRealValue(subkey, L"HostValue", host_data);

    RealKeyGuard guard{ subkey };

    /* Create the shadow in the hive with a value of its own. */
    WriteSandboxValue(subkey, "SandboxValue", sandbox_data, GetCWD(), config);

    auto rsp = EnumerateSandboxValues(subkey, GetCWD(), config);
    ASSERT_EQ(rsp.open_code, 0u);
    ASSERT_EQ(rsp.count_code, 0u);
    ASSERT_EQ(rsp.enum_code, 0u);

    /* The default value of the real layer is part of the merged view. */
    ASSERT_EQ(rsp.names.size(), 3u);
    std::set<std::string> names(rsp.names.begin(), rsp.names.end());
    ASSERT_EQ(names.size(), 3u);
    ASSERT_TRUE(names.count("") > 0);
    ASSERT_TRUE(names.count("HostValue") > 0);
    ASSERT_TRUE(names.count("SandboxValue") > 0);

    ASSERT_EQ(rsp.value_count, 3u);
    ASSERT_EQ(rsp.values.size(), 3u);
    ASSERT_EQ(rsp.values[""], host_default);
    ASSERT_EQ(rsp.values["HostValue"], host_data);
    ASSERT_EQ(rsp.values["SandboxValue"], sandbox_data);

    /* The real entries keep their own data. */
    ASSERT_EQ(ReadRealValue(subkey, L""), host_default);
    ASSERT_EQ(ReadRealValue(subkey, L"HostValue"), host_data);
}

/**
 * Condition:
 * 1. The real HKCU holds the key with the default value "HostDefault".
 * 2. Inside the sandbox the key is created (a shadow in the hive) and its
 *    default value is set to "SandboxDefault".
 * 3. The values of the key are enumerated inside the sandbox.
 *
 * Expected:
 * 1. The empty name is the name of the default value of both layers, so the
 *    merged view lists it once: the hive layer wins the name conflict exactly
 *    like it does for every other name.
 * 2. The read back of the default value returns the data of the hive layer and
 *    the value count is one.
 * 3. The real registry keeps its own default value.
 */
TEST_F(Reg, EnumValue_DefaultValueShadowed)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"Upper")
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto subkey = L"Software\\AppBoxTest\\EnumValue_DefaultValueShadowed_"
                        + appbox::UTF8ToWide(appbox::RandomString(8));

    const std::string host_default    = "HostDefault";
    const std::string sandbox_default = "SandboxDefault";

    WriteRealValue(subkey, L"", host_default);

    RealKeyGuard guard{ subkey };

    /* The shadow of the hive holds a default value of its own. */
    WriteSandboxValue(subkey, "", sandbox_default, GetCWD(), config);

    auto rsp = EnumerateSandboxValues(subkey, GetCWD(), config);
    ASSERT_EQ(rsp.open_code, 0u);
    ASSERT_EQ(rsp.count_code, 0u);
    ASSERT_EQ(rsp.enum_code, 0u);

    ASSERT_EQ(rsp.names.size(), 1u);
    ASSERT_EQ(rsp.names.front(), "");
    ASSERT_EQ(rsp.value_count, 1u);
    ASSERT_EQ(rsp.values.size(), 1u);
    ASSERT_EQ(rsp.values[""], sandbox_default);

    /* The real registry keeps its own default value. */
    ASSERT_EQ(ReadRealValue(subkey, L""), host_default);
}
