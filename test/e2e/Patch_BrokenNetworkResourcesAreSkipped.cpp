#include "probe/ResolveName.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/NetworkIsolationBuilder.hpp"
#include "utils/PatchBuilder.hpp"
#include "SandboxLayout.hpp"
#include "WString.hpp"
#include <filesystem>
#include <string>

typedef appbox::test::CommonFixture E2E_Patch;
using namespace appbox::test;

namespace
{

/** Hostname only the resources of the archive redirect. */
constexpr wchar_t kAppName[] = L"appbox-patch-broken-app.invalid";

/** Hostname only the last package redirects. */
constexpr wchar_t kLastPackageName[] = L"appbox-patch-broken-last.invalid";

/**
 * @brief Build a question which resolves a hostname with winsock.
 * @param[in] name Hostname to resolve.
 * @return The question of the probe.
 */
ProtocolResolveName::Question QuestionOf(const wchar_t* name)
{
    ProtocolResolveName::Question question;
    question.name = appbox::WideToUTF8(name);
    question.api = ProtocolResolveName::Api::GetAddrInfo;
    question.family = AF_UNSPEC;
    question.type = DNS_TYPE_A;
    return question;
}

} // namespace

/**
 * Condition:
 * 1. The resources of the archive redirect a hostname of their own.
 * 2. `00-foo.zip` carries a network document which is not valid JSON, so the
 *    layer cannot be applied.
 * 3. `01-bar.zip` redirects a hostname of its own.
 * 4. The sandboxed process resolves both hostnames.
 *
 * Expected:
 * 1. The hostname of the archive is answered, so a broken package does not
 *    fail the run and does not drop the layers below it.
 * 2. The hostname of the last package is answered as well, so the layers above
 *    a broken package are applied like they are without it.
 */
TEST_F(E2E_Patch, BrokenNetworkResourcesAreSkipped)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {}),
        FsDir(L"app", { FsDir(L"network", {}) })
    });
    /* clang-format on */

    const auto config = tree.Build();

    ASSERT_TRUE(WriteNetworkIsolationFile(GetCWD(), {
                                                        { kAppName, L"10.0.0.1" }
    }));

    const auto patch_dir = GetCWD() / appbox::layout::kPatchDirNameW;
    ASSERT_TRUE(std::filesystem::create_directories(patch_dir));
    ASSERT_TRUE(WritePatchPackage(patch_dir / L"00-foo.zip", {}, {}, {},
                                  PatchNetwork{ {}, {}, "{\"version\":1,\"entries\":[" }));
    ASSERT_TRUE(WritePatchPackage(patch_dir / L"01-bar.zip", {}, {}, {},
                                  PatchNetwork{ { { kLastPackageName, L"10.0.0.3" } } }));

    ProtocolResolveName::Req req;
    req.questions = { QuestionOf(kAppName), QuestionOf(kLastPackageName) };

    const auto rsp = ProbeResolveName.Call(req, GetCWD(), config).get<ProtocolResolveName::Rsp>();
    ASSERT_EQ(rsp.answers.size(), 2u);

    /* The layers below the broken package stay in place. */
    ASSERT_EQ(rsp.answers[0].code, 0);
    ASSERT_EQ(rsp.answers[0].addresses.size(), 1u);
    EXPECT_EQ(rsp.answers[0].addresses[0], "10.0.0.1");

    /* The layer above the broken package is applied. */
    ASSERT_EQ(rsp.answers[1].code, 0);
    ASSERT_EQ(rsp.answers[1].addresses.size(), 1u);
    EXPECT_EQ(rsp.answers[1].addresses[0], "10.0.0.3");

    /* The resources of the application are untouched. */
    ASSERT_TRUE(tree.Verify());
}
