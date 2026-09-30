#ifndef APPBOX_SANDBOX_ENVIRONMENT_CONFIGURATION_HPP
#define APPBOX_SANDBOX_ENVIRONMENT_CONFIGURATION_HPP

#include "EnvironmentIsolation.hpp"
#include <cstddef>
#include <mutex>
#include <string>
#include <vector>

namespace appbox
{
namespace environment
{

/**
 * @brief One variable of the environment isolation file of the archive.
 *
 * The variable carries what the workspace of the packer stored: the value the
 * user entered, the isolation mode, the merge mode and the text which joins
 * the value with the value of the host.
 */
struct ConfiguredVariable
{
    /**
     * @brief Name of the variable.
     */
    std::wstring name;

    /**
     * @brief Value the user entered, may be empty.
     */
    std::wstring value;

    /**
     * @brief Isolation mode of the variable.
     */
    EnvironmentIsolation isolation = EnvironmentIsolation::WriteCopy;

    /**
     * @brief Merge mode of the variable.
     */
    EnvironmentMergeMode merge = environment_isolation::kDefaultMergeMode;

    /**
     * @brief Text which joins the two values.
     */
    std::wstring merge_string;
};

/**
 * @brief Read the environment isolation file of the archive.
 *
 * The document is the one the packer writes (see
 * `common/EnvironmentIsolation.hpp` for the schema). An entry without a name,
 * an entry whose name carries an equals sign, an entry which is listed twice
 * and an entry with a mode the vocabulary does not know are refused, so a
 * malformed document is reported instead of being applied in part: the result
 * holds no variable at all while the call fails.
 *
 * @param[in] text UTF-8 text of the isolation file.
 * @param[out] out The variables of the document, in the order of the file.
 * @param[out] error Error description on failure.
 * @return true on success.
 */
bool ParseIsolationDocument(const std::string& text, std::vector<ConfiguredVariable>& out, std::string& error);

/**
 * @brief One modification the sandboxed application made to its environment.
 *
 * The modification is written for a variable the application stored and for a
 * variable it removed, so the environment of the sandbox can be rebuilt
 * without freezing the variables the application never touched.
 */
struct Modification
{
    /**
     * @brief Name of the variable.
     */
    std::wstring name;

    /**
     * @brief Value the application stored, empty for a removal.
     */
    std::wstring value;

    /**
     * @brief Whether the application removed the variable.
     */
    bool deleted = false;
};

/**
 * @brief The modifications the sandboxed application made to its environment.
 *
 * The state keeps one modification per variable — the last one the application
 * made — so its size follows the number of variables the application touched
 * and not the number of writes it performed. The modifications are kept in the
 * order the variables were touched in, which keeps the state document stable.
 *
 * The state is written to `data/environment/state.json` by the launcher, which
 * receives the document over the RPC pipe, and it is read back while the
 * environment of the next run is composed.
 */
class State
{
public:
    State();

    /**
     * @brief Drop every modification.
     */
    void Clear();

    /**
     * @brief Number of modifications the state holds.
     * @return The number of entries.
     */
    std::size_t Count() const;

    /**
     * @brief Record a value the application stored.
     * @param[in] name Name of the variable.
     * @param[in] value Value of the variable.
     */
    void Record(const std::wstring& name, const std::wstring& value);

    /**
     * @brief Record a variable the application removed.
     * @param[in] name Name of the variable.
     */
    void RecordDeletion(const std::wstring& name);

    /**
     * @brief Get a copy of every modification.
     * @return The modifications in the order of the state.
     */
    std::vector<Modification> Entries() const;

    /**
     * @brief Build the state document of the modifications.
     *
     * A name or a value which is not well formed UTF-16 is written with the
     * replacement character instead of failing the document: the text comes
     * from the packaged application, which may store anything.
     *
     * @param[out] text UTF-8 text of the state document.
     * @param[out] error Error description on failure.
     * @return true on success.
     */
    bool Build(std::string& text, std::string& error) const;

    /**
     * @brief Read a state document of an earlier run.
     *
     * The entries are applied in the order of the document, so a variable
     * which the document lists twice ends up with the modification it was
     * written with last.
     *
     * @param[in] text UTF-8 text of the state document.
     * @param[out] error Error description on failure.
     * @return true on success.
     */
    bool Parse(const std::string& text, std::string& error);

private:
    /**
     * @brief Find a modification.
     * @param[in] name Name to look for, compared ignoring the case.
     * @return The position of the modification, entries_.size() when the
     *         variable was not touched yet.
     */
    std::size_t IndexOf(const std::wstring& name) const;

    /**
     * @brief Guard of every read and write of the state.
     */
    mutable std::mutex lock_;

    /**
     * @brief The modifications in the order the variables were touched in.
     */
    std::vector<Modification> entries_;
};

} // namespace environment
} // namespace appbox

#endif // APPBOX_SANDBOX_ENVIRONMENT_CONFIGURATION_HPP
