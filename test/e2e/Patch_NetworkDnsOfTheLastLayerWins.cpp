#include "probe/ResolveName.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/NetworkIsolationBuilder.hpp"
#include "utils/PatchBuilder.hpp"
#include "SandboxLayout.hpp"
#include "WString.hpp"
#include <filesystem>
#include <string>
#include <vector>

typedef appbox::test::CommonFixture E2E_Patch;
using namespace appbox::test;

namespace
{

/** Hostname every layer of the case redirects. */
constexpr wchar_t kSharedName[] = L"appbox-patch-dns-shared.invalid";

/** Hostname only the resources of the archive redirect. */
constexpr wchar_t kAppName[] = L"appbox-patch-dns-app.invalid";

/** Hostname only the first package redirects. */
constexpr wchar_t kFirstPackageName[] = L"appbox-patch-dns-first.invalid";

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
 * 1. The resources of the archive redirect the shared hostname and a hostname
 *    of their own.
 * 2. `00-foo.zip` redirects the shared hostname to another address and adds a
 *    hostname of its own; `01-bar.zip` redirects the shared hostname a third
 *    time.
 * 3. The sandboxed process resolves the three hostnames.
 *
 * Expected:
 * 1. The shared hostname resolves to the address of `01-bar.zip`, so the
 *    redirection of a package overrides the redirection of the archive and the
 *    later package overrides the earlier one.
 * 2. The hostname only the archive redirects keeps the address of the archive,
 *    because a package overrides the entries it lists and not the whole
 *    document below it.
 * 3. The hostname only the first package redirects is answered as well, so the
 *    redirections of the layers below the last package stay in place.
 */
TEST_F(E2E_Patch, NetworkDnsOfTheLastLayerWins)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {}),
        FsDir(L"app", { FsDir(L"network", {}) })
    });
    /* clang-format on */

    const auto config = tree.Build();

    ASSERT_TRUE(WriteNetworkIsolationFile(GetCWD(), {
                                                        { kSharedName, L"10.0.0.1" },
                                                        { kAppName,    L"10.0.0.2" }
    }));

    const auto patch_dir = GetCWD() / appbox::layout::kPatchDirNameW;
    ASSERT_TRUE(std::filesystem::create_directories(patch_dir));
    ASSERT_TRUE(WritePatchPackage(
        patch_dir / L"00-foo.zip",
        {
    },
        {}, {}, PatchNetwork{ { { kSharedName, L"10.0.0.11" }, { kFirstPackageName, L"10.0.0.3" } } }));
    ASSERT_TRUE(
        WritePatchPackage(patch_dir / L"01-bar.zip", {}, {}, {}, PatchNetwork{ { { kSharedName, L"10.0.0.21" } } }));

    ProtocolResolveName::Req req;
    req.questions = { QuestionOf(kSharedName), QuestionOf(kAppName), QuestionOf(kFirstPackageName) };

    const auto rsp = ProbeResolveName.Call(req, GetCWD(), config).get<ProtocolResolveName::Rsp>();
    ASSERT_EQ(rsp.answers.size(), 3u);

    /* The entry of the last package wins. */
    ASSERT_EQ(rsp.answers[0].code, 0);
    ASSERT_EQ(rsp.answers[0].addresses.size(), 1u);
    EXPECT_EQ(rsp.answers[0].addresses[0], "10.0.0.21");

    /* The entry only the archive lists stays in place. */
    ASSERT_EQ(rsp.answers[1].code, 0);
    ASSERT_EQ(rsp.answers[1].addresses.size(), 1u);
    EXPECT_EQ(rsp.answers[1].addresses[0], "10.0.0.2");

    /* The entry only the first package lists stays in place. */
    ASSERT_EQ(rsp.answers[2].code, 0);
    ASSERT_EQ(rsp.answers[2].addresses.size(), 1u);
    EXPECT_EQ(rsp.answers[2].addresses[0], "10.0.0.3");

    /* The resources of the application are untouched. */
    ASSERT_TRUE(tree.Verify());
}
