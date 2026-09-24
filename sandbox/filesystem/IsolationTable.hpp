#ifndef APPBOX_SANDBOX_FILESYSTEM_ISOLATIONTABLE_HPP
#define APPBOX_SANDBOX_FILESYSTEM_ISOLATIONTABLE_HPP

#include "FilesystemIsolation.hpp"
#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace appbox
{
namespace filesystem
{

/**
 * @brief One lower layer of the view, as the isolation file needs it.
 *
 * A layer of the virtual filesystem is addressed by its layer key, which is
 * the name of its folder inside the base filesystem (for example
 * `#ProgramFiles#`), and it is mapped to a folder of the view (for example
 * `\??\C:\Program Files`).
 */
struct IsolationLayer
{
    /**
     * @brief Layer key of the virtual filesystem.
     *
     * The first component of every virtual path which belongs to the layer.
     */
    std::wstring layer_key;

    /**
     * @brief Folder the layer is mapped to in the view.
     */
    std::wstring mapped_nt_path;
};

/**
 * @brief Case insensitive order of the view paths of the isolation table.
 *
 * The host filesystem is case insensitive, so `\??\C:\Program Files` and
 * `\??\c:\program files` name the same folder of the view.
 */
struct IsolationPathLess
{
    /**
     * @brief Compare two view paths.
     * @param[in] left Left path.
     * @param[in] right Right path.
     * @return true when left has to be placed before right.
     */
    bool operator()(const std::wstring& left, const std::wstring& right) const;
};

/**
 * @brief One entry of the isolation table.
 */
struct IsolationEntry
{
    /**
     * @brief Path of the entry inside the view.
     */
    std::wstring view_path;

    /**
     * @brief Kind of the entry the mode was picked for.
     */
    FilesystemEntryKind kind = FilesystemEntryKind::Directory;

    /**
     * @brief Isolation mode of the entry.
     */
    FilesystemIsolation isolation = FilesystemIsolation::WriteCopy;
};

/**
 * @brief The isolation modes of the virtual filesystem inside the sandbox.
 *
 * The table is the sandbox side of the isolation file the packer writes into
 * the overlay of the archive. The document lists the entries the user set a
 * mode for, as paths of the virtual filesystem (the first component is the
 * layer key of a preset directory); the table translates them into paths of
 * the view with the layer mapping of the injected configuration, so a lookup
 * only needs the view path the hooked call resolved.
 *
 * The mode of a path which the document does not list is derived by walking
 * the path upwards: the closest listed entry above it covers its whole
 * subtree, which is what makes the mode of a folder reach the entries below it
 * and what lets a folder below override the folder above. A path without any
 * listed entry follows the default of its kind, so a sandbox without an
 * isolation file behaves like one whose document lists no entry at all.
 *
 * The class holds no dependency on the Windows API, so the lookup rules are
 * unit testable.
 */
class IsolationTable
{
public:
    /**
     * @brief Load the table from the text of an isolation file.
     *
     * The call is atomic: the parsed entries are collected into a local table
     * first and replace the current content only when the whole document was
     * accepted. A failure therefore leaves the table unchanged.
     *
     * An entry whose layer key is not part of the layer mapping cannot be
     * expressed as a view path, so it is skipped and reported through \p
     * unmapped instead of failing the document: the layers of the run may
     * differ from the layers of the pack, and the remaining entries stay
     * usable.
     *
     * @param[in] text The UTF-8 text of the isolation file.
     * @param[in] layers The layers of the view, in mapping order.
     * @param[out] unmapped Virtual paths of the entries which were skipped.
     * @param[out] error Error description on failure.
     * @return true when the document was parsed.
     */
    bool Parse(const std::string& text, const std::vector<IsolationLayer>& layers,
               std::vector<std::wstring>& unmapped, std::string& error);

    /**
     * @brief Whether the table holds no entry.
     * @return true when no mode is listed.
     */
    bool Empty() const;

    /**
     * @brief Number of listed modes.
     * @return The number of entries.
     */
    std::size_t Count() const;

    /**
     * @brief Get the mode which applies to a path of the view.
     *
     * The lookup starts at the path itself and walks the path upwards until a
     * listed entry is found, so a listed entry covers its whole subtree. The
     * kind of that entry is reported as well, because the mode alone does not
     * say which layers stay visible: `Full` hides the host folder of a
     * *folder* together with its whole subtree, while `Full` of a *file*
     * keeps the host file readable.
     *
     * @param[in] view_path Path of the entry inside the view.
     * @param[out] mode The mode of the closest listed entry.
     * @param[out] source_kind Kind of the entry which carries the mode.
     * @return true when a listed entry decided the mode.
     */
    bool Lookup(const std::wstring& view_path, FilesystemIsolation& mode, FilesystemEntryKind& source_kind) const;

    /**
     * @brief Normalize a view path to the shape the table stores.
     *
     * Trailing separators are dropped and empty components are removed, so
     * `\??\C:\Program Files\MyApp\` and `\??\C:\Program Files\MyApp` name the
     * same entry. A parent reference is rejected, because it would leave the
     * virtual filesystem.
     *
     * @param[in] path The path to normalize.
     * @return The normalized path, empty when it is not usable.
     */
    static std::wstring NormalizeViewPath(const std::wstring& path);

    /**
     * @brief Get the layer key of a mapped layer folder.
     *
     * The loader maps a layer to `<base filesystem>\filesystem\<layer key>`,
     * so the key is the last component of the host path of the layer, for
     * example `#ProgramFiles#` for
     * `\??\D:\Sandbox\filesystem\#ProgramFiles#`.
     *
     * @param[in] host_nt_path Host path of the layer.
     * @return The layer key, empty when the path has no component.
     */
    static std::wstring LayerKeyOf(const std::wstring& host_nt_path);

private:
    /**
     * @brief The listed modes, ordered by the view path of the entry.
     */
    std::map<std::wstring, IsolationEntry, IsolationPathLess> entries_;
};

} // namespace filesystem
} // namespace appbox

#endif // APPBOX_SANDBOX_FILESYSTEM_ISOLATIONTABLE_HPP
