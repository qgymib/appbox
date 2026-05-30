#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include "KnownFolder.hpp"
#include <Shlobj.h>
#include <stdexcept>

struct KnownFolderMap
{
    const wchar_t* id;
    const GUID     guid;
};

static const KnownFolderMap KnownFolders[] = {
    { L"%APPDATA%", FOLDERID_RoamingAppData },
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
