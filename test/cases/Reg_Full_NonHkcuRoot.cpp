#include "probe/RegReadValue.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/HiveBuilder.hpp"
#include "Random.hpp"
#include "WString.hpp"

typedef appbox::test::CommonFixture Reg;
using namespace appbox::test;

namespace
{

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
 * @brief Read a value of a hive key through one of the root keys.
 * @param[in] root_name Name of the root key.
 * @param[in] key_path Path of the key below the root key.
 * @param[in] value_name Name of the value.
 * @param[in] cwd Working directory of the test.
 * @param[in] config Loader configuration of the test.
 * @return The response of the probe.
 */
ProtocolRegReadValue::Rsp ReadValue(const std::string& root_name, const std::wstring& key_path,
                                    const std::string& value_name, const std::wstring& cwd,
                                    const appbox::LoaderConfig& config)
{
    ProtocolRegReadValue::Req req;
    req.Root  = root_name;
    req.Key   = appbox::WideToUTF8(key_path);
    req.Value = value_name;
    return ProbeRegReadValue.Call(req, cwd, config).get<ProtocolRegReadValue::Rsp>();
}

} // namespace

/**
 * Condition:
 * 1. The sandbox hive holds keys below `HKEY_LOCAL_MACHINE`,
 *    `HKEY_CLASSES_ROOT` and `HKEY_CURRENT_CONFIG`, and the isolation file
 *    marks another key of the machine root as `Full`.
 * 2. Read the values inside the sandbox through the matching root keys.
 *
 * Expected:
 * 1. Every root key of the view is redirected into the hive, so the values of
 *    the hive are visible through all of them.
 * 2. A key which was marked `Full` is reported as not found.
 */
TEST_F(Reg, Full_NonHkcuRoot)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"Upper")
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto suffix = appbox::UTF8ToWide(appbox::RandomString(8));
    const auto machine_key = L"Software\\AppBoxTest\\Full_NonHkcuRoot_" + suffix;
    const auto classes_key = L"AppBoxTest\\Full_NonHkcuRoot_" + suffix;
    const auto config_key = L"AppBoxTest\\Full_NonHkcuRoot_" + suffix;

    HiveBuilder builder(GetCWD() / L"Upper");
    builder.SetValue(L"HKEY_LOCAL_MACHINE\\" + machine_key, L"TestValue", REG_SZ, StringData(L"machine"));
    builder.SetValue(L"HKEY_CLASSES_ROOT\\" + classes_key, L"TestValue", REG_SZ, StringData(L"classes"));
    builder.SetValue(L"HKEY_CURRENT_CONFIG\\" + config_key, L"TestValue", REG_SZ, StringData(L"config"));
    builder.SetKeyIsolation(L"HKEY_LOCAL_MACHINE\\" + machine_key + L"\\Hidden", appbox::RegistryIsolation::Full);

    std::string error;
    ASSERT_TRUE(builder.Write(error)) << error;

    const auto machine = ReadValue("HKEY_LOCAL_MACHINE", machine_key, "TestValue", GetCWD(), config);
    ASSERT_EQ(machine.open_code, 0u);
    ASSERT_EQ(machine.query_code, 0u);
    EXPECT_EQ(machine.data, "machine");

    const auto classes = ReadValue("HKEY_CLASSES_ROOT", classes_key, "TestValue", GetCWD(), config);
    ASSERT_EQ(classes.open_code, 0u);
    ASSERT_EQ(classes.query_code, 0u);
    EXPECT_EQ(classes.data, "classes");

    const auto current_config = ReadValue("HKEY_CURRENT_CONFIG", config_key, "TestValue", GetCWD(), config);
    ASSERT_EQ(current_config.open_code, 0u);
    ASSERT_EQ(current_config.query_code, 0u);
    EXPECT_EQ(current_config.data, "config");

    /* The isolated key of the machine root is not visible. */
    const auto hidden = ReadValue("HKEY_LOCAL_MACHINE", machine_key + L"\\Hidden", "TestValue", GetCWD(), config);
    EXPECT_EQ(hidden.open_code, static_cast<DWORD>(ERROR_FILE_NOT_FOUND));

    /* An unknown root key name is reported as an invalid request. */
    const auto unknown = ReadValue("HKEY_OTHER", machine_key, "TestValue", GetCWD(), config);
    EXPECT_EQ(unknown.open_code, static_cast<DWORD>(ERROR_INVALID_PARAMETER));
}
