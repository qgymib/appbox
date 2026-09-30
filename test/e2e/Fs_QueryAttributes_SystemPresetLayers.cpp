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
 * 1. The resource tree holds a lower layer for the `Windows` folder and one for
 *    the `System32` folder of the system, each with a file of its own.
 * 2. The sandboxed process queries the attributes of both files in the folder
 *    of their own layer and in the folder of the other one.
 *
 * Expected:
 * 1. Every file is found in the folder of its own layer, so each layer is
 *    mapped to the folder its layer key names.
 * 2. A file of a layer is not found in the folder of the other layer, so the
 *    `Windows` layer is mapped to the Windows folder itself instead of its
 *    system subdirectory and the `System32` layer is mapped to the system
 *    subdirectory instead of the Windows folder.
 * 3. The resources of the application are untouched.
 */
TEST_F(E2E_Fs, QueryAttributes_SystemPresetLayers)
{
    const std::wstring windows_name = L"AppBoxE2E.WindowsPreset.txt";
    const std::wstring system32_name = L"AppBoxE2E.System32Preset.txt";

    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {}),
        FsDir(L"app", {
            FsDir(L"filesystem", {
                FsDir(L"#Windows#", {
                    FsFile(windows_name, "windows")
                }),
                FsDir(L"#System32#", {
                    FsFile(system32_name, "system32")
                })
            })
        })
    });
    /* clang-format on */

    auto config = tree.Build();

    const std::wstring windows = GetKnownFolderPath(L"#Windows#", false);
    const std::wstring system32 = GetKnownFolderPath(L"#System32#", false);

    /* The layer of the Windows folder is mapped to the Windows folder. */
    const auto in_windows = QueryAttributes(windows + L"\\" + windows_name, GetCWD(), config);
    ASSERT_EQ(in_windows.code, static_cast<DWORD>(0));
    EXPECT_NE(in_windows.attributes & FILE_ATTRIBUTE_DIRECTORY, static_cast<DWORD>(FILE_ATTRIBUTE_DIRECTORY));

    /* The layer of the system folder is mapped to the system folder. */
    const auto in_system32 = QueryAttributes(system32 + L"\\" + system32_name, GetCWD(), config);
    ASSERT_EQ(in_system32.code, static_cast<DWORD>(0));
    EXPECT_NE(in_system32.attributes & FILE_ATTRIBUTE_DIRECTORY, static_cast<DWORD>(FILE_ATTRIBUTE_DIRECTORY));

    /* The file of the Windows layer is not a file of the system subdirectory. */
    const auto windows_in_system32 = QueryAttributes(system32 + L"\\" + windows_name, GetCWD(), config);
    EXPECT_EQ(windows_in_system32.attributes, static_cast<DWORD>(INVALID_FILE_ATTRIBUTES));
    EXPECT_EQ(windows_in_system32.code, static_cast<DWORD>(ERROR_FILE_NOT_FOUND));

    /* The file of the system layer is not a file of the Windows folder. */
    const auto system32_in_windows = QueryAttributes(windows + L"\\" + system32_name, GetCWD(), config);
    EXPECT_EQ(system32_in_windows.attributes, static_cast<DWORD>(INVALID_FILE_ATTRIBUTES));
    EXPECT_EQ(system32_in_windows.code, static_cast<DWORD>(ERROR_FILE_NOT_FOUND));

    /* Verify that the resources of the application are untouched. */
    ASSERT_TRUE(tree.Verify());
}
