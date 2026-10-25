#include <gtest/gtest.h>
#include "src/core/PeResourcePatch.hpp"
#include "src/core/PackService.hpp"
#include "ExecutablePayload.hpp"
#include "ZipReader.hpp"
#include "SandboxLayout.hpp"
#include "WString.hpp"
#include "Config.hpp"
#include "launcher/EmbeddedResourceIds.h"
#include "utils/LauncherConfigReader.hpp"
#include "utils/ProbePath.hpp"
#include <nlohmann/json.hpp>
#include <zip.h>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <ios>
#include <set>
#include <string>
#include <system_error>
#include <vector>

namespace
{

/**
 * @brief Generate a unique name fragment for temporary folders.
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
 */
class TempDir
{
public:
    TempDir()
    {
        const auto base = std::filesystem::temp_directory_path();
        path_ = base / (L"appbox-packservice-" + UniqueFragment());
        std::filesystem::create_directories(path_);
    }

    ~TempDir()
    {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

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
 * @brief Create a file with content below a parent directory.
 * @param[in] parent Parent directory.
 * @param[in] relative Relative file path.
 * @param[in] content File content.
 * @return The created file path.
 */
std::filesystem::path MakeFile(const std::filesystem::path& parent, const std::wstring& relative,
                               const std::string& content)
{
    const auto file = parent / relative;
    std::filesystem::create_directories(file.parent_path());
    FILE* handle = nullptr;
    if (_wfopen_s(&handle, file.wstring().c_str(), L"wb") != 0 || handle == nullptr)
    {
        return file;
    }
    fwrite(content.data(), 1, content.size(), handle);
    fclose(handle);
    return file;
}

/**
 * @brief Create a folder below a parent directory.
 * @param[in] parent Parent directory.
 * @param[in] name Folder name.
 * @return The created folder path.
 */
std::filesystem::path MakeFolder(const std::filesystem::path& parent, const std::wstring& name)
{
    const auto folder = parent / name;
    std::filesystem::create_directories(folder);
    return folder;
}

/** Number of attempts of a verification read of a freshly written image. */
constexpr int kVersionReadAttempts = 40;

/** Wait between two attempts of a verification read, in milliseconds. */
constexpr DWORD kVersionReadDelayMs = 50;

/**
 * @brief Read the version information of an image, tolerating a busy image.
 *
 * The version API opens the image again, and an image which was written moments
 * ago can be unavailable to that open while a file system filter - the on
 * access scanner of an antivirus product for example - still inspects it: the
 * API then reports the file as missing instead of answering with the
 * information of the image. A pack run tolerates the very same window for the
 * images it patches (see `kUpdateAttempts` of `src/core/PeResourcePatch.hpp`),
 * so a case which verifies an image it just wrote repeats the read instead of
 * failing at once.
 *
 * The repetition is bounded and the reason of the last attempt is reported, so
 * a genuine failure - an image which carries no version resource at all, for
 * example - still fails the caller.
 *
 * @param[in] path Host path of the image.
 * @param[out] info The version information which was read.
 * @param[out] error Reason of the last attempt on failure, empty on success.
 * @return true when the information was read.
 */
bool ReadVersionInfoEventually(const std::wstring& path, appbox::ApplicationVersionInfo& info, std::string& error)
{
    for (int attempt = 0; attempt < kVersionReadAttempts; ++attempt)
    {
        if (attempt > 0)
        {
            Sleep(kVersionReadDelayMs);
        }

        if (appbox::ReadApplicationMetadata(path, info, error))
        {
            error.clear();
            return true;
        }
    }

    return false;
}

/**
 * @brief Read a whole file.
 * @param[in] path File path.
 * @return The file content, empty on failure.
 */
std::vector<char> ReadAllBytes(const std::wstring& path)
{
    std::ifstream file(std::filesystem::path(path), std::ios::binary | std::ios::ate);
    if (!file)
    {
        return {};
    }

    const auto size = file.tellg();
    if (size < 0)
    {
        return {};
    }

    std::vector<char> bytes(static_cast<std::size_t>(size));
    file.seekg(0, std::ios::beg);
    if (!bytes.empty())
    {
        file.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }

    return file ? bytes : std::vector<char>();
}

/**
 * @brief Read a whole file as text.
 * @param[in] path File path.
 * @return The content, empty when the file cannot be read.
 */
std::string ReadTextFile(const std::filesystem::path& path)
{
    const auto bytes = ReadAllBytes(path.wstring());
    return std::string(bytes.begin(), bytes.end());
}

/**
 * @brief Write a whole file.
 * @param[in] path File path.
 * @param[in] bytes File content.
 * @return true on success.
 */
bool WriteAllBytes(const std::wstring& path, const std::vector<char>& bytes)
{
    std::ofstream file(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
    if (!file)
    {
        return false;
    }

    if (!bytes.empty())
    {
        file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }

    file.close();
    return file ? true : false;
}

/**
 * @brief Read the content of one zip entry.
 * @param[in] archive Open zip archive.
 * @param[in] name Entry name.
 * @return The entry content, empty when the entry cannot be read.
 */
std::string ReadEntry(zip_t* archive, const char* name)
{
    const auto index = zip_name_locate(archive, name, 0);
    if (index < 0)
    {
        return {};
    }

    zip_stat_t stat = {};
    if (zip_stat_index(archive, static_cast<zip_uint64_t>(index), 0, &stat) < 0)
    {
        return {};
    }

    std::string content(stat.size, '\0');
    zip_file_t* file = zip_fopen_index(archive, static_cast<zip_uint64_t>(index), 0);
    if (file == nullptr)
    {
        return {};
    }
    const auto read = zip_fread(file, content.data(), stat.size);
    zip_fclose(file);
    if (read != static_cast<zip_int64_t>(stat.size))
    {
        return {};
    }
    return content;
}

/**
 * @brief Read a resource embedded in a launcher entry of an archive.
 *
 * @param[in] launcher The launcher entry.
 * @param[in] resource_id Identifier of the resource in that launcher.
 * @param[out] bytes The payload of the resource, empty on failure.
 * @param[out] error Description of the failure, empty on success.
 * @return true when the resource was read.
 */
bool ReadLauncherResourceFromEntry(const std::string& launcher, int resource_id, std::string& bytes, std::string& error)
{
    bytes.clear();
    error.clear();

    const auto                               path = appbox::pe_resource::TemporaryPath(L"AppBox-Test-PackService");
    const appbox::pe_resource::TemporaryFile file(path);
    if (!appbox::pe_resource::WriteFileBytes(path, launcher.data(), launcher.size(), error))
    {
        return false;
    }

    return appbox::test::ReadLauncherResource(path.wstring(), resource_id, bytes, error);
}

/**
 * @brief Read the raw JSON document of the configuration embedded in a launcher
 *        entry of an archive.
 *
 * @param[in] launcher The launcher entry.
 * @param[out] json The UTF-8 JSON document, empty on failure.
 * @param[out] error Description of the failure, empty on success.
 * @return true when the document was read.
 */
bool ReadLauncherConfigJsonFromEntry(const std::string& launcher, std::string& json, std::string& error)
{
    return ReadLauncherResourceFromEntry(launcher, IDR_APPBOX_LAUNCHER_CONFIG, json, error);
}

/**
 * @brief Open a zip archive for reading.
 * @param[in] path Archive path.
 * @return The archive, nullptr on failure.
 */
zip_t* OpenArchive(const std::wstring& path)
{
    int error_code = 0;
    return zip_open(appbox::WideToUTF8(path).c_str(), ZIP_RDONLY, &error_code);
}

/**
 * @brief RAII helper closing a zip archive.
 */
class ZipArchiveCloser
{
public:
    /**
     * @brief Remember the archive to close.
     * @param[in] archive Archive handle, may be nullptr.
     */
    explicit ZipArchiveCloser(zip_t* handle) : archive(handle)
    {
    }

    ~ZipArchiveCloser()
    {
        if (archive != nullptr)
        {
            zip_close(archive);
        }
    }

    /**
     * @brief The wrapped archive handle.
     */
    zip_t* archive = nullptr;
};

/**
 * @brief Collect the entry names of an archive.
 * @param[in] archive Open zip archive.
 * @return The entry names of the archive.
 */
std::set<std::string> EntryNames(zip_t* archive)
{
    std::set<std::string> names;

    const auto count = zip_get_num_entries(archive, 0);
    for (auto index = static_cast<zip_uint64_t>(0); index < static_cast<zip_uint64_t>(count); ++index)
    {
        const char* name = zip_get_name(archive, index, 0);
        if (name != nullptr)
        {
            names.insert(name);
        }
    }

    return names;
}

/**
 * @brief Read the stat of one entry of an archive.
 * @param[in] archive Open zip archive.
 * @param[in] name Entry name.
 * @param[out] stat Stat of the entry.
 * @return true when the entry exists and its stat could be read.
 */
bool EntryStat(zip_t* archive, const char* name, zip_stat_t& stat)
{
    const auto index = zip_name_locate(archive, name, 0);
    if (index < 0)
    {
        return false;
    }

    zip_stat_init(&stat);
    return zip_stat_index(archive, static_cast<zip_uint64_t>(index), 0, &stat) == 0;
}

/**
 * @brief Generate content deflate shrinks a lot.
 *
 * The content is repetitive, so the method a pack run picks for its entry is
 * visible in the archive: a stored entry keeps the size of the content while a
 * deflated one is a good deal smaller.
 *
 * @param[in] size Size of the content in bytes.
 * @return The content.
 */
std::string CompressibleContent(std::size_t size)
{
    std::string content;
    content.reserve(size);
    for (std::size_t index = 0; index < size; ++index)
    {
        content.push_back(static_cast<char>('A' + (index % 8)));
    }
    return content;
}

/**
 * @brief Build the payloads of a standalone pack run.
 *
 * The pack run embeds the run configuration as a resource of the launcher
 * payload, so the payload has to be a real program image. The probe of the
 * suite stands in for the launcher: it is a small PE image, unlike the test
 * executable, which keeps the file system of a case busy for seconds. The bytes
 * live in the static storage of the test executable, so the range of the
 * returned structure stays valid for the whole run.
 *
 * The two sandbox injection modules are not part of the structure: the launcher
 * carries them as resources of its own executable.
 *
 * @return The payloads of a standalone pack run.
 */
appbox::PackPayloads FakePayloads()
{
    static const std::vector<char> launcher = [] {
        const auto path = appbox::test::ProbePath(appbox::test::ProbeBitness::Bit64);
        return path.empty() ? std::vector<char>() : ReadAllBytes(path);
    }();

    appbox::PackPayloads payloads;
    payloads.launcher_bytes = launcher.data();
    payloads.launcher_size = launcher.size();
    return payloads;
}

} // namespace

TEST(Unit_PackService, CountFilesBelowCountsRecursively)
{
    TempDir temp;
    MakeFile(temp.Get(), L"a.txt", "A");
    MakeFile(temp.Get(), L"sub\\b.txt", "B");
    MakeFile(temp.Get(), L"sub\\deep\\c.txt", "C");
    std::filesystem::create_directories(temp.Get() / L"sub" / L"empty");

    EXPECT_EQ(appbox::CountFilesBelow(temp.Get().wstring()), static_cast<std::size_t>(3));
}

TEST(Unit_PackService, PackRequiresAStartupFile)
{
    TempDir               temp;
    appbox::PackModel     model;
    appbox::RegistryModel registry;

    const auto result =
        appbox::Pack(model, registry, appbox::FilesystemIsolationModel(), appbox::NetworkModel(),
                     appbox::EnvironmentModel(), appbox::ApplicationMetadata(), appbox::StartupScreen{}, FakePayloads(),
                     appbox::CompressionLevel::Normal, (temp.Get() / L"out.zip").wstring(), nullptr);
    EXPECT_NE(result.find("startup file"), std::string::npos);
}

TEST(Unit_PackService, PackRequiresAnAutoStartStartupFile)
{
    TempDir temp;
    MakeFile(temp.Get(), L"app.exe", "EXE");

    appbox::PackModel     model;
    appbox::RegistryModel registry;
    std::string           error;
    ASSERT_TRUE(model.ImportFolder("program_files", temp.Get().wstring(), error)) << error;
    ASSERT_TRUE(model.AddStartupFile("program_files", temp.Get().filename().wstring(), L"app.exe", false, error))
        << error;

    const auto result =
        appbox::Pack(model, registry, appbox::FilesystemIsolationModel(), appbox::NetworkModel(),
                     appbox::EnvironmentModel(), appbox::ApplicationMetadata(), appbox::StartupScreen{}, FakePayloads(),
                     appbox::CompressionLevel::Normal, (temp.Get() / L"out.zip").wstring(), nullptr);
    EXPECT_NE(result.find("start automatically"), std::string::npos);
}

TEST(Unit_PackService, PackRequiresLauncherBytes)
{
    TempDir temp;
    MakeFile(temp.Get(), L"app.exe", "EXE");

    appbox::PackModel     model;
    appbox::RegistryModel registry;
    std::string           error;
    ASSERT_TRUE(model.ImportFolder("program_files", temp.Get().wstring(), error)) << error;
    ASSERT_TRUE(model.AddStartupFile("program_files", temp.Get().filename().wstring(), L"app.exe", true, error))
        << error;

    const auto result = appbox::Pack(model, registry, appbox::FilesystemIsolationModel(), appbox::NetworkModel(),
                                     appbox::EnvironmentModel(), appbox::ApplicationMetadata(), appbox::StartupScreen{},
                                     appbox::PackPayloads{}, appbox::CompressionLevel::Normal,
                                     (temp.Get() / L"out.zip").wstring(), nullptr);
    EXPECT_NE(result.find("launcher payload"), std::string::npos);
}

TEST(Unit_PackService, PackProducesLauncherConfigurationAndLayers)
{
    TempDir    program_files;
    TempDir    user_profile;
    const auto my_app = program_files.Get() / L"MyApp";
    MakeFile(my_app, L"app.exe", "EXE-CONTENT");
    MakeFile(my_app, L"data\\config.txt", "CFG-CONTENT");
    std::filesystem::create_directories(my_app / L"emptydir");
    MakeFile(user_profile.Get(), L"MyUser\\settings.ini", "INI-CONTENT");

    appbox::PackModel     model;
    appbox::RegistryModel registry;
    std::string           error;
    ASSERT_TRUE(model.ImportFolder("program_files", my_app.wstring(), error)) << error;
    ASSERT_TRUE(model.ImportFolder("user_profile", (user_profile.Get() / L"MyUser").wstring(), error)) << error;
    ASSERT_TRUE(model.AddStartupFile("program_files", L"MyApp", L"app.exe", true, error)) << error;

    const auto zip_path = program_files.Get().parent_path() / (program_files.Get().filename().wstring() + L"-pack.zip");
    const auto result = appbox::Pack(model, registry, appbox::FilesystemIsolationModel(), appbox::NetworkModel(),
                                     appbox::EnvironmentModel(), appbox::ApplicationMetadata(), appbox::StartupScreen{},
                                     FakePayloads(), appbox::CompressionLevel::Normal, zip_path.wstring(), nullptr);
    EXPECT_EQ(result, "") << result;

    ZipArchiveCloser closer(OpenArchive(zip_path.wstring()));
    zip_t*           archive = closer.archive;
    ASSERT_NE(archive, nullptr);

    /* The launcher payload is embedded under the name of the main program. */
    EXPECT_EQ(appbox::LauncherEntryName(model), L"app.exe");
    const auto launcher_entry = ReadEntry(archive, "app.exe");
    ASSERT_FALSE(launcher_entry.empty());
    EXPECT_EQ(launcher_entry.substr(0, 2), "MZ");

    /* The archive carries a single naming, the launcher name is gone. */
    EXPECT_EQ(zip_name_locate(archive, "AppBoxLauncher.exe", 0), -1);
    EXPECT_EQ(zip_name_locate(archive, "AppBoxLauncher.json", 0), -1);

    /*
     * The sandbox injection modules are resources of the launcher payload and
     * not entries of the archive: the launcher releases them into the cache
     * directory of the sandbox at every start.
     */
    const auto sandbox32_entry = std::string(appbox::layout::kAppDirName) + "/" + appbox::layout::kSandbox32DllName;
    const auto sandbox64_entry = std::string(appbox::layout::kAppDirName) + "/" + appbox::layout::kSandbox64DllName;
    EXPECT_EQ(zip_name_locate(archive, sandbox32_entry.c_str(), 0), -1);
    EXPECT_EQ(zip_name_locate(archive, sandbox64_entry.c_str(), 0), -1);

    /*
     * The configuration carries the startup files only: the layout of the archive
     * is the fixed convention of `common/SandboxLayout.hpp`, so the file names
     * neither the resources nor the state of the sandbox.
     */
    std::string json_text;
    std::string config_error;
    ASSERT_TRUE(ReadLauncherConfigJsonFromEntry(launcher_entry, json_text, config_error)) << config_error;
    const auto document = nlohmann::json::parse(json_text);
    EXPECT_FALSE(document.contains("base_fs"));
    EXPECT_FALSE(document.contains("overlay_fs"));
    const auto config = document.get<appbox::LauncherConfig>();
    ASSERT_EQ(config.startups.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(config.startups[0].trigger, "app");
    EXPECT_TRUE(config.startups[0].auto_start);
    EXPECT_EQ(config.startups[0].executable, "#ProgramFiles#\\MyApp\\app.exe");
    EXPECT_TRUE(config.startups[0].arguments.empty());

    /* The startup screen of the session is shown and carries no image at all. */
    EXPECT_TRUE(config.startup.screen);

    /* Imported folders become lower layers below app/filesystem/<layer key>. */
    EXPECT_EQ(ReadEntry(archive, "app/filesystem/#ProgramFiles#/MyApp/app.exe"), "EXE-CONTENT");
    EXPECT_EQ(ReadEntry(archive, "app/filesystem/#ProgramFiles#/MyApp/data/config.txt"), "CFG-CONTENT");
    EXPECT_EQ(ReadEntry(archive, "app/filesystem/#USERPROFILE#/MyUser/settings.ini"), "INI-CONTENT");

    /* Empty folders survive as directory entries (with trailing slash). */
    const auto empty_index = zip_name_locate(archive, "app/filesystem/#ProgramFiles#/MyApp/emptydir/", 0);
    EXPECT_GE(empty_index, 0);

    /* The archive carries the resource directories of the fixed layout. */
    const auto names = EntryNames(archive);
    EXPECT_EQ(names.count("app/"), static_cast<std::size_t>(1));
    EXPECT_EQ(names.count("app/filesystem/"), static_cast<std::size_t>(1));
    EXPECT_EQ(names.count("app/registry/"), static_cast<std::size_t>(1));
    EXPECT_EQ(names.count("app/network/"), static_cast<std::size_t>(1));

    /*
     * The state of the sandbox never travels in the archive: the launcher creates
     * it at run time, so deleting it resets the sandbox to the packed state.
     */
    for (const auto& name : names)
    {
        EXPECT_EQ(name.rfind("data/", 0), std::string::npos) << name;
    }
}

TEST(Unit_PackService, SelfExtractingProgramCarriesAnAppendedPayload)
{
    TempDir    program_files;
    const auto my_app = program_files.Get() / L"MyApp";
    MakeFile(my_app, L"app.exe", "EXE-CONTENT");
    MakeFile(my_app, L"data\\config.txt", "CFG-CONTENT");

    appbox::PackModel     model;
    appbox::RegistryModel registry;
    std::string           error;
    ASSERT_TRUE(model.ImportFolder("program_files", my_app.wstring(), error)) << error;
    ASSERT_TRUE(model.AddStartupFile("program_files", L"MyApp", L"app.exe", true, error)) << error;

    const auto exe_path =
        program_files.Get().parent_path() / (program_files.Get().filename().wstring() + L"-MyApp.exe");

    std::vector<appbox::BuildProgress> reports;
    const auto                         report = [&reports](const appbox::BuildProgress& progress) {
        reports.push_back(progress);
        return true;
    };

    const auto result =
        appbox::PackSelfExtracting(model, registry, appbox::FilesystemIsolationModel(), appbox::NetworkModel(),
                                   appbox::EnvironmentModel(), appbox::ApplicationMetadata(), appbox::StartupScreen{},
                                   FakePayloads(), appbox::CompressionLevel::Normal, exe_path.wstring(), report);
    EXPECT_EQ(result, "") << result;

    /* The program starts with the image of the launcher. */
    const auto image = ReadAllBytes(exe_path.wstring());
    ASSERT_GE(image.size(), static_cast<std::size_t>(2));
    EXPECT_EQ(std::string(image.data(), 2), "MZ");

    appbox::executable_payload::PayloadLocation location;
    std::string                                 trailer_error;
    ASSERT_EQ(appbox::executable_payload::ReadTrailer(exe_path, location, trailer_error),
              appbox::executable_payload::TrailerStatus::Present)
        << trailer_error;

    const auto file_size = std::filesystem::file_size(exe_path);
    EXPECT_EQ(location.offset + location.size + appbox::executable_payload::kTrailerSize, file_size);

    /* The launcher image itself carries no trailer: it is the program, not a payload. */
    TempDir                 plain;
    const auto              image_path = plain.Get() / L"launcher.exe";
    const std::vector<char> image_bytes(image.begin(), image.begin() + static_cast<std::ptrdiff_t>(location.offset));
    ASSERT_TRUE(WriteAllBytes(image_path.wstring(), image_bytes));
    appbox::executable_payload::PayloadLocation image_location;
    EXPECT_EQ(appbox::executable_payload::ReadTrailer(image_path, image_location, trailer_error),
              appbox::executable_payload::TrailerStatus::Absent);

    /* The bytes in front of the payload are the decorated launcher image. */
    std::string json_text;
    std::string config_error;
    ASSERT_TRUE(
        ReadLauncherConfigJsonFromEntry(std::string(image_bytes.begin(), image_bytes.end()), json_text, config_error))
        << config_error;
    const auto config = nlohmann::json::parse(json_text).get<appbox::LauncherConfig>();
    ASSERT_EQ(config.startups.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(config.startups[0].executable, "#ProgramFiles#\\MyApp\\app.exe");

    /*
     * The payload holds the resources below `app` and nothing else: the
     * launcher is the program itself, so neither a launcher entry nor the
     * state directory travels.
     */
    const auto dest = plain.Get() / L"extracted";
    ASSERT_EQ(appbox::ExtractArchiveAt(exe_path.wstring(), location.offset, location.size, dest.wstring()),
              appbox::ZIPREADER_SUCCESS);

    const auto layer_root = dest / L"app" / L"filesystem" / L"#ProgramFiles#" / L"MyApp";
    EXPECT_EQ(ReadTextFile(layer_root / L"app.exe"), "EXE-CONTENT");
    EXPECT_EQ(ReadTextFile(layer_root / L"data" / L"config.txt"), "CFG-CONTENT");

    const auto resource = [&dest](const wchar_t* domain, const wchar_t* name) {
        return std::filesystem::is_regular_file(dest / L"app" / domain / name);
    };
    EXPECT_TRUE(resource(L"registry", L"user.hiv"));
    EXPECT_TRUE(resource(L"registry", L"isolation.json"));
    EXPECT_TRUE(resource(L"filesystem", L"isolation.json"));
    EXPECT_TRUE(resource(L"network", L"isolation.json"));
    EXPECT_TRUE(resource(L"environment", L"isolation.json"));

    EXPECT_FALSE(std::filesystem::exists(dest / appbox::layout::kStateDirNameW));
    EXPECT_FALSE(std::filesystem::exists(dest / L"app.exe"));

    /* The run counts the files of the payload only, without the program itself. */
    const auto expected = appbox::kNonContentExecutableEntries + appbox::ContentFileCount(model);
    ASSERT_FALSE(reports.empty());
    for (const auto& entry : reports)
    {
        EXPECT_EQ(entry.total, expected);
    }
    EXPECT_EQ(reports.front().stage, appbox::BuildStage::Preparing);
    EXPECT_EQ(reports.back().stage, appbox::BuildStage::Packing);
    EXPECT_EQ(reports.back().done, expected);
}

TEST(Unit_PackService, SelfExtractingProgramRequiresAnAutoStartStartupFile)
{
    TempDir temp;
    MakeFile(temp.Get(), L"app.exe", "EXE");

    appbox::PackModel     model;
    appbox::RegistryModel registry;
    std::string           error;
    ASSERT_TRUE(model.ImportFolder("program_files", temp.Get().wstring(), error)) << error;
    ASSERT_TRUE(model.AddStartupFile("program_files", temp.Get().filename().wstring(), L"app.exe", false, error))
        << error;

    const auto result = appbox::PackSelfExtracting(
        model, registry, appbox::FilesystemIsolationModel(), appbox::NetworkModel(), appbox::EnvironmentModel(),
        appbox::ApplicationMetadata(), appbox::StartupScreen{}, FakePayloads(), appbox::CompressionLevel::Normal,
        (temp.Get() / L"out.exe").wstring(), nullptr);
    EXPECT_NE(result.find("start automatically"), std::string::npos);
}

TEST(Unit_PackService, PackEmbedsTheStartupImageInTheLauncher)
{
    TempDir    temp;
    const auto my_app = temp.Get() / L"MyApp";
    MakeFile(my_app, L"app.exe", "EXE");
    const auto logo = MakeFile(temp.Get(), L"logo.PNG", "PNG-CONTENT");

    appbox::PackModel     model;
    appbox::RegistryModel registry;
    std::string           error;
    ASSERT_TRUE(model.ImportFolder("program_files", my_app.wstring(), error)) << error;
    ASSERT_TRUE(model.AddStartupFile("program_files", L"MyApp", L"app.exe", true, error)) << error;

    appbox::StartupScreen startup;
    startup.image = logo.wstring();

    std::vector<appbox::BuildProgress> reports;
    const auto                         zip_path = temp.Get() / L"startup.zip";
    const auto result = appbox::Pack(model, registry, appbox::FilesystemIsolationModel(), appbox::NetworkModel(),
                                     appbox::EnvironmentModel(), appbox::ApplicationMetadata(), startup, FakePayloads(),
                                     appbox::CompressionLevel::Normal, zip_path.wstring(),
                                     [&reports](const appbox::BuildProgress& report) {
                                         reports.push_back(report);
                                         return true;
                                     });
    EXPECT_EQ(result, "") << result;

    ZipArchiveCloser closer(OpenArchive(zip_path.wstring()));
    zip_t*           archive = closer.archive;
    ASSERT_NE(archive, nullptr);

    /*
     * The image is a resource of the launcher payload, so the archive carries
     * no entry of it at all.
     */
    EXPECT_EQ(zip_name_locate(archive, "app/startup/", 0), -1);
    EXPECT_EQ(zip_name_locate(archive, "app/startup/splash.png", 0), -1);

    /*
     * The bytes of the image travel in the launcher entry, whatever format the
     * user selected.
     */
    const auto launcher_entry = ReadEntry(archive, "app.exe");
    ASSERT_FALSE(launcher_entry.empty());
    std::string image_bytes;
    std::string image_error;
    ASSERT_TRUE(ReadLauncherResourceFromEntry(launcher_entry, IDR_APPBOX_STARTUP_IMAGE, image_bytes, image_error))
        << image_error;
    EXPECT_EQ(image_bytes, "PNG-CONTENT");

    /* The configuration carries the flag of the session and no image name. */
    std::string json_text;
    std::string config_error;
    ASSERT_TRUE(ReadLauncherConfigJsonFromEntry(launcher_entry, json_text, config_error)) << config_error;
    const auto document = nlohmann::json::parse(json_text);
    const auto config = document.get<appbox::LauncherConfig>();
    EXPECT_TRUE(config.startup.screen);
    ASSERT_TRUE(document.contains("startup"));
    EXPECT_FALSE(document["startup"].contains("image"));

    /* The image is no entry, so it adds nothing to the progress total. */
    ASSERT_FALSE(reports.empty());
    EXPECT_EQ(reports.back().total, appbox::kNonContentArchiveEntries + appbox::ContentFileCount(model));
    EXPECT_EQ(reports.back().done, reports.back().total);
}

TEST(Unit_PackService, PackWithoutAStartupImageWritesNoResource)
{
    TempDir    temp;
    const auto my_app = temp.Get() / L"MyApp";
    MakeFile(my_app, L"app.exe", "EXE");

    appbox::PackModel     model;
    appbox::RegistryModel registry;
    std::string           error;
    ASSERT_TRUE(model.ImportFolder("program_files", my_app.wstring(), error)) << error;
    ASSERT_TRUE(model.AddStartupFile("program_files", L"MyApp", L"app.exe", true, error)) << error;

    appbox::StartupScreen startup;
    startup.enabled = false;

    const auto zip_path = temp.Get() / L"startup-off.zip";
    const auto result = appbox::Pack(model, registry, appbox::FilesystemIsolationModel(), appbox::NetworkModel(),
                                     appbox::EnvironmentModel(), appbox::ApplicationMetadata(), startup, FakePayloads(),
                                     appbox::CompressionLevel::Normal, zip_path.wstring(), nullptr);
    EXPECT_EQ(result, "") << result;

    ZipArchiveCloser closer(OpenArchive(zip_path.wstring()));
    zip_t*           archive = closer.archive;
    ASSERT_NE(archive, nullptr);

    EXPECT_EQ(zip_name_locate(archive, "app/startup/", 0), -1);

    /*
     * The flag of the session travels all the same, and a session without an
     * image leaves the launcher without the image resource.
     */
    const auto launcher_entry = ReadEntry(archive, "app.exe");
    ASSERT_FALSE(launcher_entry.empty());

    std::string image_bytes;
    std::string image_error;
    EXPECT_FALSE(ReadLauncherResourceFromEntry(launcher_entry, IDR_APPBOX_STARTUP_IMAGE, image_bytes, image_error));
    EXPECT_FALSE(image_error.empty());

    std::string json_text;
    std::string config_error;
    ASSERT_TRUE(ReadLauncherConfigJsonFromEntry(launcher_entry, json_text, config_error)) << config_error;
    const auto config = nlohmann::json::parse(json_text).get<appbox::LauncherConfig>();
    EXPECT_FALSE(config.startup.screen);
}

TEST(Unit_PackService, PackRejectsAStartupImageWhichIsNotThere)
{
    TempDir    temp;
    const auto my_app = temp.Get() / L"MyApp";
    MakeFile(my_app, L"app.exe", "EXE");

    appbox::PackModel     model;
    appbox::RegistryModel registry;
    std::string           error;
    ASSERT_TRUE(model.ImportFolder("program_files", my_app.wstring(), error)) << error;
    ASSERT_TRUE(model.AddStartupFile("program_files", L"MyApp", L"app.exe", true, error)) << error;

    appbox::StartupScreen startup;
    startup.image = (temp.Get() / L"missing.png").wstring();

    const auto zip_path = temp.Get() / L"startup-missing.zip";
    const auto result = appbox::Pack(model, registry, appbox::FilesystemIsolationModel(), appbox::NetworkModel(),
                                     appbox::EnvironmentModel(), appbox::ApplicationMetadata(), startup, FakePayloads(),
                                     appbox::CompressionLevel::Normal, zip_path.wstring(), nullptr);

    EXPECT_NE(result.find("was not found"), std::string::npos) << result;

    /* The run is refused before the first entry is written. */
    EXPECT_FALSE(std::filesystem::exists(zip_path));
}

TEST(Unit_PackService, PackRejectsAStartupImageOfAnUnsupportedFormat)
{
    TempDir    temp;
    const auto my_app = temp.Get() / L"MyApp";
    MakeFile(my_app, L"app.exe", "EXE");
    const auto logo = MakeFile(temp.Get(), L"logo.gif", "GIF-CONTENT");

    appbox::PackModel     model;
    appbox::RegistryModel registry;
    std::string           error;
    ASSERT_TRUE(model.ImportFolder("program_files", my_app.wstring(), error)) << error;
    ASSERT_TRUE(model.AddStartupFile("program_files", L"MyApp", L"app.exe", true, error)) << error;

    appbox::StartupScreen startup;
    startup.image = logo.wstring();

    const auto zip_path = temp.Get() / L"startup-format.zip";
    const auto result = appbox::Pack(model, registry, appbox::FilesystemIsolationModel(), appbox::NetworkModel(),
                                     appbox::EnvironmentModel(), appbox::ApplicationMetadata(), startup, FakePayloads(),
                                     appbox::CompressionLevel::Normal, zip_path.wstring(), nullptr);

    EXPECT_NE(result.find("unsupported format"), std::string::npos) << result;
    EXPECT_FALSE(std::filesystem::exists(zip_path));
}

TEST(Unit_PackService, PackWritesRegistryArtifacts)
{
    TempDir    temp;
    const auto my_app = temp.Get() / L"MyApp";
    MakeFile(my_app, L"app.exe", "EXE");

    appbox::PackModel     model;
    appbox::RegistryModel registry;
    std::string           error;
    ASSERT_TRUE(model.ImportFolder("program_files", my_app.wstring(), error)) << error;
    ASSERT_TRUE(model.AddStartupFile("program_files", L"MyApp", L"app.exe", true, error)) << error;

    ASSERT_TRUE(registry.EnsureKey(L"HKEY_CURRENT_USER\\Software\\AppBox", error)) << error;
    ASSERT_TRUE(registry.SetValue(L"HKEY_CURRENT_USER\\Software\\AppBox", L"Mode", appbox::RegistryValueType::String,
                                  appbox::RegistryStringData(L"sandbox"), error))
        << error;
    ASSERT_TRUE(registry.SetKeyIsolation(L"HKEY_CURRENT_USER\\Software\\AppBox", appbox::RegistryKeyIsolation::Full));

    const auto zip_path = temp.Get().parent_path() / (temp.Get().filename().wstring() + L"-registry.zip");
    const auto result = appbox::Pack(model, registry, appbox::FilesystemIsolationModel(), appbox::NetworkModel(),
                                     appbox::EnvironmentModel(), appbox::ApplicationMetadata(), appbox::StartupScreen{},
                                     FakePayloads(), appbox::CompressionLevel::Normal, zip_path.wstring(), nullptr);
    EXPECT_EQ(result, "") << result;

    ZipArchiveCloser closer(OpenArchive(zip_path.wstring()));
    zip_t*           archive = closer.archive;
    ASSERT_NE(archive, nullptr);

    /* The hive travels in the registry domain of the resources. */
    const auto hive = ReadEntry(archive, "app/registry/user.hiv");
    ASSERT_GE(hive.size(), 4u);
    EXPECT_EQ(hive.substr(0, 4), "regf");

    /* The isolation file carries the mode of every key of the workspace. */
    const auto text = ReadEntry(archive, "app/registry/isolation.json");
    ASSERT_FALSE(text.empty());
    const auto document = nlohmann::json::parse(text);
    EXPECT_EQ(document["version"].get<int>(), 1);

    const auto& keys = document["keys"];
    ASSERT_EQ(keys.size(), 7u);
    EXPECT_EQ(keys[3]["path"].get<std::string>(), "HKEY_CURRENT_USER\\Software\\AppBox");
    EXPECT_EQ(keys[3]["isolation"].get<std::string>(), "full");

    /* A value starts with the mode its key gives it, which is `Full`. */
    const auto& values = document["values"];
    ASSERT_EQ(values.size(), 1u);
    EXPECT_EQ(values[0]["path"].get<std::string>(), "HKEY_CURRENT_USER\\Software\\AppBox");
    EXPECT_EQ(values[0]["name"].get<std::string>(), "Mode");
    EXPECT_EQ(values[0]["isolation"].get<std::string>(), "full");
}

TEST(Unit_PackService, PackWritesFilesystemIsolationFile)
{
    TempDir    temp;
    const auto my_app = temp.Get() / L"MyApp";
    MakeFile(my_app, L"app.exe", "EXE");

    appbox::PackModel                model;
    appbox::RegistryModel            registry;
    appbox::FilesystemIsolationModel isolation;
    std::string                      error;
    ASSERT_TRUE(model.ImportFolder("program_files", my_app.wstring(), error)) << error;
    ASSERT_TRUE(model.AddStartupFile("program_files", L"MyApp", L"app.exe", true, error)) << error;

    ASSERT_TRUE(isolation.SetIsolation(L"#ProgramFiles#\\MyApp", appbox::FilesystemEntryKind::Directory,
                                       appbox::FilesystemIsolation::Full, error))
        << error;
    ASSERT_TRUE(isolation.SetIsolation(L"#ProgramFiles#\\MyApp\\app.exe", appbox::FilesystemEntryKind::File,
                                       appbox::FilesystemIsolation::Whiteout, error))
        << error;
    /* The root of the view is the entry without a path. */
    ASSERT_TRUE(
        isolation.SetIsolation(L"", appbox::FilesystemEntryKind::Directory, appbox::FilesystemIsolation::Merge, error))
        << error;

    const auto zip_path = temp.Get().parent_path() / (temp.Get().filename().wstring() + L"-fs-isolation.zip");
    const auto result = appbox::Pack(model, registry, isolation, appbox::NetworkModel(), appbox::EnvironmentModel(),
                                     appbox::ApplicationMetadata(), appbox::StartupScreen{}, FakePayloads(),
                                     appbox::CompressionLevel::Normal, zip_path.wstring(), nullptr);
    EXPECT_EQ(result, "") << result;

    ZipArchiveCloser closer(OpenArchive(zip_path.wstring()));
    zip_t*           archive = closer.archive;
    ASSERT_NE(archive, nullptr);

    /*
     * The modes travel in the filesystem domain of the resources, next to the
     * layers they describe: the launcher skips the file while it enumerates the
     * layers of that folder.
     */
    const auto text = ReadEntry(archive, "app/filesystem/isolation.json");
    ASSERT_FALSE(text.empty());

    const auto document = nlohmann::json::parse(text);
    EXPECT_EQ(document["version"].get<int>(), 1);

    const auto& entries = document["entries"];
    ASSERT_EQ(entries.size(), 3u);

    /* The entries are written in path order, so the root comes first. */
    EXPECT_EQ(entries[0]["path"].get<std::string>(), "");
    EXPECT_EQ(entries[0]["kind"].get<std::string>(), "directory");
    EXPECT_EQ(entries[0]["isolation"].get<std::string>(), "merge");

    EXPECT_EQ(entries[1]["path"].get<std::string>(), "#ProgramFiles#\\MyApp");
    EXPECT_EQ(entries[1]["kind"].get<std::string>(), "directory");
    EXPECT_EQ(entries[1]["isolation"].get<std::string>(), "full");
    EXPECT_EQ(entries[2]["path"].get<std::string>(), "#ProgramFiles#\\MyApp\\app.exe");
    EXPECT_EQ(entries[2]["kind"].get<std::string>(), "file");
    EXPECT_EQ(entries[2]["isolation"].get<std::string>(), "whiteout");
}

TEST(Unit_PackService, PackWritesNetworkIsolationFile)
{
    TempDir    temp;
    const auto my_app = temp.Get() / L"MyApp";
    MakeFile(my_app, L"app.exe", "EXE");

    appbox::PackModel     model;
    appbox::RegistryModel registry;
    appbox::NetworkModel  network;
    std::string           error;
    ASSERT_TRUE(model.ImportFolder("program_files", my_app.wstring(), error)) << error;
    ASSERT_TRUE(model.AddStartupFile("program_files", L"MyApp", L"app.exe", true, error)) << error;

    ASSERT_TRUE(network.AddDnsEntry(L"update.example.com", L"127.0.0.1", error)) << error;
    ASSERT_TRUE(network.AddDnsEntry(L"api.example.com", L"::1", error)) << error;

    const auto zip_path = temp.Get().parent_path() / (temp.Get().filename().wstring() + L"-network-isolation.zip");
    const auto result = appbox::Pack(model, registry, appbox::FilesystemIsolationModel(), network,
                                     appbox::EnvironmentModel(), appbox::ApplicationMetadata(), appbox::StartupScreen{},
                                     FakePayloads(), appbox::CompressionLevel::Normal, zip_path.wstring(), nullptr);
    EXPECT_EQ(result, "") << result;

    ZipArchiveCloser closer(OpenArchive(zip_path.wstring()));
    zip_t*           archive = closer.archive;
    ASSERT_NE(archive, nullptr);

    /*
     * The redirections travel in the network domain of the resources, which is
     * where the launcher looks for them.
     */
    const auto text = ReadEntry(archive, "app/network/isolation.json");
    ASSERT_FALSE(text.empty());

    const auto document = nlohmann::json::parse(text);
    EXPECT_EQ(document["version"].get<int>(), 1);

    const auto& entries = document["entries"];
    ASSERT_EQ(entries.size(), 2u);
    EXPECT_EQ(entries[0]["hostname"].get<std::string>(), "update.example.com");
    EXPECT_EQ(entries[0]["redirect"].get<std::string>(), "127.0.0.1");
    EXPECT_EQ(entries[1]["hostname"].get<std::string>(), "api.example.com");
    EXPECT_EQ(entries[1]["redirect"].get<std::string>(), "::1");

    /* A session without a proxy keeps the document of the previous schema. */
    EXPECT_FALSE(document.contains("proxy"));
}

TEST(Unit_PackService, PackWritesEnvironmentIsolationFile)
{
    TempDir    temp;
    const auto my_app = temp.Get() / L"MyApp";
    MakeFile(my_app, L"app.exe", "EXE");

    appbox::PackModel        model;
    appbox::RegistryModel    registry;
    appbox::EnvironmentModel environment;
    std::string              error;
    ASSERT_TRUE(model.ImportFolder("program_files", my_app.wstring(), error)) << error;
    ASSERT_TRUE(model.AddStartupFile("program_files", L"MyApp", L"app.exe", true, error)) << error;

    /* The search path rule of the workspace fills in the mode and the string. */
    appbox::EnvironmentEntry path;
    path.name = L"PATH";
    path.value = L"C:/MyApp/bin";
    appbox::ApplyPathVariableDefaults(path);
    ASSERT_TRUE(environment.AddEntry(path, error)) << error;

    appbox::EnvironmentEntry mode;
    mode.name = L"APPBOX_MODE";
    mode.value = L"sandbox";
    ASSERT_TRUE(environment.AddEntry(mode, error)) << error;

    const auto zip_path = temp.Get().parent_path() / (temp.Get().filename().wstring() + L"-env-isolation.zip");
    const auto result = appbox::Pack(model, registry, appbox::FilesystemIsolationModel(), appbox::NetworkModel(),
                                     environment, appbox::ApplicationMetadata(), appbox::StartupScreen{},
                                     FakePayloads(), appbox::CompressionLevel::Normal, zip_path.wstring(), nullptr);
    EXPECT_EQ(result, "") << result;

    ZipArchiveCloser closer(OpenArchive(zip_path.wstring()));
    zip_t*           archive = closer.archive;
    ASSERT_NE(archive, nullptr);

    /*
     * The variables travel in the environment domain of the resources, which is
     * where the launcher looks for them.
     */
    const auto text = ReadEntry(archive, "app/environment/isolation.json");
    ASSERT_FALSE(text.empty());

    const auto document = nlohmann::json::parse(text);
    EXPECT_EQ(document["version"].get<int>(), 1);

    const auto& entries = document["entries"];
    ASSERT_EQ(entries.size(), 2u);
    EXPECT_EQ(entries[0]["name"].get<std::string>(), "PATH");
    EXPECT_EQ(entries[0]["value"].get<std::string>(), "C:/MyApp/bin");
    EXPECT_EQ(entries[0]["isolation"].get<std::string>(), "write_copy");
    EXPECT_EQ(entries[0]["merge"].get<std::string>(), "prepend");
    EXPECT_EQ(entries[0]["merge_string"].get<std::string>(), ";");
    EXPECT_EQ(entries[1]["name"].get<std::string>(), "APPBOX_MODE");
    EXPECT_EQ(entries[1]["isolation"].get<std::string>(), "write_copy");
    EXPECT_EQ(entries[1]["merge"].get<std::string>(), "replace");
}

TEST(Unit_PackService, PackWritesTheProxyOfTheNetworkWorkspace)
{
    TempDir    temp;
    const auto my_app = temp.Get() / L"MyApp";
    MakeFile(my_app, L"app.exe", "EXE");

    appbox::PackModel     model;
    appbox::RegistryModel registry;
    appbox::NetworkModel  network;
    std::string           error;
    ASSERT_TRUE(model.ImportFolder("program_files", my_app.wstring(), error)) << error;
    ASSERT_TRUE(model.AddStartupFile("program_files", L"MyApp", L"app.exe", true, error)) << error;

    appbox::ProxyConfig proxy;
    proxy.tcp = true;
    proxy.udp = true;
    proxy.server = L"proxy.example";
    proxy.port = L"1080";
    proxy.username = L"user";
    proxy.password = L"secret";
    ASSERT_TRUE(network.SetProxy(proxy, error)) << error;

    const auto zip_path = temp.Get().parent_path() / (temp.Get().filename().wstring() + L"-proxy.zip");
    const auto result = appbox::Pack(model, registry, appbox::FilesystemIsolationModel(), network,
                                     appbox::EnvironmentModel(), appbox::ApplicationMetadata(), appbox::StartupScreen{},
                                     FakePayloads(), appbox::CompressionLevel::Normal, zip_path.wstring(), nullptr);
    EXPECT_EQ(result, "") << result;

    ZipArchiveCloser closer(OpenArchive(zip_path.wstring()));
    zip_t*           archive = closer.archive;
    ASSERT_NE(archive, nullptr);

    const auto text = ReadEntry(archive, "app/network/isolation.json");
    ASSERT_FALSE(text.empty());

    /*
     * The proxy travels with the network isolation file: the schema version is
     * the one of a file which does not carry the optional member, so an archive
     * which was written before the proxy existed is still readable.
     */
    const auto document = nlohmann::json::parse(text);
    EXPECT_EQ(document["version"].get<int>(), 1);
    ASSERT_TRUE(document.contains("proxy"));

    const auto& item = document["proxy"];
    EXPECT_EQ(item["type"].get<std::string>(), "socks5");
    EXPECT_TRUE(item["tcp"].get<bool>());
    EXPECT_TRUE(item["udp"].get<bool>());
    EXPECT_EQ(item["server"].get<std::string>(), "proxy.example");
    EXPECT_EQ(item["port"].get<std::string>(), "1080");
    EXPECT_EQ(item["username"].get<std::string>(), "user");
    EXPECT_EQ(item["password"].get<std::string>(), "secret");
}

TEST(Unit_PackService, LauncherEntryNameIsEmptyWithoutAStartupFile)
{
    appbox::PackModel     model;
    appbox::RegistryModel registry;

    EXPECT_TRUE(appbox::LauncherEntryName(model).empty());
}

TEST(Unit_PackService, LauncherEntryNameDropsTheDirectoryOfTheStartupFile)
{
    TempDir    temp;
    const auto my_app = temp.Get() / L"MyApp";
    MakeFile(my_app, L"bin\\tool.exe", "EXE");

    appbox::PackModel     model;
    appbox::RegistryModel registry;
    std::string           error;
    ASSERT_TRUE(model.ImportFolder("program_files", my_app.wstring(), error)) << error;
    ASSERT_TRUE(model.AddStartupFile("program_files", L"MyApp", L"bin\\tool.exe", true, error)) << error;

    /* Only the file name is used: the launcher lives in the archive root. */
    EXPECT_EQ(appbox::LauncherEntryName(model), L"tool.exe");

    const auto zip_path = temp.Get().parent_path() / (temp.Get().filename().wstring() + L"-entry.zip");
    const auto result = appbox::Pack(model, registry, appbox::FilesystemIsolationModel(), appbox::NetworkModel(),
                                     appbox::EnvironmentModel(), appbox::ApplicationMetadata(), appbox::StartupScreen{},
                                     FakePayloads(), appbox::CompressionLevel::Normal, zip_path.wstring(), nullptr);
    EXPECT_EQ(result, "") << result;

    ZipArchiveCloser closer(OpenArchive(zip_path.wstring()));
    zip_t*           archive = closer.archive;
    ASSERT_NE(archive, nullptr);

    const auto launcher_entry = ReadEntry(archive, "tool.exe");
    ASSERT_FALSE(launcher_entry.empty());
    EXPECT_EQ(launcher_entry.substr(0, 2), "MZ");

    std::string json_text;
    std::string config_error;
    ASSERT_TRUE(ReadLauncherConfigJsonFromEntry(launcher_entry, json_text, config_error)) << config_error;
    const auto config = nlohmann::json::parse(json_text).get<appbox::LauncherConfig>();
    ASSERT_EQ(config.startups.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(config.startups[0].executable, "#ProgramFiles#\\MyApp\\bin\\tool.exe");

    /* The entry program itself keeps its place below the layer tree. */
    EXPECT_EQ(ReadEntry(archive, "app/filesystem/#ProgramFiles#/MyApp/bin/tool.exe"), "EXE");
}

TEST(Unit_PackService, LauncherEntryNameFollowsTheFirstStartupFile)
{
    TempDir    temp;
    const auto my_app = temp.Get() / L"MyApp";
    MakeFile(my_app, L"second.exe", "SECOND");
    MakeFile(my_app, L"first.exe", "FIRST");

    appbox::PackModel     model;
    appbox::RegistryModel registry;
    std::string           error;
    ASSERT_TRUE(model.ImportFolder("program_files", my_app.wstring(), error)) << error;
    ASSERT_TRUE(model.AddStartupFile("program_files", L"MyApp", L"second.exe", true, error)) << error;
    ASSERT_TRUE(model.AddStartupFile("program_files", L"MyApp", L"first.exe", false, error)) << error;

    EXPECT_EQ(appbox::LauncherEntryName(model), L"second.exe");

    const auto zip_path = temp.Get().parent_path() / (temp.Get().filename().wstring() + L"-order.zip");
    const auto result = appbox::Pack(model, registry, appbox::FilesystemIsolationModel(), appbox::NetworkModel(),
                                     appbox::EnvironmentModel(), appbox::ApplicationMetadata(), appbox::StartupScreen{},
                                     FakePayloads(), appbox::CompressionLevel::Normal, zip_path.wstring(), nullptr);
    EXPECT_EQ(result, "") << result;

    ZipArchiveCloser closer(OpenArchive(zip_path.wstring()));
    zip_t*           archive = closer.archive;
    ASSERT_NE(archive, nullptr);

    const auto launcher_entry = ReadEntry(archive, "second.exe");
    ASSERT_FALSE(launcher_entry.empty());
    std::string json_text;
    std::string config_error;
    ASSERT_TRUE(ReadLauncherConfigJsonFromEntry(launcher_entry, json_text, config_error)) << config_error;
    const auto config = nlohmann::json::parse(json_text).get<appbox::LauncherConfig>();

    /* Every startup file is written in the order of the model. */
    ASSERT_EQ(config.startups.size(), static_cast<std::size_t>(2));
    EXPECT_EQ(config.startups[0].trigger, "second");
    EXPECT_TRUE(config.startups[0].auto_start);
    EXPECT_EQ(config.startups[0].executable, "#ProgramFiles#\\MyApp\\second.exe");
    EXPECT_EQ(config.startups[1].trigger, "first");
    EXPECT_FALSE(config.startups[1].auto_start);
    EXPECT_EQ(config.startups[1].executable, "#ProgramFiles#\\MyApp\\first.exe");
}

TEST(Unit_PackService, LauncherEntryNameReplacesTheExtensionOfAScript)
{
    TempDir    temp;
    const auto my_app = temp.Get() / L"MyApp";
    MakeFile(my_app, L"bin\\deploy.ps1", "PS1");
    MakeFile(my_app, L"run.bat", "BAT");

    appbox::PackModel     model;
    appbox::RegistryModel registry;
    std::string           error;
    ASSERT_TRUE(model.ImportFolder("program_files", my_app.wstring(), error)) << error;
    ASSERT_TRUE(model.AddStartupFile("program_files", L"MyApp", L"bin\\deploy.ps1", true, error)) << error;
    ASSERT_TRUE(model.AddStartupFile("program_files", L"MyApp", L"run.bat", true, error)) << error;

    /*
     * The payload of the launcher is a program image, so the entry carries the
     * `.exe` extension although the first startup file is a script.
     */
    EXPECT_EQ(appbox::LauncherEntryName(model), L"deploy.exe");

    const auto zip_path = temp.Get().parent_path() / (temp.Get().filename().wstring() + L"-script.zip");
    const auto result = appbox::Pack(model, registry, appbox::FilesystemIsolationModel(), appbox::NetworkModel(),
                                     appbox::EnvironmentModel(), appbox::ApplicationMetadata(), appbox::StartupScreen{},
                                     FakePayloads(), appbox::CompressionLevel::Normal, zip_path.wstring(), nullptr);
    EXPECT_EQ(result, "") << result;

    ZipArchiveCloser closer(OpenArchive(zip_path.wstring()));
    zip_t*           archive = closer.archive;
    ASSERT_NE(archive, nullptr);

    /* The name of the script itself is gone from the root of the archive. */
    const auto launcher_entry = ReadEntry(archive, "deploy.exe");
    ASSERT_FALSE(launcher_entry.empty());
    EXPECT_EQ(launcher_entry.substr(0, 2), "MZ");
    EXPECT_EQ(zip_name_locate(archive, "deploy.ps1", 0), -1);

    /* The scripts keep their place below the layer tree of the archive. */
    EXPECT_EQ(ReadEntry(archive, "app/filesystem/#ProgramFiles#/MyApp/bin/deploy.ps1"), "PS1");
    EXPECT_EQ(ReadEntry(archive, "app/filesystem/#ProgramFiles#/MyApp/run.bat"), "BAT");

    std::string json_text;
    std::string config_error;
    ASSERT_TRUE(ReadLauncherConfigJsonFromEntry(launcher_entry, json_text, config_error)) << config_error;
    const auto config = nlohmann::json::parse(json_text).get<appbox::LauncherConfig>();
    ASSERT_EQ(config.startups.size(), static_cast<std::size_t>(2));
    EXPECT_EQ(config.startups[0].executable, "#ProgramFiles#\\MyApp\\bin\\deploy.ps1");
    EXPECT_EQ(config.startups[1].executable, "#ProgramFiles#\\MyApp\\run.bat");
}

TEST(Unit_PackService, PackReportsProgress)
{
    TempDir    temp;
    const auto my_app = temp.Get() / L"MyApp";
    MakeFile(my_app, L"app.exe", "EXE");
    MakeFile(my_app, L"data.txt", "DATA");

    appbox::PackModel     model;
    appbox::RegistryModel registry;
    std::string           error;
    ASSERT_TRUE(model.ImportFolder("program_files", my_app.wstring(), error)) << error;
    ASSERT_TRUE(model.AddStartupFile("program_files", L"MyApp", L"app.exe", true, error)) << error;

    std::vector<appbox::BuildProgress> reports;
    const auto                         progress = [&reports](const appbox::BuildProgress& report) {
        reports.emplace_back(report);
        return true;
    };

    const auto zip_path = temp.Get().parent_path() / (temp.Get().filename().wstring() + L"-progress.zip");
    const auto result = appbox::Pack(model, registry, appbox::FilesystemIsolationModel(), appbox::NetworkModel(),
                                     appbox::EnvironmentModel(), appbox::ApplicationMetadata(), appbox::StartupScreen{},
                                     FakePayloads(), appbox::CompressionLevel::Normal, zip_path.wstring(), progress);
    EXPECT_EQ(result, "") << result;

    ASSERT_FALSE(reports.empty());

    /* The run opens with the preparing stage, which has no file of its own. */
    EXPECT_EQ(reports.front().stage, appbox::BuildStage::Preparing);
    EXPECT_EQ(reports.front().done, static_cast<std::size_t>(0));
    EXPECT_EQ(reports.front().total, appbox::kNonContentArchiveEntries + 2);
    EXPECT_TRUE(reports.front().current.empty());

    /* It closes with the complete count of the packing stage. */
    EXPECT_EQ(reports.back().stage, appbox::BuildStage::Packing);
    EXPECT_EQ(reports.back().done, appbox::kNonContentArchiveEntries + 2);
    EXPECT_EQ(reports.back().total, appbox::kNonContentArchiveEntries + 2);

    /* Every packed file is named by its path below the import root. */
    std::set<std::wstring> named;
    for (const auto& report : reports)
    {
        if (report.stage != appbox::BuildStage::Packing)
        {
            continue;
        }
        if (!report.current.empty())
        {
            named.insert(report.current);
        }
    }
    EXPECT_EQ(named.count(L"MyApp\\app.exe"), static_cast<std::size_t>(1));
    EXPECT_EQ(named.count(L"MyApp\\data.txt"), static_cast<std::size_t>(1));
}

TEST(Unit_PackService, PackCanBeCancelled)
{
    TempDir    temp;
    const auto my_app = temp.Get() / L"MyApp";
    MakeFile(my_app, L"app.exe", "EXE");

    appbox::PackModel     model;
    appbox::RegistryModel registry;
    std::string           error;
    ASSERT_TRUE(model.ImportFolder("program_files", my_app.wstring(), error)) << error;
    ASSERT_TRUE(model.AddStartupFile("program_files", L"MyApp", L"app.exe", true, error)) << error;

    const auto zip_path = temp.Get().parent_path() / (temp.Get().filename().wstring() + L"-cancel.zip");
    const auto result = appbox::Pack(model, registry, appbox::FilesystemIsolationModel(), appbox::NetworkModel(),
                                     appbox::EnvironmentModel(), appbox::ApplicationMetadata(), appbox::StartupScreen{},
                                     FakePayloads(), appbox::CompressionLevel::Normal, zip_path.wstring(),
                                     [](const appbox::BuildProgress&) { return false; });
    EXPECT_EQ(result, appbox::kBuildCancelledError);
}

TEST(Unit_PackService, PackWritesImportedFiles)
{
    TempDir    temp;
    const auto my_app = temp.Get() / L"MyApp";
    MakeFile(my_app, L"app.exe", "EXE");
    MakeFolder(my_app, L"data");
    const auto extra = MakeFile(temp.Get(), L"extra.dll", "EXTRA-CONTENT");
    const auto note = MakeFile(temp.Get(), L"note.txt", "NOTE-CONTENT");

    appbox::PackModel     model;
    appbox::RegistryModel registry;
    std::string           error;
    ASSERT_TRUE(model.ImportFolder("program_files", my_app.wstring(), error)) << error;
    ASSERT_TRUE(model.AddStartupFile("program_files", L"MyApp", L"app.exe", true, error)) << error;
    ASSERT_TRUE(model.ImportFiles("program_files", L"MyApp", { extra.wstring() }, error)) << error;
    ASSERT_TRUE(model.ImportFiles("program_files", L"MyApp\\data", { note.wstring() }, error)) << error;

    const auto zip_path = temp.Get().parent_path() / (temp.Get().filename().wstring() + L"-files.zip");
    const auto result = appbox::Pack(model, registry, appbox::FilesystemIsolationModel(), appbox::NetworkModel(),
                                     appbox::EnvironmentModel(), appbox::ApplicationMetadata(), appbox::StartupScreen{},
                                     FakePayloads(), appbox::CompressionLevel::Normal, zip_path.wstring(), nullptr);
    EXPECT_EQ(result, "") << result;

    ZipArchiveCloser closer(OpenArchive(zip_path.wstring()));
    zip_t*           archive = closer.archive;
    ASSERT_NE(archive, nullptr);

    /* Imported files share the layer tree of the imported folder. */
    EXPECT_EQ(ReadEntry(archive, "app/filesystem/#ProgramFiles#/MyApp/extra.dll"), "EXTRA-CONTENT");
    EXPECT_EQ(ReadEntry(archive, "app/filesystem/#ProgramFiles#/MyApp/data/note.txt"), "NOTE-CONTENT");

    /* The imported folder content is untouched. */
    EXPECT_EQ(ReadEntry(archive, "app/filesystem/#ProgramFiles#/MyApp/app.exe"), "EXE");
}

TEST(Unit_PackService, PackCreatesDirectoriesOfImportedFiles)
{
    TempDir    temp;
    const auto my_app = temp.Get() / L"MyApp";
    MakeFile(my_app, L"app.exe", "EXE");
    const auto extra = MakeFile(temp.Get(), L"extra.dll", "EXTRA");

    appbox::PackModel     model;
    appbox::RegistryModel registry;
    std::string           error;
    ASSERT_TRUE(model.ImportFolder("program_files", my_app.wstring(), error)) << error;
    ASSERT_TRUE(model.AddStartupFile("program_files", L"MyApp", L"app.exe", true, error)) << error;

    /* The intermediate folder exists on the host. */
    MakeFolder(my_app, L"plugins");
    ASSERT_TRUE(model.ImportFiles("program_files", L"MyApp\\plugins\\deep", { extra.wstring() }, error)) << error;

    const auto zip_path = temp.Get().parent_path() / (temp.Get().filename().wstring() + L"-dirs.zip");
    const auto result = appbox::Pack(model, registry, appbox::FilesystemIsolationModel(), appbox::NetworkModel(),
                                     appbox::EnvironmentModel(), appbox::ApplicationMetadata(), appbox::StartupScreen{},
                                     FakePayloads(), appbox::CompressionLevel::Normal, zip_path.wstring(), nullptr);
    EXPECT_EQ(result, "") << result;

    ZipArchiveCloser closer(OpenArchive(zip_path.wstring()));
    zip_t*           archive = closer.archive;
    ASSERT_NE(archive, nullptr);

    /* Every missing prefix of the target directory becomes a directory entry. */
    EXPECT_GE(zip_name_locate(archive, "app/filesystem/#ProgramFiles#/MyApp/plugins/", 0), 0);
    EXPECT_GE(zip_name_locate(archive, "app/filesystem/#ProgramFiles#/MyApp/plugins/deep/", 0), 0);
    EXPECT_EQ(ReadEntry(archive, "app/filesystem/#ProgramFiles#/MyApp/plugins/deep/extra.dll"), "EXTRA");
}

TEST(Unit_PackService, PackCountsImportedFilesInTheProgressTotal)
{
    TempDir    temp;
    const auto my_app = temp.Get() / L"MyApp";
    MakeFile(my_app, L"app.exe", "EXE");
    MakeFile(my_app, L"data.txt", "DATA");
    const auto extra = MakeFile(temp.Get(), L"extra.dll", "EXTRA");

    appbox::PackModel     model;
    appbox::RegistryModel registry;
    std::string           error;
    ASSERT_TRUE(model.ImportFolder("program_files", my_app.wstring(), error)) << error;
    ASSERT_TRUE(model.AddStartupFile("program_files", L"MyApp", L"app.exe", true, error)) << error;
    ASSERT_TRUE(model.ImportFiles("program_files", L"MyApp", { extra.wstring() }, error)) << error;

    std::vector<appbox::BuildProgress> reports;
    const auto                         progress = [&reports](const appbox::BuildProgress& report) {
        reports.emplace_back(report);
        return true;
    };

    const auto zip_path = temp.Get().parent_path() / (temp.Get().filename().wstring() + L"-files-progress.zip");
    const auto result = appbox::Pack(model, registry, appbox::FilesystemIsolationModel(), appbox::NetworkModel(),
                                     appbox::EnvironmentModel(), appbox::ApplicationMetadata(), appbox::StartupScreen{},
                                     FakePayloads(), appbox::CompressionLevel::Normal, zip_path.wstring(), progress);
    EXPECT_EQ(result, "") << result;

    ASSERT_FALSE(reports.empty());
    /* Two files of the imported folder plus one imported file. */
    EXPECT_EQ(reports.front().stage, appbox::BuildStage::Preparing);
    EXPECT_EQ(reports.front().done, static_cast<std::size_t>(0));
    EXPECT_EQ(reports.front().total, appbox::kNonContentArchiveEntries + 3);
    EXPECT_EQ(reports.back().stage, appbox::BuildStage::Packing);
    EXPECT_EQ(reports.back().done, appbox::kNonContentArchiveEntries + 3);
    EXPECT_EQ(reports.back().total, appbox::kNonContentArchiveEntries + 3);

    /* An imported file is named by its target directory and its file name. */
    std::set<std::wstring> named;
    for (const auto& report : reports)
    {
        if (report.stage == appbox::BuildStage::Packing && !report.current.empty())
        {
            named.insert(report.current);
        }
    }
    EXPECT_EQ(named.count(L"MyApp\\extra.dll"), static_cast<std::size_t>(1));
}

TEST(Unit_PackService, ContentFileCountCountsTheImports)
{
    TempDir    temp;
    const auto my_app = temp.Get() / L"MyApp";
    MakeFile(my_app, L"app.exe", "EXE");
    MakeFile(my_app, L"sub\\data.txt", "DATA");
    const auto extra = MakeFile(temp.Get(), L"extra.dll", "EXTRA");

    appbox::PackModel     model;
    appbox::RegistryModel registry;
    std::string           error;
    ASSERT_TRUE(model.ImportFolder("program_files", my_app.wstring(), error)) << error;
    ASSERT_TRUE(model.ImportFiles("program_files", L"MyApp", { extra.wstring() }, error)) << error;

    /* The folders contribute their regular files, the imports one each. */
    EXPECT_EQ(appbox::ContentFileCount(model), static_cast<std::size_t>(3));

    /* The total of a pack run adds the non content entries of its product. */
    std::vector<appbox::BuildProgress> reports;
    const auto                         progress = [&reports](const appbox::BuildProgress& report) {
        reports.emplace_back(report);
        return true;
    };

    const auto zip_path = temp.Get().parent_path() / (temp.Get().filename().wstring() + L"-count.zip");
    ASSERT_EQ(appbox::PackPatch(model, registry, appbox::FilesystemIsolationModel(), appbox::NetworkModel(),
                                appbox::EnvironmentModel(), appbox::CompressionLevel::Normal, zip_path.wstring(),
                                progress),
              "");
    ASSERT_FALSE(reports.empty());
    EXPECT_EQ(reports.front().total, appbox::kNonContentPatchEntries + appbox::ContentFileCount(model));
}

TEST(Unit_PackService, PatchDoesNotRequireAStartupFileOrTheLauncher)
{
    TempDir    temp;
    const auto my_app = temp.Get() / L"MyApp";
    MakeFile(my_app, L"app.exe", "EXE");

    appbox::PackModel     model;
    appbox::RegistryModel registry;
    std::string           error;
    ASSERT_TRUE(model.ImportFolder("program_files", my_app.wstring(), error)) << error;

    /*
     * The very same model fails the standalone pack run, which needs a startup
     * file and the embedded launcher payload.
     */
    const auto standalone =
        appbox::Pack(model, registry, appbox::FilesystemIsolationModel(), appbox::NetworkModel(),
                     appbox::EnvironmentModel(), appbox::ApplicationMetadata(), appbox::StartupScreen{}, FakePayloads(),
                     appbox::CompressionLevel::Normal, (temp.Get() / L"standalone.zip").wstring(), nullptr);
    EXPECT_NE(standalone.find("startup file"), std::string::npos);

    /* A patch package carries no launcher, so neither of them is needed. */
    const auto zip_path = temp.Get() / L"patch.zip";
    EXPECT_EQ(appbox::PackPatch(model, registry, appbox::FilesystemIsolationModel(), appbox::NetworkModel(),
                                appbox::EnvironmentModel(), appbox::CompressionLevel::Normal, zip_path.wstring(),
                                nullptr),
              "");

    ZipArchiveCloser closer(OpenArchive(zip_path.wstring()));
    zip_t*           archive = closer.archive;
    ASSERT_NE(archive, nullptr);
    EXPECT_EQ(ReadEntry(archive, "filesystem/#ProgramFiles#/MyApp/app.exe"), "EXE");
}

TEST(Unit_PackService, PatchRootsTheResourceTreeAtTheArchiveRoot)
{
    TempDir    program_files;
    TempDir    user_profile;
    const auto my_app = program_files.Get() / L"MyApp";
    MakeFile(my_app, L"app.exe", "EXE-CONTENT");
    MakeFile(my_app, L"data\\config.txt", "CFG-CONTENT");
    std::filesystem::create_directories(my_app / L"emptydir");
    MakeFile(user_profile.Get(), L"MyUser\\settings.ini", "INI-CONTENT");

    appbox::PackModel     model;
    appbox::RegistryModel registry;
    std::string           error;
    ASSERT_TRUE(model.ImportFolder("program_files", my_app.wstring(), error)) << error;
    ASSERT_TRUE(model.ImportFolder("user_profile", (user_profile.Get() / L"MyUser").wstring(), error)) << error;

    /*
     * A patch project may hold startup files, a patch package simply ignores
     * them: they name the programs a launcher would start, and the package
     * carries no launcher.
     */
    ASSERT_TRUE(model.AddStartupFile("program_files", L"MyApp", L"app.exe", true, error)) << error;

    const auto zip_path =
        program_files.Get().parent_path() / (program_files.Get().filename().wstring() + L"-patch.zip");
    EXPECT_EQ(appbox::PackPatch(model, registry, appbox::FilesystemIsolationModel(), appbox::NetworkModel(),
                                appbox::EnvironmentModel(), appbox::CompressionLevel::Normal, zip_path.wstring(),
                                nullptr),
              "");

    ZipArchiveCloser closer(OpenArchive(zip_path.wstring()));
    zip_t*           archive = closer.archive;
    ASSERT_NE(archive, nullptr);

    /* The domain directories are rooted at the archive root. */
    const auto names = EntryNames(archive);
    EXPECT_EQ(names.count("filesystem/"), static_cast<std::size_t>(1));
    EXPECT_EQ(names.count("registry/"), static_cast<std::size_t>(1));
    EXPECT_EQ(names.count("network/"), static_cast<std::size_t>(1));
    EXPECT_EQ(names.count("environment/"), static_cast<std::size_t>(1));

    /* The imported folders become layers of the same layer tree. */
    EXPECT_EQ(ReadEntry(archive, "filesystem/#ProgramFiles#/MyApp/app.exe"), "EXE-CONTENT");
    EXPECT_EQ(ReadEntry(archive, "filesystem/#ProgramFiles#/MyApp/data/config.txt"), "CFG-CONTENT");
    EXPECT_EQ(ReadEntry(archive, "filesystem/#USERPROFILE#/MyUser/settings.ini"), "INI-CONTENT");
    EXPECT_GE(zip_name_locate(archive, "filesystem/#ProgramFiles#/MyApp/emptydir/", 0), 0);

    /* The registry travels as a hive file next to its isolation file. */
    const auto hive = ReadEntry(archive, "registry/user.hiv");
    ASSERT_GE(hive.size(), 4u);
    EXPECT_EQ(hive.substr(0, 4), "regf");

    /* Every isolation file of the workspace is part of the package. */
    EXPECT_FALSE(ReadEntry(archive, "filesystem/isolation.json").empty());
    EXPECT_FALSE(ReadEntry(archive, "registry/isolation.json").empty());
    EXPECT_FALSE(ReadEntry(archive, "network/isolation.json").empty());
    EXPECT_FALSE(ReadEntry(archive, "environment/isolation.json").empty());

    /*
     * Neither the launcher nor the resource directory of a standalone archive
     * travels in a patch package, and the state of the sandbox never travels in
     * either product.
     */
    EXPECT_EQ(zip_name_locate(archive, "app.exe", 0), -1);
    EXPECT_EQ(zip_name_locate(archive, "app.exe.json", 0), -1);
    for (const auto& name : names)
    {
        EXPECT_NE(name.rfind("app/", 0), 0) << name;
        EXPECT_EQ(name.rfind("data/", 0), std::string::npos) << name;
    }
}

TEST(Unit_PackService, PatchWritesImportedFilesBelowTheLayerTree)
{
    TempDir    temp;
    const auto my_app = temp.Get() / L"MyApp";
    MakeFile(my_app, L"app.exe", "EXE");
    const auto extra = MakeFile(temp.Get(), L"extra.dll", "EXTRA-CONTENT");

    appbox::PackModel     model;
    appbox::RegistryModel registry;
    std::string           error;
    ASSERT_TRUE(model.ImportFolder("program_files", my_app.wstring(), error)) << error;
    ASSERT_TRUE(model.ImportFiles("program_files", L"MyApp\\plugins", { extra.wstring() }, error)) << error;

    const auto zip_path = temp.Get().parent_path() / (temp.Get().filename().wstring() + L"-patch-files.zip");
    EXPECT_EQ(appbox::PackPatch(model, registry, appbox::FilesystemIsolationModel(), appbox::NetworkModel(),
                                appbox::EnvironmentModel(), appbox::CompressionLevel::Normal, zip_path.wstring(),
                                nullptr),
              "");

    ZipArchiveCloser closer(OpenArchive(zip_path.wstring()));
    zip_t*           archive = closer.archive;
    ASSERT_NE(archive, nullptr);

    /* Imported files share the layer tree of the imported folder. */
    EXPECT_EQ(ReadEntry(archive, "filesystem/#ProgramFiles#/MyApp/plugins/extra.dll"), "EXTRA-CONTENT");

    /* Every missing prefix of the target directory becomes a directory entry. */
    EXPECT_GE(zip_name_locate(archive, "filesystem/#ProgramFiles#/MyApp/plugins/", 0), 0);
}

TEST(Unit_PackService, PatchReportsTheProgressOfTheResourceTree)
{
    TempDir    temp;
    const auto my_app = temp.Get() / L"MyApp";
    MakeFile(my_app, L"app.exe", "EXE");
    MakeFile(my_app, L"data.txt", "DATA");
    const auto extra = MakeFile(temp.Get(), L"extra.dll", "EXTRA");

    appbox::PackModel     model;
    appbox::RegistryModel registry;
    std::string           error;
    ASSERT_TRUE(model.ImportFolder("program_files", my_app.wstring(), error)) << error;
    ASSERT_TRUE(model.ImportFiles("program_files", L"MyApp", { extra.wstring() }, error)) << error;

    std::vector<appbox::BuildProgress> reports;
    const auto                         progress = [&reports](const appbox::BuildProgress& report) {
        reports.emplace_back(report);
        return true;
    };

    const auto zip_path = temp.Get().parent_path() / (temp.Get().filename().wstring() + L"-patch-progress.zip");
    EXPECT_EQ(appbox::PackPatch(model, registry, appbox::FilesystemIsolationModel(), appbox::NetworkModel(),
                                appbox::EnvironmentModel(), appbox::CompressionLevel::Normal, zip_path.wstring(),
                                progress),
              "");

    ASSERT_FALSE(reports.empty());

    /* Two files of the imported folder plus one imported file. */
    const auto expected = appbox::kNonContentPatchEntries + 3;

    EXPECT_EQ(reports.front().stage, appbox::BuildStage::Preparing);
    EXPECT_EQ(reports.front().done, static_cast<std::size_t>(0));
    EXPECT_EQ(reports.front().total, expected);
    EXPECT_TRUE(reports.front().current.empty());

    EXPECT_EQ(reports.back().stage, appbox::BuildStage::Packing);
    EXPECT_EQ(reports.back().done, expected);
    EXPECT_EQ(reports.back().total, expected);

    std::set<std::wstring> named;
    for (const auto& report : reports)
    {
        if (report.stage == appbox::BuildStage::Packing && !report.current.empty())
        {
            named.insert(report.current);
        }
    }
    EXPECT_EQ(named.count(L"MyApp\\app.exe"), static_cast<std::size_t>(1));
    EXPECT_EQ(named.count(L"MyApp\\extra.dll"), static_cast<std::size_t>(1));
}

TEST(Unit_PackService, PatchCanBeCancelled)
{
    TempDir    temp;
    const auto my_app = temp.Get() / L"MyApp";
    MakeFile(my_app, L"app.exe", "EXE");

    appbox::PackModel     model;
    appbox::RegistryModel registry;
    std::string           error;
    ASSERT_TRUE(model.ImportFolder("program_files", my_app.wstring(), error)) << error;

    const auto zip_path = temp.Get().parent_path() / (temp.Get().filename().wstring() + L"-patch-cancel.zip");
    EXPECT_EQ(appbox::PackPatch(model, registry, appbox::FilesystemIsolationModel(), appbox::NetworkModel(),
                                appbox::EnvironmentModel(), appbox::CompressionLevel::Normal, zip_path.wstring(),
                                [](const appbox::BuildProgress&) { return false; }),
              appbox::kBuildCancelledError);
}

TEST(Unit_PackService, PatchLayoutMirrorsTheArchiveLayout)
{
    /*
     * A patch package roots the resources at the archive root while a
     * standalone archive keeps them below `app`, so the two spellings of an
     * entry have to describe the very same file.
     */
    const auto prefix = std::string(appbox::layout::kAppDirName) + "/";

    EXPECT_EQ(prefix + appbox::layout::kFilesystemDirName, appbox::layout::kLayerRootRelative);
    EXPECT_EQ(prefix + appbox::layout::kFilesystemIsolationAppRelative, appbox::layout::kFilesystemIsolationRelative);
    EXPECT_EQ(prefix + appbox::layout::kRegistryHiveAppRelative, appbox::layout::kRegistryHiveRelative);
    EXPECT_EQ(prefix + appbox::layout::kRegistryIsolationAppRelative, appbox::layout::kRegistryIsolationRelative);
    EXPECT_EQ(prefix + appbox::layout::kNetworkIsolationAppRelative, appbox::layout::kNetworkIsolationRelative);
    EXPECT_EQ(prefix + appbox::layout::kEnvironmentIsolationAppRelative, appbox::layout::kEnvironmentIsolationRelative);
}

TEST(Unit_PackService, PackWritesTheFilePropertiesOfTheMainProgram)
{
    TempDir temp;

    /*
     * A program image stands in for the main program of the session and for the
     * launcher payload: both have to be PE images, because the pack run patches
     * the resources of the payload. The probe of the suite is used instead of
     * the test executable: the case copies, packs and extracts the image, and a
     * 30 MB image keeps the file system of the case busy for seconds, which is
     * exactly the window an on access scanner interferes in. The probe is built
     * without a resource script, so it carries neither a version resource nor an
     * icon, just like the test executable.
     */
    const auto image = appbox::test::ProbePath(appbox::test::ProbeBitness::Bit64);
    ASSERT_FALSE(image.empty()) << "the run was started without the path of the 64 bit probe";

    const auto my_app = temp.Get() / L"MyApp";
    std::filesystem::create_directories(my_app);
    const auto program = my_app / L"app.exe";
    ASSERT_TRUE(CopyFileW(image.c_str(), program.c_str(), FALSE) != FALSE);

    const auto payload = ReadAllBytes(image);
    ASSERT_FALSE(payload.empty());

    /* The main program carries the file properties the launcher inherits. */
    appbox::ApplicationVersionInfo source_info;
    source_info.fields = {
        { appbox::metadata_field::kFileDescription, L"Packaged editor" },
        { appbox::metadata_field::kFileVersion,     L"3.1.4.1"         },
        { appbox::metadata_field::kCompanyName,     L"Source company"  },
    };

    std::string warning;
    const auto patched_program = appbox::ApplyApplicationMetadata(payload.data(), payload.size(), source_info, warning);
    ASSERT_TRUE(warning.empty()) << warning;
    ASSERT_FALSE(patched_program.empty());
    ASSERT_TRUE(WriteAllBytes(program.wstring(), patched_program));

    appbox::PackModel     model;
    appbox::RegistryModel registry;
    std::string           error;
    ASSERT_TRUE(model.ImportFolder("program_files", my_app.wstring(), error)) << error;
    ASSERT_TRUE(model.AddStartupFile("program_files", L"MyApp", L"app.exe", true, error)) << error;

    /*
     * The session inherits from the default source - the only program marked
     * for auto start - and edits the company name.
     */
    appbox::ApplicationMetadata metadata;
    metadata.overrides = {
        { appbox::metadata_field::kCompanyName, L"ACME" }
    };

    auto payloads = FakePayloads();
    payloads.launcher_bytes = payload.data();
    payloads.launcher_size = payload.size();

    const auto zip_path = temp.Get().parent_path() / (temp.Get().filename().wstring() + L"-metadata.zip");
    ASSERT_EQ(appbox::Pack(model, registry, appbox::FilesystemIsolationModel(), appbox::NetworkModel(),
                           appbox::EnvironmentModel(), metadata, appbox::StartupScreen{}, payloads,
                           appbox::CompressionLevel::Normal, zip_path.wstring(), nullptr),
              "");

    const auto extracted = temp.Get() / L"extracted";
    ASSERT_EQ(appbox::ExtractArchive(zip_path.wstring(), extracted.wstring()), appbox::ZIPREADER_SUCCESS);

    /*
     * The image is read back through the version API of Windows, which opens it
     * again: the image was written moments ago, so the read tolerates the window
     * in which a file system filter still inspects it.
     */
    appbox::ApplicationVersionInfo read;
    ASSERT_TRUE(ReadVersionInfoEventually((extracted / L"app.exe").wstring(), read, error)) << error;

    /* The fields of the source program travel into the launcher of the archive. */
    const auto* description = appbox::FindMetadataValue(read.fields, appbox::metadata_field::kFileDescription);
    ASSERT_NE(description, nullptr);
    EXPECT_EQ(*description, L"Packaged editor");

    const auto* version = appbox::FindMetadataValue(read.fields, appbox::metadata_field::kFileVersion);
    ASSERT_NE(version, nullptr);
    EXPECT_EQ(*version, L"3.1.4.1");

    /* A field the session edited wins over the one of the source program. */
    const auto* company = appbox::FindMetadataValue(read.fields, appbox::metadata_field::kCompanyName);
    ASSERT_NE(company, nullptr);
    EXPECT_EQ(*company, L"ACME");
}

TEST(Unit_PackService, PackWritesTheFoldersTheUserCreated)
{
    TempDir    temp;
    const auto my_app = temp.Get() / L"MyApp";
    MakeFile(my_app, L"app.exe", "EXE");

    appbox::PackModel     model;
    appbox::RegistryModel registry;
    std::string           error;
    ASSERT_TRUE(model.ImportFolder("program_files", my_app.wstring(), error)) << error;
    ASSERT_TRUE(model.AddStartupFile("program_files", L"MyApp", L"app.exe", true, error)) << error;

    /*
     * A folder inside the import and a folder of a preset directory which
     * holds no import at all: the second one is why the layer root of its
     * preset directory has to be written as well.
     */
    ASSERT_TRUE(model.CreateVirtualDirectory("program_files", L"MyApp", L"logs", error)) << error;
    ASSERT_TRUE(model.CreateVirtualDirectory("user_profile", std::wstring(), L"logs", error)) << error;

    /* A folder the user created holds no file, so the total is the one of the imports. */
    EXPECT_EQ(appbox::ContentFileCount(model), static_cast<std::size_t>(1));

    const auto zip_path = temp.Get() / L"folders.zip";
    ASSERT_EQ(appbox::Pack(model, registry, appbox::FilesystemIsolationModel(), appbox::NetworkModel(),
                           appbox::EnvironmentModel(), appbox::ApplicationMetadata(), appbox::StartupScreen{},
                           FakePayloads(), appbox::CompressionLevel::Normal, zip_path.wstring(), nullptr),
              "");

    ZipArchiveCloser closer(OpenArchive(zip_path.wstring()));
    zip_t*           archive = closer.archive;
    ASSERT_NE(archive, nullptr);

    const auto names = EntryNames(archive);

    /* A folder the user created is a directory entry of the layer of its preset. */
    EXPECT_EQ(names.count("app/filesystem/#ProgramFiles#/MyApp/logs/"), static_cast<std::size_t>(1));
    EXPECT_EQ(names.count("app/filesystem/#USERPROFILE#/logs/"), static_cast<std::size_t>(1));

    /* The layer root of a preset directory without an import is written as well. */
    EXPECT_EQ(names.count("app/filesystem/#USERPROFILE#/"), static_cast<std::size_t>(1));

    /* The content of the import is packed next to the folder of the user. */
    EXPECT_EQ(ReadEntry(archive, "app/filesystem/#ProgramFiles#/MyApp/app.exe"), "EXE");
}

TEST(Unit_PackService, PatchWritesTheFoldersTheUserCreated)
{
    TempDir    temp;
    const auto my_app = temp.Get() / L"MyApp";
    MakeFile(my_app, L"app.exe", "EXE");

    appbox::PackModel     model;
    appbox::RegistryModel registry;
    std::string           error;
    ASSERT_TRUE(model.ImportFolder("program_files", my_app.wstring(), error)) << error;
    ASSERT_TRUE(model.CreateVirtualDirectory("program_files", L"MyApp", L"logs", error)) << error;
    ASSERT_TRUE(model.CreateVirtualDirectory("program_files", L"MyApp\\logs", L"cache", error)) << error;

    const auto zip_path = temp.Get() / L"folders-patch.zip";
    ASSERT_EQ(appbox::PackPatch(model, registry, appbox::FilesystemIsolationModel(), appbox::NetworkModel(),
                                appbox::EnvironmentModel(), appbox::CompressionLevel::Normal, zip_path.wstring(),
                                nullptr),
              "");

    ZipArchiveCloser closer(OpenArchive(zip_path.wstring()));
    zip_t*           archive = closer.archive;
    ASSERT_NE(archive, nullptr);

    /*
     * A patch package roots the resources at the archive root, and every
     * folder is written once, which the zip writer requires.
     */
    const auto names = EntryNames(archive);
    EXPECT_EQ(names.count("filesystem/#ProgramFiles#/MyApp/logs/"), static_cast<std::size_t>(1));
    EXPECT_EQ(names.count("filesystem/#ProgramFiles#/MyApp/logs/cache/"), static_cast<std::size_t>(1));
    EXPECT_EQ(ReadEntry(archive, "filesystem/#ProgramFiles#/MyApp/app.exe"), "EXE");
}

TEST(Unit_PackService, PackWritesTheWhiteoutMarkers)
{
    TempDir    temp;
    const auto my_app = temp.Get() / L"MyApp";
    MakeFile(my_app, L"app.exe", "EXE");

    appbox::PackModel     model;
    appbox::RegistryModel registry;
    std::string           error;
    ASSERT_TRUE(model.ImportFolder("program_files", my_app.wstring(), error)) << error;
    ASSERT_TRUE(model.AddStartupFile("program_files", L"MyApp", L"app.exe", true, error)) << error;

    /* An entry inside the import, one of the layer root and one below no folder of the view. */
    ASSERT_TRUE(model.AddWhiteout("program_files", L"MyApp\\bin", L"old.dll", error)) << error;
    ASSERT_TRUE(model.AddWhiteout("program_files", std::wstring(), L"legacy.exe", error)) << error;
    ASSERT_TRUE(model.AddWhiteout("user_profile", L"Missing\\deep", L"gone.txt", error)) << error;

    /* The markers are file entries of the archive, so they are part of the total. */
    EXPECT_EQ(appbox::ContentFileCount(model), static_cast<std::size_t>(4));

    const auto zip_path = temp.Get() / L"whiteouts.zip";
    ASSERT_EQ(appbox::Pack(model, registry, appbox::FilesystemIsolationModel(), appbox::NetworkModel(),
                           appbox::EnvironmentModel(), appbox::ApplicationMetadata(), appbox::StartupScreen{},
                           FakePayloads(), appbox::CompressionLevel::Normal, zip_path.wstring(), nullptr),
              "");

    ZipArchiveCloser closer(OpenArchive(zip_path.wstring()));
    zip_t*           archive = closer.archive;
    ASSERT_NE(archive, nullptr);

    const auto names = EntryNames(archive);
    EXPECT_EQ(names.count("app/filesystem/#ProgramFiles#/MyApp/bin/old.dll.$APPBOX_DELETE$"),
              static_cast<std::size_t>(1));
    EXPECT_EQ(names.count("app/filesystem/#ProgramFiles#/legacy.exe.$APPBOX_DELETE$"), static_cast<std::size_t>(1));

    /* The directory chain of a marker is written, so the layer holds the marker. */
    EXPECT_EQ(names.count("app/filesystem/#USERPROFILE#/Missing/deep/gone.txt.$APPBOX_DELETE$"),
              static_cast<std::size_t>(1));
    EXPECT_EQ(names.count("app/filesystem/#USERPROFILE#/Missing/deep/"), static_cast<std::size_t>(1));

    /* A marker is an empty file. */
    zip_stat_t stat = {};
    ASSERT_TRUE(EntryStat(archive, "app/filesystem/#ProgramFiles#/legacy.exe.$APPBOX_DELETE$", stat));
    EXPECT_EQ(stat.size, static_cast<zip_uint64_t>(0));
}

TEST(Unit_PackService, PackCountsTheWhiteoutMarkersInTheProgressTotal)
{
    TempDir    temp;
    const auto my_app = temp.Get() / L"MyApp";
    MakeFile(my_app, L"app.exe", "EXE");

    appbox::PackModel     model;
    appbox::RegistryModel registry;
    std::string           error;
    ASSERT_TRUE(model.ImportFolder("program_files", my_app.wstring(), error)) << error;
    ASSERT_TRUE(model.AddStartupFile("program_files", L"MyApp", L"app.exe", true, error)) << error;
    ASSERT_TRUE(model.AddWhiteout("program_files", L"MyApp", L"old.dll", error)) << error;

    const auto expected_total = appbox::kNonContentArchiveEntries + appbox::ContentFileCount(model);

    std::size_t seen_total = 0;
    std::size_t max_done = 0;
    const auto  progress = [&](const appbox::BuildProgress& report) {
        seen_total = report.total;
        max_done = report.done > max_done ? report.done : max_done;
        EXPECT_LE(report.done, report.total);
        return true;
    };

    const auto zip_path = temp.Get() / L"whiteouts-progress.zip";
    ASSERT_EQ(appbox::Pack(model, registry, appbox::FilesystemIsolationModel(), appbox::NetworkModel(),
                           appbox::EnvironmentModel(), appbox::ApplicationMetadata(), appbox::StartupScreen{},
                           FakePayloads(), appbox::CompressionLevel::Normal, zip_path.wstring(), progress),
              "");

    EXPECT_EQ(seen_total, expected_total);
    EXPECT_EQ(max_done, expected_total);
}

TEST(Unit_PackService, PatchWritesTheWhiteoutMarkers)
{
    TempDir temp;

    appbox::PackModel     model;
    appbox::RegistryModel registry;
    std::string           error;
    ASSERT_TRUE(model.AddWhiteout("program_files", L"MyApp\\bin", L"old.dll", error)) << error;

    const auto zip_path = temp.Get() / L"whiteouts-patch.zip";
    ASSERT_EQ(appbox::PackPatch(model, registry, appbox::FilesystemIsolationModel(), appbox::NetworkModel(),
                                appbox::EnvironmentModel(), appbox::CompressionLevel::Normal, zip_path.wstring(),
                                nullptr),
              "");

    ZipArchiveCloser closer(OpenArchive(zip_path.wstring()));
    zip_t*           archive = closer.archive;
    ASSERT_NE(archive, nullptr);

    /* A patch package roots its resources at the archive root. */
    const auto names = EntryNames(archive);
    EXPECT_EQ(names.count("filesystem/#ProgramFiles#/MyApp/bin/old.dll.$APPBOX_DELETE$"), static_cast<std::size_t>(1));
    EXPECT_EQ(names.count("filesystem/#ProgramFiles#/MyApp/bin/"), static_cast<std::size_t>(1));
}

TEST(Unit_PackService, PackWritesTheEntriesWithTheLevelOfTheSession)
{
    TempDir    temp;
    const auto my_app = temp.Get() / L"MyApp";
    const auto payload = CompressibleContent(32 * 1024);
    MakeFile(my_app, L"app.exe", payload);

    appbox::PackModel     model;
    appbox::RegistryModel registry;
    std::string           error;
    ASSERT_TRUE(model.ImportFolder("program_files", my_app.wstring(), error)) << error;
    ASSERT_TRUE(model.AddStartupFile("program_files", L"MyApp", L"app.exe", true, error)) << error;

    const auto entry = "app/filesystem/#ProgramFiles#/MyApp/app.exe";
    const auto pack = [&](appbox::CompressionLevel level, const std::wstring& zip_path) {
        return appbox::Pack(model, registry, appbox::FilesystemIsolationModel(), appbox::NetworkModel(),
                            appbox::EnvironmentModel(), appbox::ApplicationMetadata(), appbox::StartupScreen{},
                            FakePayloads(), level, zip_path, nullptr);
    };

    /* The level which stores the entries keeps the content of every one of them. */
    const auto stored_path = temp.Get() / L"stored.zip";
    ASSERT_EQ(pack(appbox::CompressionLevel::StoreOnly, stored_path.wstring()), "");

    ZipArchiveCloser stored_closer(OpenArchive(stored_path.wstring()));
    ASSERT_NE(stored_closer.archive, nullptr);

    zip_stat_t stored_stat = {};
    ASSERT_TRUE(EntryStat(stored_closer.archive, entry, stored_stat));
    EXPECT_EQ(stored_stat.comp_method, ZIP_CM_STORE);
    EXPECT_EQ(stored_stat.comp_size, static_cast<zip_uint64_t>(payload.size()));
    EXPECT_EQ(ReadEntry(stored_closer.archive, entry), payload);

    /* A level above it deflates the very same entry. */
    const auto deflated_path = temp.Get() / L"deflated.zip";
    ASSERT_EQ(pack(appbox::CompressionLevel::Normal, deflated_path.wstring()), "");

    ZipArchiveCloser deflated_closer(OpenArchive(deflated_path.wstring()));
    ASSERT_NE(deflated_closer.archive, nullptr);

    zip_stat_t deflated_stat = {};
    ASSERT_TRUE(EntryStat(deflated_closer.archive, entry, deflated_stat));
    EXPECT_EQ(deflated_stat.comp_method, ZIP_CM_DEFLATE);
    EXPECT_LT(deflated_stat.comp_size, static_cast<zip_uint64_t>(payload.size()));
    EXPECT_EQ(ReadEntry(deflated_closer.archive, entry), payload);
}

TEST(Unit_PackService, PatchWritesTheEntriesWithTheLevelOfTheSession)
{
    TempDir    temp;
    const auto my_app = temp.Get() / L"MyApp";
    const auto payload = CompressibleContent(32 * 1024);
    MakeFile(my_app, L"app.exe", payload);

    appbox::PackModel     model;
    appbox::RegistryModel registry;
    std::string           error;
    ASSERT_TRUE(model.ImportFolder("program_files", my_app.wstring(), error)) << error;

    const auto zip_path = temp.Get() / L"stored-patch.zip";
    ASSERT_EQ(appbox::PackPatch(model, registry, appbox::FilesystemIsolationModel(), appbox::NetworkModel(),
                                appbox::EnvironmentModel(), appbox::CompressionLevel::StoreOnly, zip_path.wstring(),
                                nullptr),
              "");

    ZipArchiveCloser closer(OpenArchive(zip_path.wstring()));
    zip_t*           archive = closer.archive;
    ASSERT_NE(archive, nullptr);

    /* A patch package follows the level of the session like a standalone archive. */
    const auto entry = "filesystem/#ProgramFiles#/MyApp/app.exe";
    zip_stat_t stat = {};
    ASSERT_TRUE(EntryStat(archive, entry, stat));
    EXPECT_EQ(stat.comp_method, ZIP_CM_STORE);
    EXPECT_EQ(stat.comp_size, static_cast<zip_uint64_t>(payload.size()));
    EXPECT_EQ(ReadEntry(archive, entry), payload);
}
