#include <windows.h>
#include <spdlog/spdlog.h>
#include <filesystem>
#include <string>
#include <system_error>
#include "ExecutablePayload.hpp"
#include "SelfExtraction.hpp"
#include "WString.hpp"
#include "ZipReader.hpp"
#include "GetExecutableDir.hpp"

namespace
{

/** Number of attempts of the removal of the extraction folder. */
constexpr int kRemoveAttempts = 10;

/** Delay between two attempts of the removal, in milliseconds. */
constexpr int kRemoveDelayMs = 50;

} // namespace

namespace appbox
{

bool ExtractProgramPayload(const std::wstring& program, const ExtractProgressCallback& progress,
                           std::wstring& extraction_root, std::string& error)
{
    extraction_root.clear();
    error.clear();

    executable_payload::PayloadLocation location;
    std::string                         trailer_error;
    switch (executable_payload::ReadTrailer(std::filesystem::path(program), location, trailer_error))
    {
    case executable_payload::TrailerStatus::Absent:
        /* An ordinary launcher reads its resources from the folder beside it. */
        return true;
    case executable_payload::TrailerStatus::Unreadable:
        /*
         * An ordinary launcher must still run when its own image cannot be
         * inspected, so a trailer which cannot be read is not an error.
         */
        SPDLOG_WARN("the payload trailer cannot be read: {}", trailer_error);
        return true;
    case executable_payload::TrailerStatus::Present:
        break;
    }

    std::error_code ec;
    const auto      temp = std::filesystem::temp_directory_path(ec);
    if (ec)
    {
        error = "the temporary directory of the run is unavailable: " + ec.message();
        return false;
    }

    const auto folder = temp / (L"AppBox-SFX-" + GetExecutableName() + L"-" + std::to_wstring(GetCurrentProcessId()));

    /* A folder of the same name is a left over of a run which reused the pid. */
    std::filesystem::remove_all(folder, ec);
    ec.clear();

    std::filesystem::create_directories(folder, ec);
    if (ec)
    {
        error = "failed to create '" + WideToUTF8(folder.wstring()) + "': " + ec.message();
        return false;
    }

    /* The folder is reported before the extraction, so a failure leaves no rest. */
    extraction_root = folder.wstring();

    const auto result = ExtractArchiveAt(program, location.offset, location.size, extraction_root, progress);
    if (result != ZIPREADER_SUCCESS)
    {
        error = ZipReaderErrorMessage(result);
        return false;
    }

    SPDLOG_INFO("the payload of '{}' is extracted to '{}'", WideToUTF8(program), WideToUTF8(folder.wstring()));
    return true;
}

void RemoveExtraction(const std::wstring& extraction_root)
{
    if (extraction_root.empty())
    {
        return;
    }

    std::error_code ec;
    for (int attempt = 0; attempt < kRemoveAttempts; ++attempt)
    {
        std::filesystem::remove_all(extraction_root, ec);
        if (!ec)
        {
            return;
        }

        Sleep(kRemoveDelayMs);
    }

    SPDLOG_WARN("the extraction of the payload stays behind: {}", ec.message());
}

} // namespace appbox
