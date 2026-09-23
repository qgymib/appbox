#include "probe/RegOpenWriteValue.hpp"
#include "probe/RegReadValue.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/HiveBuilder.hpp"
#include "utils/RealHkcuKey.hpp"
#include "Random.hpp"
#include "WString.hpp"

typedef appbox::test::CommonFixture Reg;
using namespace appbox::test;

/**
 * Condition:
 * 1. The key and the value exist in the real HKCU, the sandbox hive does not
 *    hold the key and the isolation file marks it `Full`.
 * 2. Open the key and read the value inside the sandbox, then open it with
 *    write access.
 *
 * Expected:
 * 1. Both opens report that the key does not exist, although the host holds
 *    it: `Full` hides the host entry for every access kind and never copies it
 *    up into the hive.
 * 2. The key of the host registry is unchanged.
 */
TEST_F(Reg, Full_HidesRealKey)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"Upper")
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto subkey = L"Software\\AppBoxTest\\Full_HidesRealKey_" + appbox::UTF8ToWide(appbox::RandomString(8));

    RealHkcuKey real_key(subkey);
    ASSERT_NE(real_key.get(), nullptr);
    ASSERT_TRUE(real_key.SetString(L"TestValue", L"host"));

    /* The hive stays empty, the isolation file marks the key of the host. */
    HiveBuilder builder(GetCWD() / L"Upper");
    builder.SetKeyIsolation(L"HKEY_CURRENT_USER\\" + subkey, appbox::RegistryIsolation::Full);

    std::string error;
    ASSERT_TRUE(builder.Write(error)) << error;

    ProtocolRegReadValue::Req req;
    req.Key   = appbox::WideToUTF8(subkey);
    req.Value = "TestValue";

    const auto rsp = ProbeRegReadValue.Call(req, GetCWD(), config).get<ProtocolRegReadValue::Rsp>();

    /* The host entry is invisible for the sandbox. */
    EXPECT_EQ(rsp.open_code, static_cast<DWORD>(ERROR_FILE_NOT_FOUND));

    /* A write access open hides the host entry as well: `Full` never copies
     * the host key up into the hive. */
    ProtocolRegOpenWriteValue::Req write_req;
    write_req.Key   = appbox::WideToUTF8(subkey);
    write_req.Value = "TestValue";
    write_req.Data  = "sandbox";

    const auto write_rsp =
        ProbeRegOpenWriteValue.Call(write_req, GetCWD(), config).get<ProtocolRegOpenWriteValue::Rsp>();
    EXPECT_EQ(write_rsp.open_code, static_cast<DWORD>(ERROR_FILE_NOT_FOUND));

    /* The key of the host registry is untouched. */
    HKEY    key = nullptr;
    wchar_t buffer[64] = {};
    DWORD   size = sizeof(buffer);
    ASSERT_EQ(RegOpenKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0, KEY_QUERY_VALUE, &key), ERROR_SUCCESS);
    ASSERT_EQ(RegQueryValueExW(key, L"TestValue", nullptr, nullptr, reinterpret_cast<LPBYTE>(buffer), &size),
              ERROR_SUCCESS);
    RegCloseKey(key);
    EXPECT_EQ(appbox::WideToUTF8(buffer), "host");
}
