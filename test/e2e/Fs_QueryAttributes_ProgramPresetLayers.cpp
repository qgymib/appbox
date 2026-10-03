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
 * 1. The resource tree holds a lower layer for the `Program Data` folder of the
 *    system and one for the `Common` folder below `Program Files`, each with a
 *    file of its own.
 * 2. The sandboxed process queries the attributes of both files in the folder
 *    of their own layer and in the folder of the other one.
 *
 * Expected:
 * 1. Every file is found in the folder of its own layer, so each layer is
 *    mapped to the folder its layer key names.
 * 2. A file of a layer is not found in the folder of the other layer, so the
 *    `Program Data` layer is mapped to the real Program Data folder of the
 *    machine and the `Common` layer to the `Common Files` folder of the
 *    `Program Files` folder instead of the other one.
 * 3. The resources of the application are untouched.
 */
TEST_F(E2E_Fs, QueryAttributes_ProgramPresetLayers)
{
    const std::wstring program_data_name = L"AppBoxE2E.ProgramDataPreset.txt";
    const std::wstring common_name = L"AppBoxE2E.ProgramFilesCommonPreset.txt";

    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {}),
        FsDir(L"app", {
            FsDir(L"filesystem", {
                FsDir(L"#ProgramData#", {
                    FsFile(program_data_name, "program_data")
                }),
                FsDir(L"#ProgramFilesCommon#", {
                    FsFile(common_name, "common")
                })
            })
        })
    });
    /* clang-format on */

    auto config = tree.Build();

    const std::wstring program_data = GetKnownFolderPath(L"#ProgramData#", false);
    const std::wstring common = GetKnownFolderPath(L"#ProgramFilesCommon#", false);

    /* The layer of the Program Data folder is mapped to the Program Data folder. */
    const auto in_program_data = QueryAttributes(program_data + L"\\" + program_data_name, GetCWD(), config);
    ASSERT_EQ(in_program_data.code, static_cast<DWORD>(0));
    EXPECT_NE(in_program_data.attributes & FILE_ATTRIBUTE_DIRECTORY, static_cast<DWORD>(FILE_ATTRIBUTE_DIRECTORY));

    /* The layer of the Common folder is mapped to the Common Files folder. */
    const auto in_common = QueryAttributes(common + L"\\" + common_name, GetCWD(), config);
    ASSERT_EQ(in_common.code, static_cast<DWORD>(0));
    EXPECT_NE(in_common.attributes & FILE_ATTRIBUTE_DIRECTORY, static_cast<DWORD>(FILE_ATTRIBUTE_DIRECTORY));

    /* The file of the Program Data layer is not a file of the Common folder. */
    const auto program_data_in_common = QueryAttributes(common + L"\\" + program_data_name, GetCWD(), config);
    EXPECT_EQ(program_data_in_common.attributes, static_cast<DWORD>(INVALID_FILE_ATTRIBUTES));
    EXPECT_EQ(program_data_in_common.code, static_cast<DWORD>(ERROR_FILE_NOT_FOUND));

    /* The file of the Common layer is not a file of the Program Data folder. */
    const auto common_in_program_data = QueryAttributes(program_data + L"\\" + common_name, GetCWD(), config);
    EXPECT_EQ(common_in_program_data.attributes, static_cast<DWORD>(INVALID_FILE_ATTRIBUTES));
    EXPECT_EQ(common_in_program_data.code, static_cast<DWORD>(ERROR_FILE_NOT_FOUND));

    /* Verify that the resources of the application are untouched. */
    ASSERT_TRUE(tree.Verify());
}
