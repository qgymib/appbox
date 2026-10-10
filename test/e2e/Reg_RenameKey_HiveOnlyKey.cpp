#include "probe/RegRenameKey.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/HiveBuilder.hpp"
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
 * 1. The key exists in the sandbox hive only, with the value `SandboxValue`;
 *    the real registry does not hold the key.
 * 2. Rename the key inside the sandbox.
 *
 * Expected:
 * 1. The rename succeeds and the new name holds the key of the sandbox.
 * 2. The old name is gone from the view and the parent key reports the new name
 *    only.
 * 3. The real registry holds neither the old name nor the new name: a key which
 *    only the hive holds is never written into the real registry.
 */
TEST_F(E2E_Reg, RenameKey_HiveOnlyKey)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data")
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto subkey = L"Software\\AppBoxTest\\RenameKey_HiveOnlyKey_" + appbox::UTF8ToWide(appbox::RandomString(8));
    const auto leaf = subkey.substr(subkey.find_last_of(L'\\') + 1);
    const auto new_name = leaf + L"_Renamed";
    const auto new_subkey = subkey.substr(0, subkey.find_last_of(L'\\') + 1) + new_name;

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

    EXPECT_EQ(rsp.new_open_code, static_cast<DWORD>(ERROR_SUCCESS));
    EXPECT_EQ(rsp.new_values.count("SandboxValue"), 1u);
    EXPECT_EQ(rsp.new_values.count("HostValue"), 0u);

    EXPECT_EQ(rsp.old_open_code, static_cast<DWORD>(ERROR_FILE_NOT_FOUND));
    EXPECT_EQ(std::count(rsp.parent_subkeys.begin(), rsp.parent_subkeys.end(), appbox::WideToUTF8(leaf)), 0);
    EXPECT_EQ(std::count(rsp.parent_subkeys.begin(), rsp.parent_subkeys.end(), appbox::WideToUTF8(new_name)), 1);

    /* The real registry holds neither name. */
    HKEY key = nullptr;
    EXPECT_EQ(RegOpenKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0, KEY_QUERY_VALUE, &key),
              static_cast<LONG>(ERROR_FILE_NOT_FOUND));
    if (key != nullptr)
    {
        RegCloseKey(key);
    }

    key = nullptr;
    EXPECT_EQ(RegOpenKeyExW(HKEY_CURRENT_USER, new_subkey.c_str(), 0, KEY_QUERY_VALUE, &key),
              static_cast<LONG>(ERROR_FILE_NOT_FOUND));
    if (key != nullptr)
    {
        RegCloseKey(key);
    }
}
