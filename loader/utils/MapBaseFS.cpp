#include "utils/ConvertDosPathToNtPath.hpp"
#include "utils/KnownFolder.hpp"
#include "MapBaseFS.hpp"
#include <CLI/Encoding.hpp>
#include <filesystem>
#include <spdlog/spdlog.h>

static const wchar_t* s_retain[] = {
    L"%REGISTRY%",
    L"%NETWORK%",
};

static bool IsRetain(const std::wstring& name)
{
    for (const auto& entry : s_retain)
    {
        if (name == entry)
        {
            return true;
        }
    }
    return false;
}

DWORD appbox::MapBaseFS(const std::string& fs, std::vector<SandboxLowerFS>& mapped_fs)
{
    auto dos_path_w = CLI::widen(fs);
    /* Remove trailing slash */
    while (dos_path_w.back() == '\\')
    {
        dos_path_w.pop_back();
    }

    std::vector<appbox::SandboxLowerFS> tmp_fs;
    std::error_code                     ec;
    for (const auto& entry : std::filesystem::directory_iterator(dos_path_w, ec))
    {
        SandboxLowerFS lower_fs;
        auto           name = entry.path().filename().wstring();
        auto           host_path = dos_path_w + L"\\" + name;
        appbox::ConvertDosPathToNtPath(CLI::narrow(host_path), lower_fs.host_nt_path);

        if (IsRetain(name))
        {
            continue;
        }

        std::wstring folder_path;
        if (appbox::SearchFolderID(name, folder_path))
        {
            appbox::ConvertDosPathToNtPath(CLI::narrow(folder_path), lower_fs.mapped_nt_path);
        }
        else if (name.size() == 1)
        {
            auto driver = name + L":";
            appbox::ConvertDosPathToNtPath(CLI::narrow(driver), lower_fs.mapped_nt_path);
        }
        else
        {
            SPDLOG_ERROR(L"Unknown folder: {}", name);
            return ERROR_INVALID_PARAMETER;
        }

        tmp_fs.push_back(lower_fs);
        continue;
    }

    if (!tmp_fs.empty())
    {
        /* Sort by mapped NT path length */
        std::sort(tmp_fs.begin(), tmp_fs.end(), [](const appbox::SandboxLowerFS& a, const appbox::SandboxLowerFS& b) {
            return a.mapped_nt_path.size() > b.mapped_nt_path.size();
        });

        /* Merge lower fs paths */
        mapped_fs.insert(mapped_fs.end(), tmp_fs.begin(), tmp_fs.end());
    }

    return 0;
}
