#include "probe/LaunchProcess.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/KnownFolder.hpp"
#include "utils/ReadFileFull.hpp"
#include "WString.hpp"

typedef appbox::test::CommonFixture Fs;
using namespace appbox::test;

/**
 * Condition:
 * 1. An executable exists in the lower fs only (a copy of cmd.exe).
 * 2. Launch it from the sandbox view with CreateProcessW.
 *
 * Expected:
 * 1. The process is created successfully.
 * 2. The child exits with the requested code, which the probe reports.
 * 3. The lower layer stays untouched.
 */
TEST_F(Fs, LaunchProcess_FromLower)
{
    /* Read the host cmd.exe so it becomes part of the declared tree. */
    FsNode::Bytes cmd_bytes;
    ASSERT_EQ(ReadFileFull(L"C:\\Windows\\System32\\cmd.exe", cmd_bytes), static_cast<DWORD>(0));
    ASSERT_FALSE(cmd_bytes.empty());

    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"Upper", {}),
        FsDir(L"Lower1", {
            FsDir(L"filesystem\\#APPDATA#", {
                FsNode(L"cmd.exe", cmd_bytes)
            })
        })
    });
    /* clang-format on */

    /* Build filesystem tree. */
    auto config = tree.Build();

    /* Launch the executable from the sandbox view. */
    {
        ProtocolLaunchProcess::Req req;
        req.FileName = appbox::WideToUTF8(GetKnownFolderPath(L"#APPDATA#", false) + L"\\cmd.exe");
        req.Arguments = {"/c", "exit 42"};

        auto rsp = ProbeLaunchProcess.Call(req, GetCWD(), config).get<ProtocolLaunchProcess::Rsp>();
        ASSERT_EQ(rsp.code, static_cast<DWORD>(0)) << "CreateProcessW failed with " << rsp.code;
        EXPECT_EQ(rsp.exit_code, static_cast<DWORD>(42));
    }

    /* Verify lower filesystem content */
    ASSERT_TRUE(tree.Verify());
}
