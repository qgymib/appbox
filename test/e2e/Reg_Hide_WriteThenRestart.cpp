#include "probe/RegReadValue.hpp"
#include "probe/RegWriteValue.hpp"
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
 * @brief Read a value of a key below HKCU inside the sandbox.
 * @param[in] subkey Path of the key below HKCU.
 * @param[in] name Name of the value.
 * @param[in] cwd Working directory of the case.
 * @param[in] config Launcher configuration of the sandbox.
 * @return The response of the probe.
 */
ProtocolRegReadValue::Rsp ReadSandboxValue(const std::wstring& subkey, const std::wstring& name,
                                           const std::filesystem::path& cwd, const appbox::LauncherConfig& config)
{
    ProtocolRegReadValue::Req req;
    req.Key = appbox::WideToUTF8(subkey);
    req.Value = appbox::WideToUTF8(name);
    return ProbeRegReadValue.Call(req, cwd, config).get<ProtocolRegReadValue::Rsp>();
}

} // namespace

/**
 * Condition:
 * 1. The key is marked `Hide` and the hive holds it with the value `Packed`, so
 *    the packed content of the entry lives in the hidden store of the hive.
 * 2. The sandboxed process reads `Packed`, writes `Written` and reads it back.
 * 3. A second run of the sandbox starts on the same state directory and reads
 *    `Written` again.
 *
 * Expected:
 * 1. `Packed` reports `File Not Found`: the packed content of a hidden entry is
 *    part of neither the view of the sandbox nor the merged view of the host.
 * 2. The write lands in the view of the sandbox, which makes the entry visible:
 *    the read back of the same run reports the data of the write.
 * 3. The second run reports the same data, because the write of the sandbox is
 *    part of the state directory and not of the hidden store.
 */
TEST_F(E2E_Reg, Hide_WriteThenRestart)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data")
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto subkey = L"Software\\AppBoxTest\\Hide_WriteThenRestart_" + appbox::UTF8ToWide(appbox::RandomString(8));
    const auto hive_key = L"HKEY_CURRENT_USER\\" + subkey;

    HiveBuilder builder(GetCWD());
    builder.SetValue(hive_key, L"Packed", REG_SZ, StringData(L"packed"));
    builder.SetKeyIsolation(hive_key, appbox::RegistryKeyIsolation::Hide);

    std::string error;
    ASSERT_TRUE(builder.Write(error)) << error;

    /* The packed content of the hidden entry is invisible: the key of the entry
     * is not part of the view at all, so the open reports `File Not Found`. */
    {
        const auto rsp = ReadSandboxValue(subkey, L"Packed", GetCWD(), config);
        EXPECT_EQ(rsp.open_code, static_cast<DWORD>(ERROR_FILE_NOT_FOUND));
    }

    /* The write of the sandbox makes the entry visible. */
    {
        ProtocolRegWriteValue::Req req;
        req.Key = appbox::WideToUTF8(subkey);
        req.Value = "Written";
        req.Data = "run1";

        const auto rsp = ProbeRegWriteValue.Call(req, GetCWD(), config).get<ProtocolRegWriteValue::Rsp>();
        ASSERT_EQ(rsp.create_code, static_cast<DWORD>(ERROR_SUCCESS));
        ASSERT_EQ(rsp.set_code, static_cast<DWORD>(ERROR_SUCCESS));
        ASSERT_EQ(rsp.query_code, static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(rsp.readback, "run1");
    }

    {
        const auto rsp = ReadSandboxValue(subkey, L"Written", GetCWD(), config);
        ASSERT_EQ(rsp.open_code, static_cast<DWORD>(ERROR_SUCCESS));
        ASSERT_EQ(rsp.query_code, static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(rsp.data, "run1");
    }

    /* A second run of the sandbox sees the write of the first one. */
    {
        const auto rsp = ReadSandboxValue(subkey, L"Written", GetCWD(), config);
        ASSERT_EQ(rsp.open_code, static_cast<DWORD>(ERROR_SUCCESS));
        ASSERT_EQ(rsp.query_code, static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(rsp.data, "run1");
    }
}
