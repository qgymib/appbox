#include "probe/RegReadValue.hpp"
#include "probe/RegWriteValue.hpp"
#include "utils/ProbeCall.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/HiveBuilder.hpp"
#include "utils/RealHkcuKey.hpp"
#include "Random.hpp"
#include "WString.hpp"
#include <filesystem>
#include <string>

typedef appbox::test::CommonFixture E2E_Reg;
using namespace appbox::test;

namespace
{

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

} // namespace

/**
 * Condition:
 * 1. The key is marked `Merge` and the sandbox hive holds it, so the sandbox
 *    holds the key while the sub key `Child` exists in neither layer.
 * 2. The sandboxed process creates the sub key `Child` with the value `ChildValue`.
 *
 * Expected:
 * 1. The sub key is created in the real registry: `Merge` is the write rule of
 *    the mode, and a sub key the sandbox does not hold is created in the host
 *    instead of the hive.
 * 2. The sandbox reads the value back through the merged view, which shows the
 *    entries of the host below a merged key.
 * 3. The key of the hive keeps its own value, and the sub key of the host is the
 *    only layer which holds `Child`.
 */
TEST_F(E2E_Reg, Merge_CreatesTheSubKeyInTheHost)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data")
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto subkey = L"Software\\AppBoxTest\\Merge_CreatesTheSubKey_" + appbox::UTF8ToWide(appbox::RandomString(8));
    const auto hive_key = L"HKEY_CURRENT_USER\\" + subkey;
    const auto child = subkey + L"\\Child";

    RealKeyGuard guard{ subkey };

    HiveBuilder builder(GetCWD());
    builder.EnsureKey(hive_key);
    builder.SetKeyIsolation(hive_key, appbox::RegistryKeyIsolation::Merge);

    std::string error;
    ASSERT_TRUE(builder.Write(error)) << error;

    {
        ProtocolRegWriteValue::Req req;
        req.Key = appbox::WideToUTF8(child);
        req.Value = "ChildValue";
        req.Data = "child";

        const auto rsp = Probe(GetCWD(), config).Call<ProtocolRegWriteValue>(req);
        ASSERT_EQ(rsp.create_code, static_cast<DWORD>(ERROR_SUCCESS));
        ASSERT_EQ(rsp.set_code, static_cast<DWORD>(ERROR_SUCCESS));
        ASSERT_EQ(rsp.query_code, static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(rsp.readback, "child");
    }

    /* The sub key was created in the real registry. */
    {
        HKEY key = nullptr;
        ASSERT_EQ(RegOpenKeyExW(HKEY_CURRENT_USER, child.c_str(), 0, KEY_QUERY_VALUE, &key),
                  static_cast<LONG>(ERROR_SUCCESS));

        wchar_t    buffer[64] = {};
        DWORD      size = sizeof(buffer);
        const LONG query =
            RegQueryValueExW(key, L"ChildValue", nullptr, nullptr, reinterpret_cast<LPBYTE>(buffer), &size);
        RegCloseKey(key);

        ASSERT_EQ(query, static_cast<LONG>(ERROR_SUCCESS));
        EXPECT_EQ(appbox::WideToUTF8(buffer), "child");
    }

    /* The merged view reports the sub key of the host. */
    {
        ProtocolRegReadValue::Req req;
        req.Key = appbox::WideToUTF8(child);
        req.Value = "ChildValue";

        const auto rsp = Probe(GetCWD(), config).Call<ProtocolRegReadValue>(req);
        ASSERT_EQ(rsp.open_code, static_cast<DWORD>(ERROR_SUCCESS));
        ASSERT_EQ(rsp.query_code, static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(rsp.data, "child");
    }
}
