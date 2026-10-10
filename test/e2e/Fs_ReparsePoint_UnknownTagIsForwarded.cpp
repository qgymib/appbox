#include "probe/ReparsePoint.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/FsIsolationBuilder.hpp"
#include "utils/RealFsFolder.hpp"
#include "utils/TestKnownFolder.hpp"
#include "WString.hpp"
#include <cstddef>
#include <string>

typedef appbox::test::CommonFixture E2E_Fs;
using namespace appbox::test;

/** Name of the folder of this case below `#USERPROFILE#`. */
constexpr wchar_t kFolderName[] = L"AppBoxTest_ReparseTag";

/** Tag of a container image layer, which describes the entry itself. */
constexpr ULONG kForeignTag = 0x80000018;

/**
 * Condition:
 * 1. The sandboxed process creates a file which carries a reparse point whose
 *    tag describes the entry itself instead of naming a target, and the folder
 *    of the case is isolated with `Write Copy`.
 * 2. The process queries the attributes of the file and reads the data of its
 *    reparse point back.
 *
 * Expected:
 * 1. The view forwards the entry: the file is an entry of the view which
 *    carries the attribute of a reparse point, and the data the process reads
 *    back carries the tag it wrote.
 * 2. The view resolves only the tags which name a target, so it neither
 *    refuses nor rewrites the data of a tag it does not resolve.
 */
TEST_F(E2E_Fs, ReparsePoint_UnknownTagIsForwarded)
{
    RealFsFolder host(L"#USERPROFILE#", kFolderName);

    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {}),
        FsDir(L"app", {
            FsDir(L"filesystem", {
                FsDir(L"#USERPROFILE#", {})
            })
        })
    });
    /* clang-format on */

    auto config = tree.Build();

    const std::wstring folder = GetKnownFolderPath(L"#USERPROFILE#", false) + L"\\" + kFolderName;
    ASSERT_TRUE(WriteFsIsolationFile(
        GetCWD(), {
                      { L"#USERPROFILE#\\" + std::wstring(kFolderName), appbox::FilesystemEntryKind::Directory,
                       appbox::FilesystemIsolation::WriteCopy }
    }));

    ProtocolReparsePoint::Req req;

    ProtocolReparsePoint::Req::Item create;
    create.action = "set_tag";
    create.path = appbox::WideToUTF8(folder + L"\\entry.txt");
    create.tag = kForeignTag;
    create.directory = false;
    req.items.push_back(create);

    ProtocolReparsePoint::Req::Item attributes;
    attributes.action = "attributes";
    attributes.path = appbox::WideToUTF8(folder + L"\\entry.txt");
    req.items.push_back(attributes);

    ProtocolReparsePoint::Req::Item read;
    read.action = "read";
    read.path = appbox::WideToUTF8(folder + L"\\entry.txt");
    req.items.push_back(read);

    const auto rsp = ProbeReparsePoint.Call(req, GetCWD(), config).get<ProtocolReparsePoint::Rsp>();
    ASSERT_EQ(rsp.items.size(), static_cast<size_t>(3));

    EXPECT_EQ(rsp.items[0].status, static_cast<long>(STATUS_SUCCESS));

    EXPECT_NE(rsp.items[1].attributes, static_cast<DWORD>(INVALID_FILE_ATTRIBUTES));
    EXPECT_NE(rsp.items[1].attributes & FILE_ATTRIBUTE_REPARSE_POINT, static_cast<DWORD>(0));

    EXPECT_EQ(rsp.items[2].status, static_cast<long>(STATUS_SUCCESS));
    EXPECT_EQ(rsp.items[2].tag, kForeignTag);

    ASSERT_TRUE(tree.Verify());
}
