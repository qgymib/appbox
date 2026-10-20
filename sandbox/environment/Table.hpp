#ifndef APPBOX_SANDBOX_ENVIRONMENT_TABLE_HPP
#define APPBOX_SANDBOX_ENVIRONMENT_TABLE_HPP

#include <cstddef>
#include <mutex>
#include <string>
#include <vector>

namespace appbox
{
namespace environment
{

/**
 * @brief One environment variable of the sandbox.
 */
struct Variable
{
    /**
     * @brief Name of the variable, as the block of the process spells it.
     */
    std::wstring name;

    /**
     * @brief Value of the variable, may be empty.
     */
    std::wstring value;
};

/**
 * @brief The environment variables the sandboxed process sees.
 *
 * The table composes the environment of the packaged application: it is filled
 * with the block of the host while the sandbox DLL is injected, the variables
 * of the isolation files are composed into it, and the modifications of an
 * earlier run are applied afterwards. The composition is then built into a
 * block which becomes the environment of the process, so the table is the
 * record of what the process carries and not the place a read is answered
 * from.
 *
 * The names are compared ignoring the case, because the environment of a
 * process is case insensitive on Windows: `Path` names the same variable as
 * `PATH`. The entries keep the order they were given in, so a block reports
 * the variables of the host first, followed by the variables the configuration
 * added, and a variable which is written keeps its position.
 *
 * Every method may be called from any thread: the table is guarded by a lock
 * of its own.
 */
class Table
{
public:
    Table();

    Table(const Table&) = delete;
    Table& operator=(const Table&) = delete;
    Table(Table&&) = delete;
    Table& operator=(Table&&) = delete;

    /**
     * @brief Drop every variable.
     */
    void Clear();

    /**
     * @brief Number of variables the table holds.
     * @return The number of entries.
     */
    std::size_t Count() const;

    /**
     * @brief Whether the table holds no variable.
     * @return true when the table is empty.
     */
    bool IsEmpty() const;

    /**
     * @brief Whether the table lists a variable.
     * @param[in] name Name to look for, compared ignoring the case.
     * @return true when the variable is part of the table.
     */
    bool Contains(const std::wstring& name) const;

    /**
     * @brief Read the value of a variable.
     * @param[in] name Name to look for, compared ignoring the case.
     * @param[out] value Value of the variable, untouched when it is not listed.
     * @return true when the variable is part of the table.
     */
    bool Get(const std::wstring& name, std::wstring& value) const;

    /**
     * @brief Store a variable.
     *
     * A variable which is already listed keeps its position and is given the
     * new value; a variable which is not listed is appended.
     *
     * @param[in] name Name of the variable.
     * @param[in] value Value of the variable.
     */
    void Set(const std::wstring& name, const std::wstring& value);

    /**
     * @brief Drop a variable.
     * @param[in] name Name to look for, compared ignoring the case.
     * @return true when the variable was listed and was removed.
     */
    bool Delete(const std::wstring& name);

    /**
     * @brief Get a copy of every variable.
     * @return The variables in table order.
     */
    std::vector<Variable> Entries() const;

    /**
     * @brief Replace the whole table from an environment block.
     *
     * The block is the format of the environment of a process: entries of
     * `name=value`, each terminated by a null character, the whole block
     * terminated by a second null character. An entry which carries no name —
     * the drive relative current directory of the host, which the block spells
     * as `=C:=C:\...` — is kept with its own spelling, so the block of the
     * sandbox reports it like the block of the host does.
     *
     * @param[in] block The block to read, may be null.
     */
    void AssignBlock(const wchar_t* block);

    /**
     * @brief Build an environment block of the whole table.
     *
     * The block is allocated on the heap of the process, which is the heap the
     * environment of a process has to come from, and is owned by the caller: it
     * becomes the environment of the process, and the operating system releases
     * it when the block is replaced or the process ends.
     *
     * @return The block, null when the allocation failed.
     */
    wchar_t* CreateBlock() const;

private:
    /**
     * @brief Find a variable.
     * @param[in] name Name to look for, compared ignoring the case.
     * @return The position of the variable, entries_.size() when it is not
     *         listed.
     */
    std::size_t IndexOf(const std::wstring& name) const;

    /**
     * @brief Build an environment block of the entries.
     *
     * The block is allocated on the heap of the process; the caller holds the
     * lock of the table.
     *
     * @return The block, null when the allocation failed.
     */
    wchar_t* BuildBlock() const;

    /**
     * @brief Guard of every read and write of the table.
     */
    mutable std::mutex lock_;

    /**
     * @brief The variables in table order.
     */
    std::vector<Variable> entries_;
};

/**
 * @brief Whether two environment variable names are the same.
 *
 * The comparison ignores the case, because the environment of a process is
 * case insensitive on Windows. The names are compared as ordinal upper case
 * text, which is the comparison the operating system itself uses.
 *
 * @param[in] left Left name.
 * @param[in] right Right name.
 * @return true when both names name the same variable.
 */
bool NamesEqual(const std::wstring& left, const std::wstring& right);

} // namespace environment
} // namespace appbox

#endif // APPBOX_SANDBOX_ENVIRONMENT_TABLE_HPP
