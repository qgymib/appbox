#include "probe/RegEnumKey.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/HiveBuilder.hpp"
#include "utils/RealHkcuKey.hpp"
#include "Random.hpp"
#include "WString.hpp"
#include <algorithm>

typedef appbox::test::CommonFixture Reg;
using namespace appbox::test;

/**
 * Condition:
 * 1. The key of the host holds the sub keys `RealHidden` and `RealVisible`, the
 *    sandbox hive holds the same key with the sub key `SandboxKey` and the
 *    isolation file marks `RealHidden` as `Full`.
 * 2. Enumerate the sub keys of the key inside the sandbox.
 *
 * Expected:
 * 1. The enumeration shows the hive sub key and the visible host sub key, and
 *    it hides the host sub key which was isolated.
 * 2. The sub key count reported by RegQueryInfoKeyW() matches the enumeration.
 */
TEST_F(Reg, Full_EnumerateHidesReal)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"Upper")
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto subkey = L"Software\\AppBoxTest\\Full_EnumerateHidesReal_" + appbox::UTF8ToWide(appbox::RandomString(8));

    RealHkcuKey real_key(subkey);
    ASSERT_NE(real_key.get(), nullptr);
    {
        HKEY  hidden = nullptr;
        DWORD disposition = 0;
        ASSERT_EQ(RegCreateKeyExW(real_key.get(), L"RealHidden", 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &hidden,
                                  &disposition),
                  ERROR_SUCCESS);
        RegCloseKey(hidden);

        HKEY visible = nullptr;
        ASSERT_EQ(RegCreateKeyExW(real_key.get(), L"RealVisible", 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &visible,
                                  &disposition),
                  ERROR_SUCCESS);
        RegCloseKey(visible);
    }

    HiveBuilder builder(GetCWD() / L"Upper");
    builder.EnsureKey(L"HKEY_CURRENT_USER\\" + subkey + L"\\SandboxKey");
    builder.SetKeyIsolation(L"HKEY_CURRENT_USER\\" + subkey + L"\\RealHidden", appbox::RegistryIsolation::Full);

    std::string error;
    ASSERT_TRUE(builder.Write(error)) << error;

    ProtocolRegEnumKey::Req req;
    req.Key = appbox::WideToUTF8(subkey);

    const auto rsp = ProbeRegEnumKey.Call(req, GetCWD(), config).get<ProtocolRegEnumKey::Rsp>();
    ASSERT_EQ(rsp.open_code, 0u);
    ASSERT_EQ(rsp.enum_code, 0u);

    /* The isolated host sub key is not part of the view. */
    EXPECT_EQ(std::count(rsp.names.begin(), rsp.names.end(), "RealHidden"), 0);
    EXPECT_EQ(std::count(rsp.names.begin(), rsp.names.end(), "RealVisible"), 1);
    EXPECT_EQ(std::count(rsp.names.begin(), rsp.names.end(), "SandboxKey"), 1);

    /* The count of the merged view matches the enumeration. */
    EXPECT_EQ(rsp.count_code, 0u);
    EXPECT_EQ(rsp.subkey_count, static_cast<DWORD>(rsp.names.size()));

    /* The host registry still holds both sub keys. */
    HKEY key = nullptr;
    ASSERT_EQ(RegOpenKeyExW(real_key.get(), L"RealHidden", 0, KEY_READ, &key), ERROR_SUCCESS);
    RegCloseKey(key);
    ASSERT_EQ(RegOpenKeyExW(real_key.get(), L"RealVisible", 0, KEY_READ, &key), ERROR_SUCCESS);
    RegCloseKey(key);
}
