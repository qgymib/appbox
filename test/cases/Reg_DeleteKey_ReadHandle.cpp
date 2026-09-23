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
 * 1. The key exists in the real HKCU with the value `HostValue`; the sandbox
 *    hive does not hold the key, so a read access open is answered by the host
 *    layer (the read through).
 * 2. Call `NtDeleteKey` on that read through handle inside the sandbox.
 *
 * Expected:
 * 1. The call is refused with `STATUS_ACCESS_DENIED`: the handle is a handle of
 *    the host layer, which the isolation never lets a delete reach, and the
 *    kernel would refuse the call as well because the handle has no right to
 *    delete.
 * 2. The key stays visible inside the sandbox and the real registry keeps the
 *    key and its value.
 */
TEST_F(Reg, DeleteKey_ReadHandle)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"Upper")
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto subkey = L"Software\\AppBoxTest\\DeleteKey_ReadHandle_" + appbox::UTF8ToWide(appbox::RandomString(8));

    RealHkcuKey real_key(subkey);
    ASSERT_NE(real_key.get(), nullptr);
    ASSERT_TRUE(real_key.SetString(L"HostValue", L"host"));

    ProtocolRegDeleteKey::Req req;
    req.Key  = appbox::WideToUTF8(subkey);
    req.Mode = "read_handle";

    const auto rsp = ProbeRegDeleteKey.Call(req, GetCWD(), config).get<ProtocolRegDeleteKey::Rsp>();
    ASSERT_EQ(rsp.open_code, static_cast<DWORD>(ERROR_SUCCESS));
    EXPECT_EQ(rsp.delete_code, static_cast<DWORD>(STATUS_ACCESS_DENIED));

    /* The view still holds the key and the value of the host. */
    EXPECT_EQ(rsp.reopen_code, static_cast<DWORD>(ERROR_SUCCESS));
    EXPECT_EQ(rsp.query_code, static_cast<DWORD>(ERROR_SUCCESS));
    EXPECT_EQ(rsp.readback, "host");

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
