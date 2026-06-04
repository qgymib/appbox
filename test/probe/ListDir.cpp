#include "ListDir.hpp"
#include <CLI/Encoding.hpp>
#include <filesystem>
#include <algorithm>
#include <io.h>

using namespace appbox::test;

static std::vector<ProtocolListDir::Rsp::Entry> ListDirStd(const std::wstring& path)
{
    std::vector<ProtocolListDir::Rsp::Entry> entries;

    std::filesystem::directory_iterator it(path);
    for (const auto& entry : it)
    {
        ProtocolListDir::Rsp::Entry item;
        auto                        name = entry.path().filename().wstring();
        item.name = CLI::narrow(name);
        item.file = entry.is_regular_file();
        entries.push_back(item);
    }

    return entries;
}

static std::vector<ProtocolListDir::Rsp::Entry> ListDirWinAPI(const std::wstring& path)
{
    std::vector<ProtocolListDir::Rsp::Entry> entries;
    std::wstring                             searchPath = path + L"\\*";
    WIN32_FIND_DATAW                         findData;
    HANDLE                                   hFind = FindFirstFileW(searchPath.c_str(), &findData);

    if (hFind == INVALID_HANDLE_VALUE)
    {
        return entries;
    }

    do
    {
        /* Skip "." and ".." */
        if (wcscmp(findData.cFileName, L".") == 0 || wcscmp(findData.cFileName, L"..") == 0)
        {
            continue;
        }
        ProtocolListDir::Rsp::Entry item;
        item.name = CLI::narrow(findData.cFileName);
        item.file = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
        entries.push_back(item);
    } while (FindNextFileW(hFind, &findData));

    FindClose(hFind);

    return entries;
}

static std::vector<ProtocolListDir::Rsp::Entry> ListDirCRT(const std::wstring& path)
{
    std::vector<ProtocolListDir::Rsp::Entry> entries;
    std::string                              searchPath = CLI::narrow(path) + "\\*";
    _finddata_t                              fileInfo;
    intptr_t                                 handle = _findfirst(searchPath.c_str(), &fileInfo);
    if (handle == -1)
    {
        return entries;
    }

    do
    {
        if (strcmp(fileInfo.name, ".") == 0 || strcmp(fileInfo.name, "..") == 0)
            continue;

        ProtocolListDir::Rsp::Entry item;
        item.name = fileInfo.name;
        item.file = !(fileInfo.attrib & _A_SUBDIR);
        entries.push_back(item);
    } while (_findnext(handle, &fileInfo) == 0);

    _findclose(handle);

    return entries;
}

static nlohmann::json ProbeListDir_Entry(const nlohmann::json& data)
{
    auto req = data.get<ProtocolListDir::Req>();
    auto wPath = CLI::widen(req.path);
    while (wPath.back() == L'\\')
    {
        wPath.pop_back();
    }

    ProtocolListDir::Rsp rsp;
    switch (req.method)
    {
    case ProtocolListDir::Req::Method::Std:
        rsp.entries = ListDirStd(wPath);
        break;
    case ProtocolListDir::Req::Method::WinAPI:
        rsp.entries = ListDirWinAPI(wPath);
        break;
    case ProtocolListDir::Req::Method::CRT:
        rsp.entries = ListDirCRT(wPath);
        break;
    default:
        break;
    }

    std::sort(rsp.entries.begin(), rsp.entries.end(), [](const auto& a, const auto& b) { return a.name < b.name; });

    return rsp;
}

appbox::test::Probe appbox::test::ProbeListDir("ListDir", ProbeListDir_Entry);
