#ifndef APPBOX_PACKER_CORE_COMPRESSION_LEVEL_HPP
#define APPBOX_PACKER_CORE_COMPRESSION_LEVEL_HPP

#include <cstddef>
#include <string_view>

namespace appbox
{

/**
 * @brief Compression strength of the archives the `Build` command writes.
 *
 * The level is chosen by the `Compression` box of the `Output` tab of the
 * Settings workspace and travels with the project file, so it is a property of
 * the session and not of the models of the workspaces. It decides how hard the
 * packer asks libzip to squeeze the entries of a standalone archive and of a
 * patch package; the archive format, the reader and the extractor are the same
 * whatever the level is.
 *
 * The value of a level is the zlib level of the deflate compression the packer
 * requests, and `StoreOnly` asks for entries which are written without any
 * compression at all. A deflate level keeps the smaller of the deflated and the
 * stored data of an entry, so an entry which does not compress stays readable
 * without wasting space.
 */
enum class CompressionLevel
{
    StoreOnly, ///< `0: Store Only` - the entries are stored without compression.
    Fastest,   ///< `1: Fastest` - the entries are deflated with zlib level 1.
    Fast,      ///< `3: Fast` - the entries are deflated with zlib level 3.
    Normal,    ///< `5: Normal` - the entries are deflated with zlib level 5.
    Maximum,   ///< `7: Maximum` - the entries are deflated with zlib level 7.
    Extreme    ///< `9: Extreme` - the entries are deflated with zlib level 9.
};

/**
 * @brief The level of a session which does not pick one.
 *
 * The level is the middle of the scale: it trades the size of the product for
 * the time the pack run takes. A project file which does not name the level and
 * an archive which is written without one describe a session which uses it.
 */
constexpr CompressionLevel kDefaultCompressionLevel = CompressionLevel::Normal;

/**
 * @brief Get the token of a compression level.
 *
 * The token is the text form of the level, which the project file stores and
 * reads.
 *
 * @param[in] level The compression level.
 * @return The canonical lower case token, for example `"normal"`.
 */
const char* CompressionLevelToken(CompressionLevel level);

/**
 * @brief Resolve a compression level from its token.
 *
 * The comparison ignores the case, so the tokens of the project file are read
 * whatever the case they were written with.
 *
 * @param[in] token The token to resolve.
 * @param[out] level The resolved level when the token is known.
 * @return true when the token names a compression level.
 */
bool ParseCompressionLevelToken(std::string_view token, CompressionLevel& level);

/**
 * @brief Get the label of a compression level as the `Compression` box shows it.
 *
 * The label names the level of the scale and the zlib level it asks for, so the
 * box and the project file never spell a level differently.
 *
 * @param[in] level The compression level.
 * @return The label, for example `L"5: Normal"`.
 */
const wchar_t* CompressionLevelDisplayName(CompressionLevel level);

/**
 * @brief Get the value of a compression level.
 *
 * The value is the zlib level the packer requests; the value of `StoreOnly` is
 * `0`, which asks for entries without any compression.
 *
 * @param[in] level The compression level.
 * @return The zlib level, one of `0`, `1`, `3`, `5`, `7` and `9`.
 */
int CompressionLevelValue(CompressionLevel level);

/**
 * @brief Get the number of entries the `Compression` box offers.
 * @return The number of compression levels.
 */
std::size_t CompressionLevelCount();

/**
 * @brief Get the compression level of one entry of the `Compression` box.
 *
 * The box stores the entries in the order of CompressionLevelIndexOf(), so the
 * selection of the box is the index of the level it shows.
 *
 * @param[in] index Index of the entry.
 * @return The level of the entry; the default level when the index is outside
 *         the box, so an unknown selection never turns into an unexpected
 *         product.
 */
CompressionLevel CompressionLevelAt(std::size_t index);

/**
 * @brief Get the index of a compression level inside the `Compression` box.
 * @param[in] level The compression level.
 * @return The index of the entry; the index of the default level when the level
 *         is not part of the box.
 */
std::size_t CompressionLevelIndexOf(CompressionLevel level);

} // namespace appbox

#endif // APPBOX_PACKER_CORE_COMPRESSION_LEVEL_HPP
