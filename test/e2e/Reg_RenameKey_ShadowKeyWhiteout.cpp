#include "probe/RegRenameKey.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/HiveBuilder.hpp"
#include "utils/RealHkcuKey.hpp"
#include "Random.hpp"
#include "WString.hpp"
#include <algorithm>

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

} // namespace

/**
 * Condition:
 * 1. The key exists in the real HKCU with the value `HostValue` and the sandbox
 *    hive holds a shadow key of the same name with the value `SandboxValue`.
 * 2. Rename the key to `<name>_Renamed` inside the sandbox. The rename runs on
 *    a write access open, which the isolation answers with a handle of the
 *    sandbox hive.
 *
 * Expected:
 * 1. The rename succeeds and the new name holds the content of the sandbox.
 * 2. The old name is gone from the view of the sandbox: the read through of the
 *    host key of the old name does not bring it back, because the rename
 *    records the visible host key of the old name as deleted (a whiteout). The
 *    parent key reports the new name only.
 * 3. The real registry keeps the old key and its value and does not hold the
 *    new name.
 */
TEST_F(E2E_Reg, RenameKey_ShadowKeyWhiteout)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data")
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto subkey =
        L"Software\\AppBoxTest\\RenameKey_ShadowKeyWhiteout_" + appbox::UTF8ToWide(appbox::RandomString(8));
    const auto new_name = std::wstring(subkey.substr(subkey.find_last_of(L'\\') + 1)) + L"_Renamed";
    const auto new_subkey = subkey.substr(0, subkey.find_last_of(L'\\') + 1) + new_name;

    RealHkcuKey real_key(subkey);
    ASSERT_NE(real_key.get(), nullptr);
    ASSERT_TRUE(real_key.SetString(L"HostValue", L"host"));

    HiveBuilder builder(GetCWD());
    builder.SetValue(L"HKEY_CURRENT_USER\\" + subkey, L"SandboxValue", REG_SZ, StringData(L"sandbox"));

    std::string error;
    ASSERT_TRUE(builder.Write(error)) << error;

    ProtocolRegRenameKey::Req req;
    req.Key = appbox::WideToUTF8(subkey);
    req.NewName = appbox::WideToUTF8(new_name);
    req.Mode = "hive_handle";

    const auto rsp = ProbeRegRenameKey.Call(req, GetCWD(), config).get<ProtocolRegRenameKey::Rsp>();
    ASSERT_EQ(rsp.open_code, static_cast<DWORD>(ERROR_SUCCESS));
    EXPECT_EQ(rsp.rename_code, static_cast<DWORD>(ERROR_SUCCESS));

    /* The new name holds the key of the sandbox. */
    EXPECT_EQ(rsp.new_open_code, static_cast<DWORD>(ERROR_SUCCESS));
    EXPECT_EQ(rsp.new_values.count("SandboxValue"), 1u);
    EXPECT_EQ(rsp.new_values.count("HostValue"), 0u);

    /* The old name is gone from the view, not even through the read through of
     * the host layer. */
    EXPECT_EQ(rsp.old_open_code, static_cast<DWORD>(ERROR_FILE_NOT_FOUND));

    const auto old_leaf = appbox::WideToUTF8(subkey.substr(subkey.find_last_of(L'\\') + 1));
    EXPECT_EQ(std::count(rsp.parent_subkeys.begin(), rsp.parent_subkeys.end(), old_leaf), 0);
    EXPECT_EQ(std::count(rsp.parent_subkeys.begin(), rsp.parent_subkeys.end(), appbox::WideToUTF8(new_name)), 1);

    /* The host registry keeps the old key and does not hold the new name. */
    EXPECT_EQ(real_key.GetString(L"HostValue"), L"host");

    HKEY key = nullptr;
    EXPECT_EQ(RegOpenKeyExW(HKEY_CURRENT_USER, new_subkey.c_str(), 0, KEY_QUERY_VALUE, &key),
              static_cast<LONG>(ERROR_FILE_NOT_FOUND));
    if (key != nullptr)
    {
        RegCloseKey(key);
    }
}
