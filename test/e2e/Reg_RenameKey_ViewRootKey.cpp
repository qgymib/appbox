#include "probe/RegReadValue.hpp"
#include "probe/RegRenameKey.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/HiveBuilder.hpp"
#include "utils/RegistryRootKey.hpp"
#include "Random.hpp"
#include "WString.hpp"

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
 * 1. The sandbox hive holds a key below `HKEY_CURRENT_USER` with the value
 *    `SandboxValue`.
 * 2. Rename the root key of the current user itself inside the sandbox. The
 *    rename runs on a write access open of the root key, which the case
 *    addresses through `HKEY_USERS\<SID>` (the two roots name the same key of
 *    the view) and which the isolation answers with a handle of the hive.
 *
 * Expected:
 * 1. The call is refused with `STATUS_ACCESS_DENIED`: the name of a root key is
 *    the first component of every path of the hive, so a rename would make the
 *    whole subtree unreachable for the view. The kernel refuses the rename of
 *    the root of a hive the same way.
 * 2. The root key keeps its name and the view keeps working: the key below it
 *    is still readable with the value of the sandbox, which only the hive
 *    holds — a renamed root key would make the mapping of the view fail and the
 *    read would report a missing key.
 */
TEST_F(E2E_Reg, RenameKey_ViewRootKey)
{
    const auto sid = CurrentUserSid();
    ASSERT_FALSE(sid.empty()) << "the SID of the current user cannot be read";

    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data")
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto subkey = L"Software\\AppBoxTest\\RenameKey_ViewRootKey_" + appbox::UTF8ToWide(appbox::RandomString(8));

    HiveBuilder builder(GetCWD());
    builder.SetValue(L"HKEY_CURRENT_USER\\" + subkey, L"SandboxValue", REG_SZ, StringData(L"sandbox"));

    std::string error;
    ASSERT_TRUE(builder.Write(error)) << error;

    ProtocolRegRenameKey::Req req;
    req.Root = "HKEY_USERS";
    req.Key = appbox::WideToUTF8(sid); /* The root key of the view itself. */
    req.NewName = "HKCU_Renamed";
    req.Mode = "hive_handle";

    const auto rsp = ProbeRegRenameKey.Call(req, GetCWD(), config).get<ProtocolRegRenameKey::Rsp>();
    ASSERT_EQ(rsp.open_code, static_cast<DWORD>(ERROR_SUCCESS));
    EXPECT_EQ(rsp.rename_code, static_cast<DWORD>(STATUS_ACCESS_DENIED));

    /* The view keeps working: the key below the root is still readable. */
    ProtocolRegReadValue::Req read_req;
    read_req.Key = appbox::WideToUTF8(subkey);
    read_req.Value = "SandboxValue";

    const auto read_rsp = ProbeRegReadValue.Call(read_req, GetCWD(), config).get<ProtocolRegReadValue::Rsp>();
    EXPECT_EQ(read_rsp.open_code, static_cast<DWORD>(ERROR_SUCCESS));
    EXPECT_EQ(read_rsp.query_code, static_cast<DWORD>(ERROR_SUCCESS));
    EXPECT_EQ(read_rsp.data, "sandbox");
}
