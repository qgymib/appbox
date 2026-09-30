#include "probe/Fonts.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/TestFont.hpp"
#include "utils/TestKnownFolder.hpp"
#include "WString.hpp"
#include <string>

typedef appbox::test::CommonFixture E2E_Font;
using namespace appbox::test;

/**
 * Condition:
 * 1. The resource tree holds a lower layer of the `Fonts` folder of the system
 *    which carries a font whose family the host does not carry.
 * 2. The sandboxed process enumerates the families of its font table, creates a
 *    font for the family of the packed font and reads the file of the view.
 *
 * Expected:
 * 1. The font table of the sandboxed process carries the family, so the packed
 *    font is usable like an installed one, and the face a font which was created
 *    for the family reports is the family.
 * 2. The file of the view carries the content of the layer.
 * 3. The resources of the application are untouched.
 */
TEST_F(E2E_Font, PackedFontIsUsable)
{
    TestFont    font;
    std::string error;
    ASSERT_TRUE(MakeTestFont(font, error)) << error;

    /*
     * The family has to be one the host does not carry, otherwise the case
     * would pass without the sandbox as well.
     */
    ASSERT_FALSE(HostCarriesFamily(font.family));

    const std::wstring packed_name = L"AppBoxE2E.Font.ttf";

    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {}),
        FsDir(L"app", {
            FsDir(L"filesystem", {
                FsDir(L"#Fonts#", {
                    FsNode(packed_name, font.bytes)
                })
            })
        })
    });
    /* clang-format on */

    auto config = tree.Build();

    const std::wstring view_path = GetKnownFolderPath(L"#Fonts#", false) + L"\\" + packed_name;

    ProtocolFonts::Req req;
    req.family = appbox::WideToUTF8(font.family);
    req.view_path = appbox::WideToUTF8(view_path);

    const auto rsp = ProbeFonts.Call(req, GetCWD(), config).get<ProtocolFonts::Rsp>();

    EXPECT_TRUE(rsp.enumerated);
    EXPECT_TRUE(rsp.created);
    EXPECT_EQ(rsp.face, req.family);

    EXPECT_TRUE(rsp.file_read);
    EXPECT_EQ(rsp.file_size, font.bytes.size());

    /* Verify that the resources of the application are untouched. */
    ASSERT_TRUE(tree.Verify());
}
