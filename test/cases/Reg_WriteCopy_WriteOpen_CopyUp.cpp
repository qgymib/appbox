#include "probe/RegOpenWriteValue.hpp"
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
 *    hive does not hold the key and the isolation file does not mention it, so
 *    the key keeps the default mode `WriteCopy`.
 * 2. Open the key with write access and write a value inside the sandbox.
 *
 * Expected:
 * 1. The open succeeds: the key is copied up into the sandbox hive instead of
 *    being answered by a real key handle.
 * 2. The write lands in the hive, so the host key does not receive the value
 *    and keeps its own one.
 * 3. A write access open of a key which neither layer holds still reports that
 *    the key does not exist: an open never creates a key.
 */
TEST_F(Reg, WriteCopy_WriteOpen_CopyUp)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"Upper")
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto        subkey = L"Software\\AppBoxTest\\WriteCopy_WriteOpen_CopyUp_"
                        + appbox::UTF8ToWide(appbox::RandomString(8));
    const std::string expected = "CopiedUp";

    RealHkcuKey real_key(subkey);
    ASSERT_NE(real_key.get(), nullptr);
    ASSERT_TRUE(real_key.SetString(L"HostValue", L"host"));

    /* No isolation file entry: `WriteCopy` is the default mode of every key. */

    ProtocolRegOpenWriteValue::Req req;
    req.Key   = appbox::WideToUTF8(subkey);
    req.Value = "TestValue";
    req.Data  = expected;

    const auto rsp = ProbeRegOpenWriteValue.Call(req, GetCWD(), config).get<ProtocolRegOpenWriteValue::Rsp>();
    ASSERT_EQ(rsp.open_code, 0u);
    ASSERT_EQ(rsp.set_code, 0u);
    ASSERT_EQ(rsp.query_code, 0u);
    EXPECT_EQ(rsp.readback, expected);

    /* The host key did not receive the value of the sandbox and kept its own. */
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

    /* An open never creates a key, not even with write access. */
    ProtocolRegOpenWriteValue::Req missing;
    missing.Key   = appbox::WideToUTF8(L"Software\\AppBoxTest\\WriteCopy_Missing_"
                                     + appbox::UTF8ToWide(appbox::RandomString(8)));
    missing.Value = "TestValue";
    missing.Data  = expected;

    const auto missing_rsp =
        ProbeRegOpenWriteValue.Call(missing, GetCWD(), config).get<ProtocolRegOpenWriteValue::Rsp>();
    EXPECT_EQ(missing_rsp.open_code, static_cast<DWORD>(ERROR_FILE_NOT_FOUND));
}
