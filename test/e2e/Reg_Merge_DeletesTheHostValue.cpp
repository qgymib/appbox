#include "probe/RegDeleteValue.hpp"
#include "probe/RegReadValue.hpp"
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

/**
 * Condition:
 * 1. The key is marked `Merge`, the sandbox hive holds the key without the value
 *    `HostValue` and the real HKCU holds the key with the value.
 * 2. The sandboxed process deletes the value `HostValue` and reads the key back.
 *
 * Expected:
 * 1. The delete removes the value of the real registry: `Merge` is the write
 *    rule of the mode, and the sandbox does not hold the value, so the delete
 *    lands in the host instead of being recorded as a deletion of the sandbox.
 * 2. The read back inside the sandbox reports `File Not Found` and the value
 *    enumeration does not list the value.
 * 3. The value does not come back in the real registry.
 */
TEST_F(E2E_Reg, Merge_DeletesTheHostValue)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data")
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto subkey =
        L"Software\\AppBoxTest\\Merge_DeletesTheHostValue_" + appbox::UTF8ToWide(appbox::RandomString(8));
    const auto hive_key = L"HKEY_CURRENT_USER\\" + subkey;

    RealHkcuKey host(subkey);
    ASSERT_NE(host.get(), nullptr);
    ASSERT_TRUE(host.SetString(L"HostValue", L"host"));

    HiveBuilder builder(GetCWD());
    builder.EnsureKey(hive_key);
    builder.SetKeyIsolation(hive_key, appbox::RegistryKeyIsolation::Merge);

    std::string error;
    ASSERT_TRUE(builder.Write(error)) << error;

    {
        ProtocolRegDeleteValue::Req req;
        req.Key = appbox::WideToUTF8(subkey);
        req.Value = "HostValue";

        const auto rsp = Probe(GetCWD(), config).Call<ProtocolRegDeleteValue>(req);
        ASSERT_EQ(rsp.open_code, static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(rsp.delete_code, static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(rsp.query_code, static_cast<DWORD>(ERROR_FILE_NOT_FOUND));

        for (const auto& name : rsp.names)
        {
            EXPECT_NE(name, "HostValue");
        }
    }

    /* The delete removed the value of the real registry. */
    EXPECT_TRUE(host.GetString(L"HostValue").empty());

    {
        ProtocolRegReadValue::Req req;
        req.Key = appbox::WideToUTF8(subkey);
        req.Value = "HostValue";

        const auto rsp = Probe(GetCWD(), config).Call<ProtocolRegReadValue>(req);
        EXPECT_EQ(rsp.query_code, static_cast<DWORD>(ERROR_FILE_NOT_FOUND));
    }
}
