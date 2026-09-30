#ifndef APPBOX_TEST_UTILS_TEST_FONT_HPP
#define APPBOX_TEST_UTILS_TEST_FONT_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace appbox::test
{

/**
 * @brief A font whose family the host does not carry.
 *
 * The end-to-end cases of the font isolation have to prove that a font of a
 * layer becomes usable, which needs a family the machine does not carry: a
 * family which is installed already would be enumerated even without the
 * sandbox. The helper builds such a font from an installed one by rewriting the
 * family strings of its `name` table in place, so no offset of the file moves,
 * and by recomputing the two checksums which cover the table.
 */
struct TestFont
{
    /**
     * @brief Family name the font carries.
     */
    std::wstring family;

    /**
     * @brief Content of the font file.
     */
    std::vector<std::uint8_t> bytes;
};

/**
 * @brief Build a font whose family the host does not carry.
 *
 * The source is the first candidate of `%windir%\Fonts` whose family name is
 * long enough to hold a unique replacement of the same length. The replacement
 * is `AppBoxTest` cut or filled up with `X` to that length, so it never names a
 * family the machine carries.
 *
 * @param[out] font The font which was built.
 * @param[out] error Reason of a failure.
 * @param[in] skip Number of usable candidates which are left out, which lets a
 *                 case build a second font with a family of its own.
 * @return true when a source font was found and rewritten.
 */
bool MakeTestFont(TestFont& font, std::string& error, std::size_t skip = 0);

/**
 * @brief Whether the font table of this process carries a family.
 *
 * The cases call the helper from the process of the test executable, which is
 * not sandboxed, so the answer is the one of the host.
 *
 * @param[in] family Family name.
 * @return true when the family is installed for this process.
 */
bool HostCarriesFamily(const std::wstring& family);

} // namespace appbox::test

#endif // APPBOX_TEST_UTILS_TEST_FONT_HPP
