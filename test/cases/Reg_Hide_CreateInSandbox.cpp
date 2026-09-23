#include "probe/RegWriteValue.hpp"
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
 * 1. The key exists in the real HKCU with the value `HostValue` and the
 *    isolation file marks it `Hide`, so the host entry is invisible for the
 *    sandbox.
 * 2. Create the key and write a value inside the sandbox.
 *
 * Expected:
 * 1. The create succeeds and builds the key inside the sandbox hive, which is
 *    the part of `Hide` which describes a creation; the closed loop of the
 *    write and the read back runs against the hive.
 * 2. The real key is unchanged: it does not receive the value of the sandbox.
 */
TEST_F(Reg, Hide_CreateInSandbox)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"Upper")
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto        subkey = L"Software\\AppBoxTest\\Hide_CreateInSandbox_"
                        + appbox::UTF8ToWide(appbox::RandomString(8));
    const std::string expected = "HiddenValue";

    RealHkcuKey real_key(subkey);
    ASSERT_NE(real_key.get(), nullptr);
    ASSERT_TRUE(real_key.SetString(L"HostValue", L"host"));

    /* The hive stays empty, the isolation file marks the key of the host. */
    HiveBuilder builder(GetCWD() / L"Upper");
    builder.SetKeyIsolation(L"HKEY_CURRENT_USER\\" + subkey, appbox::RegistryIsolation::Hide);

    std::string error;
    ASSERT_TRUE(builder.Write(error)) << error;

    ProtocolRegWriteValue::Req req;
    req.Key   = appbox::WideToUTF8(subkey);
    req.Value = "TestValue";
    req.Data  = expected;

    const auto rsp = ProbeRegWriteValue.Call(req, GetCWD(), config).get<ProtocolRegWriteValue::Rsp>();
    ASSERT_EQ(rsp.create_code, 0u);
    ASSERT_EQ(rsp.set_code, 0u);
    ASSERT_EQ(rsp.query_code, 0u);
    EXPECT_EQ(rsp.readback, expected);

    /* The host entry is invisible, so the key is new for the sandbox and the
     * create built it inside the hive. */
    EXPECT_EQ(rsp.disposition, static_cast<DWORD>(REG_CREATED_NEW_KEY));

    /* The host key is unchanged: it kept its own value and did not receive the
     * value of the sandbox. */
    {
        HKEY    key = nullptr;
        wchar_t buffer[128] = {};
        DWORD   size = sizeof(buffer);
        ASSERT_EQ(RegOpenKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0, KEY_QUERY_VALUE, &key), ERROR_SUCCESS);

        EXPECT_NE(RegQueryValueExW(key, L"TestValue", nullptr, nullptr, reinterpret_cast<LPBYTE>(buffer), &size),
                  ERROR_SUCCESS);

        size = sizeof(buffer);
        ASSERT_EQ(RegQueryValueExW(key, L"HostValue", nullptr, nullptr, reinterpret_cast<LPBYTE>(buffer), &size),
                  ERROR_SUCCESS);
        RegCloseKey(key);
        EXPECT_EQ(appbox::WideToUTF8(buffer), "host");
    }
}
