#include "probe/RegDeleteKey.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/RealHkcuKey.hpp"
#include "Random.hpp"
#include "WString.hpp"

typedef appbox::test::CommonFixture Reg;
using namespace appbox::test;

/**
 * Condition:
 * 1. The key exists in the real HKCU with the sub key `HostChild` and the value
 *    `HostValue`; the sandbox hive does not hold the key.
 * 2. Delete the key inside the sandbox.
 *
 * Expected:
 * 1. The delete is refused, because the sandbox sees the host sub key: the
 *    kernel reports `STATUS_CANNOT_DELETE`, which Win32 maps to
 *    `ERROR_ACCESS_DENIED`, and the key stays visible.
 * 2. After the sub key was deleted inside the sandbox (which is recorded as
 *    deleted), the delete of the key succeeds: the merged view of the key no
 *    longer holds a sub key.
 * 3. The real registry keeps the key, its sub key and its value.
 */
TEST_F(Reg, DeleteKey_NonEmpty)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"Upper")
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto subkey = L"Software\\AppBoxTest\\DeleteKey_NonEmpty_" + appbox::UTF8ToWide(appbox::RandomString(8));

    RealHkcuKey real_key(subkey);
    ASSERT_NE(real_key.get(), nullptr);
    ASSERT_TRUE(real_key.SetString(L"HostValue", L"host"));

    {
        HKEY child = nullptr;
        ASSERT_EQ(RegCreateKeyExW(real_key.get(), L"HostChild", 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &child, nullptr),
                  ERROR_SUCCESS);
        RegCloseKey(child);
    }

    /* The key which still holds a visible sub key cannot be deleted. */
    ProtocolRegDeleteKey::Req blocked;
    blocked.Key  = appbox::WideToUTF8(subkey);
    blocked.Mode = "reg";

    const auto blocked_rsp = ProbeRegDeleteKey.Call(blocked, GetCWD(), config).get<ProtocolRegDeleteKey::Rsp>();
    EXPECT_EQ(blocked_rsp.delete_code, static_cast<DWORD>(ERROR_ACCESS_DENIED));
    EXPECT_EQ(blocked_rsp.reopen_code, static_cast<DWORD>(ERROR_SUCCESS));

    /* The sub key of the host is deleted inside the sandbox. */
    ProtocolRegDeleteKey::Req child;
    child.Key  = appbox::WideToUTF8(subkey + L"\\HostChild");
    child.Mode = "reg";

    const auto child_rsp = ProbeRegDeleteKey.Call(child, GetCWD(), config).get<ProtocolRegDeleteKey::Rsp>();
    EXPECT_EQ(child_rsp.delete_code, static_cast<DWORD>(ERROR_SUCCESS));

    /* Now the key holds no visible sub key and the delete succeeds. */
    const auto deleted_rsp = ProbeRegDeleteKey.Call(blocked, GetCWD(), config).get<ProtocolRegDeleteKey::Rsp>();
    EXPECT_EQ(deleted_rsp.delete_code, static_cast<DWORD>(ERROR_SUCCESS));
    EXPECT_EQ(deleted_rsp.reopen_code, static_cast<DWORD>(ERROR_FILE_NOT_FOUND));

    /* The host registry keeps the key, its sub key and its value. */
    HKEY    key = nullptr;
    wchar_t buffer[64] = {};
    DWORD   size = sizeof(buffer);
    ASSERT_EQ(RegOpenKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0, KEY_QUERY_VALUE, &key), ERROR_SUCCESS);
    ASSERT_EQ(RegQueryValueExW(key, L"HostValue", nullptr, nullptr, reinterpret_cast<LPBYTE>(buffer), &size),
              ERROR_SUCCESS);
    RegCloseKey(key);
    EXPECT_EQ(appbox::WideToUTF8(buffer), "host");

    HKEY child_key = nullptr;
    ASSERT_EQ(RegOpenKeyExW(HKEY_CURRENT_USER, (subkey + L"\\HostChild").c_str(), 0, KEY_READ, &child_key),
              ERROR_SUCCESS);
    RegCloseKey(child_key);
}
