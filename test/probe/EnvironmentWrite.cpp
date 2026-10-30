#include "WinAPI.h" /* Must be first include file */
#include "EnvironmentWrite.hpp"
#include "utils/GetPEB.hpp"
#include "WString.hpp"
#include <cstring>
#include <string>
#include <vector>

namespace
{

/**
 * @brief The counted writer of the environment of `ntdll`.
 *
 * The entry point takes the address of the block it writes, which may be the
 * address of the environment pointer of the process parameters, or null for the
 * environment of the process itself.
 */
typedef NTSTATUS(NTAPI* T_RtlSetEnvironmentVariable)(PWSTR* Environment, PUNICODE_STRING Name, PUNICODE_STRING Value);

/**
 * @brief The entry point which installs a whole block.
 */
typedef NTSTATUS(NTAPI* T_RtlSetCurrentEnvironment)(PWSTR Environment, PWSTR* PreviousEnvironment);

/**
 * @brief Resolve an entry point of `ntdll`.
 * @param[in] name Name of the export.
 * @return The address of the entry point, null when the module does not carry it.
 */
FARPROC NtdllEntry(const char* name)
{
    const HMODULE ntdll = ::GetModuleHandleW(L"ntdll.dll");
    if (ntdll == nullptr)
    {
        return nullptr;
    }
    return ::GetProcAddress(ntdll, name);
}

/**
 * @brief Read a variable with the wide entry point of the process environment.
 * @param[in] name Name of the variable.
 * @param[out] found Whether the environment holds the variable.
 * @return The value of the variable, empty when it is not held.
 */
std::string ReadWide(const std::wstring& name, bool& found)
{
    std::vector<wchar_t> buffer(32768);

    ::SetLastError(ERROR_SUCCESS);
    const DWORD length = ::GetEnvironmentVariableW(name.c_str(), buffer.data(), static_cast<DWORD>(buffer.size()));

    found = length != 0 || ::GetLastError() != ERROR_ENVVAR_NOT_FOUND;
    if (length == 0)
    {
        return std::string();
    }

    return appbox::WideToUTF8(std::wstring(buffer.data(), length));
}

/**
 * @brief Whether two variable names name the same variable.
 * @param[in] left Left name.
 * @param[in] right Right name.
 * @return true when both names are equal ignoring the case.
 */
bool NamesEqual(const std::wstring& left, const std::wstring& right)
{
    const int result = ::CompareStringOrdinal(left.data(), static_cast<int>(left.size()), right.data(),
                                              static_cast<int>(right.size()), TRUE);
    return result == CSTR_EQUAL;
}

/**
 * @brief Find the character which separates the name of an entry from its value.
 * @param[in] entry One entry of a block, without its terminator.
 * @return The position of the separator, npos when the entry carries none.
 */
std::size_t SeparatorOf(const std::wstring& entry)
{
    if (entry.empty())
    {
        return std::wstring::npos;
    }

    const std::size_t begin = entry.front() == L'=' ? 1 : 0;
    return entry.find(L'=', begin);
}

/**
 * @brief Read the entries of the environment of this process.
 * @return The entries, each spelled `name=value`.
 */
std::vector<std::wstring> CurrentEntries()
{
    std::vector<std::wstring> entries;

    const LPWCH block = ::GetEnvironmentStringsW();
    if (block == nullptr)
    {
        return entries;
    }

    const wchar_t* cursor = block;
    while (*cursor != L'\0')
    {
        entries.emplace_back(cursor);
        cursor += entries.back().size() + 1;
    }

    ::FreeEnvironmentStringsW(block);
    return entries;
}

/**
 * @brief Store or remove one entry of a block which is being built.
 * @param[in,out] entries The entries of the block.
 * @param[in] name Name of the variable.
 * @param[in] value The value to store, null to remove the variable.
 */
void ApplyEntry(std::vector<std::wstring>& entries, const std::wstring& name, const std::wstring* value)
{
    for (auto entry = entries.begin(); entry != entries.end(); ++entry)
    {
        const std::size_t separator = SeparatorOf(*entry);
        if (separator != std::wstring::npos && NamesEqual(entry->substr(0, separator), name))
        {
            if (value == nullptr)
            {
                entries.erase(entry);
            }
            else
            {
                *entry = name + L"=" + *value;
            }
            return;
        }
    }

    if (value != nullptr)
    {
        entries.push_back(name + L"=" + *value);
    }
}

/**
 * @brief Build the text of an environment block.
 * @param[in] entries The entries, each spelled `name=value`.
 * @return The block, terminated by an empty entry.
 */
std::vector<wchar_t> BuildBlock(const std::vector<std::wstring>& entries)
{
    std::vector<wchar_t> block;
    for (const auto& entry : entries)
    {
        block.insert(block.end(), entry.begin(), entry.end());
        block.push_back(L'\0');
    }
    block.push_back(L'\0');
    return block;
}

/**
 * @brief Replace the environment of this process with a block.
 * @param[in] entries The entries of the new block.
 * @return true when the operating system accepted the block.
 */
bool ReplaceWithEnvironmentStrings(const std::vector<std::wstring>& entries)
{
    std::vector<wchar_t> block = BuildBlock(entries);
    return ::SetEnvironmentStringsW(block.data()) != FALSE;
}

/**
 * @brief Install a block as the environment of this process.
 *
 * The block has to come from the heap of the process, because the entry point
 * asks the heap for its size and releases it when the block is replaced.
 *
 * @param[in] entries The entries of the new block.
 * @return true when the operating system accepted the block.
 */
bool ReplaceWithCurrentEnvironment(const std::vector<std::wstring>& entries)
{
    const std::vector<wchar_t> text = BuildBlock(entries);

    auto* block = static_cast<wchar_t*>(::HeapAlloc(::GetProcessHeap(), 0, text.size() * sizeof(wchar_t)));
    if (block == nullptr)
    {
        return false;
    }
    std::memcpy(block, text.data(), text.size() * sizeof(wchar_t));

    const auto entry = reinterpret_cast<T_RtlSetCurrentEnvironment>(NtdllEntry("RtlSetCurrentEnvironment"));
    if (entry == nullptr)
    {
        ::HeapFree(::GetProcessHeap(), 0, block);
        return false;
    }

    const NTSTATUS status = entry(block, nullptr);
    if (status < 0)
    {
        ::HeapFree(::GetProcessHeap(), 0, block);
        return false;
    }

    /* The operating system owns the block from here on. */
    return true;
}

/**
 * @brief Fill a counted string from text.
 * @param[in] text The text of the string.
 * @param[out] out The counted string, which points into the text.
 */
void InitCountedString(const std::wstring& text, UNICODE_STRING& out)
{
    out.Buffer = const_cast<PWSTR>(text.c_str());
    out.Length = static_cast<USHORT>(text.size() * sizeof(wchar_t));
    out.MaximumLength = static_cast<USHORT>((text.size() + 1) * sizeof(wchar_t));
}

/**
 * @brief Store or remove one variable through `RtlSetEnvironmentVariable`.
 * @param[in] block The block to write, null for the environment of the process.
 * @param[in] name Name of the variable.
 * @param[in] value The value to store, null to remove the variable.
 * @return true when the entry point accepted the modification.
 */
bool StoreCounted(PWSTR* block, const std::wstring& name, const std::wstring* value)
{
    const auto entry = reinterpret_cast<T_RtlSetEnvironmentVariable>(NtdllEntry("RtlSetEnvironmentVariable"));
    if (entry == nullptr)
    {
        return false;
    }

    UNICODE_STRING counted_name;
    InitCountedString(name, counted_name);

    UNICODE_STRING  counted_value;
    UNICODE_STRING* counted_value_ptr = nullptr;
    if (value != nullptr)
    {
        InitCountedString(*value, counted_value);
        counted_value_ptr = &counted_value;
    }

    return entry(block, &counted_name, counted_value_ptr) >= 0;
}

/**
 * @brief Store or remove one variable through one entry point of the process.
 *
 * The two entry points which replace the whole block are handled by the caller
 * of this function, so they are refused here.
 *
 * @param[in] entry Entry point the probe writes through.
 * @param[in] name Name of the variable.
 * @param[in] value The value to store, null to remove the variable.
 * @return true when the entry point accepted the modification.
 */
bool WriteVariable(appbox::test::ProtocolEnvironmentWrite::Entry entry, const std::wstring& name,
                   const std::wstring* value)
{
    using Entry = appbox::test::ProtocolEnvironmentWrite::Entry;

    switch (entry)
    {
    case Entry::Ansi: {
        const std::string ansi_name = appbox::WideToUTF8(name);
        const std::string ansi_value = value == nullptr ? std::string() : appbox::WideToUTF8(*value);
        return ::SetEnvironmentVariableA(ansi_name.c_str(), value == nullptr ? nullptr : ansi_value.c_str()) != FALSE;
    }
    case Entry::RtlNull:
        return StoreCounted(nullptr, name, value);
    case Entry::RtlBlock:
        return StoreCounted(appbox::GetProcessEnvironmentSlot(), name, value);
    case Entry::Wide:
        return ::SetEnvironmentVariableW(name.c_str(), value == nullptr ? nullptr : value->c_str()) != FALSE;
    case Entry::Strings:
    case Entry::Current:
        break;
    }

    return false;
}

} // namespace

appbox::test::ProtocolEnvironmentWrite::Rsp appbox::test::ProbeEnvironmentWrite_Entry(
    const appbox::test::ProtocolEnvironmentWrite::Req& request)
{
    using Entry = appbox::test::ProtocolEnvironmentWrite::Entry;
    const auto& req = request;

    appbox::test::ProtocolEnvironmentWrite::Rsp rsp;

    /* <windows.h> defines min and max as macros, so the smaller size is picked here. */
    const std::size_t count = req.values.size() < req.names.size() ? req.values.size() : req.names.size();

    if (req.entry == Entry::Strings || req.entry == Entry::Current)
    {
        /*
         * The two entry points replace the whole environment, so the probe builds
         * the block out of the environment it carries and applies the requested
         * modifications to it. The answer of the call is the answer of the
         * replacement, which is the same for every entry of the request.
         */
        std::vector<std::wstring> entries = CurrentEntries();

        for (const auto& name : req.remove)
        {
            const std::wstring wide_name = appbox::UTF8ToWide(name);
            ApplyEntry(entries, wide_name, nullptr);
        }

        for (std::size_t index = 0; index < count; ++index)
        {
            const std::wstring wide_name = appbox::UTF8ToWide(req.names[index]);
            const std::wstring wide_value = appbox::UTF8ToWide(req.values[index]);
            ApplyEntry(entries, wide_name, &wide_value);
        }

        const bool replaced = req.entry == Entry::Strings ? ReplaceWithEnvironmentStrings(entries)
                                                          : ReplaceWithCurrentEnvironment(entries);

        rsp.stored.assign(count, replaced);
        rsp.removed.assign(req.remove.size(), replaced);
    }
    else
    {
        for (std::size_t index = 0; index < count; ++index)
        {
            const std::wstring name = appbox::UTF8ToWide(req.names[index]);
            const std::wstring value = appbox::UTF8ToWide(req.values[index]);
            rsp.stored.push_back(WriteVariable(req.entry, name, &value));
        }

        for (const auto& name : req.remove)
        {
            rsp.removed.push_back(WriteVariable(req.entry, appbox::UTF8ToWide(name), nullptr));
        }
    }

    for (const auto& name : req.read)
    {
        bool found = false;
        rsp.values.push_back(ReadWide(appbox::UTF8ToWide(name), found));
        rsp.found.push_back(found);
    }

    return rsp;
}
