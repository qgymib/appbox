#include "TestSandboxId.hpp"
#include "SandboxId.hpp"
#include <cstddef>
#include <cstdint>
#include <string>

namespace
{

/** Basis of the FNV-1a hash of the first half of the identifier. */
constexpr std::uint64_t kFirstBasis = 14695981039346656037ull;

/**
 * @brief Basis of the second half of the identifier.
 *
 * The value is the golden ratio scaled to 64 bits, which spreads the second
 * half of the identifier away from the first one.
 */
constexpr std::uint64_t kSecondBasis = 11400714819323198485ull;

/** Prime of the FNV-1a hash. */
constexpr std::uint64_t kPrime = 1099511628211ull;

/**
 * @brief Hash a path into 64 bits with the FNV-1a hash.
 *
 * The hash only has to spread the paths of the cases, so a fast one which needs
 * no dependency is enough. The code units of the path are hashed as they are,
 * which keeps the result independent of the code page the case runs in.
 *
 * @param[in] text Text to hash.
 * @param[in] basis Start value of the hash, which selects one of the halves.
 * @return The digest of the text.
 */
std::uint64_t HashText(const std::wstring& text, std::uint64_t basis)
{
    std::uint64_t hash = basis;
    for (const wchar_t character : text)
    {
        hash ^= static_cast<std::uint64_t>(character);
        hash *= kPrime;
    }

    return hash;
}

/**
 * @brief Format a 64 bit value as 16 lower case hexadecimal digits.
 * @param[in] value Value to format.
 * @return The digits, without a prefix.
 */
std::string HexDigits(std::uint64_t value)
{
    constexpr char kDigits[] = "0123456789abcdef";

    std::string text(16, '0');
    for (std::size_t index = 0; index < text.size(); ++index)
    {
        const auto shift = static_cast<unsigned>(4 * (text.size() - 1 - index));
        text[index] = kDigits[static_cast<std::size_t>((value >> shift) & 0xF)];
    }

    return text;
}

/**
 * @brief Remove the separators a path may end with.
 *
 * The working directory of a case is passed with and without its trailing
 * separator by different helpers, and both spellings have to name one sandbox.
 *
 * @param[in] path Path to trim.
 * @return The path without its trailing separators.
 */
std::wstring TrimSeparators(const std::wstring& path)
{
    std::wstring trimmed = path;
    while (!trimmed.empty() && (trimmed.back() == L'\\' || trimmed.back() == L'/'))
    {
        trimmed.pop_back();
    }

    return trimmed;
}

} // namespace

std::string appbox::test::SandboxIdForPath(const std::wstring& path)
{
    const auto trimmed = TrimSeparators(path);

    /* Two digests fill the 32 digits of an identifier, which is the layout
       `common/SandboxId.hpp` turns into the canonical text form. */
    const auto digits = HexDigits(HashText(trimmed, kFirstBasis)) + HexDigits(HashText(trimmed, kSecondBasis));
    return appbox::NormalizeSandboxId(digits);
}
