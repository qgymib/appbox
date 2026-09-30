#include "probe/Fonts.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/TestFont.hpp"
#include "utils/TestKnownFolder.hpp"
#include "WString.hpp"
#include <filesystem>
#include <string>

typedef appbox::test::CommonFixture E2E_Font;
using namespace appbox::test;

/**
 * Condition:
 * 1. The resource tree holds a lower layer of the `Fonts` folder of the system
 *    which carries a font whose family the host does not carry, and the file of
 *    the layer is not a file of the font folder of the host.
 * 2. The sandboxed process adds the file with its view path
 *    (`C:\Windows\Fonts\<name>`) and `FR_PRIVATE`.
 *
 * Expected:
 * 1. The addition reports the font, so the sandbox resolved the path of the
 *    view to the file of the layer: the font driver of the system opens the
 *    file of a font resource without passing the file hooks, so a call which
 *    names a path of the view only reaches the file of the layer when the hook
 *    of the sandbox rewrote it.
 * 2. The resources of the application are untouched.
 */
TEST_F(E2E_Font, AddFontResourceExOfTheViewIsRedirected)
{
    TestFont    font;
    std::string error;
    ASSERT_TRUE(MakeTestFont(font, error)) << error;

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

    /*
     * The file is not a file of the host, so the addition can only report the
     * font when the hook resolved the path of the view to the file of the
     * layer.
     */
    ASSERT_FALSE(std::filesystem::exists(view_path));

    ProtocolFonts::Req req;
    req.family = appbox::WideToUTF8(font.family);
    req.view_path = appbox::WideToUTF8(view_path);

    const auto rsp = ProbeFonts.Call(req, GetCWD(), config).get<ProtocolFonts::Rsp>();

    /* A call which names the path of the view reaches the file of the layer. */
    EXPECT_GT(rsp.add_result, 0);

    /* The file of the view is the file of the layer. */
    EXPECT_TRUE(rsp.file_read);
    EXPECT_EQ(rsp.file_size, font.bytes.size());

    /* Verify that the resources of the application are untouched. */
    ASSERT_TRUE(tree.Verify());
}
