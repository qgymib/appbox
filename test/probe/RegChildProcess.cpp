#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include <CLI/CLI.hpp>
#include <cstdint>
#include <string>
#include <vector>
#include <spdlog/fmt/fmt.h>
#include "RegChildProcess.hpp"
#include "utils/RegistryRootKey.hpp"
#include "WString.hpp"

namespace
{

/** One value the worker writes after it read the value of the case. */
struct WriteRequest
{
    std::wstring name; /* Name of the value. */
    std::wstring data; /* Text of the value, the read token included. */
};

/** Token of a `--set` text which takes the place of the value of `--read`. */
constexpr const wchar_t* kReadToken = L"{read}";

/** Text which takes the place of the token when the read did not succeed. */
constexpr const wchar_t* kMissingText = L"missing";

/** Upper bound of the time the worker stays alive, a guard against a hang. */
constexpr std::int64_t kMaxHoldMilliseconds = 60000;

/**
 * @brief Split one `--set` argument into the name and the text of the value.
 *
 * @param[in] text The argument, `<name>=<text>`.
 * @param[out] request The request which was read.
 * @return true when the argument has both parts.
 */
bool ParseWriteRequest(const std::string& text, WriteRequest& request)
{
    const auto separator = text.find('=');
    if (separator == std::string::npos || separator == 0)
    {
        return false;
    }

    request.name = appbox::UTF8ToWide(text.substr(0, separator));
    request.data = appbox::UTF8ToWide(text.substr(separator + 1));
    return true;
}

/**
 * @brief Replace the read token of a `--set` text by the value of `--read`.
 *
 * @param[in] data The text of the value.
 * @param[in] read The value which was read, empty when the read failed.
 * @param[in] read_ok Whether the read succeeded.
 * @return The text with every occurrence of the token replaced.
 */
std::wstring ExpandReadToken(const std::wstring& data, const std::wstring& read, bool read_ok)
{
    const std::wstring token(kReadToken);
    const std::wstring replacement = read_ok ? read : std::wstring(kMissingText);

    std::wstring expanded;
    std::size_t  position = 0;
    while (true)
    {
        const auto found = data.find(token, position);
        if (found == std::wstring::npos)
        {
            expanded.append(data, position, std::wstring::npos);
            return expanded;
        }

        expanded.append(data, position, found - position);
        expanded.append(replacement);
        position = found + token.size();
    }
}

/**
 * @brief Read a `REG_SZ` value of a key into a string.
 *
 * The buffer grows when the value does not fit into it, so a case is not
 * limited by the size of the value it reads.
 *
 * @param[in] key The key which holds the value.
 * @param[in] name Name of the value, empty for the default value.
 * @param[out] data The text of the value, without its terminator.
 * @return The error code of the registry API.
 */
DWORD ReadStringValue(HKEY key, const std::wstring& name, std::wstring& data)
{
    data.clear();

    std::vector<wchar_t> buffer(256);
    for (;;)
    {
        DWORD size = static_cast<DWORD>(buffer.size() * sizeof(wchar_t));
        DWORD type = REG_NONE;
        DWORD code =
            RegQueryValueExW(key, name.c_str(), nullptr, &type, reinterpret_cast<LPBYTE>(buffer.data()), &size);
        if (code == ERROR_SUCCESS)
        {
            data.assign(buffer.data(), size / sizeof(wchar_t));
            /* The data of a string value carries a terminator. */
            while (!data.empty() && data.back() == L'\0')
            {
                data.pop_back();
            }
            return ERROR_SUCCESS;
        }

        if (code != ERROR_MORE_DATA)
        {
            return code;
        }

        /* The reported size covers the terminator of the string. */
        buffer.resize(size / sizeof(wchar_t) + 1);
    }
}

/**
 * @brief Write a `REG_SZ` value into a key.
 *
 * @param[in] key The key which receives the value.
 * @param[in] name Name of the value, empty for the default value.
 * @param[in] data The text of the value.
 * @return The error code of the registry API.
 */
DWORD WriteStringValue(HKEY key, const std::wstring& name, const std::wstring& data)
{
    const DWORD size = static_cast<DWORD>((data.size() + 1) * sizeof(wchar_t));
    return RegSetValueExW(key, name.c_str(), 0, REG_SZ, reinterpret_cast<const BYTE*>(data.c_str()), size);
}

/**
 * @brief Run the worker: open the key, read, write, hold and leave.
 *
 * The function never returns: it leaves the process through the exit code of
 * the observations (see RegChildExitCode).
 *
 * @param[in] root_name Name of the root key, empty for HKEY_CURRENT_USER.
 * @param[in] key_path Path of the key relative to the root key.
 * @param[in] read_name Name of the value to read, empty to read nothing.
 * @param[in] expect_text Text the value of `read_name` has to carry.
 * @param[in] writes The values to write.
 * @param[in] hold_ms Time the worker stays alive after the writes.
 */
void RunRegChildProcess(const std::string& root_name, const std::string& key_path, const std::string& read_name,
                        const std::string& expect_text, const std::vector<std::string>& writes, std::int64_t hold_ms)
{
    int bits = 0;

    const HKEY root = appbox::test::RegistryRootHandle(root_name);
    if (root == nullptr)
    {
        throw CLI::RuntimeError(bits);
    }

    HKEY key = nullptr;
    if (RegOpenKeyExW(root, appbox::UTF8ToWide(key_path).c_str(), 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &key) !=
        ERROR_SUCCESS)
    {
        throw CLI::RuntimeError(bits);
    }
    bits |= appbox::test::RegChildOpenedKey;

    std::wstring read_data;
    bool         read_ok = false;
    if (!read_name.empty())
    {
        read_ok = ReadStringValue(key, appbox::UTF8ToWide(read_name), read_data) == ERROR_SUCCESS;
        if (read_ok)
        {
            bits |= appbox::test::RegChildReadValue;
            if (!expect_text.empty() && read_data == appbox::UTF8ToWide(expect_text))
            {
                bits |= appbox::test::RegChildValueMatched;
            }
        }
    }

    bool wrote_all = !writes.empty();
    for (const auto& text : writes)
    {
        WriteRequest request;
        if (!ParseWriteRequest(text, request))
        {
            wrote_all = false;
            continue;
        }

        const std::wstring data = ExpandReadToken(request.data, read_data, read_ok);
        if (WriteStringValue(key, request.name, data) != ERROR_SUCCESS)
        {
            wrote_all = false;
        }
    }
    if (wrote_all)
    {
        bits |= appbox::test::RegChildWroteValues;
    }

    /*
     * The caller reads the values while this process is alive, which is what
     * tells a live view of the registry from a state which is only written back
     * when a process leaves.
     */
    if (hold_ms > 0)
    {
        Sleep(static_cast<DWORD>(hold_ms > kMaxHoldMilliseconds ? kMaxHoldMilliseconds : hold_ms));
    }

    RegCloseKey(key);
    throw CLI::RuntimeError(bits);
}

} // namespace

std::string appbox::test::DescribeRegChildExitCode(unsigned long code)
{
    if ((code & ~static_cast<unsigned long>(kRegChildExitCodeMask)) != 0)
    {
        return fmt::format("exit code {:#x} is not one of the worker (0x0-0xf): the process did not reach its own code",
                           code);
    }

    std::string text;
    const auto  append = [&text](bool set, const char* yes, const char* no) {
        if (!text.empty())
        {
            text += " | ";
        }
        text += set ? yes : no;
    };
    append((code & RegChildOpenedKey) != 0, "key opened", "key not opened");
    append((code & RegChildReadValue) != 0, "value read", "value not read");
    append((code & RegChildValueMatched) != 0, "value matched", "value did not match");
    append((code & RegChildWroteValues) != 0, "values written", "values not written");
    return fmt::format("{:#x} ({})", code, text);
}

void appbox::test::RegChildProcessInit(CLI::App& app)
{
    /*
     * The options are static, because the callback of the subcommand runs after
     * the parse of the command line and the subcommand is registered once for
     * the whole life of the process.
     */
    static std::string              root_name;
    static std::string              key_path;
    static std::string              read_name;
    static std::string              expect_text;
    static std::vector<std::string> writes;
    static std::int64_t             hold_ms = 0;

    auto cmd = app.add_subcommand("regchild", "Worker of a registry case: read and write values of a sandboxed key");
    cmd->add_option("--root", root_name, "Root key name, empty means HKEY_CURRENT_USER");
    cmd->add_option("--key", key_path, "Key path relative to the root key, encoding in UTF-8")->required();
    cmd->add_option("--read", read_name, "Name of the value to read, empty reads nothing");
    cmd->add_option("--expect", expect_text, "Text the value of --read has to carry");
    cmd->add_option("--set", writes, "Value to write as <name>=<text>, {read} is the value of --read");
    cmd->add_option("--hold-ms", hold_ms, "Milliseconds to stay alive after the writes");
    cmd->final_callback([&]() { RunRegChildProcess(root_name, key_path, read_name, expect_text, writes, hold_ms); });
}
