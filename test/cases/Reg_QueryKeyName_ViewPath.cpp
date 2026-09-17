#include "probe/RegQueryKeyName.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "Random.hpp"
#include "WString.hpp"
#include <string>

typedef appbox::test::CommonFixture Reg;
using namespace appbox::test;

/**
 * @brief Whether the string starts with the given prefix.
 */
static bool StartsWith(const std::string& text, const std::string& prefix)
{
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

/**
 * @brief Whether the string ends with the given suffix.
 */
static bool EndsWith(const std::string& text, const std::string& suffix)
{
    return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

/**
 * Condition:
 * 1. A key is created inside the sandbox, so the key handle refers to a key
 *    below the private hive mount (\REGISTRY\A\{GUID}).
 * 2. The key names of NtQueryKey(KeyNameInformation) and
 *    NtQueryObject(ObjectNameInformation) are queried through the handle.
 *
 * Expected:
 * 1. Both names address the logical view path
 *    \REGISTRY\USER\<SID>\Software\AppBoxTest\... instead of the private hive
 *    mount, so the sandboxed process cannot observe the mount.
 * 2. Neither name contains the private mount prefix \REGISTRY\A\.
 */
TEST_F(Reg, QueryKeyName_ViewPath)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"Upper")
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto subkey = L"Software\\AppBoxTest\\QueryKeyName_ViewPath_" + appbox::UTF8ToWide(appbox::RandomString(8));

    ProtocolRegQueryKeyName::Req req;
    req.Key = appbox::WideToUTF8(subkey);

    auto rsp = ProbeRegQueryKeyName.Call(req, GetCWD(), config).get<ProtocolRegQueryKeyName::Rsp>();
    ASSERT_EQ(rsp.create_code, 0u);
    ASSERT_EQ(rsp.query_key_code, 0u);
    ASSERT_EQ(rsp.query_object_code, 0u);

    const auto suffix = appbox::WideToUTF8(subkey);

    ASSERT_TRUE(StartsWith(rsp.key_name, "\\REGISTRY\\USER\\")) << rsp.key_name;
    ASSERT_TRUE(EndsWith(rsp.key_name, suffix)) << rsp.key_name;
    ASSERT_EQ(rsp.key_name.find("\\REGISTRY\\A\\"), std::string::npos) << rsp.key_name;

    ASSERT_TRUE(StartsWith(rsp.object_name, "\\REGISTRY\\USER\\")) << rsp.object_name;
    ASSERT_TRUE(EndsWith(rsp.object_name, suffix)) << rsp.object_name;
    ASSERT_EQ(rsp.object_name.find("\\REGISTRY\\A\\"), std::string::npos) << rsp.object_name;
}
