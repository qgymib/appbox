#include "probe/ListDirNt.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/RealFsFolder.hpp"
#include "utils/TestKnownFolder.hpp"
#include "WString.hpp"
#include <string>
#include <vector>

typedef appbox::test::CommonFixture E2E_Fs;
using namespace appbox::test;

/** Name of the folder which holds the file of this case below `#USERPROFILE#`. */
constexpr wchar_t kFolderName[] = L"AppBoxTest_ListNotInView";

/** Name of the empty folder of this case below `#USERPROFILE#`. */
constexpr wchar_t kRemovedFolderName[] = L"AppBoxTest_ListNotInViewRemoved";

/** Name of the file of this case below the folder. */
constexpr wchar_t kFileName[] = L"file.txt";

/**
 * @brief Assert that an enumeration of a handle which denotes no directory of
 *        the view was left to the file system.
 *
 * The handle denotes an object which is no directory of a disk file system, so
 * the view has nothing to merge and forwards the call: the file system reports
 * its own failure, which is a failure of this call, and no entry of a layer is
 * reported.
 *
 * @param[in] status Status the probe reported.
 * @param[in] names Names the probe reported.
 */
static void ExpectForwarded(long status, const std::vector<std::string>& names)
{
    EXPECT_NE(status, 0L);
    EXPECT_TRUE(names.empty());
}

/**
 * @brief Assert that the view refused an enumeration of a directory it does not
 *        hold.
 *
 * The view never answers with the content of the single layer the handle was
 * opened with, which would show the entries a whiteout, an opaque marker or the
 * isolation hides, together with the markers of the view themselves. The object
 * a handle denotes is named by the file system, so the view either reports that
 * the entry is not part of it or refuses the call because it cannot name the
 * object at all; both are refusals, while a status of the view which is no
 * refusal would prove that the call was answered from that layer.
 *
 * @param[in] status Status the probe reported.
 * @param[in] names Names the probe reported.
 */
static void ExpectRefusedByView(long status, const std::vector<std::string>& names)
{
    EXPECT_TRUE(status == STATUS_OBJECT_NAME_NOT_FOUND || status == STATUS_NOT_SUPPORTED) << "status=" << status;
    EXPECT_TRUE(names.empty());
}

/**
 * Condition:
 * 1. The host holds a folder with a file, a second folder which is empty and
 *    the character device `NUL`; no layer of the view holds any of them.
 * 2. The sandboxed process enumerates a handle it duplicated and closed the
 *    original of, so the handle carries no record of an open the sandbox
 *    performed, and the handle denotes an object which is no directory of the
 *    view: a file, a device, and a folder which the probe removed before the
 *    query while its handle stayed open.
 *
 * Expected:
 * 1. The call of the file and of the device is forwarded, so the failure the
 *    file system reports is the answer.
 * 2. The call of the removed folder is refused by the view, because the entry
 *    is no longer part of it and the view cannot name the object.
 * 3. No call reports an entry of a layer, and the host entries of the case are
 *    unchanged.
 */
TEST_F(E2E_Fs, ListDir_UnregisteredNotInView)
{
    RealFsFolder host(L"#USERPROFILE#", kFolderName);
    ASSERT_TRUE(host.WriteFile(kFileName, "host"));

    RealFsFolder removed(L"#USERPROFILE#", kRemovedFolderName);

    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {}),
        FsDir(L"app", {})
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto known_folder = GetKnownFolderPath(L"#USERPROFILE#", false);
    const auto folder = known_folder + L"\\" + kFolderName;
    const auto file = folder + L"\\" + kFileName;
    const auto removed_folder = known_folder + L"\\" + kRemovedFolderName;

    {
        /* A handle of a file denotes no directory of the view. */
        SCOPED_TRACE("file");

        ProtocolListDirNt::Req req;
        req.path = appbox::WideToUTF8(file);
        req.create_file = true; /* A file is not opened with `FILE_DIRECTORY_FILE`. */
        req.duplicate = true;

        auto rsp = ProbeListDirNt.Call(req, GetCWD(), config).get<ProtocolListDirNt::Rsp>();
        ExpectForwarded(rsp.status, rsp.names);
    }

    {
        /* A character device is no object of a disk file system. */
        SCOPED_TRACE("device");

        ProtocolListDirNt::Req req;
        req.path = "NUL";
        req.create_file = true;
        req.duplicate = true;

        auto rsp = ProbeListDirNt.Call(req, GetCWD(), config).get<ProtocolListDirNt::Rsp>();
        ExpectForwarded(rsp.status, rsp.names);
    }

    {
        /* A folder the probe removed while the handle above stayed open. */
        SCOPED_TRACE("removed folder");

        ProtocolListDirNt::Req req;
        req.path = appbox::WideToUTF8(removed_folder);
        req.duplicate = true;
        req.remove_before_query = true;

        auto rsp = ProbeListDirNt.Call(req, GetCWD(), config).get<ProtocolListDirNt::Rsp>();
        ExpectRefusedByView(rsp.status, rsp.names);
    }

    /* The host entries of the case were not touched. */
    EXPECT_TRUE(host.FileExists(kFileName));

    /* Verify lower filesystem content */
    ASSERT_TRUE(tree.Verify());
}
