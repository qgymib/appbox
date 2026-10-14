#include <gtest/gtest.h>
#include "FilesystemMarker.hpp"
#include "sandbox/utils/Defines.hpp"
#include <string>

/*
 * The packer writes the markers of the view and the sandbox reads them, so the
 * two spellings have to be the same text. The sandbox cannot include the header
 * of the packer (its headers belong to the injection module), which is why the
 * name is written down on both sides; these cases are what keeps the two
 * spellings from drifting apart.
 */

TEST(Unit_FilesystemMarker, TheWhiteoutSuffixIsTheOneOfTheSandbox)
{
    EXPECT_EQ(std::wstring(appbox::filesystem_marker::kWhiteoutSuffix), std::wstring(APPBOX_SANDBOX_WHITEOUT_SUFFIX_W));
}

TEST(Unit_FilesystemMarker, TheOpaqueNameIsTheOneOfTheSandbox)
{
    EXPECT_EQ(std::wstring(appbox::filesystem_marker::kOpaqueName), std::wstring(APPBOX_SANDBOX_OPAQUE_NAME_W));
}

TEST(Unit_FilesystemMarker, WhiteoutNameOfAppendsTheSuffix)
{
    EXPECT_EQ(appbox::filesystem_marker::WhiteoutNameOf(L"tool.exe"), L"tool.exe.$APPBOX_DELETE$");
    EXPECT_EQ(appbox::filesystem_marker::WhiteoutNameOf(L"bin"), L"bin.$APPBOX_DELETE$");
    EXPECT_EQ(appbox::filesystem_marker::WhiteoutNameOf(L""), std::wstring(APPBOX_SANDBOX_WHITEOUT_SUFFIX_W));
}
