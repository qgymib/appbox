#ifndef APPBOX_COMMON_STARTUP_FILE_TYPE_HPP
#define APPBOX_COMMON_STARTUP_FILE_TYPE_HPP

#include <cstddef>
#include <string>

namespace appbox
{

/**
 * @brief Kind of a file which can be a startup file of a packaged application.
 *
 * The kind is derived from the extension of a file name, so a file which no
 * extension of the table below names is not startable at all.
 */
enum class StartupFileKind
{
    None,            ///< Not a startable file.
    Program,         ///< A program image (`.exe`), started on its own.
    Batch,           ///< A batch file (`.bat`), run by the command interpreter.
    CommandScript,   ///< A command script (`.cmd`), run by the command interpreter.
    PowerShellScript ///< A PowerShell script (`.ps1`), run by Windows PowerShell.
};

namespace detail
{

/**
 * @brief Extensions of the startable files, including the dot.
 *
 * The table is the single source of truth of the packer and of the launcher:
 * the tree of the startup files dialog shows exactly these files, the model
 * accepts exactly these files as startup files and the launcher knows from the
 * kind how a file of the table has to be started.
 */
inline constexpr const wchar_t* const kStartupFileExtensions[] = { L".exe", L".bat", L".cmd", L".ps1" };

/** Number of the entries of `kStartupFileExtensions`. */
inline constexpr std::size_t kStartupFileExtensionCount = 4;

/**
 * @brief Fold an ASCII letter to lower case.
 *
 * The fold is limited to the ASCII range on purpose: a file name on Windows is
 * compared by the file system, and the extensions of the table are ASCII, so no
 * locale or Unicode table is needed here. This header has to stay usable in a
 * translation unit which does not include `<windows.h>`.
 *
 * @param[in] character Character to fold.
 * @return The folded character, unchanged when it is not an ASCII letter.
 */
inline wchar_t FoldAscii(wchar_t character)
{
    if (character >= L'A' && character <= L'Z')
    {
        return static_cast<wchar_t>(character - L'A' + L'a');
    }
    return character;
}

/**
 * @brief Whether a file name ends with one extension, ignoring the case.
 * @param[in] name Name of the file, without a directory.
 * @param[in] extension Extension to test, including the dot.
 * @return true when the name ends with the extension.
 */
inline bool EndsWithExtension(const std::wstring& name, const wchar_t* extension)
{
    std::size_t size = 0;
    while (extension[size] != L'\0')
    {
        ++size;
    }

    /* A name which is the extension alone is no file with that extension. */
    if (name.size() <= size)
    {
        return false;
    }

    for (std::size_t index = 0; index < size; ++index)
    {
        if (FoldAscii(name[name.size() - size + index]) != extension[index])
        {
            return false;
        }
    }

    return true;
}

/**
 * @brief Get the index of the first character of the file name of a path.
 *
 * A path which carries no separator is a file name of its own. Both separators
 * of Windows are accepted, because a caller may hand over a path which was
 * spelled with forward slashes.
 *
 * @param[in] path Path to inspect.
 * @return The index of the file name inside the path.
 */
inline std::size_t FileNameOffset(const std::wstring& path)
{
    const auto separator = path.find_last_of(L"\\/");
    return separator == std::wstring::npos ? 0 : separator + 1;
}

} // namespace detail

/**
 * @brief Get the kind of a file from its extension.
 *
 * The lookup ignores the case, because a file name on Windows does, and it only
 * considers the file name, so a dot of a directory of the path is never taken
 * for the extension of the file.
 *
 * @param[in] path Path or name of a file.
 * @return The kind of the file, `StartupFileKind::None` when its extension is
 *         not one of the startable ones.
 */
inline StartupFileKind StartupFileKindOf(const std::wstring& path)
{
    const auto name = path.substr(detail::FileNameOffset(path));

    for (std::size_t index = 0; index < detail::kStartupFileExtensionCount; ++index)
    {
        if (!detail::EndsWithExtension(name, detail::kStartupFileExtensions[index]))
        {
            continue;
        }

        switch (index)
        {
        case 0:
            return StartupFileKind::Program;
        case 1:
            return StartupFileKind::Batch;
        case 2:
            return StartupFileKind::CommandScript;
        default:
            return StartupFileKind::PowerShellScript;
        }
    }

    return StartupFileKind::None;
}

/**
 * @brief Whether a file can be a startup file of a packaged application.
 *
 * A startable file is a program (`.exe`), a batch file (`.bat`), a command
 * script (`.cmd`) or a PowerShell script (`.ps1`). The packer only offers these
 * files in the startup files dialog and only accepts them as startup files.
 *
 * @param[in] path Path or name of a file.
 * @return true when the file carries one of the startable extensions.
 */
inline bool IsStartableFile(const std::wstring& path)
{
    return StartupFileKindOf(path) != StartupFileKind::None;
}

/**
 * @brief Get the extensions of the startable files as one readable text.
 *
 * The text names the extensions in the order of the table, separated by commas,
 * e.g. `.exe, .bat, .cmd, .ps1`. It is meant for the messages which tell a user
 * which files can be chosen.
 *
 * @return The extension list.
 */
inline std::string StartupFileExtensionList()
{
    std::string list;
    for (std::size_t index = 0; index < detail::kStartupFileExtensionCount; ++index)
    {
        if (index != 0)
        {
            list.append(", ");
        }

        const wchar_t* extension = detail::kStartupFileExtensions[index];
        for (std::size_t character = 0; extension[character] != L'\0'; ++character)
        {
            list.push_back(static_cast<char>(extension[character]));
        }
    }

    return list;
}

} // namespace appbox

#endif // APPBOX_COMMON_STARTUP_FILE_TYPE_HPP
