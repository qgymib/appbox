#include "probe/RegReadValue.hpp"
#include "probe/RegWriteValue.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/HiveBuilder.hpp"
#include "Random.hpp"
#include "WString.hpp"
#include <filesystem>
#include <string>
#include <vector>
#include <sddl.h>

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

/**
 * @brief Get the SID of the current user as a text.
 *
 * `HKEY_USERS` holds one sub key per user, which is named after the SID of that
 * user, so the SID is what a path through that root starts with.
 *
 * @return The SID, empty when it cannot be read.
 */
std::wstring CurrentUserSid()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
    {
        return std::wstring();
    }

    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);

    std::wstring      sid;
    std::vector<BYTE> buffer(size);
    if (size != 0 && GetTokenInformation(token, TokenUser, buffer.data(), size, &size))
    {
        const auto* user = reinterpret_cast<const TOKEN_USER*>(buffer.data());

        LPWSTR text = nullptr;
        if (ConvertSidToStringSidW(user->User.Sid, &text))
        {
            sid = text;
            LocalFree(text);
        }
    }

    CloseHandle(token);
    return sid;
}

} // namespace

/**
 * Condition:
 * 1. The isolation file of the sandbox carries no mode, so the sandbox hive is
 *    the only layer which holds the key of the case.
 * 2. The sandboxed process creates the key through `HKEY_USERS` and the SID of
 *    the current user, and reads it back through `HKEY_CURRENT_USER`.
 *
 * Expected:
 * 1. `HKEY_USERS\<SID>` and `HKEY_CURRENT_USER` name the same key of the view:
 *    the write is visible through the other root, so the two roots share the
 *    hive layer instead of the sandbox writing into one of the two.
 * 2. The write landed in the hive, the real registry does not hold the key.
 */
TEST_F(E2E_Reg, UsersRoot_MapsToTheCurrentUserHive)
{
    const auto sid = CurrentUserSid();
    ASSERT_FALSE(sid.empty()) << "the SID of the current user cannot be read";

    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"Upper")
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto subkey = L"Software\\AppBoxTest\\UsersRoot_" + appbox::UTF8ToWide(appbox::RandomString(8));

    RealKeyGuard guard{ subkey };

    HiveBuilder builder(GetCWD() / L"Upper");
    std::string error;
    ASSERT_TRUE(builder.Write(error)) << error;

    /* The write goes through the root of the other users. */
    {
        ProtocolRegWriteValue::Req req;
        req.Root = "HKEY_USERS";
        req.Key = appbox::WideToUTF8(sid + L"\\" + subkey);
        req.Value = "UsersValue";
        req.Data = "users";

        const auto rsp = ProbeRegWriteValue.Call(req, GetCWD(), config).get<ProtocolRegWriteValue::Rsp>();
        ASSERT_EQ(rsp.create_code, static_cast<DWORD>(ERROR_SUCCESS));
        ASSERT_EQ(rsp.set_code, static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(rsp.readback, "users");
    }

    /* The current user root answers the same key. */
    {
        ProtocolRegReadValue::Req req;
        req.Root = "HKEY_CURRENT_USER";
        req.Key = appbox::WideToUTF8(subkey);
        req.Value = "UsersValue";

        const auto rsp = ProbeRegReadValue.Call(req, GetCWD(), config).get<ProtocolRegReadValue::Rsp>();
        ASSERT_EQ(rsp.open_code, static_cast<DWORD>(ERROR_SUCCESS));
        ASSERT_EQ(rsp.query_code, static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(rsp.data, "users");
    }

    /* The key of the sandbox never reaches the real registry. */
    {
        HKEY key = nullptr;
        EXPECT_EQ(RegOpenKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0, KEY_READ, &key),
                  static_cast<LONG>(ERROR_FILE_NOT_FOUND));
    }
}
