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
 * The table is the private environment of the sandbox: the process of the
 * packaged application keeps its own environment block untouched, and every
 * entry point which reads, writes, enumerates or expands a variable of the
 * process environment is answered from this table instead. The environment of
 * the host is therefore never modified, whatever the application does.
 *
 * The names are compared ignoring the case, because the environment of a
 * process is case insensitive on Windows: `Path` names the same variable as
 * `PATH`. The entries keep the order they were given in, so an enumeration
 * reports the variables of the host first, followed by the variables the
 * configuration added, and a variable which is written keeps its position.
 *
 * Every method may be called from any thread: the table is guarded by a lock
 * of its own. A block which CreateBlock() handed out is owned by the caller
 * and stays valid until it is passed to ReleaseBlock().
 */
class Table
{
public:
    Table();
    ~Table();

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
     * The block is allocated on the heap of the process and is owned by the
     * caller: it has to be passed to ReleaseBlock() once it is no longer used,
     * which is what the hook of FreeEnvironmentStringsW() does.
     *
     * @return The block, null when the allocation failed.
     */
    wchar_t* CreateBlock();

    /**
     * @brief Release an environment block of this table.
     * @param[in] block Block which CreateBlock() handed out.
     * @return true when the block belongs to the table and was released.
     */
    bool ReleaseBlock(wchar_t* block);

    /**
     * @brief Build an environment block of the whole table which the caller
     *        owns.
     *
     * The block is built like the one of CreateBlock(), but it is not tracked:
     * a caller which creates an environment of its own releases the block with
     * the entry point of the operating system which destroys an environment, so
     * the table must not release it a second time.
     *
     * @return The block, null when the allocation failed.
     */
    wchar_t* CreateOwnedBlock() const;

    /**
     * @brief Build an environment block of the whole table in the ANSI code
     *        page.
     *
     * The block is the one GetEnvironmentStringsA() reports, so it has to be
     * built from the same table. A character the code page cannot express is
     * written as its default character.
     *
     * @return The block, null when the allocation failed.
     */
    char* CreateAnsiBlock();

    /**
     * @brief Release an ANSI environment block of this table.
     * @param[in] block Block which CreateAnsiBlock() handed out.
     * @return true when the block belongs to the table and was released.
     */
    bool ReleaseAnsiBlock(char* block);

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
     * The block is allocated on the heap of the process and is not tracked; the
     * caller holds the lock of the table and decides whether the table owns the
     * block.
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

    /**
     * @brief The blocks CreateBlock() handed out and nobody released so far.
     */
    std::vector<wchar_t*> blocks_;

    /**
     * @brief The blocks CreateAnsiBlock() handed out and nobody released so far.
     */
    std::vector<char*> ansi_blocks_;
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
