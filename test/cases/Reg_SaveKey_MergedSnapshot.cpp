#include "probe/RegSaveKey.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/HiveBuilder.hpp"
#include "utils/RealHkcuKey.hpp"
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

} // namespace

/**
 * Condition:
 * 1. The key exists in the real HKCU with the values `HostValue` and `Hidden`,
 *    the sandbox hive holds the same key with the value `SandboxValue`, and the
 *    isolation file marks the host value `Hidden` as `Full`.
 * 2. Save the key inside the sandbox.
 * 3. Save a key which only the sandbox hive holds as well.
 *
 * Expected:
 * 1. The save succeeds and writes a hive file which holds the merged view of
 *    the key: the value of the hive and the visible value of the host.
 * 2. The host value which the isolation hides is not part of the file.
 * 3. A key which the host does not hold is exported with its hive content.
 * 4. The real registry is unchanged.
 */
TEST_F(Reg, SaveKey_MergedSnapshot)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"Upper")
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto subkey = L"Software\\AppBoxTest\\SaveKey_MergedSnapshot_" + appbox::UTF8ToWide(appbox::RandomString(8));
    const auto hive_only = L"Software\\AppBoxTest\\SaveKey_HiveOnly_" + appbox::UTF8ToWide(appbox::RandomString(8));

    RealHkcuKey real_key(subkey);
    ASSERT_NE(real_key.get(), nullptr);
    ASSERT_TRUE(real_key.SetString(L"HostValue", L"host"));
    ASSERT_TRUE(real_key.SetString(L"Hidden", L"hidden"));

    HiveBuilder builder(GetCWD() / L"Upper");
    builder.SetValue(L"HKEY_CURRENT_USER\\" + subkey, L"SandboxValue", REG_SZ, StringData(L"sandbox"));
    builder.SetValueIsolation(L"HKEY_CURRENT_USER\\" + subkey, L"Hidden", appbox::RegistryIsolation::Full);
    builder.SetValue(L"HKEY_CURRENT_USER\\" + hive_only, L"OnlyValue", REG_SZ, StringData(L"only"));

    std::string error;
    ASSERT_TRUE(builder.Write(error)) << error;

    const auto save_path = GetCWD() / (L"reg_save_" + appbox::UTF8ToWide(appbox::RandomString(8)) + L".hiv");

    /* The key which both layers hold: the export holds the merged view. */
    {
        ProtocolRegSaveKey::Req req;
        req.Key    = appbox::WideToUTF8(subkey);
        req.Path   = appbox::WideToUTF8(save_path.wstring());
        req.Expect = {"SandboxValue", "sandbox", "HostValue", "host"};
        req.Reject = {"Hidden", "hidden"};

        const auto rsp = ProbeRegSaveKey.Call(req, GetCWD(), config).get<ProtocolRegSaveKey::Rsp>();
        ASSERT_EQ(rsp.privilege_code, static_cast<DWORD>(ERROR_SUCCESS));
        ASSERT_EQ(rsp.open_code, static_cast<DWORD>(ERROR_SUCCESS));
        ASSERT_EQ(rsp.save_code, static_cast<DWORD>(ERROR_SUCCESS));

        EXPECT_TRUE(rsp.hive_signature);
        EXPECT_GT(rsp.size, 0u);
        EXPECT_TRUE(rsp.missing.empty());
        EXPECT_TRUE(rsp.unexpected.empty());
    }

    /* A key which only the hive holds is exported with its hive content. */
    {
        ProtocolRegSaveKey::Req req;
        req.Key    = appbox::WideToUTF8(hive_only);
        req.Path   = appbox::WideToUTF8(save_path.wstring());
        req.Expect = {"OnlyValue", "only"};
        req.Reject = {"SandboxValue"};

        const auto rsp = ProbeRegSaveKey.Call(req, GetCWD(), config).get<ProtocolRegSaveKey::Rsp>();
        ASSERT_EQ(rsp.open_code, static_cast<DWORD>(ERROR_SUCCESS));
        ASSERT_EQ(rsp.save_code, static_cast<DWORD>(ERROR_SUCCESS));

        EXPECT_TRUE(rsp.hive_signature);
        EXPECT_GT(rsp.size, 0u);
        EXPECT_TRUE(rsp.missing.empty());
        EXPECT_TRUE(rsp.unexpected.empty());
    }

    /* The real registry is unchanged. */
    HKEY    key = nullptr;
    wchar_t buffer[64] = {};
    DWORD   size = sizeof(buffer);
    ASSERT_EQ(RegOpenKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0, KEY_QUERY_VALUE, &key), ERROR_SUCCESS);
    ASSERT_EQ(RegQueryValueExW(key, L"Hidden", nullptr, nullptr, reinterpret_cast<LPBYTE>(buffer), &size),
              ERROR_SUCCESS);
    RegCloseKey(key);
    EXPECT_EQ(appbox::WideToUTF8(buffer), "hidden");
}
