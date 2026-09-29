#ifndef APPBOX_PACKER_CORE_ENVIRONMENT_MODEL_HPP
#define APPBOX_PACKER_CORE_ENVIRONMENT_MODEL_HPP

#include "EnvironmentIsolation.hpp"
#include <cstddef>
#include <string>
#include <vector>

namespace appbox
{

/**
 * @brief One environment variable of the Environment workspace.
 *
 * The entry names a variable the packaged application sees inside the sandbox,
 * the value the user entered for it, and the way that value is composed with
 * the value the host holds: the isolation mode decides whether the host value
 * is visible at all, and the merge mode and the merge string decide how the two
 * values are joined while the isolation mode is `WriteCopy`.
 *
 * The merge string is only used by the `Prepend` and the `Append` merge mode;
 * the `Replace` and the `Host` mode pick one of the two values and ignore it.
 */
struct EnvironmentEntry
{
    /**
     * @brief Name of the variable, which is the `Name` column of the workspace.
     */
    std::wstring name;

    /**
     * @brief Value the user entered, which is the `Value` column of the
     *        workspace.
     *
     * The value may be empty, which is what a variable the user cleared looks
     * like; the `Host` merge mode ignores it either way.
     */
    std::wstring value;

    /**
     * @brief Isolation mode of the variable.
     *
     * The mode is `WriteCopy` by default, which is the mode a variable is added
     * with: the sandbox sees the value of the host and the value the user
     * entered merged together.
     */
    EnvironmentIsolation isolation = EnvironmentIsolation::WriteCopy;

    /**
     * @brief Merge mode of the variable.
     *
     * The mode is `Replace` by default, which is what a variable of a packaged
     * application normally does: the value the user entered replaces the value
     * of the host.
     */
    EnvironmentMergeMode merge = environment_isolation::kDefaultMergeMode;

    /**
     * @brief Text which joins the two values while the merge mode is `Prepend`
     *        or `Append`, empty while the user entered none.
     *
     * The search path variable is filled in with the semicolon which separates
     * the paths of a search path, see IsPathVariableName().
     */
    std::wstring merge_string;
};

/**
 * @brief Get the display names of the isolation modes.
 *
 * The names are the text of the `IsolationMode` column of the workspace, in the
 * order of EnvironmentIsolation: the position of a name is the value of the
 * mode it names, so a dropdown which is filled with this list maps the
 * position of the selected name back to the mode.
 *
 * @return The display names in the order of the enumeration.
 */
const std::vector<std::wstring>& EnvironmentIsolationNames();

/**
 * @brief Get the display names of the merge modes.
 *
 * The names are the text of the `MergeMode` column of the workspace, in the
 * order of EnvironmentMergeMode.
 *
 * @return The display names in the order of the enumeration.
 */
const std::vector<std::wstring>& EnvironmentMergeModeNames();

/**
 * @brief Get the display name of an isolation mode.
 * @param[in] isolation The isolation mode.
 * @return The display name of the mode.
 */
std::wstring EnvironmentIsolationName(EnvironmentIsolation isolation);

/**
 * @brief Get the display name of a merge mode.
 * @param[in] merge The merge mode.
 * @return The display name of the mode.
 */
std::wstring EnvironmentMergeModeName(EnvironmentMergeMode merge);

/**
 * @brief Describe an isolation mode of a variable for a tooltip.
 *
 * The description names the mode and says what the application sees while the
 * variable carries it. It is the text the workspace shows for the mode of a row
 * and for the modes the `IsolationMode` column offers, so the explanation of a
 * mode lives in one place.
 *
 * @param[in] isolation The isolation mode.
 * @return The description of the mode.
 */
std::wstring EnvironmentIsolationDescription(EnvironmentIsolation isolation);

/**
 * @brief Describe a merge mode of a variable for a tooltip.
 *
 * The description names the mode, says how the stored value and the value of
 * the host are joined and gives an example of the result. It is the text the
 * workspace shows for the merge mode of a row and for the modes the `MergeMode`
 * column offers.
 *
 * @param[in] merge The merge mode.
 * @return The description of the mode.
 */
std::wstring EnvironmentMergeModeDescription(EnvironmentMergeMode merge);

/**
 * @brief Fill in the merge mode and the merge string of the search path
 *        variable.
 *
 * The search path of a process is a list of paths which are separated by a
 * semicolon, and the paths of a packaged application have to be searched before
 * the paths of the host, so a variable which is the search path variable is
 * stored with the merge mode `Prepend` and the merge string `;`. The call is a
 * no-op for every other name, so a caller may offer every entry to it.
 *
 * The rule is a rule of the workspace and not of the model: the model stores
 * what it is given, and a caller applies the rule while an entry becomes the
 * search path variable. A mode and a string the user picked by hand afterwards
 * are therefore never rewritten, and a configuration which is imported from a
 * project file keeps the mode and the string it was stored with.
 *
 * @param[in,out] entry The entry to fill in.
 */
void ApplyPathVariableDefaults(EnvironmentEntry& entry);

/**
 * @brief Editable content of the Environment workspace of the packer.
 *
 * The model holds the environment variables the user entered in the workspace.
 * It holds no wxWidgets dependency and never touches the environment of the
 * host, so its rules are unit testable.
 *
 * The entries keep the order they were added in, so the table of the workspace
 * shows them the way the user entered them.
 *
 * Every operation which can fail validates its input first and reports an
 * English error description without changing the model:
 *
 * - The name has to carry a value, because an entry without a name names no
 *   variable.
 * - The name must not carry an equals sign, because the equals sign separates
 *   the name of a variable from its value in the environment block of a
 *   process.
 * - The name must not be listed by another entry; the comparison ignores the
 *   case, because the environment of a process is case insensitive on Windows.
 *
 * The model stores an entry as it is given: the merge mode and the merge string
 * of the search path variable are filled in by
 * `ApplyPathVariableDefaults()`, which the workspace calls while an entry
 * becomes the search path variable.
 */
class EnvironmentModel
{
public:
    /**
     * @brief Drop every environment variable.
     *
     * The model is left in the state of a fresh session.
     */
    void Reset();

    /**
     * @brief Whether the model holds no environment variable.
     * @return true when no entry was added.
     */
    bool IsEmpty() const;

    /**
     * @brief Get every environment variable the model holds.
     *
     * The entries are ordered the way they were added, so the rows of the
     * workspace table follow the order the user entered them in.
     *
     * @return The entries in insertion order.
     */
    const std::vector<EnvironmentEntry>& Entries() const;

    /**
     * @brief Append one environment variable.
     *
     * The entry is stored as it is given; a caller which adds the search path
     * variable offers it to ApplyPathVariableDefaults() first.
     *
     * @param[in] entry The variable to append.
     * @param[out] error Error description on failure.
     * @return true on success.
     */
    bool AddEntry(const EnvironmentEntry& entry, std::string& error);

    /**
     * @brief Replace one environment variable.
     *
     * The entry keeps its position, so editing a row of the workspace table
     * does not reorder the table. An entry may keep its own name, including a
     * name which only differs in case; a name which is listed by another entry
     * is refused.
     *
     * The entry is stored as it is given, so a mode and a string the user
     * picked by hand are kept; a caller which turns a name into the search path
     * variable offers the entry to ApplyPathVariableDefaults() first.
     *
     * @param[in] index Position of the entry to replace.
     * @param[in] entry The variable to store.
     * @param[out] error Error description on failure.
     * @return true on success.
     */
    bool SetEntry(std::size_t index, const EnvironmentEntry& entry, std::string& error);

    /**
     * @brief Drop one environment variable.
     * @param[in] index Position of the entry to drop.
     * @return true when the index named an entry and it was removed.
     */
    bool RemoveEntry(std::size_t index);

    /**
     * @brief Find the entry of a name.
     * @param[in] name Name to look for, compared ignoring the case.
     * @return The position of the entry, -1 when the name is not listed.
     */
    std::ptrdiff_t IndexOfName(const std::wstring& name) const;

private:
    /**
     * @brief The environment variables in insertion order.
     */
    std::vector<EnvironmentEntry> entries_;
};

} // namespace appbox

#endif // APPBOX_PACKER_CORE_ENVIRONMENT_MODEL_HPP
