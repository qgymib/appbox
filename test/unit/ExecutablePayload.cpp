#include <gtest/gtest.h>
#include "ExecutablePayload.hpp"
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace
{

/**
 * @brief Generate a unique name fragment for temporary files.
 * @return The unique fragment.
 */
std::wstring UniqueFragment()
{
    static unsigned counter = 0;
    const auto      ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::to_wstring(ticks) + L"-" + std::to_wstring(++counter);
}

/**
 * @brief RAII helper providing a unique path below the temp directory.
 */
class TempPath
{
public:
    /**
     * @brief Reserve a unique path.
     * @param[in] extension Extension of the file.
     */
    explicit TempPath(const std::wstring& extension)
    {
        path_ = std::filesystem::temp_directory_path() / (L"appbox-payload-" + UniqueFragment() + extension);
    }

    ~TempPath()
    {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }

    /**
     * @brief Get the path.
     * @return The path.
     */
    const std::filesystem::path& Get() const
    {
        return path_;
    }

private:
    std::filesystem::path path_;
};

/**
 * @brief Write a buffer into a file.
 * @param[in] path Destination path.
 * @param[in] bytes Bytes to write.
 * @param[in] size Number of bytes.
 */
void WriteFile(const std::filesystem::path& path, const void* bytes, std::size_t size)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(file.good());
    file.write(static_cast<const char*>(bytes), static_cast<std::streamsize>(size));
    ASSERT_TRUE(file.good());
}

} // namespace

TEST(Unit_ExecutablePayload, TrailerRoundTripsThroughAProgramFile)
{
    TempPath host(L".exe");

    /* The program image is 100 bytes, the payload behind it 40 bytes. */
    const std::vector<char> image(100, 'I');
    const std::vector<char> payload(40, 'P');
    const auto              trailer =
        appbox::executable_payload::BuildTrailer(appbox::executable_payload::PayloadLocation{ 100, 40 });

    {
        std::ofstream file(host.Get(), std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(file.good());
        file.write(image.data(), static_cast<std::streamsize>(image.size()));
        file.write(payload.data(), static_cast<std::streamsize>(payload.size()));
        file.write(trailer.data(), static_cast<std::streamsize>(trailer.size()));
        ASSERT_TRUE(file.good());
    }

    appbox::executable_payload::PayloadLocation location;
    std::string                                 error;
    ASSERT_EQ(appbox::executable_payload::ReadTrailer(host.Get(), location, error),
              appbox::executable_payload::TrailerStatus::Present)
        << error;
    EXPECT_TRUE(error.empty());
    EXPECT_EQ(location.offset, static_cast<std::uint64_t>(100));
    EXPECT_EQ(location.size, static_cast<std::uint64_t>(40));
}

TEST(Unit_ExecutablePayload, AProgramWithoutATrailerCarriesNoPayload)
{
    TempPath host(L".exe");

    /* A program which is shorter than the trailer cannot carry one. */
    const std::vector<char> tiny(4, 'M');
    WriteFile(host.Get(), tiny.data(), tiny.size());

    appbox::executable_payload::PayloadLocation location;
    std::string                                 error;
    EXPECT_EQ(appbox::executable_payload::ReadTrailer(host.Get(), location, error),
              appbox::executable_payload::TrailerStatus::Absent);
    EXPECT_TRUE(error.empty());

    /* An image which ends with other bytes ends with no magic either. */
    TempPath                image(L".exe");
    const std::vector<char> longer(120, 'M');
    WriteFile(image.Get(), longer.data(), longer.size());

    EXPECT_EQ(appbox::executable_payload::ReadTrailer(image.Get(), location, error),
              appbox::executable_payload::TrailerStatus::Absent);
    EXPECT_TRUE(error.empty());
}

TEST(Unit_ExecutablePayload, AnInconsistentTrailerIsNotTrusted)
{
    TempPath host(L".exe");

    /*
     * The magic is there, but the size describes bytes which run into the
     * trailer, so the payload cannot be the region the trailer names.
     */
    auto trailer = appbox::executable_payload::BuildTrailer(appbox::executable_payload::PayloadLocation{ 4, 4 });
    std::memcpy(trailer.data() + 8, &appbox::executable_payload::kTrailerSize, sizeof(std::uint64_t));

    const std::vector<char> filler(4, 'M');
    {
        std::ofstream file(host.Get(), std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(file.good());
        file.write(filler.data(), static_cast<std::streamsize>(filler.size()));
        file.write(trailer.data(), static_cast<std::streamsize>(trailer.size()));
        ASSERT_TRUE(file.good());
    }

    appbox::executable_payload::PayloadLocation location;
    std::string                                 error;
    EXPECT_EQ(appbox::executable_payload::ReadTrailer(host.Get(), location, error),
              appbox::executable_payload::TrailerStatus::Unreadable);
    EXPECT_FALSE(error.empty());
}

TEST(Unit_ExecutablePayload, AnUnknownVersionIsRefused)
{
    TempPath host(L".exe");

    auto trailer = appbox::executable_payload::BuildTrailer(appbox::executable_payload::PayloadLocation{ 4, 4 });
    const std::uint32_t version = appbox::executable_payload::kTrailerVersion + 1;
    std::memcpy(trailer.data() + 16, &version, sizeof(version));

    const std::vector<char> image(4, 'I');
    const std::vector<char> payload(4, 'P');
    {
        std::ofstream file(host.Get(), std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(file.good());
        file.write(image.data(), static_cast<std::streamsize>(image.size()));
        file.write(payload.data(), static_cast<std::streamsize>(payload.size()));
        file.write(trailer.data(), static_cast<std::streamsize>(trailer.size()));
        ASSERT_TRUE(file.good());
    }

    appbox::executable_payload::PayloadLocation location;
    std::string                                 error;
    EXPECT_EQ(appbox::executable_payload::ReadTrailer(host.Get(), location, error),
              appbox::executable_payload::TrailerStatus::Unreadable);
    EXPECT_FALSE(error.empty());
}

TEST(Unit_ExecutablePayload, AMissingFileIsUnreadable)
{
    const auto missing = std::filesystem::temp_directory_path() / L"appbox-payload-missing.exe";
    appbox::executable_payload::PayloadLocation location;
    std::string                                 error;
    EXPECT_EQ(appbox::executable_payload::ReadTrailer(missing, location, error),
              appbox::executable_payload::TrailerStatus::Unreadable);
    EXPECT_FALSE(error.empty());
}
