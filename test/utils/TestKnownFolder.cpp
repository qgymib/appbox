#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include "TestKnownFolder.hpp"
#include <Shlobj.h>
#include <algorithm>
#include <stdexcept>

struct KnownFolderMap
{
    const wchar_t* id;
    const GUID     guid;
};

/*
 * The tokens the cases use as layer keys of their resource tree. The launcher
 * resolves the very same known folders (`launcher/utils/KnownFolder.cpp`), so a
 * case and the sandbox agree on the real directory behind a token.
 */
static const KnownFolderMap KnownFolders[] = {
    { L"#USERPROFILE#",        FOLDERID_Profile            },
    { L"#Documents#",          FOLDERID_Documents          },
    { L"#Desktop#",            FOLDERID_Desktop            },
    { L"#AppData#",            FOLDERID_RoamingAppData     },
    { L"#LocalAppData#",       FOLDERID_LocalAppData       },
    { L"#LocalAppDataLow#",    FOLDERID_LocalAppDataLow    },
    { L"#Downloads#",          FOLDERID_Downloads          },
    { L"#Favorites#",          FOLDERID_Favorites          },
    { L"#Music#",              FOLDERID_Music              },
    { L"#Pictures#",           FOLDERID_Pictures           },
    { L"#StartMenu#",          FOLDERID_StartMenu          },
    { L"#Programs#",           FOLDERID_Programs           },
    { L"#Startup#",            FOLDERID_Startup            },
    { L"#ProgramData#",        FOLDERID_ProgramData        },
    { L"#ProgramFilesCommon#", FOLDERID_ProgramFilesCommon },
    { L"#Windows#",            FOLDERID_Windows            },
    { L"#System32#",           FOLDERID_System             },
    { L"#Fonts#",              FOLDERID_Fonts              },
};

std::wstring appbox::test::GetKnownFolderPath(const std::wstring& folder_id, bool pure)
{
    std::wstring ret;

    for (const auto& folder : KnownFolders)
    {
        if (folder_id == folder.id)
        {
            wchar_t* path = nullptr;
            if (SHGetKnownFolderPath(folder.guid, 0, nullptr, &path) != S_OK)
            {
                throw std::runtime_error("Failed to get known folder path");
            }

            ret = path;
            CoTaskMemFree(path);

            if (pure)
            {
                ret.erase(std::remove(ret.begin(), ret.end(), L':'), ret.end());
            }

            return ret;
        }
    }

    throw std::runtime_error("Unknown known folder id");
}
