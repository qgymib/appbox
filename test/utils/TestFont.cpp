#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include "utils/ReadFileFull.hpp"
#include "TestFont.hpp"
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace
{

/**
 * @brief Shortest family name the helper rewrites.
 *
 * The replacement is `AppBoxTest` cut to the length of the family of the source
 * font, so a short family would leave a name which is too close to the source
 * to be a family of its own.
 */
constexpr std::size_t kMinFamilyLength = 8;

/**
 * @brief Family of the font which is built from a source of that length.
 * @param[in] length Length of the family of the source font.
 * @return The family, empty when the length is too short.
 */
std::wstring ReplacementName(std::size_t length)
{
    const std::wstring base = L"AppBoxTest";
    if (length < kMinFamilyLength)
    {
        return std::wstring();
    }

    std::wstring name;
    for (std::size_t index = 0; index < length; ++index)
    {
        name.push_back(index < base.size() ? base[index] : L'X');
    }
    return name;
}

/**
 * @brief Read a big endian 16 bit value of the file.
 * @param[in] data Content of the file.
 * @param[in] offset Offset of the value.
 * @return The value.
 */
std::uint16_t ReadU16(const std::vector<std::uint8_t>& data, std::size_t offset)
{
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(data[offset]) << 8) | data[offset + 1]);
}

/**
 * @brief Read a big endian 32 bit value of the file.
 * @param[in] data Content of the file.
 * @param[in] offset Offset of the value.
 * @return The value.
 */
std::uint32_t ReadU32(const std::vector<std::uint8_t>& data, std::size_t offset)
{
    return (static_cast<std::uint32_t>(data[offset]) << 24) | (static_cast<std::uint32_t>(data[offset + 1]) << 16) |
           (static_cast<std::uint32_t>(data[offset + 2]) << 8) | static_cast<std::uint32_t>(data[offset + 3]);
}

/**
 * @brief Write a big endian 32 bit value into the file.
 * @param[in,out] data Content of the file.
 * @param[in] offset Offset of the value.
 * @param[in] value Value to write.
 */
void WriteU32(std::vector<std::uint8_t>& data, std::size_t offset, std::uint32_t value)
{
    data[offset] = static_cast<std::uint8_t>((value >> 24) & 0xFF);
    data[offset + 1] = static_cast<std::uint8_t>((value >> 16) & 0xFF);
    data[offset + 2] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
    data[offset + 3] = static_cast<std::uint8_t>(value & 0xFF);
}

/**
 * @brief One entry of the table directory of a font.
 */
struct TableEntry
{
    std::size_t directory_offset = 0; /* Offset of the entry inside the file. */
    std::size_t offset = 0;           /* Offset of the table. */
    std::size_t length = 0;           /* Length of the table. */
};

/**
 * @brief Find a table of the font.
 * @param[in] data Content of the file.
 * @param[in] tag Four character tag of the table.
 * @param[out] entry The entry of the table.
 * @return true when the table is inside the file.
 */
bool FindTable(const std::vector<std::uint8_t>& data, const char* tag, TableEntry& entry)
{
    if (data.size() < 12)
    {
        return false;
    }

    const std::size_t count = ReadU16(data, 4);
    for (std::size_t index = 0; index < count; ++index)
    {
        const std::size_t offset = 12 + index * 16;
        if (offset + 16 > data.size())
        {
            return false;
        }
        if (memcmp(&data[offset], tag, 4) != 0)
        {
            continue;
        }

        entry.directory_offset = offset;
        entry.offset = ReadU32(data, offset + 8);
        entry.length = ReadU32(data, offset + 12);
        return entry.offset + entry.length <= data.size();
    }

    return false;
}

/**
 * @brief One record of the `name` table of a font.
 */
struct NameRecord
{
    std::uint16_t platform = 0;      /* Platform of the record. */
    std::uint16_t name_id = 0;       /* Identifier of the name. */
    std::size_t   string_offset = 0; /* Offset of the string inside the file. */
    std::size_t   length = 0;        /* Length of the string. */
};

/**
 * @brief Read the records of the `name` table.
 * @param[in] data Content of the file.
 * @param[in] name_table Entry of the `name` table.
 * @return The records which are inside the file.
 */
std::vector<NameRecord> ReadNameRecords(const std::vector<std::uint8_t>& data, const TableEntry& name_table)
{
    std::vector<NameRecord> records;
    if (name_table.length < 6)
    {
        return records;
    }

    const std::size_t count = ReadU16(data, name_table.offset + 2);
    const std::size_t string_offset = ReadU16(data, name_table.offset + 4);

    for (std::size_t index = 0; index < count; ++index)
    {
        const std::size_t offset = name_table.offset + 6 + index * 12;
        if (offset + 12 > name_table.offset + name_table.length)
        {
            break;
        }

        NameRecord record;
        record.platform = ReadU16(data, offset);
        record.name_id = ReadU16(data, offset + 6);
        record.length = ReadU16(data, offset + 8);
        record.string_offset = name_table.offset + string_offset + ReadU16(data, offset + 10);

        if (record.string_offset + record.length > data.size())
        {
            continue;
        }
        records.push_back(record);
    }

    return records;
}

/**
 * @brief Decode the string of a name record.
 * @param[in] data Content of the file.
 * @param[in] record Record to decode.
 * @return The text of the record.
 */
std::wstring DecodeName(const std::vector<std::uint8_t>& data, const NameRecord& record)
{
    std::wstring text;

    if (record.platform == 3)
    {
        /* The platform of Windows stores the text in UTF-16 with big endian. */
        for (std::size_t index = 0; index + 1 < record.length; index += 2)
        {
            text.push_back(static_cast<wchar_t>((static_cast<std::uint16_t>(data[record.string_offset + index]) << 8) |
                                                data[record.string_offset + index + 1]));
        }
        return text;
    }

    for (std::size_t index = 0; index < record.length; ++index)
    {
        text.push_back(static_cast<wchar_t>(data[record.string_offset + index]));
    }
    return text;
}

/**
 * @brief Encode a text the way a name record of a platform stores it.
 * @param[in] text Text to encode.
 * @param[in] platform Platform of the record.
 * @param[out] bytes The encoded text.
 * @return true when every character is part of the encoding.
 */
bool EncodeName(const std::wstring& text, std::uint16_t platform, std::vector<std::uint8_t>& bytes)
{
    bytes.clear();

    if (platform == 3)
    {
        for (const wchar_t character : text)
        {
            bytes.push_back(static_cast<std::uint8_t>((static_cast<std::uint16_t>(character) >> 8) & 0xFF));
            bytes.push_back(static_cast<std::uint8_t>(static_cast<std::uint16_t>(character) & 0xFF));
        }
        return true;
    }

    for (const wchar_t character : text)
    {
        if (character > 0xFF)
        {
            return false;
        }
        bytes.push_back(static_cast<std::uint8_t>(character));
    }
    return true;
}

/**
 * @brief Checksum of a range of the file, see the TrueType specification.
 * @param[in] data Content of the file.
 * @param[in] offset Offset of the range.
 * @param[in] length Length of the range, padded with zeros to four bytes.
 * @return The checksum.
 */
std::uint32_t RangeChecksum(const std::vector<std::uint8_t>& data, std::size_t offset, std::size_t length)
{
    std::uint32_t total = 0;

    for (std::size_t index = 0; index < length; index += 4)
    {
        std::uint32_t word = 0;
        for (std::size_t byte = 0; byte < 4; ++byte)
        {
            const std::size_t   position = index + byte;
            const std::uint32_t value = position < length ? data[offset + position] : 0;
            word = (word << 8) | value;
        }
        total += word;
    }

    return total;
}

/**
 * @brief Recompute the checksums the rewrite of the `name` table invalidates.
 * @param[in,out] data Content of the file.
 * @param[in] name_table Entry of the `name` table.
 * @param[in] head_table Entry of the `head` table.
 */
void FixChecksums(std::vector<std::uint8_t>& data, const TableEntry& name_table, const TableEntry& head_table)
{
    WriteU32(data, name_table.directory_offset + 4, RangeChecksum(data, name_table.offset, name_table.length));

    /*
     * The adjustment of the head table is the value which makes the checksum of
     * the whole file `0xB1B0AFBA`; the field itself is left out while the sums
     * are taken.
     */
    WriteU32(data, head_table.offset + 8, 0);
    WriteU32(data, head_table.directory_offset + 4, RangeChecksum(data, head_table.offset, head_table.length));
    WriteU32(data, head_table.offset + 8, 0xB1B0AFBA - RangeChecksum(data, 0, data.size()));
}

/**
 * @brief Collect the family names the font table of a device context carries.
 * @param[in] logfont Font the enumeration reports.
 * @param[in] metric Metrics of the font, unused.
 * @param[in] type Type of the font, unused.
 * @param[in] param Vector of names.
 * @return 1, which keeps the enumeration going.
 */
int CALLBACK CollectFamily(const LOGFONTW* logfont, const TEXTMETRICW* /*metric*/, DWORD /*type*/, LPARAM param)
{
    auto* names = reinterpret_cast<std::vector<std::wstring>*>(param);
    names->emplace_back(logfont->lfFaceName);
    return 1;
}

} // namespace

bool appbox::test::HostCarriesFamily(const std::wstring& family)
{
    HDC dc = CreateCompatibleDC(nullptr);
    if (dc == nullptr)
    {
        return false;
    }

    LOGFONTW logfont = {};
    logfont.lfCharSet = DEFAULT_CHARSET;

    std::vector<std::wstring> names;
    EnumFontFamiliesExW(dc, &logfont, CollectFamily, reinterpret_cast<LPARAM>(&names), 0);
    DeleteDC(dc);

    for (const auto& name : names)
    {
        if (CompareStringOrdinal(name.c_str(), -1, family.c_str(), -1, TRUE) == CSTR_EQUAL)
        {
            return true;
        }
    }

    return false;
}

bool appbox::test::MakeTestFont(TestFont& font, std::string& error, std::size_t skip)
{
    /*
     * The candidates are fonts a standard installation carries; the family of
     * the file has to be long enough to hold the replacement, which the loop
     * checks for every candidate.
     */
    const wchar_t* const candidates[] = {
        L"cour.ttf", L"times.ttf", L"trebuc.ttf", L"comic.ttf", L"micross.ttf", L"pala.ttf", L"consola.ttf",
    };

    wchar_t windows_directory[MAX_PATH] = {};
    if (GetWindowsDirectoryW(windows_directory, MAX_PATH) == 0)
    {
        error = "the directory of the system cannot be resolved";
        return false;
    }

    const std::wstring font_directory = std::wstring(windows_directory) + L"\\Fonts";
    std::string        last_error = "no installed font carries a family which can be rewritten";

    for (const auto* candidate : candidates)
    {
        const std::wstring path = font_directory + L"\\" + candidate;

        std::vector<std::uint8_t> data;
        if (ReadFileFull(path, data) != 0)
        {
            continue;
        }

        TableEntry name_table;
        TableEntry head_table;
        if (!FindTable(data, "name", name_table) || !FindTable(data, "head", head_table))
        {
            last_error = "an installed font carries no name or head table";
            continue;
        }

        const std::vector<NameRecord> records = ReadNameRecords(data, name_table);

        /* The family of the source font, taken from the platform of Windows. */
        std::wstring family;
        for (const auto& record : records)
        {
            if (record.platform == 3 && record.name_id == 1)
            {
                family = DecodeName(data, record);
                break;
            }
        }
        if (family.empty())
        {
            for (const auto& record : records)
            {
                if (record.name_id == 1)
                {
                    family = DecodeName(data, record);
                    break;
                }
            }
        }

        const std::wstring replacement = ReplacementName(family.size());
        if (replacement.empty() || HostCarriesFamily(replacement))
        {
            last_error = "no installed font carries a family which can hold the replacement";
            continue;
        }

        /* A case which builds a second font leaves the candidates of the first
         * one out, so the two fonts carry different families. */
        if (skip > 0)
        {
            --skip;
            continue;
        }

        /*
         * Every record which carries the family is rewritten, so the family of
         * the built font is the replacement for the enumeration of the font
         * table and for the name of a face which is created for it. The length
         * of a record never changes, which keeps the offsets of the file.
         */
        std::size_t patched = 0;
        for (const auto& record : records)
        {
            if (DecodeName(data, record) != family)
            {
                continue;
            }

            std::vector<std::uint8_t> encoded;
            if (!EncodeName(replacement, record.platform, encoded) || encoded.size() != record.length)
            {
                continue;
            }

            std::copy(encoded.begin(), encoded.end(), data.begin() + static_cast<std::ptrdiff_t>(record.string_offset));
            ++patched;
        }

        if (patched == 0)
        {
            last_error = "no name record of an installed font could be rewritten";
            continue;
        }

        FixChecksums(data, name_table, head_table);

        font.family = replacement;
        font.bytes = std::move(data);
        return true;
    }

    error = last_error;
    return false;
}
