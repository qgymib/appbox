#include "probe/RegRenameKey.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/RealHkcuKey.hpp"
#include "Random.hpp"
#include "WString.hpp"
#include <algorithm>

typedef appbox::test::CommonFixture E2E_Reg;
using namespace appbox::test;

/**
 * Condition:
 * 1. The key and the value `HostValue` exist in the real HKCU only, so a read
 *    access open of the key is answered by the real registry (the read through)
 *    and the caller holds a handle of the host layer.
 * 2. Call `NtRenameKey` on that handle inside the sandbox.
 *
 * Expected:
 * 1. The call is refused with `STATUS_ACCESS_DENIED`: the isolation never lets
 *    a rename reach the real registry, and a handle of the host layer is a
 *    read through handle which the caller opened without the right to rename.
 *    The kernel refuses such a handle as well, because a rename asks the
 *    handle for the whole `KEY_WRITE` mask; the case pins that the isolation
 *    answers the call on its own and that the host stays untouched.
 * 2. The key keeps its name and its value inside the sandbox, and the new name
 *    does not exist in the view.
 * 3. The real registry keeps the key and its value and does not hold the new
 *    name.
 */
TEST_F(E2E_Reg, RenameKey_ReadHandle)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data")
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto subkey = L"Software\\AppBoxTest\\RenameKey_ReadHandle_" + appbox::UTF8ToWide(appbox::RandomString(8));
    const auto leaf = subkey.substr(subkey.find_last_of(L'\\') + 1);
    const auto new_name = leaf + L"_Renamed";
    const auto new_subkey = subkey.substr(0, subkey.find_last_of(L'\\') + 1) + new_name;

    RealHkcuKey real_key(subkey);
    ASSERT_NE(real_key.get(), nullptr);
    ASSERT_TRUE(real_key.SetString(L"HostValue", L"host"));

    ProtocolRegRenameKey::Req req;
    req.Key = appbox::WideToUTF8(subkey);
    req.NewName = appbox::WideToUTF8(new_name);
    req.Mode = "read_handle";

    const auto rsp = ProbeRegRenameKey.Call(req, GetCWD(), config).get<ProtocolRegRenameKey::Rsp>();
    ASSERT_EQ(rsp.open_code, static_cast<DWORD>(ERROR_SUCCESS));
    EXPECT_EQ(rsp.rename_code, static_cast<DWORD>(STATUS_ACCESS_DENIED));

    /* The view still holds the key under its old name, with the value of the
     * host layer, and the new name does not exist. */
    EXPECT_EQ(rsp.old_open_code, static_cast<DWORD>(ERROR_SUCCESS));
    EXPECT_EQ(rsp.old_values.count("HostValue"), 1u);
    EXPECT_EQ(rsp.new_open_code, static_cast<DWORD>(ERROR_FILE_NOT_FOUND));
    EXPECT_EQ(std::count(rsp.parent_subkeys.begin(), rsp.parent_subkeys.end(), appbox::WideToUTF8(leaf)), 1);

    /* The host registry is untouched. */
    EXPECT_EQ(real_key.GetString(L"HostValue"), L"host");

    HKEY key = nullptr;
    EXPECT_EQ(RegOpenKeyExW(HKEY_CURRENT_USER, new_subkey.c_str(), 0, KEY_QUERY_VALUE, &key),
              static_cast<LONG>(ERROR_FILE_NOT_FOUND));
    if (key != nullptr)
    {
        RegCloseKey(key);
    }
}
