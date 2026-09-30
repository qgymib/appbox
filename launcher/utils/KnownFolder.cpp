#include "KnownFolder.hpp"
#include <spdlog/spdlog.h>
#include <Shlobj.h>
#include "WString.hpp"

struct FolderMapping
{
    const std::wstring name; /* Layer key token, e.g. L"#ProgramFiles#" */
    const GUID         guid; /* Folder GUID */
};

/*
 * The layer key is a `#Name#` delimited token. A `#` is a plain file name
 * character, so a layer key never collides with the environment variable
 * syntax of the shell (`%Name%`, where `%%` is an escape sequence) when the
 * path is handed to a command line or to an expanding API.
 */
/*
 * Only the layer keys the packer produces are mapped: every layer directory
 * below `app\filesystem` is named after the layer key of a preset directory of
 * the packer, so a key which no preset uses has no layer to describe. A layer
 * key which is not listed here is rejected by MapBaseFS with `Unknown folder`.
 *
 * The keys share no prefix with each other, which keeps the prefix match of
 * ExpandKnownFolder unambiguous.
 */
static const FolderMapping s_known_folders[] = {
    /* Known FolderID */
    { L"#ProgramFiles#", FOLDERID_ProgramFiles }, /* %ProgramFiles% (%SystemDrive%\Program Files) */
    { L"#USERPROFILE#",  FOLDERID_Profile      }, /* %USERPROFILE% (%SystemDrive%\Users\%USERNAME%) */
    { L"#Documents#",    FOLDERID_Documents    }, /* the Documents folder of the user (may be redirected) */
    { L"#Desktop#",      FOLDERID_Desktop      }, /* the Desktop folder of the user (may be redirected) */
    { L"#Windows#",      FOLDERID_Windows      }, /* the system directory, %SystemRoot% */
    { L"#System32#",     FOLDERID_System       }, /* the 32 bit system directory, %SystemRoot%\system32 */
};

static std::wstring GetFolderPath(const GUID& guid)
{
    wchar_t* path = nullptr;
    if (SHGetKnownFolderPath(guid, 0, nullptr, &path) != S_OK)
    {
        throw std::runtime_error("Failed to get known folder path");
    }

    std::wstring folder_path(path);
    CoTaskMemFree(path);

    return folder_path;
}

bool appbox::SearchFolderID(const std::wstring& name, std::wstring& folder_path)
{
    for (const auto& entry : s_known_folders)
    {
        if (name == entry.name)
        {
            folder_path = GetFolderPath(entry.guid);
            return true;
        }
    }
    return false;
}

std::vector<appbox::KnownFolderVariable> appbox::KnownFolderVariables()
{
    std::vector<KnownFolderVariable> variables;

    for (const auto& entry : s_known_folders)
    {
        /*
         * The name of the variable is the layer key without its `#` delimiters,
         * so the supported list follows the table of the known folders: a
         * preset directory which is added later brings its variable with it.
         */
        if (entry.name.size() < 3 || entry.name.front() != L'#' || entry.name.back() != L'#')
        {
            SPDLOG_WARN("a known folder of the table is not delimited by '#', its variable is skipped");
            continue;
        }

        const std::string name = WideToUTF8(entry.name.substr(1, entry.name.size() - 2));

        /*
         * A folder which cannot be resolved on this machine is skipped: the
         * other variables stay usable instead of failing the whole start.
         */
        std::wstring folder_path;
        try
        {
            folder_path = GetFolderPath(entry.guid);
        }
        catch (const std::exception& e)
        {
            SPDLOG_WARN("the variable '{}' is skipped, its known folder cannot be resolved: {}", name, e.what());
            continue;
        }

        variables.push_back(KnownFolderVariable{ name, WideToUTF8(folder_path) });
    }

    return variables;
}

std::wstring appbox::ExpandKnownFolder(const std::wstring& path)
{
    if (path.empty())
    {
        return path;
    }

    /* Only a `#Name#` delimited token is expanded. */
    if (path.front() != L'#')
    {
        return path;
    }

    for (const auto& entry : s_known_folders)
    {
        if (entry.name.size() > path.size())
        {
            continue;
        }

        if (_wcsnicmp(entry.name.c_str(), path.c_str(), entry.name.size()) == 0)
        {
            const auto folder = GetFolderPath(entry.guid);
            const auto rest = path.substr(entry.name.size());
            if (rest.empty())
            {
                return folder;
            }
            if (rest.front() == L'\\')
            {
                /* The remainder already carries a separator, appending
                 * another one would produce a doubled backslash which the
                 * NT object manager rejects. */
                return folder + rest;
            }
            return folder + L"\\" + rest;
        }
    }

    return path;
}
