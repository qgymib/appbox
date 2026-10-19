#include "probe/RegRenameKey.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/HiveBuilder.hpp"
#include "Random.hpp"
#include "WString.hpp"
#include <algorithm>
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

} // namespace

/**
 * Condition:
 * 1. The image of the run (the registry of the archive) holds the key `Old`
 *    below a parent key, with the value `SandboxValue`.
 * 2. The sandboxed process renames `Old` to `New`.
 *
 * Expected:
 * 1. The rename succeeds and the key of the state hive carries the content of
 *    the image: the copy-up of the key travels with the rename, so the new name
 *    reports `SandboxValue` instead of an empty key.
 * 2. The old name is gone from the view: its open reports `ERROR_FILE_NOT_FOUND`
 *    although the image still holds it, because the rename records the old name
 *    as deleted.
 * 3. The parent key reports the new name alone.
 */
TEST_F(E2E_Reg, RenameKey_ImageKeyCopyUp)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data")
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto parent = L"Software\\AppBoxTest\\RenameKey_Image_" + appbox::UTF8ToWide(appbox::RandomString(8));
    const auto old_key = parent + L"\\Old";
    const auto hive_key = L"HKEY_CURRENT_USER\\" + old_key;

    HiveBuilder builder(GetCWD());
    builder.SetValue(hive_key, L"SandboxValue", REG_SZ, StringData(L"sandbox"));

    std::string error;
    ASSERT_TRUE(builder.Write(error)) << error;

    ProtocolRegRenameKey::Req req;
    req.Key = appbox::WideToUTF8(old_key);
    req.NewName = "New";
    req.Mode = "hive_handle";

    const auto rsp = ProbeRegRenameKey.Call(req, GetCWD(), config).get<ProtocolRegRenameKey::Rsp>();
    ASSERT_EQ(rsp.open_code, static_cast<DWORD>(ERROR_SUCCESS));
    EXPECT_EQ(rsp.rename_code, static_cast<DWORD>(ERROR_SUCCESS));

    /* The old name is hidden, although the image still carries it. */
    EXPECT_EQ(rsp.old_open_code, static_cast<DWORD>(ERROR_FILE_NOT_FOUND));

    /* The new name carries the content of the image. */
    ASSERT_EQ(rsp.new_open_code, static_cast<DWORD>(ERROR_SUCCESS));
    const auto value = rsp.new_values.find("SandboxValue");
    ASSERT_NE(value, rsp.new_values.end());
    EXPECT_EQ(value->second, "sandbox");

    EXPECT_EQ(std::find(rsp.parent_subkeys.begin(), rsp.parent_subkeys.end(), "Old"), rsp.parent_subkeys.end());
    EXPECT_NE(std::find(rsp.parent_subkeys.begin(), rsp.parent_subkeys.end(), "New"), rsp.parent_subkeys.end());
}
