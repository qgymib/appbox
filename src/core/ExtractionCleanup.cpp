#include <windows.h>
#include "ExtractionCleanup.hpp"
#include <spdlog/spdlog.h>
#include <utility>
#include "WString.hpp"

namespace
{

/**
 * @brief Describe a folder for a log line.
 * @param[in] folder Folder to describe.
 * @return The UTF-8 text of the path, a placeholder when it cannot be converted.
 */
std::string FolderText(const std::filesystem::path& folder)
{
    return appbox::WideToUTF8(folder.wstring()).value_or("<unconvertible>");
}

} // namespace

namespace appbox
{

void ExtractionCleanup::Register(unsigned long process_id, std::filesystem::path folder)
{
    if (process_id == 0 || folder.empty())
    {
        return;
    }

    std::lock_guard<std::mutex> guard(mutex_);
    folders_.emplace(process_id, std::move(folder));
}

std::filesystem::path ExtractionCleanup::Take(unsigned long process_id)
{
    std::lock_guard<std::mutex> guard(mutex_);

    const auto entry = folders_.find(process_id);
    if (entry == folders_.end())
    {
        return {};
    }

    auto folder = std::move(entry->second);
    folders_.erase(entry);
    return folder;
}

std::vector<std::filesystem::path> ExtractionCleanup::TakeExited()
{
    std::vector<std::filesystem::path> folders;

    std::lock_guard<std::mutex> guard(mutex_);
    for (auto entry = folders_.begin(); entry != folders_.end();)
    {
        if (IsProcessAlive(entry->first))
        {
            ++entry;
            continue;
        }

        folders.push_back(std::move(entry->second));
        entry = folders_.erase(entry);
    }

    return folders;
}

void ExtractionCleanup::RemoveFolder(const std::filesystem::path& folder)
{
    if (folder.empty())
    {
        return;
    }

    std::error_code ec;
    for (int attempt = 0; attempt < kRemoveAttempts; ++attempt)
    {
        std::filesystem::remove_all(folder, ec);
        if (!ec)
        {
            return;
        }

        ::Sleep(static_cast<DWORD>(kRemoveDelayMs));
    }

    SPDLOG_WARN("the folder '{}' stays behind: {}", FolderText(folder), ec.message());
}

bool ExtractionCleanup::IsProcessAlive(unsigned long process_id)
{
    if (process_id == 0)
    {
        return false;
    }

    /*
     * A process id can be reused, so the identifier is checked against the
     * process it names right now: an open handle which answers with
     * `STILL_ACTIVE` names a process which still runs, and any other exit code
     * a process which ended.
     */
    HANDLE process = ::OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id);
    if (process == nullptr)
    {
        return false;
    }

    const DWORD state = ::WaitForSingleObject(process, 0);
    ::CloseHandle(process);
    return state == WAIT_TIMEOUT;
}

} // namespace appbox
