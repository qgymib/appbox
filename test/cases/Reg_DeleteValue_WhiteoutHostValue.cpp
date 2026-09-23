#include "probe/RegDeleteValue.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/RealHkcuKey.hpp"
#include "Random.hpp"
#include "WString.hpp"
#include <algorithm>

typedef appbox::test::CommonFixture Reg;
using namespace appbox::test;

/**
 * Condition:
 * 1. The key exists in the real HKCU with the values `HostValue` and
 *    `KeptValue`; the sandbox hive does not hold the key, so the values are
 *    answered by the read through.
 * 2. Delete `HostValue` inside the sandbox.
 *
 * Expected:
 * 1. The delete succeeds and the value is gone from the view: the read reports
 *    `ERROR_FILE_NOT_FOUND` and the merged value enumeration no longer lists
 *    the name.
 * 2. The read through of the other value of the same key keeps working.
 * 3. The real registry still holds both values.
 */
TEST_F(Reg, DeleteValue_WhiteoutHostValue)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"Upper")
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto subkey = L"Software\\AppBoxTest\\DeleteValue_WhiteoutHostValue_" + appbox::UTF8ToWide(appbox::RandomString(8));

    RealHkcuKey real_key(subkey);
    ASSERT_NE(real_key.get(), nullptr);
    ASSERT_TRUE(real_key.SetString(L"HostValue", L"host"));
    ASSERT_TRUE(real_key.SetString(L"KeptValue", L"kept"));

    ProtocolRegDeleteValue::Req req;
    req.Key   = appbox::WideToUTF8(subkey);
    req.Value = "HostValue";

    const auto rsp = ProbeRegDeleteValue.Call(req, GetCWD(), config).get<ProtocolRegDeleteValue::Rsp>();
    ASSERT_EQ(rsp.open_code, static_cast<DWORD>(ERROR_SUCCESS));
    EXPECT_EQ(rsp.delete_code, static_cast<DWORD>(ERROR_SUCCESS));

    /* The value is gone from the view, the other one stays visible. */
    EXPECT_EQ(rsp.query_code, static_cast<DWORD>(ERROR_FILE_NOT_FOUND));
    EXPECT_EQ(rsp.enum_code, static_cast<DWORD>(ERROR_SUCCESS));
    EXPECT_EQ(std::count(rsp.names.begin(), rsp.names.end(), "HostValue"), 0);
    EXPECT_EQ(std::count(rsp.names.begin(), rsp.names.end(), "KeptValue"), 1);

    /* The host registry keeps both values. */
    HKEY    key = nullptr;
    wchar_t buffer[64] = {};
    DWORD   size = sizeof(buffer);
    ASSERT_EQ(RegOpenKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0, KEY_QUERY_VALUE, &key), ERROR_SUCCESS);
    ASSERT_EQ(RegQueryValueExW(key, L"HostValue", nullptr, nullptr, reinterpret_cast<LPBYTE>(buffer), &size),
              ERROR_SUCCESS);
    EXPECT_EQ(appbox::WideToUTF8(buffer), "host");

    size = sizeof(buffer);
    ASSERT_EQ(RegQueryValueExW(key, L"KeptValue", nullptr, nullptr, reinterpret_cast<LPBYTE>(buffer), &size),
              ERROR_SUCCESS);
    RegCloseKey(key);
    EXPECT_EQ(appbox::WideToUTF8(buffer), "kept");
}
