#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include "EnvironmentRead.hpp"
#include "WString.hpp"
#include <string>
#include <vector>

namespace
{

/**
 * @brief Read a variable with the wide entry point of the process environment.
 *
 * A variable which the environment does not hold is told apart from a variable
 * which carries an empty value by the error of the call.
 *
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
 * @brief Read a variable with the ANSI entry point of the process environment.
 * @param[in] name Name of the variable.
 * @return The value of the variable, empty when it is not held.
 */
std::string ReadAnsi(const std::wstring& name)
{
    const std::string ansi_name = appbox::WideToUTF8(name);

    std::vector<char> buffer(32768);
    const DWORD length = ::GetEnvironmentVariableA(ansi_name.c_str(), buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0)
    {
        return std::string();
    }

    return std::string(buffer.data(), length);
}

/**
 * @brief Read the entries of the wide block which enumerates the environment.
 * @return The entries of the block, each one `name=value`.
 */
std::vector<std::string> ReadWideEntries()
{
    std::vector<std::string> entries;

    LPWCH block = ::GetEnvironmentStringsW();
    if (block == nullptr)
    {
        return entries;
    }

    for (const wchar_t* cursor = block; *cursor != L'\0';)
    {
        const std::wstring entry(cursor);
        entries.push_back(appbox::WideToUTF8(entry));
        cursor += entry.size() + 1;
    }

    ::FreeEnvironmentStringsW(block);
    return entries;
}

/**
 * @brief Read the entries of the ANSI block which enumerates the environment.
 * @return The entries of the block, each one `name=value`.
 */
std::vector<std::string> ReadAnsiEntries()
{
    std::vector<std::string> entries;

    LPCH block = ::GetEnvironmentStringsA();
    if (block == nullptr)
    {
        return entries;
    }

    for (const char* cursor = block; *cursor != '\0';)
    {
        const std::string entry(cursor);
        entries.push_back(entry);
        cursor += entry.size() + 1;
    }

    ::FreeEnvironmentStringsA(block);
    return entries;
}

/**
 * @brief Expand the references of a text with the wide entry point.
 * @param[in] text The text to expand.
 * @return The expanded text.
 */
std::string ExpandWide(const std::wstring& text)
{
    const DWORD needed = ::ExpandEnvironmentStringsW(text.c_str(), nullptr, 0);
    if (needed == 0)
    {
        return std::string();
    }

    std::vector<wchar_t> buffer(needed);
    const DWORD          written = ::ExpandEnvironmentStringsW(text.c_str(), buffer.data(), needed);
    if (written == 0 || written > needed)
    {
        return std::string();
    }

    /* The reported size counts the terminator of the text. */
    return appbox::WideToUTF8(std::wstring(buffer.data(), written - 1));
}

/**
 * @brief Expand the references of a text with the ANSI entry point.
 * @param[in] text The text to expand.
 * @return The expanded text.
 */
std::string ExpandAnsi(const std::string& text)
{
    const DWORD needed = ::ExpandEnvironmentStringsA(text.c_str(), nullptr, 0);
    if (needed == 0)
    {
        return std::string();
    }

    std::vector<char> buffer(needed);
    const DWORD       written = ::ExpandEnvironmentStringsA(text.c_str(), buffer.data(), needed);
    if (written == 0 || written > needed)
    {
        return std::string();
    }

    return std::string(buffer.data(), written - 1);
}

/**
 * @brief Ask the lowest reader of the process environment for the size of a
 *        variable while bringing no value buffer at all.
 *
 * The loader of the operating system asks for the search path of a DLL that
 * way: it reads the length of the value afterwards and copies the value into a
 * buffer it allocated, so an answer of success with a null buffer would make it
 * copy from the null pointer. The call is made through the entry point of
 * `ntdll` itself, which the sandbox hooks as well.
 *
 * @param[in] name Name of the variable.
 * @param[out] status Status the entry point reported.
 * @param[out] length Length the entry point wrote into the value.
 */
void QueryWithoutABuffer(const std::wstring& name, std::uint32_t& status, std::uint32_t& length)
{
    /**
     * @see https://ntdoc.m417z.com/rtlqueryenvironmentvariable_u
     */
    typedef LONG(NTAPI * T_RtlQueryEnvironmentVariable_U)(PWSTR Environment, PUNICODE_STRING Name,
                                                          PUNICODE_STRING Value, PULONG ReturnLength);

    const auto query = reinterpret_cast<T_RtlQueryEnvironmentVariable_U>(
        ::GetProcAddress(::GetModuleHandleW(L"ntdll.dll"), "RtlQueryEnvironmentVariable_U"));
    if (query == nullptr)
    {
        return;
    }

    UNICODE_STRING name_string;
    name_string.Length = static_cast<USHORT>(name.size() * sizeof(wchar_t));
    name_string.MaximumLength = static_cast<USHORT>(name_string.Length + sizeof(wchar_t));
    name_string.Buffer = const_cast<PWSTR>(name.c_str());

    UNICODE_STRING value;
    value.Length = 0;
    value.MaximumLength = 0xFFFF; /* A caller which believes it has room. */
    value.Buffer = nullptr;

    ULONG returned = 0;

    status = static_cast<std::uint32_t>(query(nullptr, &name_string, &value, &returned));
    length = value.Length;
}

} // namespace

static nlohmann::json ProbeEnvironmentRead_Entry(const nlohmann::json& data)
{
    const auto req = data.get<appbox::test::ProtocolEnvironmentRead::Req>();

    appbox::test::ProtocolEnvironmentRead::Rsp rsp;

    for (const auto& name : req.names)
    {
        const std::wstring wide_name = appbox::UTF8ToWide(name);

        bool found = false;
        rsp.values.push_back(ReadWide(wide_name, found));
        rsp.found.push_back(found);
        rsp.ansi_values.push_back(ReadAnsi(wide_name));
    }

    for (const auto& text : req.expand)
    {
        rsp.expanded.push_back(ExpandWide(appbox::UTF8ToWide(text)));
        rsp.ansi_expanded.push_back(ExpandAnsi(text));
    }

    rsp.entries = ReadWideEntries();
    rsp.ansi_entries = ReadAnsiEntries();

    if (!req.names.empty())
    {
        QueryWithoutABuffer(appbox::UTF8ToWide(req.names.front()), rsp.rtl_size_query_status,
                            rsp.rtl_size_query_length);
    }

    return rsp;
}

appbox::test::Probe appbox::test::ProbeEnvironmentRead("EnvironmentRead", ProbeEnvironmentRead_Entry);
