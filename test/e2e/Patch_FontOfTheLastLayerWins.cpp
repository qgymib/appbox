#include "probe/Fonts.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/PatchBuilder.hpp"
#include "utils/TestFont.hpp"
#include "utils/TestKnownFolder.hpp"
#include "SandboxLayout.hpp"
#include "WString.hpp"
#include <filesystem>
#include <string>

typedef appbox::test::CommonFixture E2E_Patch;
using namespace appbox::test;

/**
 * Condition:
 * 1. The resources of the archive carry a font whose family the host does not
 *    carry, in the `Fonts` layer of the system.
 * 2. `00-foo.zip` carries a font of another family under the very same name.
 * 3. The sandboxed process enumerates both families.
 *
 * Expected:
 * 1. The font table of the sandboxed process carries the family of the package,
 *    so the layer of the package is the one the view shows for that name.
 * 2. The family of the archive is not loaded at all, because the name it is
 *    stored under is overridden by the package.
 * 3. The resources of the application are untouched.
 */
TEST_F(E2E_Patch, FontOfTheLastLayerWins)
{
    TestFont    app_font;
    TestFont    patch_font;
    std::string error;
    ASSERT_TRUE(MakeTestFont(app_font, error, 0)) << error;
    ASSERT_TRUE(MakeTestFont(patch_font, error, 1)) << error;

    /* The two fonts carry different families, which is what tells the case
     * which of them was loaded. */
    ASSERT_NE(app_font.family, patch_font.family);
    ASSERT_FALSE(HostCarriesFamily(app_font.family));
    ASSERT_FALSE(HostCarriesFamily(patch_font.family));

    const std::wstring packed_name = L"AppBoxE2E.PatchFont.ttf";

    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {}),
        FsDir(L"app", {
            FsDir(L"filesystem", {
                FsDir(L"#Fonts#", {
                    FsNode(packed_name, app_font.bytes)
                })
            })
        })
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto patch_dir = GetCWD() / appbox::layout::kPatchDirNameW;
    ASSERT_TRUE(std::filesystem::create_directories(patch_dir));

    const std::wstring virtual_path = std::wstring(L"#Fonts#\\") + packed_name;
    ASSERT_TRUE(
        WritePatchPackage(patch_dir / L"00-foo.zip",
                          {
                              { virtual_path, std::string(patch_font.bytes.begin(), patch_font.bytes.end()) }
    }));

    ProtocolFonts::Req req;
    req.family = appbox::WideToUTF8(patch_font.family);
    req.absent_family = appbox::WideToUTF8(app_font.family);
    req.view_path = appbox::WideToUTF8(GetKnownFolderPath(L"#Fonts#", false) + L"\\" + packed_name);

    const auto rsp = ProbeFonts.Call(req, GetCWD(), config).get<ProtocolFonts::Rsp>();

    /* The font of the package is the font of the name: the layer of a package
     * comes before the layer of the archive in the view. */
    EXPECT_TRUE(rsp.enumerated);

    /* The font of the archive is not loaded at all. */
    EXPECT_FALSE(rsp.absent_enumerated);

    /* Verify that the resources of the application are untouched. */
    ASSERT_TRUE(tree.Verify());
}
