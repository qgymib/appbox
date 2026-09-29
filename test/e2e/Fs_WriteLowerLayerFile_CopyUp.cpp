#include "probe/WriteFile.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/ReadFileFull.hpp"
#include "utils/TestKnownFolder.hpp"
#include "WString.hpp"
#include <filesystem>
#include <string>

typedef appbox::test::CommonFixture E2E_Fs;
using namespace appbox::test;

namespace
{

/** Name of the folder of this case below `#USERPROFILE#`. */
constexpr wchar_t kFolderName[] = L"AppBoxTest_CopyUp";

} // namespace

/**
 * Condition:
 * 1. The file exists in a lower layer only, so the view answers it with the
 *    content of that layer.
 * 2. The sandboxed process opens the file for writing and writes another
 *    content into it.
 *
 * Expected:
 * 1. The open copies the file up into the overlay, the write succeeds and the
 *    read back reports the content which was written.
 * 2. The overlay holds the copy with the new content, so the modification of
 *    the sandbox never reaches the layer it was copied from.
 */
TEST_F(E2E_Fs, WriteLowerLayerFile_CopyUp)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {}),
        FsDir(L"app", {
            FsDir(L"filesystem\\#USERPROFILE#", {
                FsDir(kFolderName, {
                    FsFile(L"data.txt", "packed")
                })
            })
        })
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto file = GetKnownFolderPath(L"#USERPROFILE#", false) + L"\\" + kFolderName + L"\\data.txt";

    /* The write opens the file of the lower layer for writing, which copies it
     * into the overlay of the sandbox, and reports what the file holds after
     * the write. */
    {
        ProtocolWriteFile::Req req;
        req.FileName = appbox::WideToUTF8(file);
        req.Data = "written";

        const auto rsp = ProbeWriteFile.Call(req, GetCWD(), config).get<ProtocolWriteFile::Rsp>();
        ASSERT_EQ(rsp.code, static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(rsp.readback, "written");
    }

    /* The copy of the overlay carries the new content. */
    {
        const auto overlay = GetCWDString() + L"\\data\\filesystem\\" + GetKnownFolderPath(L"#USERPROFILE#", true) +
                             L"\\" + kFolderName + L"\\data.txt";

        std::string data;
        ASSERT_EQ(ReadFileFull(overlay, data), static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(data, "written");
    }

    /* The lower layer keeps its own content. */
    ASSERT_TRUE(tree.Verify());
}
