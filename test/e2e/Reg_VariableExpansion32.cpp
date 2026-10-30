#include "probe/RegReadValues.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/HiveBuilder.hpp"
#include "utils/ProbeCall.hpp"
#include "utils/TestKnownFolder.hpp"
#include "WString.hpp"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

typedef appbox::test::CommonFixture E2E_Reg;
using namespace appbox::test;

namespace
{

/**
 * @brief Build the raw data of a `REG_SZ` value.
 * @param[in] text Text of the value.
 * @return The UTF-16 data with its trailing terminator.
 */
std::vector<BYTE> StringData(const std::wstring& text)
{
    std::vector<BYTE> data((text.size() + 1) * sizeof(wchar_t), 0);
    memcpy(data.data(), text.c_str(), text.size() * sizeof(wchar_t));
    return data;
}

} // namespace

/**
 * Condition:
 * 1. The packed hive holds `REG_SZ` values which reference the folders the
 *    operating system shows to a 32 bit process.
 * 2. The sandboxed application is the 32 bit probe, so the hive is expanded
 *    while the module of a 32 bit process mounts it.
 *
 * Expected:
 * 1. `%APPBOX:ProgramFiles%` is `C:\Program Files (x86)` and
 *    `%APPBOX:ProgramFilesCommon%` is `C:\Program Files (x86)\Common Files`.
 * 2. `%APPBOX:ProgramFilesX64%` keeps its own spelling, because the folder is
 *    not supported for a 32 bit process, while
 *    `%APPBOX:ProgramFilesCommonX64%` names the 64 bit `Common Files` folder.
 * 3. `%APPBOX:SystemX86%` names `%windir%\SysWOW64` like it does in a 64 bit
 *    process, so the 32 bit system folder is reachable from both kinds of
 *    process.
 */
TEST_F(E2E_Reg, VariableExpansion32)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"app", { FsDir(L"filesystem", {}) })
    });
    /* clang-format on */

    const auto config = tree.Build();

    const std::wstring programs_x86 = GetKnownFolderPath(L"#ProgramFilesX86#", false);
    const std::wstring common_x86 = GetKnownFolderPath(L"#ProgramFilesCommonX86#", false);
    const std::wstring common_x64 = GetKnownFolderPath(L"#ProgramFilesCommonX64#", false);
    const std::wstring system_x86 = GetKnownFolderPath(L"#SystemX86#", false);

    const std::wstring subkey = L"Software\\AppBoxTest\\VariableExpansion32";
    const std::wstring key = L"HKEY_CURRENT_USER\\" + subkey;

    HiveBuilder builder(GetCWD());
    builder.SetValue(key, L"ProgramFiles", REG_SZ, StringData(L"%APPBOX:ProgramFiles%\\Foo"));
    builder.SetValue(key, L"ProgramFilesX64", REG_SZ, StringData(L"%APPBOX:ProgramFilesX64%\\Foo"));
    builder.SetValue(key, L"ProgramFilesCommon", REG_SZ, StringData(L"%APPBOX:ProgramFilesCommon%\\Foo"));
    builder.SetValue(key, L"ProgramFilesCommonX64", REG_SZ, StringData(L"%APPBOX:ProgramFilesCommonX64%\\Foo"));
    builder.SetValue(key, L"SystemX86", REG_SZ, StringData(L"%APPBOX:SystemX86%\\Foo"));
    builder.SetKeyIsolation(key, appbox::RegistryKeyIsolation::Full);

    std::string error;
    ASSERT_TRUE(builder.Write(error)) << error;

    ProtocolRegReadValues::Req req;
    req.Key = appbox::WideToUTF8(subkey);
    req.Values = { "ProgramFiles", "ProgramFilesX64", "ProgramFilesCommon", "ProgramFilesCommonX64", "SystemX86" };

    const auto rsp = Probe(GetCWD(), config, ProbeBitness::Bit32).Call<ProtocolRegReadValues>(req);
    ASSERT_EQ(rsp.values.size(), req.Values.size());

    for (const auto& answer : rsp.values)
    {
        ASSERT_EQ(answer.open_code, static_cast<DWORD>(ERROR_SUCCESS));
        ASSERT_EQ(answer.query_code, static_cast<DWORD>(ERROR_SUCCESS));
    }

    /* The folders a 32 bit process sees. */
    EXPECT_EQ(rsp.values[0].text, appbox::WideToUTF8(programs_x86 + L"\\Foo"));
    EXPECT_EQ(rsp.values[2].text, appbox::WideToUTF8(common_x86 + L"\\Foo"));

    /* A folder which does not exist for a 32 bit process keeps the reference. */
    EXPECT_EQ(rsp.values[1].text, "%APPBOX:ProgramFilesX64%\\Foo");

    /* The 64 bit `Common Files` folder is reachable from a 32 bit process. */
    EXPECT_EQ(rsp.values[3].text, appbox::WideToUTF8(common_x64 + L"\\Foo"));

    /* The 32 bit system folder is the same in both views. */
    EXPECT_EQ(rsp.values[4].text, appbox::WideToUTF8(system_x86 + L"\\Foo"));

    ASSERT_TRUE(tree.Verify());
}
