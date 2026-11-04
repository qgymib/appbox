#include <windows.h>
#include <gtest/gtest.h>
#include "core/ExtractionCleanup.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <system_error>
#include <vector>

namespace
{

/**
 * @brief Unique name fragment for the folders of a case.
 * @return The unique fragment.
 */
std::wstring UniqueFragment()
{
    static unsigned counter = 0;
    const auto      ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::to_wstring(ticks) + L"-" + std::to_wstring(++counter);
}

/**
 * @brief RAII helper creating a unique folder below the temp directory.
 *
 * The folder is removed when the object goes away, so a case leaves nothing
 * below the temporary directory of the machine.
 */
class TempDir
{
public:
    TempDir()
    {
        path_ = std::filesystem::temp_directory_path() / (L"appbox-extraction-cleanup-" + UniqueFragment());
        std::filesystem::create_directories(path_);
    }

    ~TempDir()
    {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    /**
     * @brief Get the folder path.
     * @return The folder path.
     */
    const std::filesystem::path& Get() const
    {
        return path_;
    }

private:
    std::filesystem::path path_;
};

/**
 * @brief Create a folder with one nested file below a parent.
 * @param[in] parent Parent folder.
 * @param[in] name Name of the folder.
 * @return The path of the created folder.
 */
std::filesystem::path MakeExtraction(const std::filesystem::path& parent, const std::wstring& name)
{
    const auto folder = parent / name;
    std::filesystem::create_directories(folder / L"app" / L"deep");

    for (const auto& file : { folder / L"app.exe", folder / L"app" / L"a.txt", folder / L"app" / L"deep" / L"b.txt" })
    {
        std::ofstream stream(file, std::ios::binary | std::ios::trunc);
        stream << "content";
    }

    return folder;
}

/**
 * @brief Get the process id of a process which already ended.
 *
 * The case starts a program of the machine which exits at once and waits for
 * it, so the identifier names no running process any more.
 *
 * @return The process id of the ended process.
 */
unsigned long EndedProcessId()
{
    wchar_t command[] = L"cmd.exe /c exit 0";

    STARTUPINFOW        startup = {};
    PROCESS_INFORMATION process = {};
    startup.cb = sizeof(startup);

    if (::CreateProcessW(nullptr, command, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup,
                         &process) == FALSE)
    {
        return 0;
    }

    ::WaitForSingleObject(process.hProcess, 30000);
    ::CloseHandle(process.hThread);
    ::CloseHandle(process.hProcess);
    return process.dwProcessId;
}

/**
 * @brief Get the process id of the process which runs the case.
 * @return The process id of the case.
 */
unsigned long CurrentProcessId()
{
    return static_cast<unsigned long>(::GetCurrentProcessId());
}

} // namespace

/**
 * @brief A folder which was registered for a process is handed back once.
 */
TEST(Unit_ExtractionCleanup, RegisterHandsTheFolderBackOnce)
{
    TempDir                   temp;
    const auto                folder = MakeExtraction(temp.Get(), L"registered");
    appbox::ExtractionCleanup cleanup;

    cleanup.Register(CurrentProcessId(), folder);

    const auto taken = cleanup.Take(CurrentProcessId());
    EXPECT_EQ(taken, folder);

    /* The entry is gone, so a second request answers with nothing. */
    EXPECT_TRUE(cleanup.Take(CurrentProcessId()).empty());
}

/**
 * @brief A folder which was never registered is not handed back.
 */
TEST(Unit_ExtractionCleanup, TakeAnswersNothingForAnUnknownProcess)
{
    appbox::ExtractionCleanup cleanup;
    EXPECT_TRUE(cleanup.Take(4242).empty());
}

/**
 * @brief The folders of the processes which ended are handed back, the ones of
 *        a running process are kept.
 */
TEST(Unit_ExtractionCleanup, TakeExitedNamesOnlyTheFoldersOfEndedProcesses)
{
    TempDir                   temp;
    const auto                ended_folder = MakeExtraction(temp.Get(), L"ended");
    const auto                running_folder = MakeExtraction(temp.Get(), L"running");
    appbox::ExtractionCleanup cleanup;

    const auto ended = EndedProcessId();
    ASSERT_NE(ended, 0u) << "a process which ends cannot be started";

    cleanup.Register(ended, ended_folder);
    cleanup.Register(CurrentProcessId(), running_folder);

    const auto folders = cleanup.TakeExited();
    ASSERT_EQ(folders.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(folders.front(), ended_folder);

    /*
     * A running process keeps its entry: the folder cannot be released while
     * the program started from it is still reading it.
     */
    EXPECT_EQ(cleanup.Take(CurrentProcessId()), running_folder);
}

/**
 * @brief A folder which holds no entry is removed without a failure.
 */
TEST(Unit_ExtractionCleanup, RemoveFolderAcceptsAMissingFolder)
{
    TempDir    temp;
    const auto folder = temp.Get() / L"never-created";

    appbox::ExtractionCleanup::RemoveFolder(folder);

    EXPECT_FALSE(std::filesystem::exists(folder));
}

/**
 * @brief A folder is removed with the folders and the files below it.
 */
TEST(Unit_ExtractionCleanup, RemoveFolderDropsTheContentOfTheFolder)
{
    TempDir    temp;
    const auto folder = MakeExtraction(temp.Get(), L"released");
    ASSERT_TRUE(std::filesystem::exists(folder));

    appbox::ExtractionCleanup::RemoveFolder(folder);

    EXPECT_FALSE(std::filesystem::exists(folder));

    /* Only the folder of the case is named: the parent stays in place. */
    EXPECT_TRUE(std::filesystem::exists(temp.Get()));
}

/**
 * @brief An empty path names no folder, so it is not removed.
 */
TEST(Unit_ExtractionCleanup, RemoveFolderIgnoresAnEmptyPath)
{
    appbox::ExtractionCleanup::RemoveFolder(std::filesystem::path());
    SUCCEED();
}

/**
 * @brief A process which runs is told apart from one which ended.
 */
TEST(Unit_ExtractionCleanup, IsProcessAliveTellsARunningProcessApart)
{
    EXPECT_TRUE(appbox::ExtractionCleanup::IsProcessAlive(CurrentProcessId()));

    const auto ended = EndedProcessId();
    ASSERT_NE(ended, 0u) << "a process which ends cannot be started";
    EXPECT_FALSE(appbox::ExtractionCleanup::IsProcessAlive(ended));

    /* A process id which names no process is not alive either. */
    EXPECT_FALSE(appbox::ExtractionCleanup::IsProcessAlive(0));
}
