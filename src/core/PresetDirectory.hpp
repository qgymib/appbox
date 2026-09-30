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
 * directory name below `app\filesystem` which MapBaseFS translates into the
 * real location at sandbox runtime.
 *
 * The preset directories form a tree: a preset which names another preset as
 * its parent is shown below that preset (the folders of the user profile are
 * offered below `Current User Directory` and the system directory is offered
 * below `Windows`), while a preset without a parent is a direct child of the
 * container of the filesystem view. Every preset owns a layer of its own, so
 * the nesting only shapes the tree and never shares a layer between two
 * presets.
 */
struct PresetDirectory
{
    /**
     * @brief Stable identifier used by the model layer.
     */
    std::string id;

    /**
     * @brief Identifier of the preset which holds this one, empty at top level.
     */
    std::string parent_id;

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
 * stays valid for the process lifetime. The presets are returned in the order
 * of their definition, which keeps every preset behind the preset it hangs
 * below. A preset whose known folder cannot be resolved, and a nested preset
 * whose parent is not offered, are skipped.
 *
 * @return The list of preset directories.
 */
const std::vector<PresetDirectory>& PresetDirectories();

/**
 * @brief Get the preset directories which hang below one preset directory.
 *
 * The lookup reads the table of the preset directories only; it never touches
 * the host filesystem.
 *
 * @param[in] parent_id Identifier of the holding preset directory, empty for
 *            the top level of the tree.
 * @return The nested preset directories in definition order.
 */
std::vector<PresetDirectory> ChildPresets(const std::string& parent_id);

/**
 * @brief Find a preset directory by identifier.
 * @param[in] id Preset identifier.
 * @param[out] out The preset description when found.
 * @return true when the preset exists.
 */
bool FindPresetDirectory(const std::string& id, PresetDirectory& out);

} // namespace appbox

#endif // APPBOX_PACKER_CORE_PRESET_DIRECTORY_HPP
