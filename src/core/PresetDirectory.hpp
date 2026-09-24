#ifndef APPBOX_PACKER_CORE_PRESET_DIRECTORY_HPP
#define APPBOX_PACKER_CORE_PRESET_DIRECTORY_HPP

#include <string>
#include <vector>

namespace appbox
{

/**
 * @brief Label of the virtual filesystem container shown as the top tree item.
 *
 * The container is not a node of the pack model: it holds the preset
 * directories of the filesystem view and is the parent of every imported
 * folder below them. It mirrors the `Sandbox Registry` container of the
 * registry view, which is why the label lives next to the preset directories
 * it carries.
 */
inline constexpr const wchar_t* kFilesystemContainerLabel = L"Sandbox Filesystem";

/**
 * @brief A system preset directory offered by the packer tree.
 *
 * A preset directory is a well known host location (such as Program Files)
 * which imported folders become subdirectories of. The layer key is the
 * directory name below `<base_fs>\filesystem` which MapBaseFS translates
 * into the real location at sandbox runtime.
 */
struct PresetDirectory
{
    /**
     * @brief Stable identifier used by the model layer.
     */
    std::string id;

    /**
     * @brief Label shown in the tree control.
     */
    std::wstring display_name;

    /**
     * @brief Lower layer key token, e.g. `L"#ProgramFiles#"`.
     */
    std::wstring layer_key;

    /**
     * @brief Expanded host path of the preset directory.
     */
    std::wstring real_path;
};

/**
 * @brief Get the preset directories.
 *
 * The list is resolved from the known folder table on the first call and
 * stays valid for the process lifetime.
 *
 * @return The list of preset directories.
 */
const std::vector<PresetDirectory>& PresetDirectories();

/**
 * @brief Find a preset directory by identifier.
 * @param[in] id Preset identifier.
 * @param[out] out The preset description when found.
 * @return true when the preset exists.
 */
bool FindPresetDirectory(const std::string& id, PresetDirectory& out);

} // namespace appbox

#endif // APPBOX_PACKER_CORE_PRESET_DIRECTORY_HPP
