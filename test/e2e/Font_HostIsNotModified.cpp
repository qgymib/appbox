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

namespace
{

/**
 * @brief Whether a text carries another one, ignoring the case.
 * @param[in] text Text to look at.
 * @param[in] part Text to look for.
 * @return true when the text carries the other one.
 */
bool ContainsIgnoreCase(const std::wstring& text, const std::wstring& part)
{
    if (part.empty() || part.size() > text.size())
    {
        return false;
    }

    for (std::size_t offset = 0; offset + part.size() <= text.size(); ++offset)
    {
        const int length = static_cast<int>(part.size());
        if (CompareStringOrdinal(text.c_str() + offset, length, part.c_str(), length, TRUE) == CSTR_EQUAL)
        {
            return true;
        }
    }

    return false;
}

/**
 * @brief Whether the font registry of the host names a family or a file.
 *
 * The check reads the real registry, because the process of the test executable
 * is not sandboxed.
 *
 * @param[in] family Family name of the packed font.
 * @param[in] file_name Name of the file of the layer.
 * @return true when the registry carries an entry of the font.
 */
bool RegistryCarriesFont(const std::wstring& family, const std::wstring& file_name)
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts", 0, KEY_READ,
                      &key) != ERROR_SUCCESS)
    {
        return false;
    }

    bool found = false;
    for (DWORD index = 0;; ++index)
    {
        wchar_t name[512] = {};
        DWORD   name_length = static_cast<DWORD>(std::size(name));

        wchar_t value[512] = {};
        DWORD   value_length = sizeof(value);
        DWORD   type = 0;

        if (RegEnumValueW(key, index, name, &name_length, nullptr, &type, reinterpret_cast<LPBYTE>(value),
                          &value_length) != ERROR_SUCCESS)
        {
            break;
        }

        if (ContainsIgnoreCase(name, family) || ContainsIgnoreCase(value, file_name))
        {
            found = true;
            break;
        }
    }

    RegCloseKey(key);
    return found;
}

} // namespace

/**
 * Condition:
 * 1. The resource tree holds a lower layer of the `Fonts` folder of the system
 *    which carries a font whose family the host does not carry.
 * 2. The sandboxed process enumerates the family, so the font really is loaded
 *    into the font table of the sandboxed process.
 *
 * Expected:
 * 1. The font folder of the host carries no file of the layer.
 * 2. The font registry of the host carries no entry of the font.
 * 3. The family is gone once the sandboxed process ended: the font was added to
 *    the font table of that process only.
 * 4. The resources of the application are untouched.
 */
TEST_F(E2E_Font, HostIsNotModified)
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

    const std::wstring fonts_folder = GetKnownFolderPath(L"#Fonts#", false);

    ProtocolFonts::Req req;
    req.family = appbox::WideToUTF8(font.family);
    req.view_path = appbox::WideToUTF8(fonts_folder + L"\\" + packed_name);

    const auto rsp = ProbeFonts.Call(req, GetCWD(), config).get<ProtocolFonts::Rsp>();
    ASSERT_TRUE(rsp.enumerated);

    /* The font folder of the host carries no file of the layer. */
    EXPECT_FALSE(std::filesystem::exists(fonts_folder + L"\\" + packed_name));

    /* The font registry of the host carries no entry of the font. */
    EXPECT_FALSE(RegistryCarriesFont(font.family, packed_name));

    /* The font is not part of the font table of the host, which is what the
     * process of this case observes once the sandboxed process ended. */
    EXPECT_FALSE(HostCarriesFamily(font.family));

    /* Verify that the resources of the application are untouched. */
    ASSERT_TRUE(tree.Verify());
}
