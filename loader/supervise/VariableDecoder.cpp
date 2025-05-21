#include <wx/stdpaths.h>
#include <Windows.h>
#include <atlbase.h>
#include <Knownfolders.h>
#include <Shobjidl.h>
#include "VariableDecoder.hpp"

#include "utils/pair.hpp"

struct VariableDecoder::Data
{
    bool                  DecodeOnce(std::wstring& str);
    std::vector<Variable> mVars;
};

/**
 * @note To use these values correctly, It must be compiled in x64.
 * @see https://learn.microsoft.com/en-us/windows/win32/shell/knownfolderid
 */
static const appbox::Pair<const wchar_t*, const KNOWNFOLDERID*> s_known_folders[] = {
    /* Common variable for convenience. */
    { L"%ALLUSERSPROFILE%",   &FOLDERID_ProgramData     },
    { L"%APPDATA%",           &FOLDERID_RoamingAppData  },
    { L"%LOCALAPPDATA%",      &FOLDERID_LocalAppData    },
    { L"%ProgramData%",       &FOLDERID_ProgramData     },
    { L"%ProgramFiles%",      &FOLDERID_ProgramFiles    },
    { L"%ProgramFiles(X86)%", &FOLDERID_ProgramFilesX86 },
    { L"%PUBLIC%",            &FOLDERID_Public          },
    { L"%USERPROFILE%",       &FOLDERID_Profile         },
    { L"%windir%",            &FOLDERID_Windows         },
};

bool VariableDecoder::Data::DecodeOnce(std::wstring& str)
{
    bool ret = false;
    for (auto it = mVars.begin(); it != mVars.end(); ++it)
    {
        const Variable&         var = *it;
        std::wstring::size_type pos = str.find(var.key);
        if (pos != std::wstring::npos)
        {
            ret = true;
            str.replace(pos, var.key.size(), var.value);
        }
    }
    return ret;
}

VariableDecoder::VariableDecoder()
{
    mData = new Data();

    CComPtr<IKnownFolderManager> pManager;
    HRESULT hr = CoCreateInstance(CLSID_KnownFolderManager, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&pManager));
    if (!SUCCEEDED(hr))
    {
        throw std::runtime_error("CoCreateInstance(CLSID_KnownFolderManager) failed");
    }

    for (size_t i = 0; i < std::size(s_known_folders); i++)
    {
        CComPtr<IKnownFolder> folder;
        hr = pManager->GetFolder(*s_known_folders[i].v, &folder);
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

        Append(s_known_folders[i].k, path);
        CoTaskMemFree(path);
    }
}

VariableDecoder::~VariableDecoder()
{
    delete mData;
}

void VariableDecoder::Append(const Variable& var)
{
    mData->mVars.push_back(var);
}

void VariableDecoder::Append(const std::wstring& key, const std::wstring& value)
{
    Append({ key, value });
}

void VariableDecoder::Append(const Variable* vars, size_t size)
{
    for (size_t i = 0; i < size; ++i)
    {
        Append(vars[i]);
    }
}

std::wstring VariableDecoder::Decode(const std::wstring& str) const
{
    std::wstring ret = str;
    while (mData->DecodeOnce(ret))
    {
    }
    return ret;
}
