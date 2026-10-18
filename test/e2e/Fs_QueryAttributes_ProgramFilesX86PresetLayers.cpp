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
 * 1. The resource tree holds a lower layer for the 64 bit `Program Files` folder,
 *    one for the 32 bit `Program Files (x86)` folder and one for the `Common`
 *    folder below the 32 bit one, each with a file of its own.
 * 2. The sandboxed process queries the attributes of every file in the folder of
 *    its own layer and in the folder of the other layers.
 *
 * Expected:
 * 1. Every file is found in the folder of its own layer, so each layer is mapped
 *    to the folder its layer key names.
 * 2. The 32 bit folder is a sibling of the 64 bit one and not a folder below it:
 *    a file of the 32 bit layer is not found in the 64 bit folder and a file of
 *    the 64 bit layer is not found in the 32 bit folder, although the two paths
 *    share the `C:\Program Files` prefix.
 * 3. The `Common` folder of the 32 bit folder is a folder inside the 32 bit
 *    layer: a file of the `Common` layer is not found in the 32 bit folder and a
 *    file of the 32 bit folder is not found in the `Common` folder.
 * 4. The resources of the application are untouched.
 */
TEST_F(E2E_Fs, QueryAttributes_ProgramFilesX86PresetLayers)
{
    const std::wstring programs_name = L"AppBoxE2E.ProgramFilesPreset.txt";
    const std::wstring programs_x86_name = L"AppBoxE2E.ProgramFilesX86Preset.txt";
    const std::wstring common_x86_name = L"AppBoxE2E.ProgramFilesCommonX86Preset.txt";

    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {}),
        FsDir(L"app", {
            FsDir(L"filesystem", {
                FsDir(L"#ProgramFiles#", {
                    FsFile(programs_name, "programs")
                }),
                FsDir(L"#ProgramFilesX86#", {
                    FsFile(programs_x86_name, "programs_x86")
                }),
                FsDir(L"#ProgramFilesCommonX86#", {
                    FsFile(common_x86_name, "common_x86")
                })
            })
        })
    });
    /* clang-format on */

    auto config = tree.Build();

    const std::wstring programs = GetKnownFolderPath(L"#ProgramFiles#", false);
    const std::wstring programs_x86 = GetKnownFolderPath(L"#ProgramFilesX86#", false);
    const std::wstring common_x86 = GetKnownFolderPath(L"#ProgramFilesCommonX86#", false);

    /* The layer of the 64 bit folder is mapped to the 64 bit folder. */
    const auto in_programs = QueryAttributes(programs + L"\\" + programs_name, GetCWD(), config);
    ASSERT_EQ(in_programs.code, static_cast<DWORD>(0));
    EXPECT_NE(in_programs.attributes & FILE_ATTRIBUTE_DIRECTORY, static_cast<DWORD>(FILE_ATTRIBUTE_DIRECTORY));

    /* The layer of the 32 bit folder is mapped to the 32 bit folder. */
    const auto in_programs_x86 = QueryAttributes(programs_x86 + L"\\" + programs_x86_name, GetCWD(), config);
    ASSERT_EQ(in_programs_x86.code, static_cast<DWORD>(0));
    EXPECT_NE(in_programs_x86.attributes & FILE_ATTRIBUTE_DIRECTORY, static_cast<DWORD>(FILE_ATTRIBUTE_DIRECTORY));

    /* The layer of the `Common` folder of the 32 bit folder is mapped to it. */
    const auto in_common_x86 = QueryAttributes(common_x86 + L"\\" + common_x86_name, GetCWD(), config);
    ASSERT_EQ(in_common_x86.code, static_cast<DWORD>(0));
    EXPECT_NE(in_common_x86.attributes & FILE_ATTRIBUTE_DIRECTORY, static_cast<DWORD>(FILE_ATTRIBUTE_DIRECTORY));

    /*
     * The 32 bit folder is a sibling of the 64 bit one, so a file of one layer
     * is not a file of the other one although the two share their prefix.
     */
    const auto programs_x86_in_programs = QueryAttributes(programs + L"\\" + programs_x86_name, GetCWD(), config);
    EXPECT_EQ(programs_x86_in_programs.attributes, static_cast<DWORD>(INVALID_FILE_ATTRIBUTES));
    EXPECT_EQ(programs_x86_in_programs.code, static_cast<DWORD>(ERROR_FILE_NOT_FOUND));

    const auto programs_in_programs_x86 = QueryAttributes(programs_x86 + L"\\" + programs_name, GetCWD(), config);
    EXPECT_EQ(programs_in_programs_x86.attributes, static_cast<DWORD>(INVALID_FILE_ATTRIBUTES));
    EXPECT_EQ(programs_in_programs_x86.code, static_cast<DWORD>(ERROR_FILE_NOT_FOUND));

    /* The `Common` folder is a folder inside the 32 bit layer. */
    const auto common_x86_in_programs_x86 = QueryAttributes(programs_x86 + L"\\" + common_x86_name, GetCWD(), config);
    EXPECT_EQ(common_x86_in_programs_x86.attributes, static_cast<DWORD>(INVALID_FILE_ATTRIBUTES));
    EXPECT_EQ(common_x86_in_programs_x86.code, static_cast<DWORD>(ERROR_FILE_NOT_FOUND));

    const auto programs_x86_in_common_x86 = QueryAttributes(common_x86 + L"\\" + programs_x86_name, GetCWD(), config);
    EXPECT_EQ(programs_x86_in_common_x86.attributes, static_cast<DWORD>(INVALID_FILE_ATTRIBUTES));
    EXPECT_EQ(programs_x86_in_common_x86.code, static_cast<DWORD>(ERROR_FILE_NOT_FOUND));

    /* Verify that the resources of the application are untouched. */
    ASSERT_TRUE(tree.Verify());
}
