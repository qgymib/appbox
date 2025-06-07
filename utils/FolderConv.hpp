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
     * Decodes the given string using the folder map. The method performs substitutions
     * until no further decoding operations are needed or possible. The mappings for
     * decoding are maintained within the class.
     *
     * @param[in] str The input string to be decoded.
     * @return The fully decoded string after applying all applicable substitutions.
     */
    std::wstring Decode(const std::wstring& str);

protected:
    /**
     * Processes the input string, attempting to decode a single substitution based on
     * the folder map. The method searches for encoded patterns and replaces them with their
     * corresponding decoded values if a match is found.
     *
     * @param[in,out] str The string to process for decoding. This string is modified
     *                    in place if a substitution occurs.
     * @return True if a substitution was performed, false otherwise.
     */
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
        { L"@AppDataDesktop@",         &FOLDERID_AppDataDesktop         },
        { L"@AppDataDocuments@",       &FOLDERID_AppDataDocuments       },
        { L"@AppDataProgramData@",     &FOLDERID_AppDataProgramData     },
        { L"@ApplicationShortcuts@",   &FOLDERID_ApplicationShortcuts   },
        { L"@CommonStartup@",          &FOLDERID_CommonStartup          },
        { L"@Cookies@",                &FOLDERID_Cookies                },
        { L"@Desktop@",                &FOLDERID_Desktop                },
        { L"@Documents@",              &FOLDERID_Documents              },
        { L"@Downloads@",              &FOLDERID_Downloads              },
        { L"@Favorites@",              &FOLDERID_Favorites              },
        { L"@Fonts@",                  &FOLDERID_Fonts                  },
        { L"@History@",                &FOLDERID_History                },
        { L"@InternetCache@",          &FOLDERID_InternetCache          },
        { L"@InternetFolder@",         &FOLDERID_InternetFolder         },
        { L"@LocalAppData@",           &FOLDERID_LocalAppData           },
        { L"@LocalAppDataLow@",        &FOLDERID_LocalAppDataLow        },
        { L"@LocalizedResourcesDir@",  &FOLDERID_LocalizedResourcesDir  },
        { L"@Music@",                  &FOLDERID_Music                  },
        { L"@Pictures@",               &FOLDERID_Pictures               },
        { L"@Profile@",                &FOLDERID_Profile                },
        { L"@ProgramData@",            &FOLDERID_ProgramData            },
        { L"@ProgramFiles@",           &FOLDERID_ProgramFiles           },
        { L"@ProgramFilesX64@",        &FOLDERID_ProgramFilesX64        },
        { L"@ProgramFilesX86@",        &FOLDERID_ProgramFilesX86        },
        { L"@ProgramFilesCommon@",     &FOLDERID_ProgramFilesCommon     },
        { L"@ProgramFilesCommonX64@",  &FOLDERID_ProgramFilesCommonX64  },
        { L"@ProgramFilesCommonX86@",  &FOLDERID_ProgramFilesCommonX86  },
        { L"@Programs@",               &FOLDERID_Programs               },
        { L"@Public@",                 &FOLDERID_Public                 },
        { L"@PublicDesktop@",          &FOLDERID_PublicDesktop          },
        { L"@PublicDocuments@",        &FOLDERID_PublicDocuments        },
        { L"@PublicDownloads@",        &FOLDERID_PublicDownloads        },
        { L"@PublicMusic@",            &FOLDERID_PublicMusic            },
        { L"@PublicPictures@",         &FOLDERID_PublicPictures         },
        { L"@PublicVideos@",           &FOLDERID_PublicVideos           },
        { L"@QuickLaunch@",            &FOLDERID_QuickLaunch            },
        { L"@Recent@",                 &FOLDERID_Recent                 },
        { L"@RoamingAppData@",         &FOLDERID_RoamingAppData         },
        { L"@Screenshots@",            &FOLDERID_Screenshots            },
        { L"@SendTo@",                 &FOLDERID_SendTo                 },
        { L"@StartMenu@",              &FOLDERID_StartMenu              },
        { L"@Startup@",                &FOLDERID_Startup                },
        { L"@System@",                 &FOLDERID_System                 },
        { L"@SystemX86@",              &FOLDERID_SystemX86              },
        { L"@UserProfiles@",           &FOLDERID_UserProfiles           },
        { L"@UserProgramFiles@",       &FOLDERID_UserProgramFiles       },
        { L"@UserProgramFilesCommon@", &FOLDERID_UserProgramFilesCommon },
        { L"@Windows@",                &FOLDERID_Windows                },
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
