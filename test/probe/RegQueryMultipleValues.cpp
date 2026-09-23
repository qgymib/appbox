#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include "RegQueryMultipleValues.hpp"
#include "utils/RegistryRootKey.hpp"
#include "WString.hpp"
#include <vector>

namespace
{

/**
 * @brief Format raw value data of a query result.
 *
 * A string is reported as its text without the terminating nulls, every other
 * type as a hexadecimal byte dump, which keeps the probe independent of the
 * exact layout of the value.
 *
 * @param[in] data The raw data of the value.
 * @param[in] size The number of bytes.
 * @return The formatted data. Encoding in UTF-8.
 */
std::string FormatValueData(const BYTE* data, DWORD size)
{
    if (size == 0 || data == nullptr)
    {
        return {};
    }

    std::wstring text(reinterpret_cast<const wchar_t*>(data), size / sizeof(wchar_t));
    while (!text.empty() && text.back() == L'\0')
    {
        text.pop_back();
    }

    /* A printable wide string is reported as text, everything else as bytes. */
    bool printable = true;
    for (const wchar_t character : text)
    {
        if (character < 0x20 || character > 0x7e)
        {
            printable = false;
            break;
        }
    }
    if (printable)
    {
        return appbox::WideToUTF8(text);
    }

    static const char* digits = "0123456789abcdef";
    std::string        hex;
    hex.reserve(size * 2);
    for (DWORD index = 0; index < size; ++index)
    {
        hex.push_back(digits[(data[index] >> 4) & 0x0f]);
        hex.push_back(digits[data[index] & 0x0f]);
    }
    return hex;
}

} // namespace

/**
 * @brief Query a batch of values of a key and report the answer of every entry.
 *
 * The call runs inside the sandbox, so the registry isolation has to answer the
 * batch from the layer which holds each value.
 */
static nlohmann::json ProbeRegQueryMultipleValues_Entry(const nlohmann::json& data)
{
    auto req = data.get<appbox::test::ProtocolRegQueryMultipleValues::Req>();

    appbox::test::ProtocolRegQueryMultipleValues::Rsp rsp;

    const auto root = appbox::test::RegistryRootHandle(req.Root);
    if (root == nullptr)
    {
        rsp.open_code = ERROR_INVALID_PARAMETER;
        return rsp;
    }

    HKEY key = nullptr;
    rsp.open_code = RegOpenKeyExW(root, appbox::UTF8ToWide(req.Key).c_str(), 0, KEY_QUERY_VALUE, &key);
    if (rsp.open_code != ERROR_SUCCESS)
    {
        return rsp;
    }

    std::vector<std::wstring> names;
    names.reserve(req.Names.size());
    for (const auto& name : req.Names)
    {
        names.push_back(appbox::UTF8ToWide(name));
    }

    std::vector<VALENTW> entries(names.size());
    for (std::size_t index = 0; index < names.size(); ++index)
    {
        entries[index].ve_valuename = const_cast<LPWSTR>(names[index].c_str());
        entries[index].ve_valuelen  = 0;
        entries[index].ve_valueptr  = 0;
        entries[index].ve_type      = 0;
    }

    std::vector<wchar_t> buffer(2048);
    DWORD                total = static_cast<DWORD>(buffer.size() * sizeof(wchar_t));
    rsp.query_code = static_cast<DWORD>(
        RegQueryMultipleValuesW(key, entries.data(), static_cast<DWORD>(entries.size()), buffer.data(), &total));
    rsp.total_size = total;

    if (rsp.query_code == ERROR_SUCCESS)
    {
        const auto base = reinterpret_cast<DWORD_PTR>(buffer.data());
        const auto end  = base + buffer.size() * sizeof(wchar_t);

        for (const auto& entry : entries)
        {
            rsp.types.push_back(entry.ve_type);

            /*
             * The offset of the data inside the caller buffer is reported as a
             * pointer by some builds and as a byte offset by others, so both
             * shapes are accepted.
             */
            const DWORD_PTR value_ptr = entry.ve_valueptr;
            const BYTE*     value_data = nullptr;
            if (value_ptr >= base && value_ptr + entry.ve_valuelen <= end)
            {
                value_data = reinterpret_cast<const BYTE*>(value_ptr);
            }
            else
            {
                value_data = reinterpret_cast<const BYTE*>(buffer.data()) + value_ptr;
            }

            rsp.values.push_back(FormatValueData(value_data, entry.ve_valuelen));
        }
    }

    RegCloseKey(key);
    return rsp;
}

appbox::test::Probe appbox::test::ProbeRegQueryMultipleValues("RegQueryMultipleValues",
                                                              ProbeRegQueryMultipleValues_Entry);
