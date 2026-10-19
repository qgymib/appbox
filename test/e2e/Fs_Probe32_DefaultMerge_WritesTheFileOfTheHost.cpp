#include "probe/WriteFile.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/ProbePath.hpp"
#include "utils/ReadFileFull.hpp"
#include "utils/RealFsFolder.hpp"
#include "utils/TestKnownFolder.hpp"
#include "WString.hpp"
#include "tracer/PeImage.hpp"
#include <filesystem>
#include <string>

typedef appbox::test::CommonFixture E2E_Fs;
using namespace appbox::test;

namespace
{

/** Name of the folder of this case below `#USERPROFILE#`. */
constexpr wchar_t kFolderName[] = L"AppBoxTest_Probe32DefaultMergeWrite";

/**
 * @brief Get the path of the folder of the case inside the view.
 * @return Path of the folder.
 */
std::wstring FolderPath()
{
    return GetKnownFolderPath(L"#USERPROFILE#", false) + L"\\" + kFolderName;
}

/**
 * @brief Get the path of the folder of the case inside the overlay.
 * @param[in] cwd Working directory of the case.
 * @return The path of the folder in the overlay.
 */
std::wstring FolderInOverlay(const std::wstring& cwd)
{
    return cwd + L"\\data\\filesystem\\" + GetKnownFolderPath(L"#USERPROFILE#", true) + L"\\" + kFolderName;
}

} // namespace

/**
 * Condition:
 * 1. The operations run in the 32 bit probe: this case is the case
 *    `Fs_DefaultMerge_WritesTheFileOfTheHost` with the bit size of the probe
 *    changed, so the hooks of the 32 bit sandbox module are the ones which
 *    serve the write. The executable is checked to be a 32 bit image, so a run
 *    which was given the wrong file for `--probe32` cannot pass the case.
 * 2. No isolation file of the case lists an entry for the file, so the mode of
 *    the path is the default of the view, which is `Merge`.
 * 3. The host holds the file `data.txt`; the sandboxed process opens it for
 *    writing and writes another content into it.
 *
 * Expected:
 * 1. The write of the 32 bit process reaches the file of the host filesystem,
 *    which carries the new content afterwards.
 * 2. The overlay holds no copy of the file, so the state of the sandbox stays
 *    empty and the resources of the application stay untouched.
 */
TEST_F(E2E_Fs, Probe32_DefaultMerge_WritesTheFileOfTheHost)
{
    ASSERT_TRUE(appbox::tracer::PeImage::FromFile(ProbePath(ProbeBitness::Bit32)).Is32Bit())
        << "the probe of the run is not a 32 bit image: pass the 32 bit probe as --probe32=";

    RealFsFolder host(L"#USERPROFILE#", kFolderName);
    ASSERT_TRUE(host.WriteFile(L"data.txt", "host"));

    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {}),
        FsDir(L"app", {})
    });
    /* clang-format on */

    auto launcher_config = tree.Build();

    /* The write reaches the file of the host filesystem. */
    {
        ProtocolWriteFile::Req req;
        req.FileName = appbox::WideToUTF8(FolderPath() + L"\\data.txt");
        req.Data = "written";

        const auto rsp =
            ProbeWriteFile.Call(req, GetCWD(), launcher_config, ProbeBitness::Bit32).get<ProtocolWriteFile::Rsp>();
        ASSERT_EQ(rsp.code, static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(rsp.readback, "written");
    }

    /* The host file carries the new content, the overlay holds no copy of it. */
    {
        std::string data;
        ASSERT_EQ(ReadFileFull((host.Get() / L"data.txt").wstring(), data), static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(data, "written");
        EXPECT_FALSE(std::filesystem::exists(FolderInOverlay(GetCWDString()) + L"\\data.txt"));
    }

    ASSERT_TRUE(tree.Verify());
}
