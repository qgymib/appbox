#include "CompressionLevel.hpp"
#include <string>

namespace
{

/**
 * @brief One entry of the `Compression` box.
 *
 * The table is the single place which spells a compression level: the token of
 * the project file, the label of the box, the value the packer asks libzip for
 * and the order of the entries are derived from it, so the four of them cannot
 * drift apart.
 */
struct CompressionLevelEntry
{
    appbox::CompressionLevel level; ///< The level of the entry.
    const char*              token; ///< Token stored by the project file.
    const wchar_t*           label; ///< Label shown by the `Compression` box.
    int                      value; ///< zlib level the packer requests, `0` to store.
};

/** The entries of the `Compression` box, in the order of the box. */
constexpr CompressionLevelEntry kCompressionLevels[] = {
    { appbox::CompressionLevel::StoreOnly, "store_only", L"0: Store Only", 0 },
    { appbox::CompressionLevel::Fastest,   "fastest",    L"1: Fastest",    1 },
    { appbox::CompressionLevel::Fast,      "fast",       L"3: Fast",       3 },
    { appbox::CompressionLevel::Normal,    "normal",     L"5: Normal",     5 },
    { appbox::CompressionLevel::Maximum,   "maximum",    L"7: Maximum",    7 },
    { appbox::CompressionLevel::Extreme,   "extreme",    L"9: Extreme",    9 },
};

/** Number of entries of the table above. */
constexpr std::size_t kCompressionLevelCount = sizeof(kCompressionLevels) / sizeof(kCompressionLevels[0]);

/**
 * @brief Get the entry of the default compression level.
 * @return The entry the session uses when it does not pick one.
 */
const CompressionLevelEntry& DefaultEntry()
{
    for (const auto& entry : kCompressionLevels)
    {
        if (entry.level == appbox::kDefaultCompressionLevel)
        {
            return entry;
        }
    }

    return kCompressionLevels[0];
}

/**
 * @brief Get the entry of a compression level.
 *
 * The entry of the default level is returned for a level which the table does
 * not hold, so an unknown level never turns into an unexpected product: a level
 * which cannot be spelled still packs a compressed archive instead of a stored
 * one.
 *
 * @param[in] level The compression level.
 * @return The entry of the level.
 */
const CompressionLevelEntry& EntryOf(appbox::CompressionLevel level)
{
    for (const auto& entry : kCompressionLevels)
    {
        if (entry.level == level)
        {
            return entry;
        }
    }

    return DefaultEntry();
}

/**
 * @brief Lower the ASCII letters of a token.
 *
 * The tokens are ASCII, so the fold is done by hand instead of through a
 * locale aware conversion.
 *
 * @param[in] token The token to fold.
 * @return The token with every upper case letter lowered.
 */
std::string ToLowerToken(std::string_view token)
{
    std::string lowered;
    lowered.reserve(token.size());
    for (const char character : token)
    {
        lowered.push_back(character >= 'A' && character <= 'Z' ? static_cast<char>(character - 'A' + 'a') : character);
    }
    return lowered;
}

} // namespace

namespace appbox
{

const char* CompressionLevelToken(CompressionLevel level)
{
    return EntryOf(level).token;
}

bool ParseCompressionLevelToken(std::string_view token, CompressionLevel& level)
{
    const auto lowered = ToLowerToken(token);
    for (const auto& entry : kCompressionLevels)
    {
        if (lowered == entry.token)
        {
            level = entry.level;
            return true;
        }
    }

    return false;
}

const wchar_t* CompressionLevelDisplayName(CompressionLevel level)
{
    return EntryOf(level).label;
}

int CompressionLevelValue(CompressionLevel level)
{
    return EntryOf(level).value;
}

std::size_t CompressionLevelCount()
{
    return kCompressionLevelCount;
}

CompressionLevel CompressionLevelAt(std::size_t index)
{
    if (index >= kCompressionLevelCount)
    {
        return kDefaultCompressionLevel;
    }

    return kCompressionLevels[index].level;
}

std::size_t CompressionLevelIndexOf(CompressionLevel level)
{
    for (std::size_t index = 0; index < kCompressionLevelCount; ++index)
    {
        if (kCompressionLevels[index].level == level)
        {
            return index;
        }
    }

    /* A level which the box does not offer shows the default one. */
    for (std::size_t index = 0; index < kCompressionLevelCount; ++index)
    {
        if (kCompressionLevels[index].level == kDefaultCompressionLevel)
        {
            return index;
        }
    }

    return 0;
}

} // namespace appbox
