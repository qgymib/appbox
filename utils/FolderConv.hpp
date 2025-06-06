#ifndef APPBOX_UTILS_FOLDER_CONV_HPP
#define APPBOX_UTILS_FOLDER_CONV_HPP

#include "utils/winapi.hpp"
#include "utils/pair.hpp"
#include <atlbase.h>
#include <Knownfolders.h>
#include <Shobjidl.h>
#include <map>
#include <string>
#include <stdexcept>

namespace appbox
{

class FolderConv
{
public:
    FolderConv();

    /**
     *
     */
    std::wstring Decode(const std::wstring& str);

protected:
    bool DecodeOnce(std::wstring& str);

private:
    std::map<std::wstring, std::wstring> mFolderMap; /* Key: FolderID, value: Path. */
    std::map<std::wstring, std::wstring> mPathMap;   /* Key: Path, value: FolderID. */
};

inline FolderConv::FolderConv()
{
    /*
     * @see https://learn.microsoft.com/en-us/windows/win32/shell/knownfolderid
     */
    const appbox::Pair<const wchar_t*, const KNOWNFOLDERID*> folders[] = {
        { L"@FOLDERID_AppDataDesktop@",         &FOLDERID_AppDataDesktop         },
        { L"@FOLDERID_AppDataDocuments@",       &FOLDERID_AppDataDocuments       },
        { L"@FOLDERID_AppDataProgramData@",     &FOLDERID_AppDataProgramData     },
        { L"@FOLDERID_ApplicationShortcuts@",   &FOLDERID_ApplicationShortcuts   },
        { L"@FOLDERID_CommonStartup@",          &FOLDERID_CommonStartup          },
        { L"@FOLDERID_Cookies@",                &FOLDERID_Cookies                },
        { L"@FOLDERID_Desktop@",                &FOLDERID_Desktop                },
        { L"@FOLDERID_Documents@",              &FOLDERID_Documents              },
        { L"@FOLDERID_Downloads@",              &FOLDERID_Downloads              },
        { L"@FOLDERID_Favorites@",              &FOLDERID_Favorites              },
        { L"@FOLDERID_Fonts@",                  &FOLDERID_Fonts                  },
        { L"@FOLDERID_History@",                &FOLDERID_History                },
        { L"@FOLDERID_InternetCache@",          &FOLDERID_InternetCache          },
        { L"@FOLDERID_InternetFolder@",         &FOLDERID_InternetFolder         },
        { L"@FOLDERID_LocalAppData@",           &FOLDERID_LocalAppData           },
        { L"@FOLDERID_LocalAppDataLow@",        &FOLDERID_LocalAppDataLow        },
        { L"@FOLDERID_LocalizedResourcesDir@",  &FOLDERID_LocalizedResourcesDir  },
        { L"@FOLDERID_Music@",                  &FOLDERID_Music                  },
        { L"@FOLDERID_Pictures@",               &FOLDERID_Pictures               },
        { L"@FOLDERID_Profile@",                &FOLDERID_Profile                },
        { L"@FOLDERID_ProgramData@",            &FOLDERID_ProgramData            },
        { L"@FOLDERID_ProgramFiles@",           &FOLDERID_ProgramFiles           },
        { L"@FOLDERID_ProgramFilesX64@",        &FOLDERID_ProgramFilesX64        },
        { L"@FOLDERID_ProgramFilesX86@",        &FOLDERID_ProgramFilesX86        },
        { L"@FOLDERID_ProgramFilesCommon@",     &FOLDERID_ProgramFilesCommon     },
        { L"@FOLDERID_ProgramFilesCommonX64@",  &FOLDERID_ProgramFilesCommonX64  },
        { L"@FOLDERID_ProgramFilesCommonX86@",  &FOLDERID_ProgramFilesCommonX86  },
        { L"@FOLDERID_Programs@",               &FOLDERID_Programs               },
        { L"@FOLDERID_Public@",                 &FOLDERID_Public                 },
        { L"@FOLDERID_PublicDesktop@",          &FOLDERID_PublicDesktop          },
        { L"@FOLDERID_PublicDocuments@",        &FOLDERID_PublicDocuments        },
        { L"@FOLDERID_PublicDownloads@",        &FOLDERID_PublicDownloads        },
        { L"@FOLDERID_PublicMusic@",            &FOLDERID_PublicMusic            },
        { L"@FOLDERID_PublicPictures@",         &FOLDERID_PublicPictures         },
        { L"@FOLDERID_PublicVideos@",           &FOLDERID_PublicVideos           },
        { L"@FOLDERID_QuickLaunch@",            &FOLDERID_QuickLaunch            },
        { L"@FOLDERID_Recent@",                 &FOLDERID_Recent                 },
        { L"@FOLDERID_RoamingAppData@",         &FOLDERID_RoamingAppData         },
        { L"@FOLDERID_Screenshots@",            &FOLDERID_Screenshots            },
        { L"@FOLDERID_SendTo@",                 &FOLDERID_SendTo                 },
        { L"@FOLDERID_StartMenu@",              &FOLDERID_StartMenu              },
        { L"@FOLDERID_Startup@",                &FOLDERID_Startup                },
        { L"@FOLDERID_System@",                 &FOLDERID_System                 },
        { L"@FOLDERID_SystemX86@",              &FOLDERID_SystemX86              },
        { L"@FOLDERID_UserProfiles@",           &FOLDERID_UserProfiles           },
        { L"@FOLDERID_UserProgramFiles@",       &FOLDERID_UserProgramFiles       },
        { L"@FOLDERID_UserProgramFilesCommon@", &FOLDERID_UserProgramFilesCommon },
        { L"@FOLDERID_Windows@",                &FOLDERID_Windows                },
    };

    CComPtr<IKnownFolderManager> pManager;
    HRESULT hr = CoCreateInstance(CLSID_KnownFolderManager, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&pManager));
    if (!SUCCEEDED(hr))
    {
        throw std::runtime_error("CoCreateInstance(CLSID_KnownFolderManager) failed");
    }

    for (size_t i = 0; i < std::size(folders); i++)
    {
        CComPtr<IKnownFolder> folder;
        hr = pManager->GetFolder(*folders[i].v, &folder);
        if (!SUCCEEDED(hr))
        {
            continue;
        }

        wchar_t* path = nullptr;
        hr = folder->GetPath(0, &path);
        if (!SUCCEEDED(hr))
        {
            continue;
        }

        mFolderMap[folders[i].k] = path;
        mPathMap[path] = folders[i].k;
        CoTaskMemFree(path);
    }
}

inline std::wstring FolderConv::Decode(const std::wstring& str)
{
    std::wstring result = str;
    while (DecodeOnce(result))
    {
    }
    return result;
}

inline bool FolderConv::DecodeOnce(std::wstring& str)
{
    std::wstring::size_type start_pos = std::wstring::npos;
    std::wstring::size_type end_pos = std::wstring::npos;

    bool escaped = false;
    for (size_t i = 0; i < str.size(); i++)
    {
        if (escaped)
        {
            escaped = false;
            continue;
        }

        if (str[i] == L'\\')
        {
            escaped = true;
            continue;
        }

        if (str[i] == L'@')
        {
            if (start_pos == std::wstring::npos)
            {
                start_pos = i;
                continue;
            }
            else
            {
                end_pos = i;
                break;
            }
        }
    }

    if (end_pos == std::wstring::npos)
    {
        return false;
    }

    std::wstring key = str.substr(start_pos, end_pos - start_pos);
    auto         it = mFolderMap.find(key);
    if (it == mFolderMap.end())
    {
        return false;
    }

    str.replace(start_pos, end_pos - start_pos, it->second);
    return true;
}

} // namespace appbox

#endif
