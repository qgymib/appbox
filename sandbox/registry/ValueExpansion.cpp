#include "WinAPI.h" /* Must be first include file */
#include <cstddef>
#include <cstring>
#include <utility>
#include <vector>
#include "ValueExpansion.hpp"

namespace
{

/**
 * @brief Whether a value type carries a text which may hold a reference.
 * @param[in] type The `REG_*` type code of the value.
 * @return true when the data of the type is a text of the sandbox.
 */
bool IsTextType(DWORD type)
{
    return type == REG_SZ || type == REG_EXPAND_SZ || type == REG_MULTI_SZ;
}

/**
 * @brief Whether the data of a value carries a percent sign at all.
 *
 * The scan is the cheap half of the expansion: a text without a `%` cannot
 * carry a `%APPBOX:<NAME>%` reference, so its data is never copied and the
 * answer of the kernel is returned as it is. A blob which is not made of whole
 * wide characters is no text of the registry either.
 *
 * @param[in] data Raw data of the value.
 * @return true when the data may carry a reference.
 */
bool CarriesReference(const std::vector<BYTE>& data)
{
    if ((data.size() % sizeof(wchar_t)) != 0)
    {
        return false;
    }

    const auto*       text = reinterpret_cast<const wchar_t*>(data.data());
    const std::size_t count = data.size() / sizeof(wchar_t);
    for (std::size_t index = 0; index < count; ++index)
    {
        if (text[index] == L'%')
        {
            return true;
        }
    }

    return false;
}

/**
 * @brief Expand the references of the data of one value.
 *
 * The data is returned unchanged unless the type is a string type and the text
 * carries a `%`, so a value without a reference never pays for a copy.
 *
 * @param[in] type The `REG_*` type code of the value.
 * @param[in] data Raw data of the value.
 * @param[in] variables Variables of the process which expands the value.
 * @return The data with every known reference replaced.
 */
std::vector<BYTE> ExpandData(DWORD type, std::vector<BYTE> data, const std::vector<appbox::VariableMapping>& variables)
{
    if (!IsTextType(type) || !CarriesReference(data))
    {
        return data;
    }

    return appbox::ExpandRegistryValueData(type, data, variables);
}

/**
 * @brief Set the length of the data inside the record of a query answer.
 * @param[in] info_class The information class of the call.
 * @param[in,out] buffer The buffer which holds the record.
 * @param[in] data_length The length of the data in bytes.
 */
void SetDataLength(KEY_VALUE_INFORMATION_CLASS info_class, void* buffer, ULONG data_length)
{
    switch (info_class)
    {
    case KeyValueFullInformation:
    case KeyValueFullInformationAlign64:
        static_cast<KEY_VALUE_FULL_INFORMATION*>(buffer)->DataLength = data_length;
        break;

    case KeyValuePartialInformation:
        static_cast<KEY_VALUE_PARTIAL_INFORMATION*>(buffer)->DataLength = data_length;
        break;

    case KeyValuePartialInformationAlign64:
        static_cast<KEY_VALUE_PARTIAL_INFORMATION_ALIGN64*>(buffer)->DataLength = data_length;
        break;

    default:
        /* A class without data carries no length to update. */
        break;
    }
}

} // namespace

appbox::registry::ValueDataView appbox::registry::ViewValueData(KEY_VALUE_INFORMATION_CLASS info_class,
                                                                const void* buffer, ULONG length)
{
    ValueDataView view;
    if (buffer == nullptr)
    {
        return view;
    }

    switch (info_class)
    {
    case KeyValueFullInformation:
    case KeyValueFullInformationAlign64: {
        /* The aligned variant carries the same record, with the data at an
         * aligned offset the record itself names. */
        if (length < offsetof(KEY_VALUE_FULL_INFORMATION, Name))
        {
            return view;
        }

        const auto* info = static_cast<const KEY_VALUE_FULL_INFORMATION*>(buffer);
        view.type = info->Type;
        view.data_offset = info->DataOffset;
        view.data_length = info->DataLength;
        break;
    }

    case KeyValuePartialInformation: {
        if (length < offsetof(KEY_VALUE_PARTIAL_INFORMATION, Data))
        {
            return view;
        }

        const auto* info = static_cast<const KEY_VALUE_PARTIAL_INFORMATION*>(buffer);
        view.type = info->Type;
        view.data_offset = static_cast<ULONG>(offsetof(KEY_VALUE_PARTIAL_INFORMATION, Data));
        view.data_length = info->DataLength;
        break;
    }

    case KeyValuePartialInformationAlign64: {
        if (length < offsetof(KEY_VALUE_PARTIAL_INFORMATION_ALIGN64, Data))
        {
            return view;
        }

        const auto* info = static_cast<const KEY_VALUE_PARTIAL_INFORMATION_ALIGN64*>(buffer);
        view.type = info->Type;
        view.data_offset = static_cast<ULONG>(offsetof(KEY_VALUE_PARTIAL_INFORMATION_ALIGN64, Data));
        view.data_length = info->DataLength;
        break;
    }

    default:
        /* `KeyValueBasicInformation` and `KeyValueLayerInformation` carry no
         * data, and a class the module does not know is left alone. */
        return view;
    }

    /* A record which describes data outside of the buffer is not rewritten. */
    if (view.data_offset > length || view.data_length > length - view.data_offset)
    {
        return ValueDataView();
    }

    view.has_data = true;
    return view;
}

NTSTATUS appbox::registry::ExpandValueBuffer(KEY_VALUE_INFORMATION_CLASS info_class, void* buffer, ULONG length,
                                             const std::vector<VariableMapping>& variables, PULONG result_length)
{
    const ValueDataView view = ViewValueData(info_class, buffer, length);
    if (!view.has_data)
    {
        return STATUS_SUCCESS;
    }

    auto* data = static_cast<BYTE*>(buffer) + view.data_offset;

    std::vector<BYTE>       original(data, data + view.data_length);
    const std::vector<BYTE> expanded = ExpandData(view.type, std::move(original), variables);

    /* A value without a reference leaves the answer of the kernel untouched,
     * the result length included. */
    if (expanded.size() == view.data_length &&
        (expanded.empty() || memcmp(expanded.data(), data, expanded.size()) == 0))
    {
        return STATUS_SUCCESS;
    }

    const auto required = static_cast<ULONG>(view.data_offset + expanded.size());
    SetDataLength(info_class, buffer, static_cast<ULONG>(expanded.size()));

    if (required > length)
    {
        /* The expanded text does not fit: the caller retries with the size it
         * is told here, which is the retry the registry API performs. */
        if (result_length != nullptr)
        {
            *result_length = required;
        }
        return STATUS_BUFFER_OVERFLOW;
    }

    if (!expanded.empty())
    {
        memcpy(data, expanded.data(), expanded.size());
    }

    if (result_length != nullptr)
    {
        *result_length = required;
    }
    return STATUS_SUCCESS;
}

NTSTATUS appbox::registry::ExpandValueBatch(PKEY_VALUE_ENTRY entries, ULONG entry_count, void* value_buffer,
                                            PULONG buffer_length, PULONG required_buffer_length,
                                            const std::vector<VariableMapping>& variables)
{
    if (entries == nullptr || entry_count == 0 || value_buffer == nullptr || buffer_length == nullptr)
    {
        return STATUS_SUCCESS;
    }

    const ULONG capacity = *buffer_length;

    /*
     * The data is taken out of the buffer before anything is written back: the
     * expansion can grow a value, which would otherwise overwrite the data of
     * an entry which follows it.
     */
    std::vector<std::vector<BYTE>> datas(entry_count);
    unsigned long long             total = 0;

    for (ULONG index = 0; index < entry_count; ++index)
    {
        const ULONG offset = entries[index].DataOffset;
        const ULONG length = entries[index].DataLength;
        if (offset > capacity || length > capacity - offset)
        {
            /* The kernel never reports such a layout. Rewriting a buffer the
             * call does not describe is worse than leaving the answer alone. */
            return STATUS_SUCCESS;
        }

        const auto* begin = static_cast<const BYTE*>(value_buffer) + offset;
        datas[index] = ExpandData(entries[index].Type, std::vector<BYTE>(begin, begin + length), variables);

        total += datas[index].size();
        if (total > 0xFFFFFFFFull)
        {
            return STATUS_BUFFER_TOO_SMALL;
        }
    }

    const auto required = static_cast<ULONG>(total);
    if (required_buffer_length != nullptr)
    {
        *required_buffer_length = required;
    }

    if (required > capacity)
    {
        return STATUS_BUFFER_OVERFLOW;
    }

    auto* out = static_cast<BYTE*>(value_buffer);
    ULONG offset = 0;
    for (ULONG index = 0; index < entry_count; ++index)
    {
        if (!datas[index].empty())
        {
            memcpy(out + offset, datas[index].data(), datas[index].size());
        }

        entries[index].DataOffset = offset;
        entries[index].DataLength = static_cast<ULONG>(datas[index].size());
        offset += static_cast<ULONG>(datas[index].size());
    }

    *buffer_length = required;
    return STATUS_SUCCESS;
}
