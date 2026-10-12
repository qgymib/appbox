#ifndef APPBOX_COMMON_LOG_FILE_NAME_HPP
#define APPBOX_COMMON_LOG_FILE_NAME_HPP

#include <cstddef>
#include <string>

namespace appbox
{

/**
 * @brief Time the name of a log file carries.
 *
 * The fields are the ones `SYSTEMTIME` carries in UTC. They are passed as plain
 * numbers because the rule below is shared by the launcher and by the sandbox
 * and this header has to stay usable in a translation unit which does not
 * include `<windows.h>`.
 */
struct LogFileTimestamp
{
    unsigned year = 0;   /* Year, e.g. 2026. */
    unsigned month = 0;  /* Month of the year, 1 to 12. */
    unsigned day = 0;    /* Day of the month, 1 to 31. */
    unsigned hour = 0;   /* Hour of the day, 0 to 23. */
    unsigned minute = 0; /* Minute of the hour, 0 to 59. */
    unsigned second = 0; /* Second of the minute, 0 to 59. */
};

namespace detail
{

/** Extension of a program, which is dropped from the name of a log file. */
inline constexpr const wchar_t kProgramExtension[] = L".exe";

/** Number of the characters of the extension of a program. */
inline constexpr std::size_t kProgramExtensionSize = 4;

/**
 * @brief Append a number with leading zeros.
 * @param[in,out] out Text the number is appended to.
 * @param[in] value The number.
 * @param[in] digits Number of the digits to append.
 */
inline void AppendPaddedNumber(std::wstring& out, unsigned value, unsigned digits)
{
    wchar_t buffer[10];

    for (unsigned index = 0; index < digits; ++index)
    {
        buffer[digits - 1 - index] = static_cast<wchar_t>(L'0' + (value % 10));
        value /= 10;
    }

    out.append(buffer, digits);
}

/**
 * @brief Whether a text ends with the extension of a program.
 *
 * The comparison ignores the case, because a file name on Windows does.
 *
 * @param[in] name Name to test.
 * @return true when the name ends with `.exe`.
 */
inline bool EndsWithProgramExtension(const std::wstring& name)
{
    const std::size_t size = kProgramExtensionSize;
    if (name.size() <= size)
    {
        return false;
    }

    for (std::size_t index = 0; index < size; ++index)
    {
        wchar_t left = name[name.size() - size + index];
        if (left >= L'A' && left <= L'Z')
        {
            left = static_cast<wchar_t>(left - L'A' + L'a');
        }

        if (left != kProgramExtension[index])
        {
            return false;
        }
    }

    return true;
}

} // namespace detail

/**
 * @brief Build the name of the log file of one process.
 *
 * The name is `<program>.<time utc>.<pid>.log`: the name of the executable
 * without its directory and without its extension, the UTC time in the form
 * `YYYYMMDDTHHMMSSZ`, the process id and the extension `log`, separated by
 * dots. The time is part of the name because the process id of an earlier
 * process can be reused.
 *
 * The rule is shared: the launcher names the log file of its own run with it
 * and the sandbox names the log file of every process it is injected into with
 * it, so the log files of one run are recognized by a single name pattern and a
 * run never overwrites the log of an earlier one.
 *
 * @param[in] image_path Path of the executable of the process.
 * @param[in] utc UTC time the process started at.
 * @param[in] pid Process id of the process.
 * @return The file name without a directory.
 */
inline std::wstring LogFileNameOf(const std::wstring& image_path, const LogFileTimestamp& utc, unsigned long pid)
{
    /* The name of the program, without its directory. */
    std::wstring program = image_path;
    const auto   separator = program.find_last_of(L"\\/");
    if (separator != std::wstring::npos)
    {
        program.erase(0, separator + 1);
    }

    if (detail::EndsWithProgramExtension(program))
    {
        program.resize(program.size() - detail::kProgramExtensionSize);
    }

    if (program.empty())
    {
        /* A process whose image path is not readable still gets a log file. */
        program = L"process";
    }

    std::wstring file_name = program;
    file_name.push_back(L'.');
    detail::AppendPaddedNumber(file_name, utc.year, 4);
    detail::AppendPaddedNumber(file_name, utc.month, 2);
    detail::AppendPaddedNumber(file_name, utc.day, 2);
    file_name.push_back(L'T');
    detail::AppendPaddedNumber(file_name, utc.hour, 2);
    detail::AppendPaddedNumber(file_name, utc.minute, 2);
    detail::AppendPaddedNumber(file_name, utc.second, 2);
    file_name.append(L"Z.");
    file_name.append(std::to_wstring(pid));
    file_name.append(L".log");

    return file_name;
}

} // namespace appbox

#endif
