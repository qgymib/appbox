#include "probe/RegWriteValue.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "Random.hpp"
#include "WString.hpp"
#include <filesystem>

typedef appbox::test::CommonFixture Reg;
using namespace appbox::test;

/**
 * Condition:
 * 1. The key does not exist in the sandbox hive nor in the real registry.
 * 2. Create the key inside the sandbox, write a value and read it back.
 *
 * Expected:
 * 1. The read back inside the sandbox returns the written data (closed loop).
 * 2. The real HKCU does not contain the key afterwards.
 * 3. The overlay registry directory contains the hive file.
 */
TEST_F(Reg, WriteValue_NewKey)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"Upper")
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto       subkey = L"Software\\AppBoxTest\\WriteValue_NewKey_" + appbox::UTF8ToWide(appbox::RandomString(8));
    const std::string expected = "HelloAppBox";

    ProtocolRegWriteValue::Req req;
    req.Key   = appbox::WideToUTF8(subkey);
    req.Value = "TestValue";
    req.Data  = expected;

    auto rsp = ProbeRegWriteValue.Call(req, GetCWD(), config).get<ProtocolRegWriteValue::Rsp>();
    ASSERT_EQ(rsp.create_code, 0u);
    ASSERT_EQ(rsp.set_code, 0u);
    ASSERT_EQ(rsp.query_code, 0u);
    ASSERT_EQ(rsp.type, static_cast<DWORD>(REG_SZ));
    ASSERT_EQ(rsp.readback, expected);

    /* The real registry must not contain the key. */
    {
        HKEY key = nullptr;
        auto r   = RegOpenKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0, KEY_READ, &key);
        ASSERT_NE(r, ERROR_SUCCESS);
        if (r == ERROR_SUCCESS)
        {
            RegCloseKey(key);
        }
    }

    /* The hive file must exist in the overlay. */
    auto hive = GetCWD() / L"Upper" / L"registry" / L"user.hiv";
    ASSERT_TRUE(std::filesystem::exists(hive));
    ASSERT_GT(std::filesystem::file_size(hive), 0u);
}
