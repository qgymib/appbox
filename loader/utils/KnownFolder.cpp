#include "KnownFolder.hpp"
#include <spdlog/spdlog.h>
#include <Shlobj.h>

struct FolderMapping
{
    const std::wstring name; /* Folder name */
    const GUID         guid; /* Folder GUID */
};

static const FolderMapping s_known_folders[] = {
    /* Known FolderID */
    { L"%Fonts%",                  FOLDERID_Fonts                  }, /* %windir%\Fonts */
    { L"%LocalAppData%",           FOLDERID_LocalAppData           }, /* %LOCALAPPDATA% (%USERPROFILE%\AppData\Local) */
    { L"%LocalAppDataLow%",        FOLDERID_LocalAppDataLow        }, /* %USERPROFILE%\AppData\LocalLow */
    { L"%Profile%",                FOLDERID_Profile                }, /* %USERPROFILE% (%SystemDrive%\Users\%USERNAME%) */
    { L"%ProgramData%",            FOLDERID_ProgramData            }, /* %ALLUSERSPROFILE% (%ProgramData%, %SystemDrive%\ProgramData) */
    { L"%ProgramFiles%",           FOLDERID_ProgramFiles           }, /* %ProgramFiles% (%SystemDrive%\Program Files) */
    { L"%ProgramFilesX64%",        FOLDERID_ProgramFilesX64        }, /* %ProgramFiles% (%SystemDrive%\Program Files) */
    { L"%ProgramFilesX86%",        FOLDERID_ProgramFilesX86        }, /* %ProgramFiles% (%SystemDrive%\Program Files) */
    { L"%ProgramFilesCommon%",     FOLDERID_ProgramFilesCommon     }, /* %ProgramFiles%\Common Files */
    { L"%ProgramFilesCommonX64%",  FOLDERID_ProgramFilesCommonX64  }, /* %ProgramFiles%\Common Files */
    { L"%ProgramFilesCommonX86%",  FOLDERID_ProgramFilesCommonX86  }, /* %ProgramFiles%\Common Files */
    { L"%RoamingAppData%",         FOLDERID_RoamingAppData         }, /* %APPDATA% (%USERPROFILE%\AppData\Roaming) */
    { L"%UserProfiles%",           FOLDERID_UserProfiles           }, /* %SystemDrive%\Users */
    { L"%UserProgramFiles%",       FOLDERID_UserProgramFiles       }, /* %LOCALAPPDATA%\Programs */
    { L"%UserProgramFilesCommon%", FOLDERID_UserProgramFilesCommon }, /* %LOCALAPPDATA%\Programs\Common */
    { L"%Windows%",                FOLDERID_Windows                }, /* %windir% */
    /* For compatibility */
    { L"%ALLUSERSPROFILE%",        FOLDERID_ProgramData            }, /* %ALLUSERSPROFILE% (%ProgramData%, %SystemDrive%\ProgramData) */
    { L"%APPDATA%",                FOLDERID_RoamingAppData         }, /* %APPDATA% (%USERPROFILE%\AppData\Roaming) */
    { L"%USERPROFILE%",            FOLDERID_Profile                }, /* %USERPROFILE% (%SystemDrive%\Users\%USERNAME%) */
    { L"%windir%",                 FOLDERID_Windows                }, /* %windir% */
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

std::wstring appbox::ExpandKnownFolder(const std::wstring& path)
{
    if (path.empty())
    {
        return path;
    }

    if (path.front() != L'%')
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
            return GetFolderPath(entry.guid) + L"\\" + path.substr(entry.name.size());
        }
    }

    return path;
}
