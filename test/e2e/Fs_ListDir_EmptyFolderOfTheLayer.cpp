#include "probe/CreateFileW.hpp"
#include "probe/ListDir.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/FsIsolationBuilder.hpp"
#include "utils/TestKnownFolder.hpp"
#include "WString.hpp"
#include <algorithm>
#include <CLI/Encoding.hpp>

typedef appbox::test::CommonFixture E2E_Fs;
using namespace appbox::test;

/**
 * Condition:
 * 1. The lower layer carries a folder without content, which is the shape the
 *    packer writes for a folder the user created in its filesystem workspace.
 * 2. The layer `#USERPROFILE#` of the case is pinned to `Write Copy`, so a file
 *    the application creates stays in the sandbox and the case never reaches
 *    the host filesystem.
 * 3. List the folder of the layer and create a file inside it.
 *
 * Expected:
 * 1. The empty folder of the layer is visible in the merged view.
 * 2. The new file is created in the state of the sandbox.
 * 3. The resources of the application are untouched.
 */
TEST_F(E2E_Fs, ListDir_EmptyFolderOfTheLayer)
{
    const std::string  fName = "created.txt";
    const std::wstring wName = CLI::widen(fName);

    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {}),
        FsDir(L"app", {
            FsDir(L"filesystem\\#USERPROFILE#", {
                FsDir(L"logs", {})
            })
        })
    });
    /* clang-format on */

    /* Build filesystem tree. */
    auto config = tree.Build();

    /*
     * The layer of the case is pinned to `Write Copy`, so the file the case
     * creates below the folder of the layer stays in the sandbox: the case pins
     * the rule it is about and never reaches the host filesystem.
     */
    ASSERT_TRUE(WriteFsIsolationFile(GetCWD(), {
                                                   { L"#USERPROFILE#", appbox::FilesystemEntryKind::Directory,
                                                    appbox::FilesystemIsolation::WriteCopy }
    }));

    const auto profile = GetKnownFolderPath(L"#USERPROFILE#", false);
    const auto logs = profile + L"\\logs";

    /* The folder of the layer is an entry of the merged view. */
    {
        ProtocolListDir::Req req;
        req.path = CLI::narrow(profile);
        req.method = ProtocolListDir::Req::Method::Std;

        ProtocolListDir::Rsp rsp;
        ProbeListDir.Call(req, GetCWD(), config).get_to(rsp);

        const auto it = std::find_if(rsp.entries.begin(), rsp.entries.end(),
                                     [](const auto& e) { return e.name == "logs" && !e.file; });
        ASSERT_NE(it, rsp.entries.end());
    }

    /* The folder of the layer holds nothing, so the view lists no entry of it. */
    {
        ProtocolListDir::Req req;
        req.path = CLI::narrow(logs);
        req.method = ProtocolListDir::Req::Method::Std;

        ProtocolListDir::Rsp rsp;
        ProbeListDir.Call(req, GetCWD(), config).get_to(rsp);
        ASSERT_TRUE(rsp.entries.empty());
    }

    /* Create a file inside the folder of the layer should success. */
    {
        ProtocolCreateFileW::Req req;
        req.FileName = appbox::WideToUTF8(logs + L"\\" + wName);
        req.dwDesiredAccess = GENERIC_WRITE;
        req.dwCreationDisposition = CREATE_NEW;
        auto rsp = ProbeCreateFileW.Call(req, GetCWD(), config).get<ProtocolCreateFileW::Rsp>();
        ASSERT_EQ(rsp.code, 0);
    }

    /* Target file should be created in the state of the sandbox. */
    {
        auto fPath =
            GetCWDString() + L"\\data\\filesystem\\" + GetKnownFolderPath(L"#USERPROFILE#", true) + L"\\logs\\" + wName;
        ASSERT_TRUE(std::filesystem::exists(fPath));
    }

    /* Verify that the resources of the application are untouched. */
    ASSERT_TRUE(tree.Verify());
}
