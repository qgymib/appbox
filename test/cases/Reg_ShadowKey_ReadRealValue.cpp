#include "probe/RegShadowRead.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "Random.hpp"
#include "WString.hpp"

typedef appbox::test::CommonFixture Reg;
using namespace appbox::test;

/**
 * @brief Delete the key of the test from the real HKCU.
 */
struct RealKeyGuard
{
    std::wstring subkey;

    ~RealKeyGuard()
    {
        RegDeleteTreeW(HKEY_CURRENT_USER, subkey.c_str());
    }
};

/**
 * Condition:
 * 1. The key and the REG_SZ value "TestValue" exist in the real HKCU, created
 *    by the test process outside the sandbox.
 * 2. Inside the sandbox the key is created. NtCreateKey redirects the creation
 *    into the hive, so the handle is a shadow of the real key.
 * 3. The value "TestValue" is read through the shadow inside the sandbox.
 *
 * Expected:
 * 1. The read through the shadow returns the real value (read through of
 *    NtQueryValueKey), so the shadow no longer hides the values of the real
 *    key.
 * 2. The real value is unchanged afterwards.
 */
TEST_F(Reg, ShadowKey_ReadRealValue)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"Upper")
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto       subkey = L"Software\\AppBoxTest\\ShadowKey_ReadRealValue_" + appbox::UTF8ToWide(appbox::RandomString(8));
    const std::string expected = "RealValue";

    /* Create the key and the value in the real registry. */
    {
        HKEY key = nullptr;
        ASSERT_EQ(RegCreateKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key,
                                  nullptr),
                  ERROR_SUCCESS);

        auto wdata = appbox::UTF8ToWide(expected);
        ASSERT_EQ(RegSetValueExW(key, L"TestValue", 0, REG_SZ, reinterpret_cast<const BYTE*>(wdata.c_str()),
                                 static_cast<DWORD>((wdata.size() + 1) * sizeof(wchar_t))),
                  ERROR_SUCCESS);
        RegCloseKey(key);
    }

    RealKeyGuard guard{ subkey };

    ProtocolRegShadowRead::Req req;
    req.Key = appbox::WideToUTF8(subkey);
    req.Value = "TestValue";

    auto rsp = ProbeRegShadowRead.Call(req, GetCWD(), config).get<ProtocolRegShadowRead::Rsp>();
    ASSERT_EQ(rsp.create_code, 0u);
    /* Known gap: the disposition reflects the hive, not the real registry. */
    ASSERT_EQ(rsp.disposition, static_cast<DWORD>(REG_CREATED_NEW_KEY));
    ASSERT_EQ(rsp.query_code, 0u);
    ASSERT_EQ(rsp.data, expected);

    /* The real value must be unchanged. */
    {
        HKEY    key = nullptr;
        wchar_t buf[128] = {};
        DWORD   buf_size = sizeof(buf);
        ASSERT_EQ(RegOpenKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0, KEY_READ, &key), ERROR_SUCCESS);
        ASSERT_EQ(RegQueryValueExW(key, L"TestValue", nullptr, nullptr, reinterpret_cast<LPBYTE>(buf), &buf_size),
                  ERROR_SUCCESS);
        RegCloseKey(key);
        ASSERT_EQ(appbox::WideToUTF8(buf), expected);
    }
}
