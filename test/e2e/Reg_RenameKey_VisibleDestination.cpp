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
 * 2. The destination name exists in the real HKCU as well, with the value
 *    `HostValue`, and the isolation file does not mention it, so it keeps the
 *    default mode `WriteCopy` and stays visible in the view.
 * 3. Rename the key onto the name of the second key inside the sandbox.
 *
 * Expected:
 * 1. The call is refused with `STATUS_CANNOT_DELETE`, which is the answer the
 *    kernel reports for a destination key which exists: the renamed key must
 *    not be merged with a visible key of the host layer, and the host layer is
 *    the only layer which holds the destination.
 * 2. Both names keep the content they had: the source shows the shadow key of
 *    the hive with the read through of the value of the host, the destination
 *    shows the key of the host.
 * 3. The real registry keeps both keys and their values.
 */
TEST_F(E2E_Reg, RenameKey_VisibleDestination)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data")
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto subkey =
        L"Software\\AppBoxTest\\RenameKey_VisibleDestination_" + appbox::UTF8ToWide(appbox::RandomString(8));
    const auto leaf = subkey.substr(subkey.find_last_of(L'\\') + 1);
    const auto new_name = leaf + L"_Renamed";
    const auto new_subkey = subkey.substr(0, subkey.find_last_of(L'\\') + 1) + new_name;

    RealHkcuKey real_key(subkey);
    ASSERT_NE(real_key.get(), nullptr);
    ASSERT_TRUE(real_key.SetString(L"HostValue", L"host_source"));

    RealHkcuKey real_destination(new_subkey);
    ASSERT_NE(real_destination.get(), nullptr);
    ASSERT_TRUE(real_destination.SetString(L"HostValue", L"host_destination"));

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
    EXPECT_EQ(rsp.rename_code, static_cast<DWORD>(STATUS_CANNOT_DELETE));

    /* The source keeps the shadow key of the hive and the read through of the
     * value of the host key. */
    EXPECT_EQ(rsp.old_open_code, static_cast<DWORD>(ERROR_SUCCESS));
    EXPECT_EQ(rsp.old_values.count("SandboxValue"), 1u);
    EXPECT_EQ(rsp.old_values.count("HostValue"), 1u);

    /* The destination keeps the key of the host layer. */
    EXPECT_EQ(rsp.new_open_code, static_cast<DWORD>(ERROR_SUCCESS));
    EXPECT_EQ(rsp.new_values.count("HostValue"), 1u);
    EXPECT_EQ(rsp.new_values.count("SandboxValue"), 0u);

    EXPECT_EQ(std::count(rsp.parent_subkeys.begin(), rsp.parent_subkeys.end(), appbox::WideToUTF8(leaf)), 1);
    EXPECT_EQ(std::count(rsp.parent_subkeys.begin(), rsp.parent_subkeys.end(), appbox::WideToUTF8(new_name)), 1);

    /* The host registry keeps both keys and their values. */
    EXPECT_EQ(real_key.GetString(L"HostValue"), L"host_source");
    EXPECT_EQ(real_destination.GetString(L"HostValue"), L"host_destination");
}
