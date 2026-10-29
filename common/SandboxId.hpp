#ifndef APPBOX_COMMON_SANDBOX_ID_HPP
#define APPBOX_COMMON_SANDBOX_ID_HPP

#include <cstddef>
#include <random>
#include <string>
#include <string_view>

namespace appbox
{

/**
 * @brief Identifier of one sandbox, as the packer stores it and the launcher uses it.
 *
 * The identifier is a UUID in the text form of RFC 4122: 36 characters in the
 * `8-4-4-4-12` layout, lower case hexadecimal digits and the four hyphens kept.
 * The packer generates it once per project session and writes it into the run
 * configuration of the launcher, so every product of one project shares the
 * same identity while two projects never do.
 *
 * The launcher derives the name of its control pipe from the identifier, so the
 * identifier is the only thing which decides which sandboxes belong together:
 * the rules below are therefore the single source of truth of the packer, the
 * launcher and the test suite, and this header stays free of any Windows
 * dependency so every one of them can include it.
 *
 * A missing or malformed identifier is never replaced by a random one: the
 * launcher refuses to run instead, because a run with an identifier of its own
 * would silently open a second sandbox next to the one the archive describes.
 */
namespace sandbox_id
{

/** Length of the text form of an identifier, including its hyphens. */
inline constexpr std::size_t kLength = 36;

/** Number of hexadecimal digits of an identifier, hyphens not counted. */
inline constexpr std::size_t kDigitCount = 32;

/** Number of hyphens of the text form. */
inline constexpr std::size_t kHyphenCount = 4;

/**
 * @brief Positions of the hyphens of the text form.
 *
 * The table is the `8-4-4-4-12` layout of RFC 4122: the hyphens follow the
 * eighth, the twelfth, the sixteenth and the twentieth digit.
 */
inline constexpr std::size_t kHyphenPositions[kHyphenCount] = { 8, 13, 18, 23 };

/**
 * @brief Digits the generator draws from.
 *
 * The alphabet is lower case and hexadecimal on purpose: `RandomString()` of
 * `common/Random.hpp` uses the letters of the alphabet as well, which no
 * identifier may carry.
 */
inline constexpr char kHexDigits[] = "0123456789abcdef";

/**
 * @brief Whether a character is a lower case hexadecimal digit.
 * @param[in] character Character to test.
 * @return true when the character is one of `0`-`9`, `a`-`f`.
 */
inline bool IsLowerHexDigit(char character)
{
    return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f');
}

/**
 * @brief Whether a character is a hexadecimal digit of either case.
 * @param[in] character Character to test.
 * @return true when the character is one of `0`-`9`, `a`-`f`, `A`-`F`.
 */
inline bool IsHexDigit(char character)
{
    return IsLowerHexDigit(character) || (character >= 'A' && character <= 'F');
}

/**
 * @brief Fold an ASCII letter to lower case.
 * @param[in] character Character to fold.
 * @return The folded character, unchanged when it is not an upper case letter.
 */
inline char FoldAscii(char character)
{
    if (character >= 'A' && character <= 'Z')
    {
        return static_cast<char>(character - 'A' + 'a');
    }
    return character;
}

/**
 * @brief Whether a position of the text form carries a hyphen.
 * @param[in] position Position inside the 36 characters of the text form.
 * @return true when the position is one of `kHyphenPositions`.
 */
inline bool IsHyphenPosition(std::size_t position)
{
    for (std::size_t index = 0; index < kHyphenCount; ++index)
    {
        if (kHyphenPositions[index] == position)
        {
            return true;
        }
    }
    return false;
}

/**
 * @brief Store the 32 digits of an identifier in the text form.
 *
 * The hyphens are inserted at the positions of the layout, so the caller only
 * hands over the digits themselves.
 *
 * @param[in] digits The 32 lower case hexadecimal digits.
 * @return The 36 characters of the text form.
 */
inline std::string FormatDigits(const std::string& digits)
{
    std::string text;
    text.reserve(kLength);

    std::size_t digit = 0;
    for (std::size_t position = 0; position < kLength; ++position)
    {
        if (IsHyphenPosition(position))
        {
            text.push_back('-');
            continue;
        }

        text.push_back(digits[digit]);
        ++digit;
    }

    return text;
}

} // namespace sandbox_id

/**
 * @brief Whether a text is an identifier in the canonical form.
 *
 * The check is strict on purpose: the text has to carry exactly 36 characters,
 * the four hyphens at the positions of the `8-4-4-4-12` layout and lower case
 * hexadecimal digits everywhere else. An identifier of another spelling is
 * rejected instead of being accepted, because the same text is compared with
 * the identifier of another launcher and two spellings of one identifier would
 * otherwise describe two sandboxes.
 *
 * @param[in] text Text to check.
 * @return true when the text is a canonical identifier.
 */
inline bool IsValidSandboxId(std::string_view text)
{
    if (text.size() != sandbox_id::kLength)
    {
        return false;
    }

    for (std::size_t position = 0; position < text.size(); ++position)
    {
        if (sandbox_id::IsHyphenPosition(position))
        {
            if (text[position] != '-')
            {
                return false;
            }
            continue;
        }

        if (!sandbox_id::IsLowerHexDigit(text[position]))
        {
            return false;
        }
    }

    return true;
}

/**
 * @brief Normalize a text to the canonical form of an identifier.
 *
 * Two spellings are accepted, which is what a document written by hand or by
 * another tool may carry: the 32 hexadecimal digits alone, and the 36
 * characters of the `8-4-4-4-12` layout. Upper case digits are folded to lower
 * case, so the result is always the canonical form. Anything else - a text of
 * another length, a hyphen at a position the layout does not name, a character
 * which is no hexadecimal digit - is refused with an empty result instead of
 * being repaired.
 *
 * @param[in] text Text to normalize.
 * @return The canonical identifier, empty when the text is not an identifier.
 */
inline std::string NormalizeSandboxId(std::string_view text)
{
    std::string digits;
    digits.reserve(sandbox_id::kDigitCount);

    if (text.size() == sandbox_id::kDigitCount)
    {
        for (const char character : text)
        {
            if (!sandbox_id::IsHexDigit(character))
            {
                return {};
            }
            digits.push_back(sandbox_id::FoldAscii(character));
        }

        return sandbox_id::FormatDigits(digits);
    }

    if (text.size() != sandbox_id::kLength)
    {
        return {};
    }

    for (std::size_t position = 0; position < text.size(); ++position)
    {
        const char character = text[position];
        if (sandbox_id::IsHyphenPosition(position))
        {
            if (character != '-')
            {
                return {};
            }
            continue;
        }

        if (!sandbox_id::IsHexDigit(character))
        {
            return {};
        }
        digits.push_back(sandbox_id::FoldAscii(character));
    }

    return sandbox_id::FormatDigits(digits);
}

/**
 * @brief Generate an identifier in the canonical form.
 *
 * The digits are drawn from a random device, and the two fields of RFC 4122
 * which name the kind of the identifier are set afterwards: the version nibble
 * is `4` (a randomly generated UUID) and the two highest bits of the variant
 * octet are `10`, which is what the letters `8`, `9`, `a` and `b` carry.
 *
 * @return The generated identifier.
 */
inline std::string GenerateSandboxId()
{
    /* Positions of the two fields, counted in the digits of the text form. */
    constexpr std::size_t kVersionDigit = 12;
    constexpr std::size_t kVariantDigit = 16;

    std::random_device                         device;
    std::mt19937                               generator(device());
    std::uniform_int_distribution<std::size_t> distribution(0, sizeof(sandbox_id::kHexDigits) - 2);

    std::string digits;
    digits.reserve(sandbox_id::kDigitCount);
    for (std::size_t index = 0; index < sandbox_id::kDigitCount; ++index)
    {
        digits.push_back(sandbox_id::kHexDigits[distribution(generator)]);
    }

    digits[kVersionDigit] = '4';
    digits[kVariantDigit] = sandbox_id::kHexDigits[8 + distribution(generator) % 4];

    return sandbox_id::FormatDigits(digits);
}

/**
 * @brief Name of the control pipe of a sandbox.
 *
 * The name is the one the sandboxed processes connect to and the one a second
 * launcher of the same sandbox competes for, so it is derived from the
 * identifier alone: `\\.\pipe\appbox_<id>`, with the hyphens of the identifier
 * kept. The identifier itself is a random value and carries no path, so the
 * name never leaks where the sandbox lives on the host.
 *
 * @param[in] id Identifier of the sandbox.
 * @return The pipe name, empty when the identifier is not canonical.
 */
inline std::string SandboxPipeName(std::string_view id)
{
    if (!IsValidSandboxId(id))
    {
        return {};
    }

    std::string name = "\\\\.\\pipe\\appbox_";
    name.append(id);
    return name;
}

} // namespace appbox

#endif // APPBOX_COMMON_SANDBOX_ID_HPP
