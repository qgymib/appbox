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
 * 1. The key exists in the sandbox hive only, with the value `SandboxValue`.
 * 2. The destination name exists in the real HKCU with the value `HostValue`
 *    and the isolation file marks it `Full`, so the host entry is invisible in
 *    the view.
 * 3. Rename the key onto the name of the hidden key inside the sandbox.
 *
 * Expected:
 * 1. The rename succeeds: the destination does not exist in the merged view, so
 *    the renamed key is not merged with a key the caller can see.
 * 2. The new name shows the key of the sandbox alone — the value of the hidden
 *    host key stays invisible — and the old name is gone from the view.
 * 3. The real registry keeps its key and its value and does not hold the old
 *    name.
 */
TEST_F(E2E_Reg, RenameKey_HiddenDestination)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data")
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto subkey =
        L"Software\\AppBoxTest\\RenameKey_HiddenDestination_" + appbox::UTF8ToWide(appbox::RandomString(8));
    const auto leaf = subkey.substr(subkey.find_last_of(L'\\') + 1);
    const auto new_name = leaf + L"_Renamed";
    const auto new_subkey = subkey.substr(0, subkey.find_last_of(L'\\') + 1) + new_name;

    RealHkcuKey real_destination(new_subkey);
    ASSERT_NE(real_destination.get(), nullptr);
    ASSERT_TRUE(real_destination.SetString(L"HostValue", L"host_destination"));

    HiveBuilder builder(GetCWD());
    builder.SetValue(L"HKEY_CURRENT_USER\\" + subkey, L"SandboxValue", REG_SZ, StringData(L"sandbox"));
    builder.SetKeyIsolation(L"HKEY_CURRENT_USER\\" + new_subkey, appbox::RegistryIsolation::Full);

    std::string error;
    ASSERT_TRUE(builder.Write(error)) << error;

    ProtocolRegRenameKey::Req req;
    req.Key = appbox::WideToUTF8(subkey);
    req.NewName = appbox::WideToUTF8(new_name);
    req.Mode = "hive_handle";

    const auto rsp = ProbeRegRenameKey.Call(req, GetCWD(), config).get<ProtocolRegRenameKey::Rsp>();
    ASSERT_EQ(rsp.open_code, static_cast<DWORD>(ERROR_SUCCESS));
    EXPECT_EQ(rsp.rename_code, static_cast<DWORD>(ERROR_SUCCESS));

    /* The new name holds the key of the sandbox, and the value of the hidden
     * host key is not part of it. */
    EXPECT_EQ(rsp.new_open_code, static_cast<DWORD>(ERROR_SUCCESS));
    EXPECT_EQ(rsp.new_values.count("SandboxValue"), 1u);
    EXPECT_EQ(rsp.new_values.count("HostValue"), 0u);

    EXPECT_EQ(rsp.old_open_code, static_cast<DWORD>(ERROR_FILE_NOT_FOUND));
    EXPECT_EQ(std::count(rsp.parent_subkeys.begin(), rsp.parent_subkeys.end(), appbox::WideToUTF8(leaf)), 0);
    EXPECT_EQ(std::count(rsp.parent_subkeys.begin(), rsp.parent_subkeys.end(), appbox::WideToUTF8(new_name)), 1);

    /* The host registry keeps its key and its value. */
    EXPECT_EQ(real_destination.GetString(L"HostValue"), L"host_destination");

    HKEY key = nullptr;
    EXPECT_EQ(RegOpenKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0, KEY_QUERY_VALUE, &key),
              static_cast<LONG>(ERROR_FILE_NOT_FOUND));
    if (key != nullptr)
    {
        RegCloseKey(key);
    }
}
