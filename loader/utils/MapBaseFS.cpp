#include "utils/ConvertDosPathToNtPath.hpp"
#include "utils/KnownFolder.hpp"
#include "MapBaseFS.hpp"
#include "SandboxLayout.hpp"
#include <CLI/Encoding.hpp>
#include <algorithm>
#include <filesystem>
#include <spdlog/spdlog.h>

/**
 * @brief Layer names which are reserved for the other isolation domains.
 *
 * The filesystem domain of the archive holds one directory per layer key, so a
 * directory which is named after another isolation domain never describes a
 * layer of the view.
 */
static const wchar_t* s_retain[] = {
    L"#REGISTRY#",
    L"#NETWORK#",
};

/**
 * @brief Whether a child of the layer root is not a layer of the view.
 *
 * The reserved names of the other isolation domains and the isolation file of
 * the filesystem domain live next to the layers, so both are skipped; every
 * other name has to be a layer key.
 *
 * @param[in] name File name of the directory entry.
 * @return true when the entry must not become a layer.
 */
static bool IsSkipped(const std::wstring& name)
{
    for (const auto& entry : s_retain)
    {
        if (name == entry)
        {
            return true;
        }
    }

    return name == appbox::layout::kIsolationFileNameW;
}

DWORD appbox::MapBaseFS(const std::string& layer_root, std::vector<SandboxLowerFS>& mapped_fs)
{
    auto dos_path_w = CLI::widen(layer_root);
    /* Remove trailing slash */
    while (!dos_path_w.empty() && dos_path_w.back() == '\\')
    {
        dos_path_w.pop_back();
    }

    if (dos_path_w.empty())
    {
        SPDLOG_ERROR("the layer root of the sandbox is empty");
        return ERROR_INVALID_PARAMETER;
    }

    std::vector<appbox::SandboxLowerFS> tmp_fs;
    std::error_code                     ec;
    std::filesystem::directory_iterator it(dos_path_w, ec);
    if (ec)
    {
        SPDLOG_ERROR(L"failed to enumerate the layer root: {}", dos_path_w);
        return ERROR_PATH_NOT_FOUND;
    }

    for (const auto& entry : it)
    {
        auto name = entry.path().filename().wstring();
        if (IsSkipped(name))
        {
            continue;
        }

        SandboxLowerFS lower_fs;
        auto           host_path = dos_path_w + L"\\" + name;
        if (appbox::ConvertDosPathToNtPath(CLI::narrow(host_path), lower_fs.host_nt_path) != 0)
        {
            SPDLOG_ERROR(L"failed to convert the host path: {}", host_path);
            return ERROR_INVALID_PARAMETER;
        }

        std::wstring folder_path;
        if (appbox::SearchFolderID(name, folder_path))
        {
            if (appbox::ConvertDosPathToNtPath(CLI::narrow(folder_path), lower_fs.mapped_nt_path) != 0)
            {
                SPDLOG_ERROR(L"failed to convert the known folder path: {}", folder_path);
                return ERROR_INVALID_PARAMETER;
            }
        }
        else if (name.size() == 1)
        {
            auto driver = name + L":";
            if (appbox::ConvertDosPathToNtPath(CLI::narrow(driver), lower_fs.mapped_nt_path) != 0)
            {
                SPDLOG_ERROR(L"failed to convert the drive path: {}", driver);
                return ERROR_INVALID_PARAMETER;
            }
        }
        else
        {
            SPDLOG_ERROR(L"Unknown folder: {}", name);
            return ERROR_INVALID_PARAMETER;
        }

        tmp_fs.push_back(lower_fs);
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
