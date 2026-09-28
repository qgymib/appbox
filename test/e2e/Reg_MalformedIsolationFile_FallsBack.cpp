#include "probe/RegReadValue.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/HiveBuilder.hpp"
#include "Random.hpp"
#include "WString.hpp"
#include <filesystem>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

typedef appbox::test::CommonFixture E2E_Reg;
using namespace appbox::test;

namespace
{

/** A version number no sandbox of this build knows. */
constexpr int kUnknownVersion = 99;

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
 * 1. The key exists in the real HKCU with the value `HostValue`, the sandbox
 *    hive holds the same key with the value `SandboxValue`.
 * 2. The isolation file of the overlay cannot be used: it is not a JSON
 *    document in the first half of the case and it carries a version the
 *    sandbox does not know in the second one. The document of the last half is
 *    the valid one which hides the host value.
 *
 * Expected:
 * 1. A document which cannot be read is ignored: the sandbox behaves like one
 *    without an isolation file, so the host value stays readable while the
 *    value of the hive is readable as well.
 * 2. The same entry hides the host value as soon as the document is readable,
 *    which is what makes the two runs above a check of the fallback instead of
 *    a check of the default mode.
 */
TEST_F(E2E_Reg, MalformedIsolationFile_FallsBack)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"Upper")
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto subkey = L"Software\\AppBoxTest\\MalformedIsolation_" + appbox::UTF8ToWide(appbox::RandomString(8));

    RealKeyGuard guard{ subkey };
    ASSERT_TRUE(WriteRealValue(subkey, L"HostValue", L"host"));

    HiveBuilder builder(GetCWD() / L"Upper");
    builder.SetValue(L"HKEY_CURRENT_USER\\" + subkey, L"SandboxValue", REG_SZ, StringData(L"sandbox"));

    std::string error;
    ASSERT_TRUE(builder.Write(error)) << error;

    /* A document which is not JSON at all is ignored. */
    ASSERT_TRUE(builder.WriteRawIsolation("{ \"version\": 1, \"keys\": [", error)) << error;
    {
        const auto rsp = ReadSandboxValue(subkey, L"HostValue", GetCWD(), config);
        ASSERT_EQ(rsp.open_code, static_cast<DWORD>(ERROR_SUCCESS));
        ASSERT_EQ(rsp.query_code, static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(rsp.data, "host");
    }

    /* A document of a version the sandbox does not know is ignored as well. */
    {
        nlohmann::json document;
        document[appbox::registry_isolation::kVersionKey] = kUnknownVersion;
        document[appbox::registry_isolation::kKeysKey] = nlohmann::json::array();
        document[appbox::registry_isolation::kValuesKey] = nlohmann::json::array();
        ASSERT_TRUE(builder.WriteRawIsolation(document.dump(2), error)) << error;
    }
    {
        const auto rsp = ReadSandboxValue(subkey, L"HostValue", GetCWD(), config);
        ASSERT_EQ(rsp.open_code, static_cast<DWORD>(ERROR_SUCCESS));
        ASSERT_EQ(rsp.query_code, static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(rsp.data, "host");
    }

    /* The mode of a readable document hides the host value, so the two runs
     * above really are the fallback of a refused document. */
    builder.SetKeyIsolation(L"HKEY_CURRENT_USER\\" + subkey, appbox::RegistryIsolation::Full);
    ASSERT_TRUE(builder.Write(error)) << error;
    {
        const auto rsp = ReadSandboxValue(subkey, L"HostValue", GetCWD(), config);
        ASSERT_EQ(rsp.open_code, static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(rsp.query_code, static_cast<DWORD>(ERROR_FILE_NOT_FOUND));
    }

    /* The value of the hive is visible in every one of them. */
    {
        const auto rsp = ReadSandboxValue(subkey, L"SandboxValue", GetCWD(), config);
        ASSERT_EQ(rsp.open_code, static_cast<DWORD>(ERROR_SUCCESS));
        ASSERT_EQ(rsp.query_code, static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(rsp.data, "sandbox");
    }
}
