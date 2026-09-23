#include "probe/RegDeleteKey.hpp"
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
 * 1. The key exists in the real HKCU with the value `HostValue` and the sandbox
 *    hive holds a shadow key of the same name with the value `SandboxValue`.
 * 2. Delete the key inside the sandbox.
 *
 * Expected:
 * 1. The delete removes the shadow key of the hive, which is the key the view
 *    answered.
 * 2. The key stays gone: the read through of the host key does not bring it
 *    back, because the delete records the key as deleted as well.
 * 3. The real registry still holds the key and its value.
 */
TEST_F(Reg, DeleteKey_ShadowKeyRemoved)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"Upper")
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto subkey = L"Software\\AppBoxTest\\DeleteKey_ShadowKeyRemoved_" + appbox::UTF8ToWide(appbox::RandomString(8));

    RealHkcuKey real_key(subkey);
    ASSERT_NE(real_key.get(), nullptr);
    ASSERT_TRUE(real_key.SetString(L"HostValue", L"host"));

    HiveBuilder builder(GetCWD() / L"Upper");
    builder.SetValue(L"HKEY_CURRENT_USER\\" + subkey, L"SandboxValue", REG_SZ, StringData(L"sandbox"));

    std::string error;
    ASSERT_TRUE(builder.Write(error)) << error;

    ProtocolRegDeleteKey::Req req;
    req.Key  = appbox::WideToUTF8(subkey);
    req.Mode = "reg";

    const auto rsp = ProbeRegDeleteKey.Call(req, GetCWD(), config).get<ProtocolRegDeleteKey::Rsp>();
    EXPECT_EQ(rsp.delete_code, static_cast<DWORD>(ERROR_SUCCESS));
    EXPECT_EQ(rsp.reopen_code, static_cast<DWORD>(ERROR_FILE_NOT_FOUND));

    /* The host registry is untouched. */
    HKEY    key = nullptr;
    wchar_t buffer[64] = {};
    DWORD   size = sizeof(buffer);
    ASSERT_EQ(RegOpenKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0, KEY_QUERY_VALUE, &key), ERROR_SUCCESS);
    ASSERT_EQ(RegQueryValueExW(key, L"HostValue", nullptr, nullptr, reinterpret_cast<LPBYTE>(buffer), &size),
              ERROR_SUCCESS);
    RegCloseKey(key);
    EXPECT_EQ(appbox::WideToUTF8(buffer), "host");
}
