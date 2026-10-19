#include "probe/RegDeleteKey.hpp"
#include "probe/RegReadValue.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/HiveBuilder.hpp"
#include "Random.hpp"
#include "WString.hpp"
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
 * 1. The image of the run (the registry of the archive) holds the key with the
 *    value `HostValue`; no layer of the state of the sandbox holds it.
 * 2. The sandboxed process deletes the key.
 * 3. The next run reads the key again, which rebuilds the image from the
 *    resources of the archive.
 *
 * Expected:
 * 1. The delete succeeds, because the key exists in the view of the sandbox.
 * 2. The key is gone from the view of the run which deleted it: the reopen
 *    reports `ERROR_FILE_NOT_FOUND`, although the image still holds the key.
 * 3. The key stays invisible in the next run: the whiteout of the state hive
 *    hides the key of the rebuilt image as well.
 */
TEST_F(E2E_Reg, DeleteKey_ImageKeyWhiteout)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data")
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto subkey = L"Software\\AppBoxTest\\DeleteKey_Image_" + appbox::UTF8ToWide(appbox::RandomString(8));
    const auto hive_key = L"HKEY_CURRENT_USER\\" + subkey;

    HiveBuilder builder(GetCWD());
    builder.SetValue(hive_key, L"HostValue", REG_SZ, StringData(L"image"));

    std::string error;
    ASSERT_TRUE(builder.Write(error)) << error;

    {
        ProtocolRegDeleteKey::Req req;
        req.Key = appbox::WideToUTF8(subkey);
        req.Mode = "reg";

        const auto rsp = ProbeRegDeleteKey.Call(req, GetCWD(), config).get<ProtocolRegDeleteKey::Rsp>();
        EXPECT_EQ(rsp.delete_code, static_cast<DWORD>(ERROR_SUCCESS));

        /* The key of the image is hidden by the marker of the state hive. */
        EXPECT_EQ(rsp.reopen_code, static_cast<DWORD>(ERROR_FILE_NOT_FOUND));
    }

    /* The next run rebuilds the image and keeps the marker. */
    {
        ProtocolRegReadValue::Req req;
        req.Key = appbox::WideToUTF8(subkey);
        req.Value = "HostValue";

        const auto rsp = ProbeRegReadValue.Call(req, GetCWD(), config).get<ProtocolRegReadValue::Rsp>();
        EXPECT_EQ(rsp.open_code, static_cast<DWORD>(ERROR_FILE_NOT_FOUND));
    }
}
