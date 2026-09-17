#include "probe/RegReadValue.hpp"
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
 * 1. The key and value exist in the real HKCU, created by the test process
 *    outside the sandbox.
 * 2. Read the value inside the sandbox.
 *
 * Expected:
 * 1. The read inside the sandbox returns the real value (read through).
 * 2. The key still exists in the real HKCU afterwards.
 */
TEST_F(Reg, ReadValue_RealFallback)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"Upper")
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto       subkey = L"Software\\AppBoxTest\\ReadValue_RealFallback_" + appbox::UTF8ToWide(appbox::RandomString(8));
    const std::string expected = "RealFallbackValue";

    /* Create the key and the value in the real registry. */
    {
        HKEY  key = nullptr;
        DWORD disposition = 0;
        ASSERT_EQ(RegCreateKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key,
                                 &disposition),
                  ERROR_SUCCESS);

        auto wdata = appbox::UTF8ToWide(expected);
        ASSERT_EQ(RegSetValueExW(key, L"TestValue", 0, REG_SZ,
                                 reinterpret_cast<const BYTE*>(wdata.c_str()),
                                 static_cast<DWORD>((wdata.size() + 1) * sizeof(wchar_t))),
                  ERROR_SUCCESS);
        RegCloseKey(key);
    }

    RealKeyGuard guard{ subkey };

    ProtocolRegReadValue::Req req;
    req.Key   = appbox::WideToUTF8(subkey);
    req.Value = "TestValue";

    auto rsp = ProbeRegReadValue.Call(req, GetCWD(), config).get<ProtocolRegReadValue::Rsp>();
    ASSERT_EQ(rsp.open_code, 0u);
    ASSERT_EQ(rsp.query_code, 0u);
    ASSERT_EQ(rsp.data, expected);

    /* The key must still exist in the real registry. */
    {
        HKEY key = nullptr;
        ASSERT_EQ(RegOpenKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0, KEY_READ, &key), ERROR_SUCCESS);
        RegCloseKey(key);
    }
}
