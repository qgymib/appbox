#include "control/PeerIdentity.hpp" /* Must be first include file: it brings <windows.h> in. */
#include <gtest/gtest.h>
#include <string>

namespace
{

/**
 * @brief Copy the identity of this process, as a client of it would look.
 * @param[in] own Identity of the process of the case.
 * @return A copy which the cases modify.
 */
appbox::control::ProcessIdentity PeerOf(const appbox::control::ProcessIdentity& own)
{
    appbox::control::ProcessIdentity peer = own;
    return peer;
}

} // namespace

/**
 * @brief The identity of the process of the case is readable: it names an
 *        image, the SID of the user of the token and the level of that token.
 */
TEST(Unit_PeerIdentity, OwnIdentityIsReadable)
{
    appbox::control::ProcessIdentity own;
    ASSERT_TRUE(appbox::control::QueryOwnIdentity(own));

    EXPECT_TRUE(own.valid);
    EXPECT_FALSE(own.image_path.empty());
    EXPECT_NE(own.image_path.find(L".exe"), std::wstring::npos) << own.image_path;

    EXPECT_EQ(own.user_sid.rfind(L"S-1-", 0), 0u) << own.user_sid;
    EXPECT_NE(own.integrity_rid, 0u);
}

/**
 * @brief The peer of a request of this process is this process: the identity a
 *        case reads for its own process id equals its own identity, and the
 *        authentication accepts it.
 */
TEST(Unit_PeerIdentity, PeerOfThisProcessIsItsOwn)
{
    appbox::control::ProcessIdentity own;
    ASSERT_TRUE(appbox::control::QueryOwnIdentity(own));

    appbox::control::ProcessIdentity peer;
    ASSERT_TRUE(appbox::control::QueryProcessIdentity(GetCurrentProcessId(), peer));

    EXPECT_EQ(peer.image_path, own.image_path);
    EXPECT_EQ(peer.user_sid, own.user_sid);
    EXPECT_EQ(peer.integrity_rid, own.integrity_rid);

    std::string message;
    EXPECT_EQ(appbox::control::AuthorizePeer(peer, own, message), appbox::control::PeerVerdict::Authorized);
    EXPECT_TRUE(message.empty()) << message;
}

/**
 * @brief The image paths of two clients compare equal although they are spelled
 *        differently, and a program of another name never does.
 */
TEST(Unit_PeerIdentity, SameImagePathIgnoresCaseAndSeparators)
{
    EXPECT_TRUE(
        appbox::control::IsSameImagePath(L"C:\\Sandbox\\AppBoxLauncher.exe", L"c:\\sandbox\\appboxlauncher.exe"));
    EXPECT_TRUE(
        appbox::control::IsSameImagePath(L"C:\\Sandbox\\AppBoxLauncher.exe", L"C:\\Sandbox\\AppBoxLauncher.exe\\"));

    EXPECT_FALSE(appbox::control::IsSameImagePath(L"C:\\Sandbox\\AppBoxLauncher.exe", L"C:\\Sandbox\\Other.exe"));
    EXPECT_FALSE(
        appbox::control::IsSameImagePath(L"C:\\Sandbox\\AppBoxLauncher.exe", L"D:\\Sandbox\\AppBoxLauncher.exe"));
    EXPECT_FALSE(appbox::control::IsSameImagePath(L"", L"C:\\Sandbox\\AppBoxLauncher.exe"));
}

/**
 * @brief A client which runs another program is refused, even when it runs as
 *        the same user and at the same integrity level.
 */
TEST(Unit_PeerIdentity, ForeignImageIsUnauthorized)
{
    appbox::control::ProcessIdentity own;
    ASSERT_TRUE(appbox::control::QueryOwnIdentity(own));

    auto peer = PeerOf(own);
    peer.image_path = L"C:\\AppBoxTests\\SomeOtherProgram.exe";

    std::string message;
    EXPECT_EQ(appbox::control::AuthorizePeer(peer, own, message), appbox::control::PeerVerdict::Unauthorized);
    EXPECT_FALSE(message.empty());
}

/**
 * @brief A client of the same program which runs as another user is refused.
 */
TEST(Unit_PeerIdentity, ForeignUserIsUnauthorized)
{
    appbox::control::ProcessIdentity own;
    ASSERT_TRUE(appbox::control::QueryOwnIdentity(own));

    auto peer = PeerOf(own);
    peer.user_sid = L"S-1-5-21-1-2-3-1001";

    std::string message;
    EXPECT_EQ(appbox::control::AuthorizePeer(peer, own, message), appbox::control::PeerVerdict::Unauthorized);
    EXPECT_FALSE(message.empty());
}

/**
 * @brief A client of the same program and the same user which runs at another
 *        integrity level is refused with the verdict of its own, so the two
 *        reasons stay distinguishable.
 */
TEST(Unit_PeerIdentity, IntegrityMismatchIsRefused)
{
    appbox::control::ProcessIdentity own;
    ASSERT_TRUE(appbox::control::QueryOwnIdentity(own));

    auto peer = PeerOf(own);
    peer.integrity_rid = own.integrity_rid == 0x1000u ? 0x2000u : 0x1000u;

    std::string message;
    EXPECT_EQ(appbox::control::AuthorizePeer(peer, own, message), appbox::control::PeerVerdict::IntegrityMismatch);
    EXPECT_FALSE(message.empty());
}

/**
 * @brief A program of a layer of the sandbox is another file than the launcher of
 *        the host, so it is refused in both directions.
 *
 * The injection module of the sandbox leaves every launcher alone, so a launcher
 * always runs the file it was started from: a process which runs a copy below
 * `app\filesystem` or `data\filesystem` is no launcher of this sandbox, however
 * it is named.
 */
TEST(Unit_PeerIdentity, LayerImageIsUnauthorized)
{
    appbox::control::ProcessIdentity own;
    ASSERT_TRUE(appbox::control::QueryOwnIdentity(own));
    own.image_path = L"C:\\AppBoxTests\\Sandbox\\AppBoxLauncher.exe";

    std::string message;

    auto writable = PeerOf(own);
    writable.image_path = L"C:\\AppBoxTests\\Sandbox\\data\\filesystem\\c\\users\\tester\\appbox\\AppBoxLauncher.exe";
    EXPECT_EQ(appbox::control::AuthorizePeer(writable, own, message), appbox::control::PeerVerdict::Unauthorized);

    auto read_only = PeerOf(own);
    read_only.image_path = L"C:\\AppBoxTests\\Sandbox\\app\\filesystem\\#USERPROFILE#\\appbox\\AppBoxLauncher.exe";
    EXPECT_EQ(appbox::control::AuthorizePeer(read_only, own, message), appbox::control::PeerVerdict::Unauthorized);

    /* The check is symmetric: a copy of a layer verifies the host with it too. */
    EXPECT_EQ(appbox::control::AuthorizePeer(own, writable, message), appbox::control::PeerVerdict::Unauthorized);

    /* A copy of a layer of another sandbox is no launcher of this one either. */
    auto other_root = PeerOf(own);
    other_root.image_path =
        L"C:\\AppBoxTests\\Elsewhere\\data\\filesystem\\c\\users\\tester\\appbox\\AppBoxLauncher.exe";
    EXPECT_EQ(appbox::control::AuthorizePeer(other_root, own, message), appbox::control::PeerVerdict::Unauthorized);
}

/**
 * @brief The server of the control pipe is trusted when it runs the image of
 *        this run, which is what a self-extracting program is as well: the
 *        program is the image the client runs.
 */
TEST(Unit_PeerIdentity, ServerOfTheSameImageIsAccepted)
{
    appbox::control::ProcessIdentity own;
    ASSERT_TRUE(appbox::control::QueryOwnIdentity(own));
    own.image_path = L"C:\\AppBoxTests\\Build\\AppBoxLauncher.exe";

    std::string message;

    auto same_program = PeerOf(own);
    EXPECT_EQ(appbox::control::AuthorizeServer(same_program, own, message), appbox::control::PeerVerdict::Authorized)
        << message;

    /* Another program of the same directory is no master instance. */
    auto another_program = PeerOf(own);
    another_program.image_path = L"C:\\AppBoxTests\\Build\\Other.exe";
    EXPECT_EQ(appbox::control::AuthorizeServer(another_program, own, message),
              appbox::control::PeerVerdict::Unauthorized);
}

/**
 * @brief A server which runs another program, as another user or at another
 *        integrity level is refused before a request is sent.
 */
TEST(Unit_PeerIdentity, ServerOfAnotherIdentityIsRefused)
{
    appbox::control::ProcessIdentity own;
    ASSERT_TRUE(appbox::control::QueryOwnIdentity(own));
    own.image_path = L"C:\\AppBoxTests\\Build\\AppBoxLauncher.exe";

    std::string message;

    auto foreign = PeerOf(own);
    foreign.image_path = L"C:\\AppBoxTests\\Other\\Program.exe";
    EXPECT_EQ(appbox::control::AuthorizeServer(foreign, own, message), appbox::control::PeerVerdict::Unauthorized);

    auto another_user = PeerOf(own);
    another_user.user_sid = L"S-1-5-21-1-2-3-1001";
    EXPECT_EQ(appbox::control::AuthorizeServer(another_user, own, message), appbox::control::PeerVerdict::Unauthorized);

    auto another_level = PeerOf(own);
    another_level.integrity_rid = own.integrity_rid == 0x1000u ? 0x2000u : 0x1000u;
    EXPECT_EQ(appbox::control::AuthorizeServer(another_level, own, message),
              appbox::control::PeerVerdict::IntegrityMismatch);
}

/**
 * @brief A client whose identity cannot be read is refused instead of being
 *        trusted, which is what keeps a dead process from passing the check.
 */
TEST(Unit_PeerIdentity, UnreadableIdentityIsUnauthorized)
{
    appbox::control::ProcessIdentity own;
    ASSERT_TRUE(appbox::control::QueryOwnIdentity(own));

    appbox::control::ProcessIdentity peer;
    peer.image_path = own.image_path;
    peer.user_sid = own.user_sid;
    peer.integrity_rid = own.integrity_rid;

    std::string message;
    EXPECT_EQ(appbox::control::AuthorizePeer(peer, own, message), appbox::control::PeerVerdict::Unauthorized);

    /* A process id which belongs to no process cannot be read at all. */
    appbox::control::ProcessIdentity missing;
    EXPECT_FALSE(appbox::control::QueryProcessIdentity(0, missing));
    EXPECT_FALSE(missing.valid);
}
