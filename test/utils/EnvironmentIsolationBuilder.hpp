#ifndef APPBOX_TEST_UTILS_ENVIRONMENT_ISOLATION_BUILDER_HPP
#define APPBOX_TEST_UTILS_ENVIRONMENT_ISOLATION_BUILDER_HPP

#include "EnvironmentIsolation.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace appbox::test
{

/**
 * @brief One variable of the environment isolation file of a test sandbox.
 */
struct EnvironmentIsolationEntry
{
    /**
     * @brief Name of the variable.
     */
    std::wstring name;

    /**
     * @brief Value the packaged application receives.
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
 * @brief One modification of the environment state file of a test sandbox.
 */
struct EnvironmentStateEntry
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
 * @brief Write the environment isolation file of a test sandbox.
 *
 * The file describes the environment variables of the packaged application and
 * lives in the environment domain of the resources of the case, which is where
 * the launcher looks for it (`<case root>/app/environment/isolation.json`). The
 * document is built from the schema structure of
 * `common/EnvironmentIsolation.hpp` instead of through the packer, so a case
 * also pins that the sandbox accepts a file which the workspace did not write.
 *
 * @param[in] case_root Root directory of the case, normally the working
 *                      directory.
 * @param[in] entries Entries to list.
 * @return true on success.
 */
bool WriteEnvironmentIsolationFile(const std::filesystem::path&                  case_root,
                                   const std::vector<EnvironmentIsolationEntry>& entries);

/**
 * @brief Write the text of the environment isolation file of a test sandbox.
 *
 * The call writes the text as it is, so a case can pin what the sandbox does
 * with a document which is malformed or which carries members the builder does
 * not know.
 *
 * @param[in] case_root Root directory of the case, normally the working
 *                      directory.
 * @param[in] text Text of the isolation file.
 * @return true on success.
 */
bool WriteEnvironmentIsolationFileText(const std::filesystem::path& case_root, const std::string& text);

/**
 * @brief Build the text of an environment isolation file.
 *
 * The call is the document builder of the write helpers: it returns the text
 * the file carries without writing a file, so a case which writes the document
 * somewhere else (a patch package, for example) shares the very same document.
 *
 * @param[in] entries Entries to list.
 * @return The text of the document.
 */
std::string BuildEnvironmentIsolationText(const std::vector<EnvironmentIsolationEntry>& entries);

/**
 * @brief Write the environment state file of a test sandbox.
 *
 * The file describes the modifications an earlier run made and lives in the
 * state directory of the sandbox, which the launcher owns
 * (`<case root>/data/environment/state.json`). A case which writes the document
 * itself pins what a run reads back without a run which wrote it first.
 *
 * @param[in] case_root Root directory of the case, normally the working
 *                      directory.
 * @param[in] entries Modifications to list.
 * @return true on success.
 */
bool WriteEnvironmentStateFile(const std::filesystem::path&              case_root,
                               const std::vector<EnvironmentStateEntry>& entries);

/**
 * @brief One variable of the environment of the test process.
 *
 * The environment of the packaged application is the environment of the launcher,
 * which the test process passes to it: a case which needs a value of the host
 * sets the variable before it starts the launcher and the variable is removed
 * again when the case leaves.
 */
class HostEnvironmentVariable
{
public:
    /**
     * @brief Store the variable in the environment of the test process.
     * @param[in] name Name of the variable.
     * @param[in] value Value of the variable.
     */
    HostEnvironmentVariable(const std::wstring& name, const std::wstring& value);

    /**
     * @brief Remove the variable from the environment of the test process.
     */
    ~HostEnvironmentVariable();

    HostEnvironmentVariable(const HostEnvironmentVariable&) = delete;
    HostEnvironmentVariable& operator=(const HostEnvironmentVariable&) = delete;
    HostEnvironmentVariable(HostEnvironmentVariable&&) = delete;
    HostEnvironmentVariable& operator=(HostEnvironmentVariable&&) = delete;

    /**
     * @brief Read the variable of the test process.
     * @return The value of the variable, empty when the process does not hold
     *         it.
     */
    std::wstring Value() const;

private:
    /**
     * @brief Name of the variable.
     */
    std::wstring name_;
};

} // namespace appbox::test

#endif // APPBOX_TEST_UTILS_ENVIRONMENT_ISOLATION_BUILDER_HPP
