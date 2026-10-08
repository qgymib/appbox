#ifndef APPBOX_PACKER_CORE_APPLICATION_METADATA_HPP
#define APPBOX_PACKER_CORE_APPLICATION_METADATA_HPP

#include "PackModel.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace appbox
{

/**
 * @brief Key names of the string fields of a version resource.
 *
 * The keys are the names a `VERSIONINFO` resource script spells in its
 * `StringFileInfo` block, for example `FileDescription`. They are plain ASCII
 * text because that is how they travel in a project file, while the values are
 * wide text like every other text of the packer.
 */
namespace metadata_field
{
inline constexpr const char* kComments = "Comments";
inline constexpr const char* kCompanyName = "CompanyName";
inline constexpr const char* kFileDescription = "FileDescription";
inline constexpr const char* kFileVersion = "FileVersion";
inline constexpr const char* kInternalName = "InternalName";
inline constexpr const char* kLegalCopyright = "LegalCopyright";
inline constexpr const char* kLegalTrademarks = "LegalTrademarks";
inline constexpr const char* kOriginalFilename = "OriginalFilename";
inline constexpr const char* kPrivateBuild = "PrivateBuild";
inline constexpr const char* kProductName = "ProductName";
inline constexpr const char* kProductVersion = "ProductVersion";
inline constexpr const char* kSpecialBuild = "SpecialBuild";
} // namespace metadata_field

/**
 * @brief Number of fields the `Metadata` tab of the Settings workspace shows.
 *
 * The tab shows the fields a user fills in most of the time; every other field
 * of the version resource is edited in the customization dialog which the tab
 * opens.
 */
inline constexpr std::size_t kCommonMetadataFields = 6;

/** Language of a version resource which names none: English (United States). */
inline constexpr std::uint16_t kDefaultMetadataLanguage = 0x0409;

/** Code page of a version resource which names none: Unicode. */
inline constexpr std::uint16_t kDefaultMetadataCodePage = 1200;

/**
 * @brief Get every string field a version resource may carry.
 *
 * The order is the order the `Metadata` tab and its customization dialog show
 * the fields in, with the fields of the tab first.
 *
 * @return The keys of the fields.
 */
const std::vector<std::string>& MetadataFields();

/**
 * @brief Get the fields the `Metadata` tab of the Settings workspace shows.
 * @return The first kCommonMetadataFields keys of MetadataFields().
 */
const std::vector<std::string>& CommonMetadataFields();

/**
 * @brief Whether a key names a string field of a version resource.
 * @param[in] key Key to test.
 * @return true when the key is one of MetadataFields().
 */
bool IsMetadataField(const std::string& key);

/**
 * @brief Get the label the workspace shows for a key.
 * @param[in] key Key of a version resource field.
 * @return The label, empty for a key which is not a field.
 */
std::string MetadataFieldLabel(const std::string& key);

/**
 * @brief One string field of a version resource.
 */
struct MetadataField
{
    /**
     * @brief Key of the field, one of MetadataFields().
     */
    std::string key;

    /**
     * @brief Value of the field, which may be empty.
     */
    std::wstring value;
};

/**
 * @brief Get the value of one field of a list.
 * @param[in] fields Fields to search.
 * @param[in] key Key to look up.
 * @return The value of the field, null when the list does not hold the key.
 */
const std::wstring* FindMetadataValue(const std::vector<MetadataField>& fields, const std::string& key);

/**
 * @brief Store the value of one field of a list.
 *
 * A key which the list does not hold yet is appended, so the order of the
 * fields of a version resource is stable while it is edited.
 *
 * @param[in,out] fields Fields to change.
 * @param[in] key Key of the field.
 * @param[in] value New value of the field.
 */
void SetMetadataValue(std::vector<MetadataField>& fields, const std::string& key, const std::wstring& value);

/**
 * @brief Language and code page of a version resource.
 *
 * The pair is the `Translation` entry of the resource: it names the language
 * the strings of the resource are written in. The packer never asks the user
 * for it, it follows the source program and falls back to the defaults.
 */
struct MetadataTranslation
{
    /**
     * @brief Language identifier, e.g. `0x0409` for English (United States).
     */
    std::uint16_t language = kDefaultMetadataLanguage;

    /**
     * @brief Code page of the strings, e.g. `1200` for Unicode.
     */
    std::uint16_t code_page = kDefaultMetadataCodePage;
};

/**
 * @brief The version information of one image.
 */
struct ApplicationVersionInfo
{
    /**
     * @brief Language and code page of the resource.
     */
    MetadataTranslation translation;

    /**
     * @brief The string fields the resource carries, in MetadataFields() order.
     */
    std::vector<MetadataField> fields;
};

/**
 * @brief The version information of a packer session.
 *
 * The session stores two things only: the program the information is inherited
 * from and the fields the user edited. A field the user did not touch is read
 * from the source program again on every pack run, so an update of that
 * program is picked up without touching the project.
 *
 * A key which the override list holds is an override even while its value is
 * empty, so clearing a field of the source is told apart from leaving it alone.
 */
struct ApplicationMetadata
{
    /**
     * @brief Host path of the program the information is inherited from.
     *
     * Empty while the session inherits from the default source, which is the
     * first program marked for auto start, see DefaultMetadataSource().
     */
    std::wstring source;

    /**
     * @brief The fields the user edited, in MetadataFields() order.
     */
    std::vector<MetadataField> overrides;

    /**
     * @brief Whether the session holds no source selection and no override.
     * @return true when the session describes a plain default.
     */
    bool IsEmpty() const;
};

/**
 * @brief Get the program the version information of a session inherits from by default.
 *
 * The default source is the first program which is marked for auto start, in
 * ascending order of its executable file name: the launcher of the archive is
 * named after the main program, so the version information of the packed
 * program is what the extracted launcher shows. Programs which are not marked
 * for auto start are not candidates, and a model without one has no default.
 *
 * @param[in] model The pack model.
 * @return The host path of the default source, empty when the model has no
 *         auto start program.
 */
std::wstring DefaultMetadataSource(const PackModel& model);

/**
 * @brief Resolve the program the version information of a session is read from.
 * @param[in] model The pack model.
 * @param[in] metadata Metadata of the session.
 * @return The host path of the source, empty when the session names none and
 *         the model has no default source.
 */
std::wstring MetadataSourcePath(const PackModel& model, const ApplicationMetadata& metadata);

/**
 * @brief Read the version information of an image.
 *
 * The version resource of the image is read with the version API of Windows,
 * so the image is never executed and no part of it has to be parsed by hand.
 *
 * A field the resource does not carry stays absent from the result, so a
 * caller can tell "the source leaves this field empty" from "the source does
 * not name this field at all". The call fails when the image carries no
 * version information or no string field at all.
 *
 * @param[in] path Host path of the image.
 * @param[out] info The version information of the image.
 * @param[out] error Error description on failure.
 * @return true when the version information was read.
 */
bool ReadApplicationMetadata(const std::wstring& path, ApplicationVersionInfo& info, std::string& error);

/**
 * @brief Merge the fields of a source program with the overrides of a session.
 * @param[in] inherited Fields read from the source program.
 * @param[in] overrides Fields the user edited.
 * @return The fields of the version resource to write, in MetadataFields()
 *         order: the fields of the source with the overrides applied.
 */
std::vector<MetadataField> MergeMetadataFields(const std::vector<MetadataField>& inherited,
                                               const std::vector<MetadataField>& overrides);

/**
 * @brief Get the fields which differ from the fields of the source program.
 *
 * A field which the session shows with the value of the source is not an
 * override: it follows the source again on the next pack run. A field which
 * the source does not carry and which the session leaves empty is not an
 * override either, so a field the user never touched stays out of the project
 * file. Every other field is an override, an emptied field included.
 *
 * @param[in] inherited Fields read from the source program.
 * @param[in] values Fields the session shows.
 * @return The overrides of the session, in MetadataFields() order.
 */
std::vector<MetadataField> DiffMetadataFields(const std::vector<MetadataField>& inherited,
                                              const std::vector<MetadataField>& values);

/**
 * @brief Write version information into a launcher payload.
 *
 * The packer stores the launcher under the file name of the main program of
 * the packaged application, so the extracted archive looks like the
 * application itself. This function continues that idea for the file
 * properties: the version resource of the payload is replaced by the fields of
 * @p info, so the `Details` page of the shell shows the information of the
 * packaged application for the extracted program.
 *
 * The resource is *replaced*, not merged: a version resource the payload
 * already carries is dropped first, so the file never holds two of them and
 * the shell cannot show the information of the launcher by accident.
 *
 * A payload which cannot be patched - not a PE image, a failing resource
 * update - is not an error of the pack run: the function then returns an empty
 * vector and describes the reason in @p warning, and the caller packs the
 * original payload.
 *
 * The function uses the Windows resource APIs only (LoadLibraryExW with
 * LOAD_LIBRARY_AS_DATAFILE, BeginUpdateResourceW, UpdateResourceW and
 * EndUpdateResourceW); because those work on files, the payload is written to
 * a temporary file below the temporary directory, patched there and read back.
 * The temporary file is removed on every path, including the error paths.
 *
 * @param[in] launcher_bytes Embedded launcher payload.
 * @param[in] launcher_size Payload size in bytes.
 * @param[in] info The version information to write.
 * @param[out] warning Reason why the information was not applied, empty on
 *             success.
 * @return The patched PE image, empty when the payload stays unchanged.
 */
std::vector<char> ApplyApplicationMetadata(const void* launcher_bytes, std::size_t launcher_size,
                                           const ApplicationVersionInfo& info, std::string& warning);

} // namespace appbox

#endif // APPBOX_PACKER_CORE_APPLICATION_METADATA_HPP
