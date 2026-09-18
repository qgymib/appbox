#include "probe/QueryAttributes.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/KnownFolder.hpp"
#include "WString.hpp"

typedef appbox::test::CommonFixture Fs;
using namespace appbox::test;

/**
 * Condition:
 * 1. File exists in lower fs only.
 * 2. Query the file attributes with GetFileAttributesW (NtQueryAttributesFile).
 *
 * Expected:
 * 1. The query succeeds and reports a regular file.
 */
TEST_F(Fs, QueryAttributes_MultiLower_ExistsInLower)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"Upper", {}),
        FsDir(L"Lower1", {
            FsDir(L"filesystem\\%APPDATA%", {
                FsFile(L"data.txt", "hello1")
            })
        }),
        FsDir(L"Lower2", {
            FsDir(L"filesystem\\%APPDATA%", {
                FsFile(L"data.txt", "hello2")
            })
        })
    });
    /* clang-format on */

    /* Build filesystem tree. */
    auto config = tree.Build();

    /* Query the file which only exists in the lower layers. */
    {
        ProtocolQueryAttributes::Req req;
        req.FileName = appbox::WideToUTF8(GetKnownFolderPath(L"%APPDATA%", false) + L"\\data.txt");

        auto rsp = ProbeQueryAttributes.Call(req, GetCWD(), config).get<ProtocolQueryAttributes::Rsp>();
        ASSERT_EQ(rsp.code, static_cast<DWORD>(0));
        EXPECT_NE(rsp.attributes & FILE_ATTRIBUTE_DIRECTORY, static_cast<DWORD>(FILE_ATTRIBUTE_DIRECTORY));
    }

    /* Verify lower filesystem content */
    ASSERT_TRUE(tree.Verify());
}

/**
 * Condition:
 * 1. File does not exist in any layer.
 * 2. Query the file attributes.
 *
 * Expected:
 * 1. The query fails with ERROR_FILE_NOT_FOUND.
 */
TEST_F(Fs, QueryAttributes_MultiLower_NonExists)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"Upper", {}),
        FsDir(L"Lower1", {
            FsDir(L"filesystem\\%APPDATA%", {
                FsFile(L"other.txt", "hello1")
            })
        })
    });
    /* clang-format on */

    /* Build filesystem tree. */
    auto config = tree.Build();

    /* Query a file which does not exist anywhere. */
    {
        ProtocolQueryAttributes::Req req;
        req.FileName = appbox::WideToUTF8(GetKnownFolderPath(L"%APPDATA%", false) + L"\\data.txt");

        auto rsp = ProbeQueryAttributes.Call(req, GetCWD(), config).get<ProtocolQueryAttributes::Rsp>();
        ASSERT_EQ(rsp.attributes, static_cast<DWORD>(INVALID_FILE_ATTRIBUTES));
        EXPECT_EQ(rsp.code, static_cast<DWORD>(ERROR_FILE_NOT_FOUND));
    }

    /* Verify lower filesystem content */
    ASSERT_TRUE(tree.Verify());
}
