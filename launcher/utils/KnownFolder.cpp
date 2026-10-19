#include "KnownFolder.hpp"
#include <spdlog/spdlog.h>
#include <Shlobj.h>
#include "WString.hpp"

/**
 * @brief Folder which a 32 bit process does not see at all.
 *
 * `FOLDERID_ProgramFilesX64` is not supported for a 32 bit application, so the
 * variable of such a folder names no path there and the reference keeps its own
 * spelling. The sentinel marks that row of the table below; every other folder
 * names a real path in both views, either the same one or the redirected one.
 */
static const GUID kFolderNotApplicable = {};

/**
 * @brief One row of the known folder table.
 *
 * The folder is resolved twice: `guid` is the folder a 64 bit process sees and
 * `guid_x86` the folder a 32 bit process sees. The operating system redirects
 * some of them, which is what the table of the variable expansion describes:
 * `FOLDERID_ProgramFiles` names `%ProgramFiles(x86)%` in a 32 bit process while
 * `FOLDERID_ProgramFilesX86` names the same folder there as it does in a 64 bit
 * one.
 *
 * `is_layer` tells whether the key names a layer directory of the packer. Only
 * such a key is mounted (`SearchFolderID`) and expanded in the path of a
 * startup file (`ExpandKnownFolder`): a key which only exists to be expanded in
 * a value of the workspace names no layer, because no preset directory of the
 * packer produces it.
 */
struct FolderMapping
{
    const std::wstring name;     /* Layer key token, e.g. L"#ProgramFiles#" */
    const GUID         guid;     /* Folder of a 64 bit process */
    const GUID*        guid_x86; /* Folder of a 32 bit process: nullptr = the same folder,
                                    &kFolderNotApplicable = the folder does not exist there */
    const bool is_layer;         /* Whether the key names a layer directory of the packer */
};

/*
 * The layer key is a `#Name#` delimited token. A `#` is a plain file name
 * character, so a layer key never collides with the environment variable
 * syntax of the shell (`%Name%`, where `%%` is an escape sequence) when the
 * path is handed to a command line or to an expanding API.
 */
/*
 * Only the layer keys the packer produces are layers: every layer directory
 * below `app\filesystem` is named after the layer key of a preset directory of
 * the packer, so a key which no preset uses has no layer to describe. A layer
 * key which is not listed here is rejected by MapBaseFS with `Unknown folder`.
 *
 * The keys share no prefix with each other, which keeps the prefix match of
 * ExpandKnownFolder unambiguous. Both lookups compare a key exactly as it is
 * listed here, so only the keys the packer produces name a folder.
 *
 * A key whose `is_layer` is false exists for the variable expansion alone:
 * `#ProgramFilesX64#` and `#ProgramFilesCommonX64#` name the 64 bit folder
 * unambiguously, which is what the capture needs to reference it from a value
 * that a 32 bit process may expand as well.
 */
static const FolderMapping s_known_folders[] = {
    /* Known FolderID of a 64 bit process, folder of a 32 bit process */
    { L"#ProgramFiles#",          FOLDERID_ProgramFiles,          &FOLDERID_ProgramFilesX86,       true  }, /* %ProgramFiles% */
    { L"#ProgramFilesX64#",       FOLDERID_ProgramFilesX64,       &kFolderNotApplicable,           false }, /* %ProgramFiles%, 64 bit only */
    { L"#ProgramFilesCommon#",    FOLDERID_ProgramFilesCommon,    &FOLDERID_ProgramFilesCommonX86,
     true                                                                                                }, /* %ProgramFilesCommon% */
    { L"#ProgramFilesCommonX64#", FOLDERID_ProgramFilesCommonX64, nullptr,
     false                                                                                               }, /* %ProgramFilesCommon%, 64 bit only */
    { L"#ProgramFilesX86#",       FOLDERID_ProgramFilesX86,       nullptr,                         true  }, /* %ProgramFiles(x86)% */
    { L"#ProgramFilesCommonX86#", FOLDERID_ProgramFilesCommonX86, nullptr,                         true  }, /* %ProgramFilesCommon(x86)% */
    { L"#USERPROFILE#",           FOLDERID_Profile,               nullptr,                         true  }, /* %USERPROFILE% (%SystemDrive%\Users\%USERNAME%) */
    { L"#Documents#",             FOLDERID_Documents,             nullptr,                         true  }, /* the Documents folder of the user (may be redirected) */
    { L"#Desktop#",               FOLDERID_Desktop,               nullptr,                         true  }, /* the Desktop folder of the user (may be redirected) */
    { L"#AppData#",               FOLDERID_RoamingAppData,        nullptr,                         true  }, /* %AppData% (%USERPROFILE%\AppData\Roaming) */
    { L"#LocalAppData#",          FOLDERID_LocalAppData,          nullptr,                         true  }, /* %LocalAppData% (%USERPROFILE%\AppData\Local) */
    { L"#LocalAppDataLow#",       FOLDERID_LocalAppDataLow,       nullptr,
     true                                                                                                }, /* %LocalAppDataLow% (%USERPROFILE%\AppData\LocalLow) */
    { L"#Downloads#",             FOLDERID_Downloads,             nullptr,                         true  }, /* the Downloads folder of the user (may be redirected) */
    { L"#Favorites#",             FOLDERID_Favorites,             nullptr,                         true  }, /* the Favorites folder of the user (may be redirected) */
    { L"#Music#",                 FOLDERID_Music,                 nullptr,                         true  }, /* the Music folder of the user (may be redirected) */
    { L"#Pictures#",              FOLDERID_Pictures,              nullptr,                         true  }, /* the Pictures folder of the user (may be redirected) */
    { L"#StartMenu#",             FOLDERID_StartMenu,             nullptr,                         true  }, /* the Start Menu folder of the user (may be redirected) */
    { L"#Programs#",              FOLDERID_Programs,              nullptr,                         true  }, /* the Programs folder inside the Start Menu of the user */
    { L"#Startup#",               FOLDERID_Startup,               nullptr,                         true  }, /* the Startup folder inside the Programs folder of the user */
    { L"#ProgramData#",           FOLDERID_ProgramData,           nullptr,                         true  }, /* %ProgramData% (%SystemDrive%\ProgramData) */
    { L"#Windows#",               FOLDERID_Windows,               nullptr,                         true  }, /* the system directory, %SystemRoot% */
    { L"#System32#",              FOLDERID_System,                nullptr,                         true  }, /* the 32 bit system directory, %SystemRoot%\system32 */
    { L"#SystemX86#",             FOLDERID_SystemX86,             nullptr,                         true  }, /* %SystemRoot%\SysWOW64 */
    { L"#Fonts#",                 FOLDERID_Fonts,                 nullptr,                         true  }, /* the font directory of the system, %SystemRoot%\Fonts */
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

/**
 * @brief Get the name of a variable out of a layer key.
 *
 * The name of a variable is the layer key without its `#` delimiters, so the
 * supported list follows the table of the known folders: a folder which is
 * added later brings its variable with it.
 *
 * @param[in] key Layer key of the table.
 * @param[out] name Name of the variable, in UTF-8.
 * @return false when the key is not delimited by `#` and names no variable.
 */
static bool VariableNameOf(const std::wstring& key, std::string& name)
{
    if (key.size() < 3 || key.front() != L'#' || key.back() != L'#')
    {
        return false;
    }

    name = appbox::WideToUTF8(key.substr(1, key.size() - 2));
    return true;
}

/**
 * @brief Whether a folder is written as a reference by the capture.
 *
 * Only a folder whose 32 bit view is the same folder, or no folder at all, is
 * written: the capture does not know the bitness of the process which expands
 * the reference, so a folder a 32 bit process resolves elsewhere would make the
 * reference name another directory there. `ProgramFiles` and
 * `ProgramFilesCommon` are the two rows this rules out.
 *
 * @param[in] entry Row of the known folder table.
 * @return true when the capture may write the folder as a reference.
 */
static bool IsReferenceFolder(const FolderMapping& entry)
{
    return entry.guid_x86 == nullptr || entry.guid_x86 == &kFolderNotApplicable;
}

bool appbox::SearchFolderID(const std::wstring& name, std::wstring& folder_path)
{
    for (const auto& entry : s_known_folders)
    {
        if (!entry.is_layer)
        {
            continue;
        }

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
        std::string name;
        if (!VariableNameOf(entry.name, name))
        {
            SPDLOG_WARN("a known folder of the table is not delimited by '#', its variable is skipped");
            continue;
        }

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

        /*
         * The second view is the folder a 32 bit process sees. A folder which
         * does not exist for such a process leaves the text empty, which is the
         * `Not applicable` row of the table of the variable expansion: the
         * reference keeps its own spelling there instead of naming a folder.
         */
        std::wstring folder_path_x86;
        if (entry.guid_x86 == nullptr)
        {
            folder_path_x86 = folder_path;
        }
        else if (entry.guid_x86 != &kFolderNotApplicable)
        {
            try
            {
                folder_path_x86 = GetFolderPath(*entry.guid_x86);
            }
            catch (const std::exception& e)
            {
                SPDLOG_WARN("the variable '{}' names no folder in a 32 bit process: {}", name, e.what());
            }
        }

        variables.push_back(KnownFolderVariable{ name, WideToUTF8(folder_path), WideToUTF8(folder_path_x86) });
    }

    return variables;
}

std::vector<appbox::KnownFolderReference> appbox::KnownFolderReferences()
{
    std::vector<KnownFolderReference> references;

    for (const auto& entry : s_known_folders)
    {
        if (!IsReferenceFolder(entry))
        {
            continue;
        }

        std::string name;
        if (!VariableNameOf(entry.name, name))
        {
            SPDLOG_WARN("a known folder of the table is not delimited by '#', its reference is skipped");
            continue;
        }

        /* A folder which cannot be resolved on this machine names no path. */
        std::wstring folder_path;
        try
        {
            folder_path = GetFolderPath(entry.guid);
        }
        catch (const std::exception& e)
        {
            SPDLOG_WARN("the reference '{}' is skipped, its known folder cannot be resolved: {}", name, e.what());
            continue;
        }

        references.push_back(KnownFolderReference{ entry.name.substr(1, entry.name.size() - 2), folder_path });
    }

    return references;
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
        if (!entry.is_layer)
        {
            continue;
        }

        if (entry.name.size() > path.size())
        {
            continue;
        }

        /*
         * The key is compared exactly as the table spells it, like the lookup
         * of `SearchFolderID` which decides whether a layer directory is
         * mounted: only the keys the packer produces name a folder, so a
         * spelling of another case names none.
         */
        if (path.compare(0, entry.name.size(), entry.name) == 0)
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
