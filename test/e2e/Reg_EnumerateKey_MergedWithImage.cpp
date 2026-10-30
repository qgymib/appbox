#include "probe/RegEnumKey.hpp"
#include "probe/RegWriteValue.hpp"
#include "utils/ProbeCall.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/HiveBuilder.hpp"
#include "Random.hpp"
#include "WString.hpp"
#include <algorithm>
#include <string>

typedef appbox::test::CommonFixture E2E_Reg;
using namespace appbox::test;

/**
 * Condition:
 * 1. The image of the run (the registry of the archive) holds the parent key
 *    with the sub keys `Alpha` and `Beta`.
 * 2. The sandboxed process creates the sub key `Gamma` below the parent, which
 *    copies the parent up into the state hive.
 * 3. The sandboxed process enumerates the sub keys of the parent.
 *
 * Expected:
 * 1. The enumeration reports the three names, because the sub keys of the image
 *    and of the state hive are merged.
 * 2. The count of the key reports three sub keys as well, so a caller which
 *    allocates based on it holds every entry.
 */
TEST_F(E2E_Reg, EnumerateKey_MergedWithImage)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data")
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto parent = L"Software\\AppBoxTest\\EnumKey_Image_" + appbox::UTF8ToWide(appbox::RandomString(8));
    const auto hive_parent = L"HKEY_CURRENT_USER\\" + parent;

    HiveBuilder builder(GetCWD());
    builder.EnsureKey(hive_parent + L"\\Alpha");
    builder.EnsureKey(hive_parent + L"\\Beta");

    std::string error;
    ASSERT_TRUE(builder.Write(error)) << error;

    /* The create of the sandbox adds the third sub key. */
    {
        ProtocolRegWriteValue::Req req;
        req.Key = appbox::WideToUTF8(parent + L"\\Gamma");
        req.Value = "Value";
        req.Data = "gamma";

        const auto rsp = Probe(GetCWD(), config).Call<ProtocolRegWriteValue>(req);
        ASSERT_EQ(rsp.create_code, 0u);
        ASSERT_EQ(rsp.set_code, 0u);
    }

    {
        ProtocolRegEnumKey::Req req;
        req.Key = appbox::WideToUTF8(parent);

        const auto rsp = Probe(GetCWD(), config).Call<ProtocolRegEnumKey>(req);
        ASSERT_EQ(rsp.open_code, static_cast<DWORD>(ERROR_SUCCESS));
        ASSERT_EQ(rsp.count_code, static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(rsp.subkey_count, 3u);
        EXPECT_EQ(rsp.enum_code, 0u);

        for (const char* name : { "Alpha", "Beta", "Gamma" })
        {
            EXPECT_NE(std::find(rsp.names.begin(), rsp.names.end(), name), rsp.names.end()) << name;
        }
    }
}
