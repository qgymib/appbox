#ifndef APPBOX_SANDBOX_UTILS_DIRECTORYINFORMATIONWALKER_HPP
#define APPBOX_SANDBOX_UTILS_DIRECTORYINFORMATIONWALKER_HPP

#include "utils/WinAPI.h"
#include <functional>
#include <string>

namespace appbox
{

/**
 * @brief Layout of one directory information class.
 *
 * The directory information classes share the same entry prefix — the
 * `NextEntryOffset` chain, the attributes and the name length — but they place
 * the name of an entry at a different offset: `FILE_FULL_DIR_INFORMATION`
 * carries an `EaSize` before the name and `FILE_BOTH_DIR_INFORMATION` carries
 * the short name of the entry. The walker needs both offsets to read an entry
 * and to move it, so the class of the call is described instead of being
 * compiled in.
 */
struct DirectoryInformationLayout
{
    /**
     * @brief Offset of the `FileNameLength` member of an entry.
     */
    size_t name_length_offset = 0;

    /**
     * @brief Offset of the `FileName` member of an entry.
     */
    size_t name_offset = 0;
};

/**
 * @brief Get the layout of a directory information class.
 *
 * @param[in] info_class Information class of a directory query.
 * @param[out] layout Layout of the class.
 * @return true when the class carries the name of an entry, which is what the
 *         merge and the filter of the view need.
 */
bool DirectoryInformationLayoutOf(FILE_INFORMATION_CLASS info_class, DirectoryInformationLayout& layout);

/**
 * @brief Walk through a buffer of directory entries and delete entries.
 *
 * The entries of a directory query follow each other through `NextEntryOffset`;
 * an entry which the callback reports is unlinked from the chain and the bytes
 * of the following entries are moved, so the buffer stays compact and the
 * offsets keep describing the entries which remain.
 *
 * The walker never reads beyond the size the caller declared: an entry which
 * announces a name that does not fit into the buffer ends the walk, and a chain
 * which points outside the buffer is cut instead of being followed.
 */
struct DirectoryInformationWalker
{
    /**
     * @brief Callback which decides whether an entry is deleted.
     *
     * @param[in] entry The entry itself, for the attributes of the caller.
     * @param[in] name Name of the entry.
     * @return true when the entry has to be deleted.
     */
    typedef std::function<bool(void* entry, const std::wstring& name)> Callback;

    /**
     * @brief Walk through a buffer of directory entries and delete entries.
     *
     * @param[in,out] buff Buffer of directory entries.
     * @param[in] size Size of the buffer in bytes.
     * @param[in] layout Layout of the information class of the buffer.
     * @param[in] cb Callback which reports the entries to delete.
     * @return The number of valid bytes which remain in the buffer.
     */
    static size_t Walk(void* buff, size_t size, const DirectoryInformationLayout& layout, const Callback& cb);
};

} // namespace appbox

#endif // APPBOX_SANDBOX_UTILS_DIRECTORYINFORMATIONWALKER_HPP
