#include "probe/RegReadValue.hpp"
#include "utils/ProbeCall.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/HiveBuilder.hpp"
#include "Random.hpp"
#include "SandboxLayout.hpp"
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
 * @param[in] config Launcher configuration of the sandbox.
 * @return The response of the probe.
 * @throw std::exception The sandbox refused the start.
 */
ProtocolRegReadValue::Rsp ReadSandboxValue(const std::wstring& subkey, const std::wstring& name,
                                           const std::filesystem::path& cwd, const appbox::LauncherConfig& config)
{
    ProtocolRegReadValue::Req req;
    req.Key = appbox::WideToUTF8(subkey);
    req.Value = appbox::WideToUTF8(name);
    return Probe(cwd, config).Call<ProtocolRegReadValue>(req);
}

/**
 * @brief Path of the isolation file of the resources of the case.
 * @param[in] cwd Working directory of the case.
 * @return The path of the file.
 */
std::filesystem::path IsolationFilePath(const std::filesystem::path& cwd)
{
    return cwd / appbox::layout::kAppDirNameW / appbox::layout::kRegistryDirNameW / appbox::layout::kIsolationFileNameW;
}

} // namespace

/**
 * Condition:
 * 1. The key exists in the real HKCU with the value `HostValue`, the sandbox
 *    hive holds the same key with the value `SandboxValue`.
 * 2. The isolation file of the archive is replaced one after the other by a
 *    document which is not JSON, by a document of a version the sandbox does not
 *    know, by a document which names no root key of the view and by no document
 *    at all.
 * 3. The sandboxed process reads the value of the hive in every one of the four
 *    cases, and once more after the resources carry a usable document again.
 *
 * Expected:
 * 1. Every one of the four cases refuses the start of the sandbox: the modes of
 *    the view cannot be resolved without a usable document, and there is no
 *    default mode to fall back to, so a run which would show the host registry
 *    never starts.
 * 2. The usable document starts the sandbox and the value of the hive is
 *    readable, which is what makes the four runs above a check of the refusal
 *    instead of a check of a broken case.
 */
TEST_F(E2E_Reg, MalformedIsolationFile_FailsTheStart)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data")
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto subkey = L"Software\\AppBoxTest\\MalformedIsolation_" + appbox::UTF8ToWide(appbox::RandomString(8));

    RealKeyGuard guard{ subkey };
    ASSERT_TRUE(WriteRealValue(subkey, L"HostValue", L"host"));

    HiveBuilder builder(GetCWD());
    builder.SetValue(L"HKEY_CURRENT_USER\\" + subkey, L"SandboxValue", REG_SZ, StringData(L"sandbox"));

    std::string error;
    ASSERT_TRUE(builder.Write(error)) << error;

    /* A document which is not JSON at all. */
    ASSERT_TRUE(builder.WriteRawIsolation("{ \"version\": 1, \"keys\": [", error)) << error;
    EXPECT_THROW(ReadSandboxValue(subkey, L"SandboxValue", GetCWD(), config), std::exception);

    /* A document of a version the sandbox does not know. */
    {
        nlohmann::json document;
        document[appbox::registry_isolation::kVersionKey] = kUnknownVersion;
        document[appbox::registry_isolation::kKeysKey] = nlohmann::json::array();
        document[appbox::registry_isolation::kValuesKey] = nlohmann::json::array();
        ASSERT_TRUE(builder.WriteRawIsolation(document.dump(2), error)) << error;
    }
    EXPECT_THROW(ReadSandboxValue(subkey, L"SandboxValue", GetCWD(), config), std::exception);

    /* A document which names no root key of the view. */
    {
        nlohmann::json document;
        document[appbox::registry_isolation::kVersionKey] = appbox::registry_isolation::kVersion;
        document[appbox::registry_isolation::kKeysKey] = nlohmann::json::array();
        document[appbox::registry_isolation::kValuesKey] = nlohmann::json::array();
        ASSERT_TRUE(builder.WriteRawIsolation(document.dump(2), error)) << error;
    }
    EXPECT_THROW(ReadSandboxValue(subkey, L"SandboxValue", GetCWD(), config), std::exception);

    /* No document at all. */
    std::error_code ec;
    std::filesystem::remove(IsolationFilePath(GetCWD()), ec);
    EXPECT_THROW(ReadSandboxValue(subkey, L"SandboxValue", GetCWD(), config), std::exception);

    /* The usable document starts the sandbox, and the hive is the hive of it. */
    ASSERT_TRUE(builder.Write(error)) << error;
    {
        const auto rsp = ReadSandboxValue(subkey, L"SandboxValue", GetCWD(), config);
        ASSERT_EQ(rsp.open_code, static_cast<DWORD>(ERROR_SUCCESS));
        ASSERT_EQ(rsp.query_code, static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(rsp.data, "sandbox");
    }
}
