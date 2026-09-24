#include "DirectoryInformationWalker.hpp"
#include <cstddef>
#include <cstring>

namespace
{

/**
 * @brief Read the offset of the next entry of an entry.
 * @param[in] entry The entry.
 * @return The offset, zero for the last entry of the buffer.
 */
ULONG ReadNextEntryOffset(const uint8_t* entry)
{
    ULONG value = 0;
    std::memcpy(&value, entry, sizeof(value));
    return value;
}

/**
 * @brief Write the offset of the next entry of an entry.
 * @param[in,out] entry The entry.
 * @param[in] value The offset to store, zero for the last entry.
 */
void WriteNextEntryOffset(uint8_t* entry, ULONG value)
{
    std::memcpy(entry, &value, sizeof(value));
}

/**
 * @brief Read the length of the name of an entry.
 * @param[in] entry The entry.
 * @param[in] layout Layout of the information class of the buffer.
 * @return The length of the name in bytes.
 */
ULONG ReadNameLength(const uint8_t* entry, const appbox::DirectoryInformationLayout& layout)
{
    ULONG value = 0;
    std::memcpy(&value, entry + layout.name_length_offset, sizeof(value));
    return value;
}

/**
 * @brief Read the name of an entry.
 * @param[in] entry The entry.
 * @param[in] layout Layout of the information class of the buffer.
 * @return The name of the entry.
 */
std::wstring ReadName(const uint8_t* entry, const appbox::DirectoryInformationLayout& layout)
{
    const ULONG length = ReadNameLength(entry, layout);
    const auto* name = reinterpret_cast<const wchar_t*>(entry + layout.name_offset);
    return std::wstring(name, length / sizeof(wchar_t));
}

/**
 * @brief Size of the entry at an offset.
 * @param[in] offset Offset of the entry inside the buffer.
 * @param[in] size Size of the whole buffer in bytes.
 * @param[in] layout Layout of the information class of the buffer.
 * @param[in] base Start of the buffer.
 * @return The size of the entry, zero when it is not complete.
 */
size_t EntrySize(size_t offset, size_t size, const appbox::DirectoryInformationLayout& layout, const uint8_t* base)
{
    if (offset > size || size - offset < layout.name_offset)
    {
        return 0;
    }

    /*
     * A caller owns the buffer, so the name length it declares may not fit into
     * the buffer at all: such an entry is not read.
     */
    const ULONG name_length = ReadNameLength(base + offset, layout);
    if (name_length > size - offset - layout.name_offset)
    {
        return 0;
    }

    return layout.name_offset + name_length;
}

} // namespace

bool appbox::DirectoryInformationLayoutOf(FILE_INFORMATION_CLASS info_class, DirectoryInformationLayout& layout)
{
    switch (info_class)
    {
    case FileDirectoryInformation:
        layout.name_length_offset = offsetof(FILE_DIRECTORY_INFORMATION, FileNameLength);
        layout.name_offset = offsetof(FILE_DIRECTORY_INFORMATION, FileName);
        return true;

    case FileFullDirectoryInformation:
        layout.name_length_offset = offsetof(FILE_FULL_DIR_INFORMATION, FileNameLength);
        layout.name_offset = offsetof(FILE_FULL_DIR_INFORMATION, FileName);
        return true;

    case FileBothDirectoryInformation:
        layout.name_length_offset = offsetof(FILE_BOTH_DIR_INFORMATION, FileNameLength);
        layout.name_offset = offsetof(FILE_BOTH_DIR_INFORMATION, FileName);
        return true;

    default:
        return false;
    }
}

size_t appbox::DirectoryInformationWalker::Walk(void* buff, size_t size, const DirectoryInformationLayout& layout,
                                                const Callback& cb)
{
    if (buff == nullptr || size == 0 || layout.name_offset == 0)
    {
        return 0;
    }

    const size_t header_size = layout.name_offset;
    auto* const  base = static_cast<uint8_t*>(buff);

    /*
     * Phase one: the first entry may be deleted, in which case its successor is
     * moved to the start of the buffer and its offset is fixed up, until an
     * entry which stays is found.
     */
    for (;;)
    {
        if (EntrySize(0, size, layout, base) == 0)
        {
            /* The first entry is not complete: there is no valid data. */
            return 0;
        }

        uint8_t* first = base;
        ULONG    next_relative = ReadNextEntryOffset(first);

        /* A chain which points at a truncated entry is cut instead of followed. */
        if (next_relative != 0 && (next_relative < header_size || EntrySize(next_relative, size, layout, base) == 0))
        {
            WriteNextEntryOffset(first, 0);
            next_relative = 0;
        }

        if (!cb(first, ReadName(first, layout)))
        {
            /* The first entry stays, the walk continues with phase two. */
            break;
        }

        if (next_relative == 0)
        {
            /* The deleted entry was the last one, the buffer is empty. */
            return 0;
        }

        /* Move the successor to the start of the buffer. */
        uint8_t*     next = base + next_relative;
        const ULONG  next_next_relative = ReadNextEntryOffset(next);
        const size_t move_length = EntrySize(next_relative, size, layout, base);

        std::memmove(base, next, move_length);

        /*
         * The entry after the successor keeps its position, so its offset is
         * the sum of the two offsets of the chain.
         */
        WriteNextEntryOffset(base, next_next_relative == 0 ? 0 : next_relative + next_next_relative);
    }

    /* Phase two: walk the entries which stay. */
    size_t previous_offset = 0;
    for (;;)
    {
        uint8_t*    previous = base + previous_offset;
        const ULONG relative = ReadNextEntryOffset(previous);

        if (relative == 0)
        {
            /* The previous entry is the last one. */
            return previous_offset + EntrySize(previous_offset, size, layout, base);
        }

        const size_t current_offset = previous_offset + relative;
        if (EntrySize(current_offset, size, layout, base) == 0)
        {
            /* The entry is truncated: drop it and keep the previous one. */
            WriteNextEntryOffset(previous, 0);
            return previous_offset + EntrySize(previous_offset, size, layout, base);
        }

        uint8_t* current = base + current_offset;
        ULONG    current_relative = ReadNextEntryOffset(current);

        /* A chain which points at a truncated entry is cut instead of followed. */
        if (current_relative != 0 &&
            (current_relative < header_size || EntrySize(current_offset + current_relative, size, layout, base) == 0))
        {
            WriteNextEntryOffset(current, 0);
            current_relative = 0;
        }

        if (cb(current, ReadName(current, layout)))
        {
            if (current_relative == 0)
            {
                /* The deleted entry was the last one: shorten the buffer. */
                WriteNextEntryOffset(previous, 0);
                return previous_offset + EntrySize(previous_offset, size, layout, base);
            }

            /* Unlink the entry: its bytes stay in the buffer as a hole. */
            WriteNextEntryOffset(previous, relative + current_relative);
        }
        else
        {
            previous_offset = current_offset;
        }
    }
}
