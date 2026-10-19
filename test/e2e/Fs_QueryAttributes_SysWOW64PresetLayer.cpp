#include "probe/QueryAttributes.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/TestKnownFolder.hpp"
#include "WString.hpp"
#include <filesystem>
#include <string>

typedef appbox::test::CommonFixture E2E_Fs;
using namespace appbox::test;

namespace
{

/**
 * @brief Query the attributes of one path inside the sandbox.
 *
 * The path is a view path of the sandbox: the case spells it with the real
 * folder of a layer, so the answer tells which layer the folder is mapped to.
 *
 * @param[in] path Path to query.
 * @param[in] cwd Working directory of the case.
 * @param[in] config Configuration of the case.
 * @return The answer of the sandboxed process.
 */
ProtocolQueryAttributes::Rsp QueryAttributes(const std::wstring& path, const std::filesystem::path& cwd,
                                             appbox::LauncherConfig& config)
{
    ProtocolQueryAttributes::Req req;
    req.FileName = appbox::WideToUTF8(path);

    return ProbeQueryAttributes.Call(req, cwd, config).get<ProtocolQueryAttributes::Rsp>();
}

} // namespace

/**
 * Condition:
 * 1. The resource tree holds a lower layer for the `System32` folder of the
 *    preset directories and one for the `SysWOW64` folder, each with a file of
 *    its own.
 * 2. The sandboxed process queries the attributes of every file in the folder
 *    of its own layer and in the folder of the other layer.
 *
 * Expected:
 * 1. Every file is found in the folder of its own layer, so the layer key
 *    `#SystemX86#` is mapped to `%windir%\SysWOW64` and `#System32#` to
 *    `%windir%\system32`.
 * 2. The two folders are told apart: a file of one layer is not found in the
 *    folder of the other one, so the 32 bit system folder is a sibling of the
 *    64 bit one and not a folder below it.
 * 3. The resources of the application are untouched.
 */
TEST_F(E2E_Fs, QueryAttributes_SysWOW64PresetLayer)
{
    const std::wstring system_name = L"AppBoxE2E.System32Preset.txt";
    const std::wstring syswow64_name = L"AppBoxE2E.SystemX86Preset.txt";

    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {}),
        FsDir(L"app", {
            FsDir(L"filesystem", {
                FsDir(L"#System32#", {
                    FsFile(system_name, "system32")
                }),
                FsDir(L"#SystemX86#", {
                    FsFile(syswow64_name, "syswow64")
                })
            })
        })
    });
    /* clang-format on */

    auto config = tree.Build();

    const std::wstring system32 = GetKnownFolderPath(L"#System32#", false);
    const std::wstring system_x86 = GetKnownFolderPath(L"#SystemX86#", false);

    /* The layer of `#System32#` is mapped to the 64 bit system folder. */
    const auto in_system32 = QueryAttributes(system32 + L"\\" + system_name, GetCWD(), config);
    ASSERT_EQ(in_system32.code, static_cast<DWORD>(0));
    EXPECT_NE(in_system32.attributes & FILE_ATTRIBUTE_DIRECTORY, static_cast<DWORD>(FILE_ATTRIBUTE_DIRECTORY));

    /* The layer of `#SystemX86#` is mapped to the 32 bit system folder. */
    const auto in_system_x86 = QueryAttributes(system_x86 + L"\\" + syswow64_name, GetCWD(), config);
    ASSERT_EQ(in_system_x86.code, static_cast<DWORD>(0));
    EXPECT_NE(in_system_x86.attributes & FILE_ATTRIBUTE_DIRECTORY, static_cast<DWORD>(FILE_ATTRIBUTE_DIRECTORY));

    /* The two folders are siblings, so neither layer leaks into the other one. */
    const auto syswow64_in_system32 = QueryAttributes(system32 + L"\\" + syswow64_name, GetCWD(), config);
    EXPECT_EQ(syswow64_in_system32.attributes, static_cast<DWORD>(INVALID_FILE_ATTRIBUTES));
    EXPECT_EQ(syswow64_in_system32.code, static_cast<DWORD>(ERROR_FILE_NOT_FOUND));

    const auto system32_in_system_x86 = QueryAttributes(system_x86 + L"\\" + system_name, GetCWD(), config);
    EXPECT_EQ(system32_in_system_x86.attributes, static_cast<DWORD>(INVALID_FILE_ATTRIBUTES));
    EXPECT_EQ(system32_in_system_x86.code, static_cast<DWORD>(ERROR_FILE_NOT_FOUND));

    /* Verify that the resources of the application are untouched. */
    ASSERT_TRUE(tree.Verify());
}
