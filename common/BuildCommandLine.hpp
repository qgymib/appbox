#ifndef APPBOX_COMMON_BUILD_COMMANDLINE_HPP
#define APPBOX_COMMON_BUILD_COMMANDLINE_HPP

#include <cstddef>
#include <string>
#include <vector>

namespace appbox
{
namespace detail
{

/**
 * @brief Quote one argument by the inverse rules of CommandLineToArgvW.
 * @param[in] arg The argument to quote.
 * @return The quoted argument, or the plain argument when no quoting is needed.
 */
inline std::wstring QuoteArgument(const std::wstring& arg)
{
    /* Quoting is needed when the argument is empty or contains a space, tab or quote. */
    if (!arg.empty() && arg.find_first_of(L" \t\"") == std::wstring::npos)
    {
        return arg;
    }

    std::wstring quoted = L"\"";
    for (auto it = arg.begin();; ++it)
    {
        size_t num_backslashes = 0;

        while (it != arg.end() && *it == L'\\')
        {
            ++it;
            ++num_backslashes;
        }

        if (it == arg.end())
        {
            /* End of the argument: backslashes are doubled, the closing quote follows. */
            quoted.append(num_backslashes * 2, L'\\');
            break;
        }
        else if (*it == L'"')
        {
            /* Backslashes before a quote are doubled, the quote itself is escaped. */
            quoted.append(num_backslashes * 2 + 1, L'\\');
            quoted.push_back(*it);
        }
        else
        {
            /* Backslashes not followed by a quote are kept as they are. */
            quoted.append(num_backslashes, L'\\');
            quoted.push_back(*it);
        }
    }
    quoted.push_back(L'"');
    return quoted;
}

} // namespace detail

/**
 * @brief Build a command line string from an executable path and arguments.
 * @param[in] exe Path to the executable.
 * @param[in] args List of command line arguments.
 * @return Command line string with quotes and escaping as needed.
 */
inline std::wstring BuildCommandLine(const std::wstring& exe, const std::vector<std::wstring>& args)
{
    std::wstring cmdline = detail::QuoteArgument(exe);
    for (const auto& arg : args)
    {
        cmdline += L' ';
        cmdline += detail::QuoteArgument(arg);
    }
    return cmdline;
}

} // namespace appbox

#endif // APPBOX_COMMON_BUILD_COMMANDLINE_HPP
