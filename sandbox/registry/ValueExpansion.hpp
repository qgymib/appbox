#ifndef APPBOX_SANDBOX_REGISTRY_VALUEEXPANSION_HPP
#define APPBOX_SANDBOX_REGISTRY_VALUEEXPANSION_HPP

#include "WinAPI.h" /* Must be first include file */
#include "utils/VariableExpansion.hpp"
#include <vector>

namespace appbox
{
namespace registry
{

/**
 * @brief The layout of the value data inside a `KEY_VALUE_*_INFORMATION` buffer.
 *
 * The information classes of a value query differ in the fields they report in
 * front of the data: the partial classes place the data behind a fixed part and
 * the full classes name its offset. The aligned variants of both place the data
 * at an aligned offset, which the record of the kernel already describes, so a
 * reader does not need a layout of its own for them.
 */
struct ValueDataView
{
    /** @brief Whether the information class carries the data of the value. */
    bool has_data = false;

    /** @brief The `REG_*` type code of the value. */
    DWORD type = REG_NONE;

    /** @brief Offset of the data inside the buffer. */
    ULONG data_offset = 0;

    /** @brief Length of the data in bytes. */
    ULONG data_length = 0;
};

/**
 * @brief Read the layout of the value data which a query answer carries.
 *
 * `KeyValueBasicInformation` reports the name and the type of a value but not
 * its data, and `KeyValueLayerInformation` reports the layer of the value, so
 * neither of them carries a text to expand. A record which does not fit into
 * the buffer is reported as carrying no data as well: the caller of a hook
 * never rewrites a buffer it cannot describe.
 *
 * @param[in] info_class The information class of the call.
 * @param[in] buffer The buffer the kernel filled.
 * @param[in] length The size of the buffer in bytes.
 * @return The layout of the data.
 */
ValueDataView ViewValueData(KEY_VALUE_INFORMATION_CLASS info_class, const void* buffer, ULONG length);

/**
 * @brief Expand the references of the value a query returned into a caller buffer.
 *
 * The answer of `NtQueryValueKey` and of `NtEnumerateValueKey` carries the data
 * of the value inside the buffer of the caller. The references of a text are
 * replaced here, so every process reads the folders of its own bitness: the
 * hives keep the references and are never written by the expansion.
 *
 * Only the string types of the registry carry a reference (`REG_SZ`,
 * `REG_EXPAND_SZ` and `REG_MULTI_SZ`), and a value which carries no `%` is left
 * untouched byte for byte, so the common case costs one scan of the data. The
 * data is rewritten in place when the expansion changed it.
 *
 * An expanded text which does not fit into the buffer is reported with
 * `STATUS_BUFFER_OVERFLOW` and the size the caller needs, which is the retry
 * the registry API performs (`RegQueryValueEx` reports `ERROR_MORE_DATA` for
 * it). The data length of the record is set either way, so a caller which reads
 * the record of a refused call learns the size of the expanded text as well.
 *
 * @param[in] info_class The information class of the call.
 * @param[in,out] buffer The buffer which holds the answer of the kernel.
 * @param[in] length The size of the buffer in bytes.
 * @param[in] variables The variables of the process which expands the value.
 * @param[out] result_length The size the answer uses, or the size the caller
 *                           needs when the call reports an overflow. May be
 *                           null, in which case only the buffer is rewritten.
 * @return `STATUS_SUCCESS` when the buffer holds the answer, including the case
 *         of a value without a reference, or `STATUS_BUFFER_OVERFLOW` when the
 *         expanded text does not fit into the buffer.
 */
NTSTATUS ExpandValueBuffer(KEY_VALUE_INFORMATION_CLASS info_class, void* buffer, ULONG length,
                           const std::vector<VariableMapping>& variables, PULONG result_length);

/**
 * @brief Expand the values of a batch query and pack them again.
 *
 * `NtQueryMultipleValueKey` answers every entry with the offset and the length
 * of its data inside one buffer of the caller. The expansion changes the length
 * of a value, so the entries are packed again in the order of the call, without
 * padding, which is the layout the kernel produces for a batch as well.
 *
 * The entries are taken out of the buffer before they are written back, so a
 * value which grows never overwrites the data of an entry which follows it.
 *
 * @param[in,out] entries The entries of the call, filled with the data length,
 *                        the data offset and the type.
 * @param[in] entry_count The number of entries.
 * @param[in,out] value_buffer The caller buffer which holds the data.
 * @param[in,out] buffer_length The size of the buffer on entry, the size the
 *                              answer uses on success.
 * @param[out] required_buffer_length The size the buffer needs. May be null.
 * @param[in] variables The variables of the process which expands the values.
 * @return `STATUS_SUCCESS` when the buffer holds the answer, or
 *         `STATUS_BUFFER_OVERFLOW` when the expanded values do not fit into it.
 */
NTSTATUS ExpandValueBatch(PKEY_VALUE_ENTRY entries, ULONG entry_count, void* value_buffer, PULONG buffer_length,
                          PULONG required_buffer_length, const std::vector<VariableMapping>& variables);

} // namespace registry
} // namespace appbox

#endif // APPBOX_SANDBOX_REGISTRY_VALUEEXPANSION_HPP
