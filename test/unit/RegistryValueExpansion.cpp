#include "utils/WinAPI.h" /* Must be first include file */
#include <gtest/gtest.h>
#include "registry/ValueExpansion.hpp"
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

namespace
{

using appbox::VariableMapping;
using appbox::registry::ExpandValueBatch;
using appbox::registry::ExpandValueBuffer;
using appbox::registry::ValueDataView;
using appbox::registry::ViewValueData;

/**
 * @brief Build the variable table of a case.
 *
 * `Documents` names a folder which is longer than its reference, so a case can
 * pin the growth of a value; `Windows` is shorter than its reference.
 *
 * @return The variables of the case.
 */
std::vector<VariableMapping> Table()
{
    return {
        { L"Documents", L"C:\\Users\\appbox\\Documents" },
        { L"Windows",   L"C:\\Windows"                  }
    };
}

/**
 * @brief Build the data of a registry string value.
 * @param[in] text Text of the value, without a terminator.
 * @return The UTF-16 data of the value with its terminator.
 */
std::vector<BYTE> Terminated(const std::wstring& text)
{
    std::vector<BYTE> data((text.size() + 1) * sizeof(wchar_t), 0);
    memcpy(data.data(), text.c_str(), text.size() * sizeof(wchar_t));
    return data;
}

/**
 * @brief Read the raw text out of value data, null characters included.
 * @param[in] data Raw data of a string value.
 * @param[in] size Size of the data in bytes.
 * @return The text of the value.
 */
std::wstring RawText(const BYTE* data, std::size_t size)
{
    std::wstring text(size / sizeof(wchar_t), L'\0');
    if (!text.empty())
    {
        memcpy(text.data(), data, text.size() * sizeof(wchar_t));
    }
    return text;
}

/**
 * @brief Read a text out of raw value data.
 * @param[in] data Raw data of a `REG_SZ` value.
 * @return The text of the value, without its terminator.
 */
std::wstring TextOf(const BYTE* data, std::size_t size)
{
    std::wstring text = RawText(data, size);

    while (!text.empty() && text.back() == L'\0')
    {
        text.pop_back();
    }
    return text;
}

/**
 * @brief Build a `KeyValuePartialInformation` buffer which holds a value.
 * @param[in] type The `REG_*` type code of the value.
 * @param[in] data Raw data of the value.
 * @param[in] extra Bytes the buffer carries behind the record.
 * @return The buffer.
 */
std::vector<BYTE> PartialBuffer(DWORD type, const std::vector<BYTE>& data, std::size_t extra = 0)
{
    std::vector<BYTE> buffer(offsetof(KEY_VALUE_PARTIAL_INFORMATION, Data) + data.size() + extra, 0);

    auto* info = reinterpret_cast<KEY_VALUE_PARTIAL_INFORMATION*>(buffer.data());
    info->Type = type;
    info->DataLength = static_cast<ULONG>(data.size());
    if (!data.empty())
    {
        memcpy(info->Data, data.data(), data.size());
    }

    return buffer;
}

/**
 * @brief Build a `KeyValuePartialInformationAlign64` buffer which holds a value.
 * @param[in] type The `REG_*` type code of the value.
 * @param[in] data Raw data of the value.
 * @param[in] extra Bytes the buffer carries behind the record.
 * @return The buffer.
 */
std::vector<BYTE> PartialAlign64Buffer(DWORD type, const std::vector<BYTE>& data, std::size_t extra = 0)
{
    std::vector<BYTE> buffer(offsetof(KEY_VALUE_PARTIAL_INFORMATION_ALIGN64, Data) + data.size() + extra, 0);

    auto* info = reinterpret_cast<KEY_VALUE_PARTIAL_INFORMATION_ALIGN64*>(buffer.data());
    info->Type = type;
    info->DataLength = static_cast<ULONG>(data.size());
    if (!data.empty())
    {
        memcpy(info->Data, data.data(), data.size());
    }

    return buffer;
}

/**
 * @brief Build a `KeyValueFullInformation` buffer which holds a named value.
 * @param[in] type The `REG_*` type code of the value.
 * @param[in] name Name of the value.
 * @param[in] data Raw data of the value.
 * @param[in] extra Bytes the buffer carries behind the data.
 * @return The buffer.
 */
std::vector<BYTE> FullBuffer(DWORD type, const std::wstring& name, const std::vector<BYTE>& data, std::size_t extra = 0)
{
    const auto header = static_cast<std::size_t>(offsetof(KEY_VALUE_FULL_INFORMATION, Name));
    const auto name_bytes = name.size() * sizeof(wchar_t);
    const auto data_offset = (header + name_bytes + 7) & ~static_cast<std::size_t>(7);

    std::vector<BYTE> buffer(data_offset + data.size() + extra, 0);

    auto* info = reinterpret_cast<KEY_VALUE_FULL_INFORMATION*>(buffer.data());
    info->Type = type;
    info->DataOffset = static_cast<ULONG>(data_offset);
    info->DataLength = static_cast<ULONG>(data.size());
    info->NameLength = static_cast<ULONG>(name_bytes);
    if (!name.empty())
    {
        memcpy(buffer.data() + header, name.c_str(), name_bytes);
    }
    if (!data.empty())
    {
        memcpy(buffer.data() + data_offset, data.data(), data.size());
    }

    return buffer;
}

/**
 * @brief Read the data of a `KeyValuePartialInformation` buffer.
 * @param[in] buffer The buffer.
 * @return The data of the value.
 */
std::wstring PartialText(const std::vector<BYTE>& buffer)
{
    const auto* info = reinterpret_cast<const KEY_VALUE_PARTIAL_INFORMATION*>(buffer.data());
    return TextOf(info->Data, info->DataLength);
}

} // namespace

TEST(Unit_RegistryValueExpansion, PartialInformationExpandsTheText)
{
    const std::vector<BYTE> data = Terminated(L"%APPBOX:Documents%\\Foo");

    /* The expanded text is longer than the reference, so the buffer carries the
     * room the retry of the caller would provide. */
    std::vector<BYTE> buffer = PartialBuffer(REG_SZ, data, 0x100);
    ULONG             result = static_cast<ULONG>(buffer.size());

    const NTSTATUS status = ExpandValueBuffer(KeyValuePartialInformation, buffer.data(),
                                              static_cast<ULONG>(buffer.size()), Table(), &result);

    EXPECT_EQ(status, STATUS_SUCCESS);

    const std::wstring expected = L"C:\\Users\\appbox\\Documents\\Foo";
    EXPECT_EQ(PartialText(buffer), expected);

    const auto* info = reinterpret_cast<const KEY_VALUE_PARTIAL_INFORMATION*>(buffer.data());
    EXPECT_EQ(info->Type, static_cast<ULONG>(REG_SZ));
    EXPECT_EQ(info->DataLength, static_cast<ULONG>((expected.size() + 1) * sizeof(wchar_t)));
    EXPECT_EQ(result, static_cast<ULONG>(offsetof(KEY_VALUE_PARTIAL_INFORMATION, Data) + info->DataLength));
}

TEST(Unit_RegistryValueExpansion, FullInformationKeepsTheDataOffset)
{
    std::vector<BYTE> buffer = FullBuffer(REG_EXPAND_SZ, L"Path", Terminated(L"%APPBOX:Windows%\\Foo"));

    const auto* before = reinterpret_cast<const KEY_VALUE_FULL_INFORMATION*>(buffer.data());
    const ULONG offset = before->DataOffset;
    const ULONG name_length = before->NameLength;

    ULONG          result = static_cast<ULONG>(buffer.size());
    const NTSTATUS status =
        ExpandValueBuffer(KeyValueFullInformation, buffer.data(), static_cast<ULONG>(buffer.size()), Table(), &result);

    EXPECT_EQ(status, STATUS_SUCCESS);

    const auto* info = reinterpret_cast<const KEY_VALUE_FULL_INFORMATION*>(buffer.data());
    EXPECT_EQ(info->DataOffset, offset);
    EXPECT_EQ(info->NameLength, name_length);
    EXPECT_EQ(TextOf(buffer.data() + info->DataOffset, info->DataLength), L"C:\\Windows\\Foo");
    EXPECT_EQ(result, info->DataOffset + info->DataLength);
}

TEST(Unit_RegistryValueExpansion, AlignedClassesAreExpandedAsWell)
{
    /* The aligned partial class places the data behind its fixed part. */
    {
        std::vector<BYTE> buffer = PartialAlign64Buffer(REG_SZ, Terminated(L"%APPBOX:Windows%"));
        ULONG             result = static_cast<ULONG>(buffer.size());

        const NTSTATUS status = ExpandValueBuffer(KeyValuePartialInformationAlign64, buffer.data(),
                                                  static_cast<ULONG>(buffer.size()), Table(), &result);
        EXPECT_EQ(status, STATUS_SUCCESS);

        const auto* info = reinterpret_cast<const KEY_VALUE_PARTIAL_INFORMATION_ALIGN64*>(buffer.data());
        EXPECT_EQ(TextOf(info->Data, info->DataLength), L"C:\\Windows");
    }

    /* The aligned full class names the offset of the data like the plain one. */
    {
        std::vector<BYTE> buffer = FullBuffer(REG_SZ, L"Path", Terminated(L"%APPBOX:Windows%"));
        ULONG             result = static_cast<ULONG>(buffer.size());

        const NTSTATUS status = ExpandValueBuffer(KeyValueFullInformationAlign64, buffer.data(),
                                                  static_cast<ULONG>(buffer.size()), Table(), &result);
        EXPECT_EQ(status, STATUS_SUCCESS);

        const auto* info = reinterpret_cast<const KEY_VALUE_FULL_INFORMATION*>(buffer.data());
        EXPECT_EQ(TextOf(buffer.data() + info->DataOffset, info->DataLength), L"C:\\Windows");
    }
}

TEST(Unit_RegistryValueExpansion, MultiStringExpandsEveryItem)
{
    const std::wstring text = std::wstring(L"%APPBOX:Windows%\\A") + L'\0' + L"plain" + L'\0' + L'\0';

    std::vector<BYTE> data(text.size() * sizeof(wchar_t));
    memcpy(data.data(), text.data(), data.size());

    std::vector<BYTE> buffer = PartialBuffer(REG_MULTI_SZ, data);
    ULONG             result = static_cast<ULONG>(buffer.size());

    const NTSTATUS status = ExpandValueBuffer(KeyValuePartialInformation, buffer.data(),
                                              static_cast<ULONG>(buffer.size()), Table(), &result);
    EXPECT_EQ(status, STATUS_SUCCESS);

    const auto*        info = reinterpret_cast<const KEY_VALUE_PARTIAL_INFORMATION*>(buffer.data());
    const std::wstring expanded = RawText(info->Data, info->DataLength);

    EXPECT_EQ(expanded, std::wstring(L"C:\\Windows\\A") + L'\0' + L"plain" + L'\0' + L'\0');
}

TEST(Unit_RegistryValueExpansion, NonStringTypeIsLeftAlone)
{
    const std::vector<BYTE> reference = Terminated(L"%APPBOX:Windows%");

    std::vector<BYTE>       buffer = PartialBuffer(REG_BINARY, reference);
    const std::vector<BYTE> before = buffer;

    ULONG          result = static_cast<ULONG>(buffer.size());
    const NTSTATUS status = ExpandValueBuffer(KeyValuePartialInformation, buffer.data(),
                                              static_cast<ULONG>(buffer.size()), Table(), &result);

    EXPECT_EQ(status, STATUS_SUCCESS);
    EXPECT_EQ(buffer, before);
    EXPECT_EQ(result, static_cast<ULONG>(buffer.size()));
}

TEST(Unit_RegistryValueExpansion, ValueWithoutAReferenceIsLeftAlone)
{
    std::vector<BYTE>       buffer = PartialBuffer(REG_SZ, Terminated(L"C:\\Foo\\Bar"));
    const std::vector<BYTE> before = buffer;

    ULONG          result = static_cast<ULONG>(buffer.size());
    const NTSTATUS status = ExpandValueBuffer(KeyValuePartialInformation, buffer.data(),
                                              static_cast<ULONG>(buffer.size()), Table(), &result);

    EXPECT_EQ(status, STATUS_SUCCESS);
    EXPECT_EQ(buffer, before);
    EXPECT_EQ(result, static_cast<ULONG>(buffer.size()));
}

TEST(Unit_RegistryValueExpansion, BasicInformationCarriesNoData)
{
    std::vector<BYTE> buffer(offsetof(KEY_VALUE_BASIC_INFORMATION, Name) + 5 * sizeof(wchar_t), 0);

    auto* info = reinterpret_cast<KEY_VALUE_BASIC_INFORMATION*>(buffer.data());
    info->Type = REG_SZ;
    info->NameLength = 5 * sizeof(wchar_t);
    memcpy(info->Name, L"Value", 5 * sizeof(wchar_t));

    const std::vector<BYTE> before = buffer;

    ULONG          result = static_cast<ULONG>(buffer.size());
    const NTSTATUS status =
        ExpandValueBuffer(KeyValueBasicInformation, buffer.data(), static_cast<ULONG>(buffer.size()), Table(), &result);

    EXPECT_EQ(status, STATUS_SUCCESS);
    EXPECT_EQ(buffer, before);
    EXPECT_EQ(result, static_cast<ULONG>(buffer.size()));

    const ValueDataView view =
        ViewValueData(KeyValueBasicInformation, buffer.data(), static_cast<ULONG>(buffer.size()));
    EXPECT_FALSE(view.has_data);
}

TEST(Unit_RegistryValueExpansion, BufferTooSmallReportsTheRequiredSize)
{
    const std::vector<BYTE> data = Terminated(L"%APPBOX:Documents%\\Foo");

    /* The buffer holds the record of the kernel exactly, not the expanded text. */
    std::vector<BYTE>       buffer = PartialBuffer(REG_SZ, data);
    const std::vector<BYTE> before_data(data.begin(), data.end());

    ULONG          result = 0;
    const NTSTATUS status = ExpandValueBuffer(KeyValuePartialInformation, buffer.data(),
                                              static_cast<ULONG>(buffer.size()), Table(), &result);

    EXPECT_EQ(status, STATUS_BUFFER_OVERFLOW);

    const std::wstring expected = L"C:\\Users\\appbox\\Documents\\Foo";
    const auto         required =
        static_cast<ULONG>(offsetof(KEY_VALUE_PARTIAL_INFORMATION, Data) + (expected.size() + 1) * sizeof(wchar_t));
    EXPECT_EQ(result, required);

    /* The record names the size of the expanded text, the data is untouched. */
    const auto* info = reinterpret_cast<const KEY_VALUE_PARTIAL_INFORMATION*>(buffer.data());
    EXPECT_EQ(info->DataLength, static_cast<ULONG>((expected.size() + 1) * sizeof(wchar_t)));
    EXPECT_EQ(std::vector<BYTE>(info->Data, info->Data + data.size()), before_data);
}

TEST(Unit_RegistryValueExpansion, NullResultLengthIsAccepted)
{
    std::vector<BYTE> buffer = PartialBuffer(REG_SZ, Terminated(L"%APPBOX:Windows%\\Foo"));

    const NTSTATUS status = ExpandValueBuffer(KeyValuePartialInformation, buffer.data(),
                                              static_cast<ULONG>(buffer.size()), Table(), nullptr);

    EXPECT_EQ(status, STATUS_SUCCESS);
    EXPECT_EQ(PartialText(buffer), L"C:\\Windows\\Foo");
}

TEST(Unit_RegistryValueExpansion, RecordOutsideTheBufferIsLeftAlone)
{
    const std::vector<BYTE> data = Terminated(L"%APPBOX:Windows%\\Foo");

    std::vector<BYTE> buffer = PartialBuffer(REG_SZ, data);

    /* The record claims more data than the buffer carries. */
    auto* info = reinterpret_cast<KEY_VALUE_PARTIAL_INFORMATION*>(buffer.data());
    info->DataLength = static_cast<ULONG>(data.size() + 0x100);

    const std::vector<BYTE> claimed = buffer;

    ULONG          result = static_cast<ULONG>(buffer.size());
    const NTSTATUS status = ExpandValueBuffer(KeyValuePartialInformation, buffer.data(),
                                              static_cast<ULONG>(buffer.size()), Table(), &result);

    EXPECT_EQ(status, STATUS_SUCCESS);
    EXPECT_EQ(buffer, claimed);
    EXPECT_EQ(result, static_cast<ULONG>(buffer.size()));
}

TEST(Unit_RegistryValueExpansion, BatchIsExpandedAndPackedAgain)
{
    const std::vector<BYTE> text = Terminated(L"%APPBOX:Documents%\\Foo");
    const std::vector<BYTE> dword = { 0x78, 0x56, 0x34, 0x12 };

    /* The entries are packed the way the kernel packs them. The expanded text
     * is longer than the one of the kernel, so the buffer of the case carries
     * the room the retry of the caller would provide. */
    std::vector<BYTE> buffer(text.size() + dword.size() + 0x100, 0);
    memcpy(buffer.data(), text.data(), text.size());
    memcpy(buffer.data() + text.size(), dword.data(), dword.size());

    KEY_VALUE_ENTRY entries[2] = {};
    entries[0].DataOffset = 0;
    entries[0].DataLength = static_cast<ULONG>(text.size());
    entries[0].Type = REG_SZ;
    entries[1].DataOffset = static_cast<ULONG>(text.size());
    entries[1].DataLength = static_cast<ULONG>(dword.size());
    entries[1].Type = REG_DWORD;

    const std::wstring expected = L"C:\\Users\\appbox\\Documents\\Foo";
    const std::size_t  expanded = (expected.size() + 1) * sizeof(wchar_t);

    ULONG          length = static_cast<ULONG>(buffer.size());
    ULONG          required = 0;
    const NTSTATUS status = ExpandValueBatch(entries, 2, buffer.data(), &length, &required, Table());

    EXPECT_EQ(status, STATUS_SUCCESS);
    EXPECT_EQ(required, static_cast<ULONG>(expanded + dword.size()));
    EXPECT_EQ(length, required);

    /* The entries are packed again in the order of the call, without padding. */
    EXPECT_EQ(entries[0].DataOffset, 0u);
    EXPECT_EQ(entries[0].DataLength, static_cast<ULONG>(expanded));
    EXPECT_EQ(entries[1].DataOffset, static_cast<ULONG>(expanded));
    EXPECT_EQ(entries[1].DataLength, static_cast<ULONG>(dword.size()));

    EXPECT_EQ(TextOf(buffer.data() + entries[0].DataOffset, entries[0].DataLength), expected);
    EXPECT_EQ(std::vector<BYTE>(buffer.begin() + entries[1].DataOffset,
                                buffer.begin() + entries[1].DataOffset + entries[1].DataLength),
              dword);
}

TEST(Unit_RegistryValueExpansion, BatchReportsTheRequiredSize)
{
    const std::vector<BYTE> text = Terminated(L"%APPBOX:Documents%\\Foo");

    std::vector<BYTE> buffer = text;
    KEY_VALUE_ENTRY   entries[1] = {};
    entries[0].DataOffset = 0;
    entries[0].DataLength = static_cast<ULONG>(text.size());
    entries[0].Type = REG_SZ;

    ULONG          length = static_cast<ULONG>(buffer.size());
    ULONG          required = 0;
    const NTSTATUS status = ExpandValueBatch(entries, 1, buffer.data(), &length, &required, Table());

    EXPECT_EQ(status, STATUS_BUFFER_OVERFLOW);

    const std::wstring expected = L"C:\\Users\\appbox\\Documents\\Foo";
    EXPECT_EQ(required, static_cast<ULONG>((expected.size() + 1) * sizeof(wchar_t)));
    EXPECT_EQ(length, static_cast<ULONG>(buffer.size()));
}
