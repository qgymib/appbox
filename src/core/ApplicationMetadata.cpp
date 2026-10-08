#include "ApplicationMetadata.hpp"
#include "PeResourcePatch.hpp"
#include "WString.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace
{

using appbox::pe_resource::kUpdateAttempts;
using appbox::pe_resource::LoadAsDataFile;
using appbox::pe_resource::LoadedImage;
using appbox::pe_resource::LooksLikePeImage;
using appbox::pe_resource::ReadFileBytes;
using appbox::pe_resource::TemporaryFile;
using appbox::pe_resource::TemporaryPath;
using appbox::pe_resource::UpdateSession;
using appbox::pe_resource::WaitForResourceUpdate;
using appbox::pe_resource::WriteFileBytes;

/** Resource id of the version resource which is written. */
constexpr WORD kVersionResourceId = 1;

/**
 * @brief Largest length a node of a version resource can describe.
 *
 * Every node of the resource stores its own length in a 16 bit member, so a
 * block which does not fit is rejected instead of being written with a
 * truncated length.
 */
constexpr std::size_t kMaxNodeSize = 0xFFFF;

/** Number of parts of a dotted version, e.g. the four parts of `1.2.3.4`. */
constexpr std::size_t kVersionParts = 4;

/** Signature of the fixed part of a version resource. */
constexpr std::uint32_t kFixedFileInfoSignature = 0xFEEF04BD;

/** Version of the structure of the fixed part, which is 1.0. */
constexpr std::uint32_t kFixedFileInfoStructureVersion = 0x00010000;

/** Flags the fixed part declares to know: the debug and the prerelease flag. */
constexpr std::uint32_t kFixedFileInfoFlagMask = 0x3F;

/** Operating system of the fixed part: Windows NT on 32 bit Windows. */
constexpr std::uint32_t kFixedFileInfoOs = 0x00040004;

/** Kind of the file of the fixed part: an application. */
constexpr std::uint32_t kFixedFileInfoType = 0x00000001;

/** Size of the fixed part of a version resource in bytes. */
constexpr std::size_t kFixedFileInfoSize = 13 * sizeof(std::uint32_t);

/** Path of the translation entry of a version resource. */
constexpr const wchar_t* const kTranslationPath = L"\\VarFileInfo\\Translation";

/** Key of the root node of a version resource. */
constexpr const wchar_t* const kVersionInfoKey = L"VS_VERSION_INFO";

/** Key of the string table node of a version resource. */
constexpr const wchar_t* const kStringFileInfoKey = L"StringFileInfo";

/** Key of the variable node of a version resource. */
constexpr const wchar_t* const kVarFileInfoKey = L"VarFileInfo";

/** Key of the translation entry inside the variable node. */
constexpr const wchar_t* const kTranslationKey = L"Translation";

/**
 * @brief Append a 16 bit value in little endian order.
 * @param[in,out] bytes Buffer to append to.
 * @param[in] value Value to append.
 */
void AppendU16(std::vector<char>& bytes, std::uint16_t value)
{
    bytes.push_back(static_cast<char>(value & 0xFF));
    bytes.push_back(static_cast<char>((value >> 8) & 0xFF));
}

/**
 * @brief Append a 32 bit value in little endian order.
 * @param[in,out] bytes Buffer to append to.
 * @param[in] value Value to append.
 */
void AppendU32(std::vector<char>& bytes, std::uint32_t value)
{
    AppendU16(bytes, static_cast<std::uint16_t>(value & 0xFFFF));
    AppendU16(bytes, static_cast<std::uint16_t>(value >> 16));
}

/**
 * @brief Append wide text.
 * @param[in,out] bytes Buffer to append to.
 * @param[in] text Text to append.
 * @param[in] terminated Whether the terminating null character is appended.
 */
void AppendText(std::vector<char>& bytes, const std::wstring& text, bool terminated)
{
    const std::size_t count = text.size() + (terminated ? 1 : 0);
    const auto* const data = reinterpret_cast<const char*>(text.c_str());
    bytes.insert(bytes.end(), data, data + count * sizeof(wchar_t));
}

/**
 * @brief Append the content of another buffer.
 * @param[in,out] bytes Buffer to append to.
 * @param[in] other Buffer to append.
 */
void AppendBytes(std::vector<char>& bytes, const std::vector<char>& other)
{
    bytes.insert(bytes.end(), other.begin(), other.end());
}

/**
 * @brief Pad a buffer to the next 32 bit boundary.
 *
 * The nodes of a version resource are aligned on 32 bit boundaries, which is
 * the alignment the resource compiler produces as well.
 *
 * @param[in,out] bytes Buffer to pad.
 */
void AlignTo4(std::vector<char>& bytes)
{
    while (bytes.size() % 4 != 0)
    {
        bytes.push_back('\0');
    }
}

/**
 * @brief Store a 16 bit value at an offset of a buffer.
 * @param[in,out] bytes Buffer to change.
 * @param[in] offset Offset of the value.
 * @param[in] value Value to store.
 */
void StoreU16(std::vector<char>& bytes, std::size_t offset, std::uint16_t value)
{
    bytes[offset] = static_cast<char>(value & 0xFF);
    bytes[offset + 1] = static_cast<char>((value >> 8) & 0xFF);
}

/**
 * @brief Format one 16 bit value as four hexadecimal digits.
 * @param[in] value Value to format.
 * @return The four digits, in upper case.
 */
std::wstring HexWord(std::uint16_t value)
{
    const wchar_t digits[] = L"0123456789ABCDEF";

    std::wstring text(4, L'0');
    for (std::size_t index = 0; index < text.size(); ++index)
    {
        const auto shift = static_cast<unsigned>(3 - index) * 4;
        text[index] = digits[(value >> shift) & 0xF];
    }

    return text;
}

/**
 * @brief Build the path of one field inside a version resource.
 *
 * @param[in] translation Language and code page of the resource.
 * @param[in] key Key of the field.
 * @return The path, e.g. `\StringFileInfo\040904B0\FileDescription`.
 */
std::wstring FieldPath(const appbox::MetadataTranslation& translation, const std::string& key)
{
    return std::wstring(L"\\") + kStringFileInfoKey + L"\\" + HexWord(translation.language) +
           HexWord(translation.code_page) + L"\\" + appbox::UTF8ToWide(key);
}

/**
 * @brief Parse a dotted version text into its four parts.
 *
 * A text which is not a dotted version yields zeros, and a part which does not
 * fit into a 16 bit value is clamped. Parsing stops at the first character
 * which is neither a digit nor a dot, so a text like `1.2.3.4 (build 7)` is
 * accepted as well.
 *
 * @param[in] text Text to parse.
 * @param[out] parts The four parts of the version, in order.
 */
void ParseVersionText(const std::wstring& text, std::uint16_t parts[kVersionParts])
{
    for (std::size_t index = 0; index < kVersionParts; ++index)
    {
        parts[index] = 0;
    }

    std::size_t position = 0;
    for (std::size_t part = 0; part < kVersionParts; ++part)
    {
        if (position >= text.size())
        {
            return;
        }
        if (text[position] == L'.')
        {
            /* An empty part, e.g. the second part of `1..2`, stays zero. */
            ++position;
            continue;
        }
        if (text[position] < L'0' || text[position] > L'9')
        {
            return;
        }

        std::uint32_t value = 0;
        bool          clamped = false;
        while (position < text.size() && text[position] >= L'0' && text[position] <= L'9')
        {
            if (!clamped)
            {
                value = value * 10 + static_cast<std::uint32_t>(text[position] - L'0');
                clamped = value > 0xFFFF;
            }
            ++position;
        }

        parts[part] = clamped ? 0xFFFF : static_cast<std::uint16_t>(value);
    }
}

/**
 * @brief Combine two parts of a version into the 32 bit form of the fixed part.
 * @param[in] high High part of the version.
 * @param[in] low Low part of the version.
 * @return The combined value.
 */
std::uint32_t CombineVersion(std::uint16_t high, std::uint16_t low)
{
    return (static_cast<std::uint32_t>(high) << 16) | static_cast<std::uint32_t>(low);
}

/**
 * @brief Build the fixed part of a version resource.
 *
 * The version numbers are derived from the version fields of the resource: a
 * shell which reads the fixed part instead of the string fields would
 * otherwise show a version which contradicts the text of the field.
 *
 * @param[in] fields Fields of the resource.
 * @return The 52 bytes of the fixed part.
 */
std::vector<char> BuildFixedFileInfo(const std::vector<appbox::MetadataField>& fields)
{
    std::uint16_t file_version[kVersionParts] = {};
    std::uint16_t product_version[kVersionParts] = {};

    if (const auto* text = appbox::FindMetadataValue(fields, appbox::metadata_field::kFileVersion))
    {
        ParseVersionText(*text, file_version);
    }
    if (const auto* text = appbox::FindMetadataValue(fields, appbox::metadata_field::kProductVersion))
    {
        ParseVersionText(*text, product_version);
    }

    std::vector<char> fixed;
    AppendU32(fixed, kFixedFileInfoSignature);
    AppendU32(fixed, kFixedFileInfoStructureVersion);
    AppendU32(fixed, CombineVersion(file_version[0], file_version[1]));
    AppendU32(fixed, CombineVersion(file_version[2], file_version[3]));
    AppendU32(fixed, CombineVersion(product_version[0], product_version[1]));
    AppendU32(fixed, CombineVersion(product_version[2], product_version[3]));
    AppendU32(fixed, kFixedFileInfoFlagMask);
    AppendU32(fixed, 0); /* dwFileFlags: the file is neither debug nor prerelease. */
    AppendU32(fixed, kFixedFileInfoOs);
    AppendU32(fixed, kFixedFileInfoType);
    AppendU32(fixed, 0); /* dwFileSubtype: an application has none. */
    AppendU32(fixed, 0); /* dwFileDateMS */
    AppendU32(fixed, 0); /* dwFileDateLS */
    return fixed;
}

/**
 * @brief Build one string node of a version resource.
 *
 * @param[in] key Key of the field, e.g. `FileDescription`.
 * @param[in] value Value of the field.
 * @return The node, empty when it does not fit into one resource node.
 */
std::vector<char> BuildStringNode(const std::string& key, const std::wstring& value)
{
    std::vector<char> node;
    AppendU16(node, 0);                                            /* wLength, patched below. */
    AppendU16(node, static_cast<std::uint16_t>(value.size() + 1)); /* wValueLength, in characters. */
    AppendU16(node, 1);                                            /* wType: text. */
    AppendText(node, appbox::UTF8ToWide(key), true);
    AlignTo4(node);
    AppendText(node, value, true);
    AlignTo4(node);

    if (node.size() > kMaxNodeSize)
    {
        return {};
    }

    StoreU16(node, 0, static_cast<std::uint16_t>(node.size()));
    return node;
}

/**
 * @brief Build the string table of a version resource.
 *
 * @param[in] translation Language and code page of the resource.
 * @param[in] fields Fields of the resource.
 * @return The node, empty when it does not fit into one resource node.
 */
std::vector<char> BuildStringTableNode(const appbox::MetadataTranslation&        translation,
                                       const std::vector<appbox::MetadataField>& fields)
{
    std::vector<char> node;
    AppendU16(node, 0); /* wLength, patched below. */
    AppendU16(node, 0); /* wValueLength: a container has no value. */
    AppendU16(node, 1); /* wType: text. */
    AppendText(node, HexWord(translation.language) + HexWord(translation.code_page), true);
    AlignTo4(node);

    for (const auto& field : fields)
    {
        const auto child = BuildStringNode(field.key, field.value);
        if (child.empty())
        {
            return {};
        }
        AppendBytes(node, child);
    }

    if (node.size() > kMaxNodeSize)
    {
        return {};
    }

    StoreU16(node, 0, static_cast<std::uint16_t>(node.size()));
    return node;
}

/**
 * @brief Build the string file info node of a version resource.
 * @param[in] string_table The string table of the resource.
 * @return The node, empty when it does not fit into one resource node.
 */
std::vector<char> BuildStringFileInfoNode(const std::vector<char>& string_table)
{
    std::vector<char> node;
    AppendU16(node, 0); /* wLength, patched below. */
    AppendU16(node, 0); /* wValueLength: a container has no value. */
    AppendU16(node, 1); /* wType: text. */
    AppendText(node, kStringFileInfoKey, true);
    AlignTo4(node);
    AppendBytes(node, string_table);

    if (node.size() > kMaxNodeSize)
    {
        return {};
    }

    StoreU16(node, 0, static_cast<std::uint16_t>(node.size()));
    return node;
}

/**
 * @brief Build the variable file info node of a version resource.
 *
 * The node carries the translation of the resource, which is the language and
 * the code page its strings are written in.
 *
 * @param[in] translation Language and code page of the resource.
 * @return The node, empty when it does not fit into one resource node.
 */
std::vector<char> BuildVarFileInfoNode(const appbox::MetadataTranslation& translation)
{
    std::vector<char> var;
    AppendU16(var, 0);                     /* wLength, patched below. */
    AppendU16(var, sizeof(std::uint32_t)); /* wValueLength: the two words of the translation. */
    AppendU16(var, 0);                     /* wType: binary. */
    AppendText(var, kTranslationKey, true);
    AlignTo4(var);
    AppendU16(var, translation.language);
    AppendU16(var, translation.code_page);
    AlignTo4(var);
    StoreU16(var, 0, static_cast<std::uint16_t>(var.size()));

    std::vector<char> node;
    AppendU16(node, 0); /* wLength, patched below. */
    AppendU16(node, 0); /* wValueLength: a container has no value. */
    AppendU16(node, 0); /* wType: binary. */
    AppendText(node, kVarFileInfoKey, true);
    AlignTo4(node);
    AppendBytes(node, var);

    if (node.size() > kMaxNodeSize)
    {
        return {};
    }

    StoreU16(node, 0, static_cast<std::uint16_t>(node.size()));
    return node;
}

/**
 * @brief Build the whole version resource of one image.
 *
 * @param[in] info The version information to write.
 * @return The content of the `RT_VERSION` resource, empty when the information
 *         does not fit into one resource node.
 */
std::vector<char> BuildVersionResource(const appbox::ApplicationVersionInfo& info)
{
    const auto string_table = BuildStringTableNode(info.translation, info.fields);
    if (string_table.empty())
    {
        return {};
    }

    const auto string_file_info = BuildStringFileInfoNode(string_table);
    if (string_file_info.empty())
    {
        return {};
    }

    const auto var_file_info = BuildVarFileInfoNode(info.translation);
    if (var_file_info.empty())
    {
        return {};
    }

    std::vector<char> block;
    AppendU16(block, 0);                                              /* wLength, patched below. */
    AppendU16(block, static_cast<std::uint16_t>(kFixedFileInfoSize)); /* wValueLength: the fixed part. */
    AppendU16(block, 0);                                              /* wType: binary. */
    AppendText(block, kVersionInfoKey, true);
    AlignTo4(block);
    AppendBytes(block, BuildFixedFileInfo(info.fields));
    AlignTo4(block);
    AppendBytes(block, string_file_info);
    AppendBytes(block, var_file_info);

    if (block.size() > kMaxNodeSize)
    {
        return {};
    }

    StoreU16(block, 0, static_cast<std::uint16_t>(block.size()));
    return block;
}

/**
 * @brief One `RT_VERSION` entry of an image.
 */
struct VersionResourceEntry
{
    bool         numeric = false; /* Whether the entry is addressed by id. */
    WORD         id = 0;          /* Resource id of a numeric entry. */
    std::wstring name;            /* Resource name of a named entry. */
    WORD         language = 0;    /* Language of the entry. */
};

/**
 * @brief Context of the enumeration of the version resources of an image.
 */
struct VersionResourceEnumeration
{
    std::vector<VersionResourceEntry>* entries = nullptr; /* Entries which were found. */
};

/**
 * @brief Callback collecting one language of a version resource.
 *
 * The name of the entry is a constant here, which is what the enumeration of
 * the languages of a resource declares; the enumeration of the names of a
 * resource declares a mutable one instead.
 *
 * @param[in] module Module of the enumeration, unused.
 * @param[in] type Resource type of the enumeration, unused.
 * @param[in] name Resource name or id.
 * @param[in] language Language of the entry.
 * @param[in] param The VersionResourceEnumeration collecting the entries.
 * @return TRUE to continue the enumeration.
 */
BOOL CALLBACK CollectVersionLanguage(HMODULE module, LPCWSTR type, LPCWSTR name, WORD language, LONG_PTR param)
{
    static_cast<void>(module);
    static_cast<void>(type);

    auto* enumeration = reinterpret_cast<VersionResourceEnumeration*>(param);

    VersionResourceEntry entry;
    entry.numeric = IS_INTRESOURCE(name) != FALSE;
    entry.id = entry.numeric ? static_cast<WORD>(reinterpret_cast<ULONG_PTR>(name)) : 0;
    entry.name = entry.numeric ? std::wstring() : std::wstring(name);
    entry.language = language;
    enumeration->entries->push_back(std::move(entry));
    return TRUE;
}

/**
 * @brief Callback collecting the languages of one version resource.
 *
 * @param[in] module Module of the enumeration.
 * @param[in] type Resource type of the enumeration.
 * @param[in] name Resource name or id.
 * @param[in] param The VersionResourceEnumeration collecting the entries.
 * @return TRUE to continue the enumeration.
 */
BOOL CALLBACK CollectVersionName(HMODULE module, LPCWSTR type, LPWSTR name, LONG_PTR param)
{
    EnumResourceLanguagesW(module, type, name, CollectVersionLanguage, param);
    return TRUE;
}

/**
 * @brief Get the version resources of an image.
 *
 * @param[in] path Host path of the image.
 * @param[out] entries The entries which were found, in resource order.
 * @param[out] error Error description on failure.
 * @return true when the image was read.
 */
bool VersionResources(const std::filesystem::path& path, std::vector<VersionResourceEntry>& entries, std::string& error)
{
    entries.clear();

    LoadedImage image(LoadAsDataFile(path.wstring(), error));
    if (!image)
    {
        return false;
    }

    VersionResourceEnumeration enumeration{ &entries };
    EnumResourceNamesW(image.Get(), RT_VERSION, CollectVersionName, reinterpret_cast<LONG_PTR>(&enumeration));
    return true;
}

/**
 * @brief Write the version resource of an image.
 *
 * Every version resource the image already carries is dropped first, so the
 * image never holds two of them and the shell cannot show the information of
 * the launcher instead of the one which was written.
 *
 * @param[in] path Host path of the image to patch.
 * @param[in] block Content of the version resource.
 * @param[in] language Language of the resource.
 * @param[in] existing Version resources the image carries.
 * @param[out] error Error description on failure.
 * @param[out] code Error code of the resource API on failure.
 * @return true when the resource was written.
 */
bool AddVersionResource(const std::filesystem::path& path, const std::vector<char>& block, WORD language,
                        const std::vector<VersionResourceEntry>& existing, std::string& error, DWORD& code)
{
    code = 0;

    const HANDLE update = BeginUpdateResourceW(path.c_str(), FALSE);
    if (update == nullptr)
    {
        code = GetLastError();
        error = "failed to open '" + appbox::WideToUTF8(path.wstring()) + "' for the version update (error " +
                std::to_string(code) + ")";
        return false;
    }

    UpdateSession session(update);
    for (const auto& entry : existing)
    {
        const auto name = entry.numeric ? MAKEINTRESOURCEW(entry.id) : entry.name.c_str();
        if (!UpdateResourceW(session.Get(), RT_VERSION, name, entry.language, nullptr, 0))
        {
            code = GetLastError();
            error = "failed to drop the version resource of '" + appbox::WideToUTF8(path.wstring()) + "' (error " +
                    std::to_string(code) + ")";
            return false;
        }
    }

    if (!UpdateResourceW(session.Get(), RT_VERSION, MAKEINTRESOURCEW(kVersionResourceId), language,
                         const_cast<char*>(block.data()), static_cast<DWORD>(block.size())))
    {
        code = GetLastError();
        error = "failed to write the version resource (error " + std::to_string(code) + ")";
        return false;
    }

    if (!session.Commit())
    {
        code = GetLastError();
        error = "failed to write the version resource of '" + appbox::WideToUTF8(path.wstring()) + "' (error " +
                std::to_string(code) + ")";
        return false;
    }

    return true;
}

/**
 * @brief Write the version resource of an image, repeating a denied attempt.
 *
 * The resource update of an image which was written moments ago can be denied
 * while a file system filter - the on access scanner of an antivirus product
 * for example - still holds the file, so a denied update is repeated with a
 * growing wait before it is given up.
 *
 * @param[in] path Host path of the image to patch.
 * @param[in] block Content of the version resource.
 * @param[in] language Language of the resource.
 * @param[out] error Error description on failure.
 * @return true when the resource was written.
 */
bool WriteVersionResource(const std::filesystem::path& path, const std::vector<char>& block, WORD language,
                          std::string& error)
{
    std::vector<VersionResourceEntry> existing;
    if (!VersionResources(path, existing, error))
    {
        return false;
    } /* The mapping has to be released before the file can be written. */

    for (int attempt = 0; attempt < kUpdateAttempts; ++attempt)
    {
        if (attempt > 0)
        {
            WaitForResourceUpdate(attempt);
        }

        std::string attempt_error;
        DWORD       code = 0;
        if (AddVersionResource(path, block, language, existing, attempt_error, code))
        {
            /* An attempt which succeeded clears the reason of an earlier one. */
            error.clear();
            return true;
        }

        error = std::move(attempt_error);
        if (code != ERROR_ACCESS_DENIED && code != ERROR_SHARING_VIOLATION)
        {
            break;
        }
    }

    return false;
}

} // namespace

namespace appbox
{

const std::vector<std::string>& MetadataFields()
{
    static const std::vector<std::string> fields = {
        metadata_field::kFileDescription, metadata_field::kFileVersion,      metadata_field::kProductName,
        metadata_field::kProductVersion,  metadata_field::kCompanyName,      metadata_field::kLegalCopyright,
        metadata_field::kInternalName,    metadata_field::kOriginalFilename, metadata_field::kComments,
        metadata_field::kLegalTrademarks, metadata_field::kPrivateBuild,     metadata_field::kSpecialBuild,
    };
    return fields;
}

const std::vector<std::string>& CommonMetadataFields()
{
    static const std::vector<std::string> fields(
        MetadataFields().begin(), MetadataFields().begin() + static_cast<std::ptrdiff_t>(kCommonMetadataFields));
    return fields;
}

bool IsMetadataField(const std::string& key)
{
    const auto& fields = MetadataFields();
    return std::find(fields.begin(), fields.end(), key) != fields.end();
}

std::string MetadataFieldLabel(const std::string& key)
{
    if (key == metadata_field::kFileDescription)
    {
        return "File description";
    }
    if (key == metadata_field::kFileVersion)
    {
        return "File version";
    }
    if (key == metadata_field::kProductName)
    {
        return "Product name";
    }
    if (key == metadata_field::kProductVersion)
    {
        return "Product version";
    }
    if (key == metadata_field::kCompanyName)
    {
        return "Company name";
    }
    if (key == metadata_field::kLegalCopyright)
    {
        return "Copyright";
    }
    if (key == metadata_field::kInternalName)
    {
        return "Internal name";
    }
    if (key == metadata_field::kOriginalFilename)
    {
        return "Original filename";
    }
    if (key == metadata_field::kComments)
    {
        return "Comments";
    }
    if (key == metadata_field::kLegalTrademarks)
    {
        return "Legal trademarks";
    }
    if (key == metadata_field::kPrivateBuild)
    {
        return "Private build";
    }
    if (key == metadata_field::kSpecialBuild)
    {
        return "Special build";
    }

    return {};
}

const std::wstring* FindMetadataValue(const std::vector<MetadataField>& fields, const std::string& key)
{
    const auto found =
        std::find_if(fields.begin(), fields.end(), [&key](const MetadataField& field) { return field.key == key; });
    return found != fields.end() ? &found->value : nullptr;
}

void SetMetadataValue(std::vector<MetadataField>& fields, const std::string& key, const std::wstring& value)
{
    for (auto& field : fields)
    {
        if (field.key == key)
        {
            field.value = value;
            return;
        }
    }

    fields.push_back(MetadataField{ key, value });
}

bool ApplicationMetadata::IsEmpty() const
{
    return source.empty() && overrides.empty();
}

std::wstring DefaultMetadataSource(const PackModel& model)
{
    /*
     * The launcher of the archive carries the file name of the main program, so
     * the program which starts the application is the one its version
     * information should describe. Programs which are not marked for auto start
     * are not candidates, and the first executable name decides the choice, so
     * the default does not depend on the order the files were added in.
     */
    const StartupFile* choice = nullptr;
    std::wstring       choice_name;

    for (const auto& file : model.StartupFiles())
    {
        if (!file.auto_start)
        {
            continue;
        }

        const auto name = std::filesystem::path(file.relative_path).filename().wstring();
        if (choice == nullptr || name < choice_name)
        {
            choice = &file;
            choice_name = name;
        }
    }

    if (choice == nullptr)
    {
        return {};
    }

    std::wstring path;
    if (!model.StartupFilePath(*choice, path))
    {
        return {};
    }

    return path;
}

std::wstring MetadataSourcePath(const PackModel& model, const ApplicationMetadata& metadata)
{
    return metadata.source.empty() ? DefaultMetadataSource(model) : metadata.source;
}

bool ReadApplicationMetadata(const std::wstring& path, ApplicationVersionInfo& info, std::string& error)
{
    info = ApplicationVersionInfo{};
    error.clear();

    if (path.empty())
    {
        error = "the source program of the version information is empty";
        return false;
    }

    const auto shown = WideToUTF8(path);

    DWORD       handle = 0;
    const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &handle);
    if (size == 0)
    {
        error = "'" + shown + "' carries no version information (error " + std::to_string(GetLastError()) + ")";
        return false;
    }

    std::vector<char> buffer(size);
    if (!GetFileVersionInfoW(path.c_str(), 0, size, buffer.data()))
    {
        error =
            "the version information of '" + shown + "' cannot be read (error " + std::to_string(GetLastError()) + ")";
        return false;
    }

    void* value = nullptr;
    UINT  length = 0;
    if (VerQueryValueW(buffer.data(), kTranslationPath, &value, &length) != FALSE && value != nullptr &&
        length >= 2 * sizeof(WORD))
    {
        const auto* words = static_cast<const WORD*>(value);
        info.translation.language = words[0];
        info.translation.code_page = words[1];
    }

    for (const auto& key : MetadataFields())
    {
        const auto path_in_resource = FieldPath(info.translation, key);

        value = nullptr;
        length = 0;
        if (VerQueryValueW(buffer.data(), path_in_resource.c_str(), &value, &length) == FALSE || value == nullptr ||
            length == 0)
        {
            continue;
        }

        /* The length covers the terminating null character of the string. */
        info.fields.push_back(MetadataField{ key, std::wstring(static_cast<const wchar_t*>(value), length - 1) });
    }

    if (info.fields.empty())
    {
        error = "'" + shown + "' carries no version string fields";
        return false;
    }

    return true;
}

std::vector<MetadataField> MergeMetadataFields(const std::vector<MetadataField>& inherited,
                                               const std::vector<MetadataField>& overrides)
{
    std::vector<MetadataField> merged = inherited;
    for (const auto& field : overrides)
    {
        if (IsMetadataField(field.key))
        {
            SetMetadataValue(merged, field.key, field.value);
        }
    }

    return merged;
}

std::vector<MetadataField> DiffMetadataFields(const std::vector<MetadataField>& inherited,
                                              const std::vector<MetadataField>& values)
{
    std::vector<MetadataField> overrides;

    for (const auto& key : MetadataFields())
    {
        const auto* inherited_value = FindMetadataValue(inherited, key);
        const auto* value = FindMetadataValue(values, key);

        const std::wstring source = inherited_value != nullptr ? *inherited_value : std::wstring();
        const std::wstring shown = value != nullptr ? *value : std::wstring();

        /* A field which follows the source is not an override. */
        if (shown == source)
        {
            continue;
        }

        /* A field which neither the source nor the session names is left alone. */
        if (shown.empty() && inherited_value == nullptr)
        {
            continue;
        }

        overrides.push_back(MetadataField{ key, shown });
    }

    return overrides;
}

std::vector<char> ApplyApplicationMetadata(const void* launcher_bytes, std::size_t launcher_size,
                                           const ApplicationVersionInfo& info, std::string& warning)
{
    warning.clear();

    if (launcher_bytes == nullptr || launcher_size == 0)
    {
        warning = "the launcher payload is empty";
        return {};
    }
    if (info.fields.empty())
    {
        warning = "the version information of the source program is empty";
        return {};
    }
    if (!LooksLikePeImage(launcher_bytes, launcher_size))
    {
        warning = "the launcher payload is not a PE image";
        return {};
    }

    try
    {
        const auto block = BuildVersionResource(info);
        if (block.empty())
        {
            warning = "the version information does not fit into one resource";
            return {};
        }

        const auto          temporary_path = TemporaryPath(L"AppBox-Metadata");
        const TemporaryFile payload(temporary_path);
        if (!WriteFileBytes(temporary_path, launcher_bytes, launcher_size, warning))
        {
            return {};
        }

        if (!WriteVersionResource(temporary_path, block, info.translation.language, warning))
        {
            return {};
        }

        std::vector<char> patched;
        if (!ReadFileBytes(temporary_path, patched, warning))
        {
            return {};
        }

        return patched;
    }
    catch (const std::exception& e)
    {
        warning = std::string("failed to apply the version information: ") + e.what();
        return {};
    }
}

} // namespace appbox
