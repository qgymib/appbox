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
 * 1. The key exists in the real HKCU with the value `HostValue`, the sandbox
 *    hive does not hold it and the isolation file does not mention it, so the
 *    key keeps the default mode `WriteCopy`.
 * 2. Delete the key inside the sandbox.
 *
 * Expected:
 * 1. The delete succeeds, although the key only exists in the host registry:
 *    the sandbox records the key as deleted (a whiteout) instead of forwarding
 *    the call to the real registry.
 * 2. The key is gone from the view of the sandbox: the open reports
 *    `ERROR_FILE_NOT_FOUND`, so the read through does not resurrect it.
 * 3. The real registry still holds the key and its value.
 */
TEST_F(Reg, DeleteKey_WhiteoutHostKey)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"Upper")
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto subkey = L"Software\\AppBoxTest\\DeleteKey_WhiteoutHostKey_" + appbox::UTF8ToWide(appbox::RandomString(8));

    RealHkcuKey real_key(subkey);
    ASSERT_NE(real_key.get(), nullptr);
    ASSERT_TRUE(real_key.SetString(L"HostValue", L"host"));

    ProtocolRegDeleteKey::Req req;
    req.Key  = appbox::WideToUTF8(subkey);
    req.Mode = "reg";

    const auto rsp = ProbeRegDeleteKey.Call(req, GetCWD(), config).get<ProtocolRegDeleteKey::Rsp>();
    EXPECT_EQ(rsp.delete_code, static_cast<DWORD>(ERROR_SUCCESS));

    /* The view of the sandbox no longer holds the key, not even through the
     * read through of the host layer. */
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
