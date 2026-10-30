#include "probe/LaunchProcess.hpp"
#include "utils/ProbeCall.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/TestKnownFolder.hpp"
#include "utils/ReadFileFull.hpp"
#include "WString.hpp"

typedef appbox::test::CommonFixture E2E_Fs;
using namespace appbox::test;

/**
 * Condition:
 * 1. An executable exists in the lower fs only (a copy of cmd.exe).
 * 2. Launch it from the sandbox view with `RtlCreateUserProcess`, which builds
 *    the parameters of the process itself and reaches `NtCreateUserProcess`
 *    without passing the Win32 creation code.
 *
 * Expected:
 * 1. The process is created successfully, so the image path of the view was
 *    resolved into the layer which holds the file and the child was injected.
 * 2. The child exits with the requested code, which the probe reports.
 * 3. The lower layer stays untouched.
 */
TEST_F(E2E_Fs, LaunchProcess_ThroughNtCreateUserProcess)
{
    /* Read the host cmd.exe so it becomes part of the declared tree. */
    FsNode::Bytes cmd_bytes;
    ASSERT_EQ(ReadFileFull(L"C:\\Windows\\System32\\cmd.exe", cmd_bytes), static_cast<DWORD>(0));
    ASSERT_FALSE(cmd_bytes.empty());

    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {}),
        FsDir(L"app", {
            FsDir(L"filesystem\\#USERPROFILE#", {
                FsNode(L"cmd.exe", cmd_bytes)
            })
        })
    });
    /* clang-format on */

    /* Build filesystem tree. */
    auto config = tree.Build();

    /* Launch the executable of the view through the runtime of the system. */
    {
        ProtocolLaunchProcess::Req req;
        req.FileName = appbox::WideToUTF8(GetKnownFolderPath(L"#USERPROFILE#", false) + L"\\cmd.exe");
        req.Arguments = { "/c", "exit 42" };
        req.Api = LaunchApi::RtlCreateUserProcess;

        auto rsp = Probe(GetCWD(), config).Call<ProtocolLaunchProcess>(req);
        ASSERT_EQ(rsp.code, static_cast<DWORD>(0)) << "RtlCreateUserProcess failed with " << rsp.code;
        EXPECT_EQ(rsp.exit_code, static_cast<DWORD>(42));
    }

    /* Verify lower filesystem content */
    ASSERT_TRUE(tree.Verify());
}
