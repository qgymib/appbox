#include "WinAPI.h" /* Must be first include file */
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iterator>
#include <thread>
#include <vector>
#include "SandboxId.hpp"
#include "WString.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/ProbeCall.hpp"
#include "MultiInstance.hpp"

appbox::LauncherConfig appbox::test::MultiInstanceConfig(const std::filesystem::path& root,
                                                         const std::string& sandbox_id, const std::string& marker)
{
    /* clang-format off */
    auto tree = FsRoot(root, {
        FsDir(L"data", {}),
        FsDir(L"app", { FsDir(L"filesystem\\#USERPROFILE#", {}) })
    });
    /* clang-format on */

    auto config = tree.Build();
    config.sandbox_id = sandbox_id;

    appbox::LauncherStartup startup;
    startup.trigger = "app";
    startup.auto_start = true;
    startup.arguments = { "--startup_marker", marker };
    config.startups.push_back(std::move(startup));

    return config;
}

nlohmann::json appbox::test::MultiInstanceRequestDocument(const std::string& sandbox_id)
{
    nlohmann::json document = appbox::LauncherConfig{};
    document["sandbox_id"] = sandbox_id;
    return document;
}

bool appbox::test::WaitForControlPipe(const std::string& sandbox_id, int timeout_ms)
{
    const auto expected = appbox::SandboxPipeName(sandbox_id);
    if (expected.empty())
    {
        return false;
    }

    const std::wstring name = appbox::UTF8ToWide(expected);
    const auto         deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

    for (;;)
    {
        WIN32_FIND_DATAW entry;
        ZeroMemory(&entry, sizeof(entry));

        bool       found = false;
        const auto handle = FindFirstFileW(L"\\\\.\\pipe\\*", &entry);
        if (handle != INVALID_HANDLE_VALUE)
        {
            do
            {
                if (name == std::wstring(L"\\\\.\\pipe\\") + entry.cFileName)
                {
                    found = true;
                    break;
                }
            } while (FindNextFileW(handle, &entry) != FALSE);

            FindClose(handle);
        }

        if (found)
        {
            return true;
        }

        if (std::chrono::steady_clock::now() >= deadline)
        {
            return false;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

std::string appbox::test::ReadRunLog(const std::filesystem::path& root)
{
    std::string     text;
    std::error_code ec;

    for (const auto& entry : std::filesystem::directory_iterator(root / kCaseLogDirName, ec))
    {
        if (!entry.is_regular_file(ec) || entry.path().extension() != L".log")
        {
            continue;
        }

        std::ifstream stream(entry.path(), std::ios::binary);
        if (!stream.is_open())
        {
            continue;
        }

        text.append(std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()));
    }

    return text;
}

bool appbox::test::WaitForRunLog(const std::filesystem::path& root, const std::string& text, int timeout_ms)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

    for (;;)
    {
        if (ReadRunLog(root).find(text) != std::string::npos)
        {
            return true;
        }

        if (std::chrono::steady_clock::now() >= deadline)
        {
            return false;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

std::size_t appbox::test::LogFileCount(const std::filesystem::path& root)
{
    std::size_t     count = 0;
    std::error_code ec;

    for (const auto& entry : std::filesystem::directory_iterator(root / kCaseLogDirName, ec))
    {
        if (entry.is_regular_file(ec) && entry.path().extension() == L".log")
        {
            ++count;
        }
    }

    return count;
}

std::vector<std::string> appbox::test::TreeSnapshot(const std::filesystem::path& dir)
{
    std::vector<std::string> entries;
    std::error_code          ec;

    for (const auto& entry : std::filesystem::recursive_directory_iterator(dir, ec))
    {
        const auto relative = std::filesystem::relative(entry.path(), dir, ec);
        if (ec)
        {
            continue;
        }

        std::string description = appbox::WideToUTF8(relative.wstring());

        if (entry.is_regular_file(ec))
        {
            const auto size = std::filesystem::file_size(entry.path(), ec);
            description += "|" + std::to_string(ec ? 0 : size);
        }
        else
        {
            description += "|<dir>";
        }

        const auto written = std::filesystem::last_write_time(entry.path(), ec);
        if (!ec)
        {
            description += "|" + std::to_string(written.time_since_epoch().count());
        }

        entries.push_back(std::move(description));
    }

    std::sort(entries.begin(), entries.end());
    return entries;
}
