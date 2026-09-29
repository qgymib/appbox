#include <gtest/gtest.h>
#include "src/core/PackService.hpp"
#include "WString.hpp"
#include "Config.hpp"
#include <nlohmann/json.hpp>
#include <zip.h>
#include <chrono>
#include <filesystem>
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
                     appbox::EnvironmentModel(), "LOADER", 6, (temp.Get() / L"out.zip").wstring(), nullptr);
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
                     appbox::EnvironmentModel(), "LOADER", 6, (temp.Get() / L"out.zip").wstring(), nullptr);
    EXPECT_NE(result.find("start automatically"), std::string::npos);
}

TEST(Unit_PackService, PackRequiresLoaderBytes)
{
    TempDir temp;
    MakeFile(temp.Get(), L"app.exe", "EXE");

    appbox::PackModel     model;
    appbox::RegistryModel registry;
    std::string           error;
    ASSERT_TRUE(model.ImportFolder("program_files", temp.Get().wstring(), error)) << error;
    ASSERT_TRUE(model.AddStartupFile("program_files", temp.Get().filename().wstring(), L"app.exe", true, error))
        << error;

    const auto result =
        appbox::Pack(model, registry, appbox::FilesystemIsolationModel(), appbox::NetworkModel(),
                     appbox::EnvironmentModel(), nullptr, 0, (temp.Get() / L"out.zip").wstring(), nullptr);
    EXPECT_NE(result.find("loader payload"), std::string::npos);
}

TEST(Unit_PackService, PackProducesLoaderConfigurationAndLayers)
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
                                     appbox::EnvironmentModel(), "FAKE-LOADER", 11, zip_path.wstring(), nullptr);
    EXPECT_EQ(result, "") << result;

    ZipArchiveCloser closer(OpenArchive(zip_path.wstring()));
    zip_t*           archive = closer.archive;
    ASSERT_NE(archive, nullptr);

    /* The loader payload is embedded under the name of the main program. */
    EXPECT_EQ(appbox::LoaderEntryName(model), L"app.exe");
    EXPECT_EQ(ReadEntry(archive, "app.exe"), "FAKE-LOADER");

    /* The archive carries a single naming, the loader name is gone. */
    EXPECT_EQ(zip_name_locate(archive, "AppBoxLoader.exe", 0), -1);
    EXPECT_EQ(zip_name_locate(archive, "AppBoxLoader.json", 0), -1);

    /*
     * The configuration carries the startup files only: the layout of the archive
     * is the fixed convention of `common/SandboxLayout.hpp`, so the file names
     * neither the resources nor the state of the sandbox.
     */
    const auto json_text = ReadEntry(archive, "app.exe.json");
    ASSERT_FALSE(json_text.empty());
    const auto document = nlohmann::json::parse(json_text);
    EXPECT_FALSE(document.contains("base_fs"));
    EXPECT_FALSE(document.contains("overlay_fs"));
    const auto config = document.get<appbox::LoaderConfig>();
    ASSERT_EQ(config.startups.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(config.startups[0].trigger, "app");
    EXPECT_TRUE(config.startups[0].auto_start);
    EXPECT_EQ(config.startups[0].executable, "#ProgramFiles#\\MyApp\\app.exe");
    EXPECT_TRUE(config.startups[0].arguments.empty());

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
     * The state of the sandbox never travels in the archive: the loader creates
     * it at run time, so deleting it resets the sandbox to the packed state.
     */
    for (const auto& name : names)
    {
        EXPECT_EQ(name.rfind("data/", 0), std::string::npos) << name;
    }
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
    ASSERT_TRUE(registry.SetKeyIsolation(L"HKEY_CURRENT_USER\\Software\\AppBox", appbox::RegistryIsolation::Full));

    const auto zip_path = temp.Get().parent_path() / (temp.Get().filename().wstring() + L"-registry.zip");
    const auto result = appbox::Pack(model, registry, appbox::FilesystemIsolationModel(), appbox::NetworkModel(),
                                     appbox::EnvironmentModel(), "FAKE", 4, zip_path.wstring(), nullptr);
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

    /* The value keeps the mode of the key it was created in. */
    const auto& values = document["values"];
    ASSERT_EQ(values.size(), 1u);
    EXPECT_EQ(values[0]["path"].get<std::string>(), "HKEY_CURRENT_USER\\Software\\AppBox");
    EXPECT_EQ(values[0]["name"].get<std::string>(), "Mode");
    EXPECT_EQ(values[0]["isolation"].get<std::string>(), "write_copy");
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

    const auto zip_path = temp.Get().parent_path() / (temp.Get().filename().wstring() + L"-fs-isolation.zip");
    const auto result = appbox::Pack(model, registry, isolation, appbox::NetworkModel(), appbox::EnvironmentModel(),
                                     "FAKE", 4, zip_path.wstring(), nullptr);
    EXPECT_EQ(result, "") << result;

    ZipArchiveCloser closer(OpenArchive(zip_path.wstring()));
    zip_t*           archive = closer.archive;
    ASSERT_NE(archive, nullptr);

    /*
     * The modes travel in the filesystem domain of the resources, next to the
     * layers they describe: the loader skips the file while it enumerates the
     * layers of that folder.
     */
    const auto text = ReadEntry(archive, "app/filesystem/isolation.json");
    ASSERT_FALSE(text.empty());

    const auto document = nlohmann::json::parse(text);
    EXPECT_EQ(document["version"].get<int>(), 1);

    const auto& entries = document["entries"];
    ASSERT_EQ(entries.size(), 2u);
    EXPECT_EQ(entries[0]["path"].get<std::string>(), "#ProgramFiles#\\MyApp");
    EXPECT_EQ(entries[0]["kind"].get<std::string>(), "directory");
    EXPECT_EQ(entries[0]["isolation"].get<std::string>(), "full");
    EXPECT_EQ(entries[1]["path"].get<std::string>(), "#ProgramFiles#\\MyApp\\app.exe");
    EXPECT_EQ(entries[1]["kind"].get<std::string>(), "file");
    EXPECT_EQ(entries[1]["isolation"].get<std::string>(), "whiteout");
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
                                     appbox::EnvironmentModel(), "FAKE", 4, zip_path.wstring(), nullptr);
    EXPECT_EQ(result, "") << result;

    ZipArchiveCloser closer(OpenArchive(zip_path.wstring()));
    zip_t*           archive = closer.archive;
    ASSERT_NE(archive, nullptr);

    /*
     * The redirections travel in the network domain of the resources, which is
     * where the loader looks for them.
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
                                     environment, "FAKE", 4, zip_path.wstring(), nullptr);
    EXPECT_EQ(result, "") << result;

    ZipArchiveCloser closer(OpenArchive(zip_path.wstring()));
    zip_t*           archive = closer.archive;
    ASSERT_NE(archive, nullptr);

    /*
     * The variables travel in the environment domain of the resources, which is
     * where the loader looks for them.
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
                                     appbox::EnvironmentModel(), "FAKE", 4, zip_path.wstring(), nullptr);
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

TEST(Unit_PackService, LoaderEntryNameIsEmptyWithoutAStartupFile)
{
    appbox::PackModel     model;
    appbox::RegistryModel registry;

    EXPECT_TRUE(appbox::LoaderEntryName(model).empty());
}

TEST(Unit_PackService, LoaderEntryNameDropsTheDirectoryOfTheStartupFile)
{
    TempDir    temp;
    const auto my_app = temp.Get() / L"MyApp";
    MakeFile(my_app, L"bin\\tool.exe", "EXE");

    appbox::PackModel     model;
    appbox::RegistryModel registry;
    std::string           error;
    ASSERT_TRUE(model.ImportFolder("program_files", my_app.wstring(), error)) << error;
    ASSERT_TRUE(model.AddStartupFile("program_files", L"MyApp", L"bin\\tool.exe", true, error)) << error;

    /* Only the file name is used: the loader lives in the archive root. */
    EXPECT_EQ(appbox::LoaderEntryName(model), L"tool.exe");

    const auto zip_path = temp.Get().parent_path() / (temp.Get().filename().wstring() + L"-entry.zip");
    const auto result = appbox::Pack(model, registry, appbox::FilesystemIsolationModel(), appbox::NetworkModel(),
                                     appbox::EnvironmentModel(), "FAKE-LOADER", 11, zip_path.wstring(), nullptr);
    EXPECT_EQ(result, "") << result;

    ZipArchiveCloser closer(OpenArchive(zip_path.wstring()));
    zip_t*           archive = closer.archive;
    ASSERT_NE(archive, nullptr);

    EXPECT_EQ(ReadEntry(archive, "tool.exe"), "FAKE-LOADER");

    const auto json_text = ReadEntry(archive, "tool.exe.json");
    ASSERT_FALSE(json_text.empty());
    const auto config = nlohmann::json::parse(json_text).get<appbox::LoaderConfig>();
    ASSERT_EQ(config.startups.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(config.startups[0].executable, "#ProgramFiles#\\MyApp\\bin\\tool.exe");

    /* The entry program itself keeps its place below the layer tree. */
    EXPECT_EQ(ReadEntry(archive, "app/filesystem/#ProgramFiles#/MyApp/bin/tool.exe"), "EXE");
}

TEST(Unit_PackService, LoaderEntryNameFollowsTheFirstStartupFile)
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

    EXPECT_EQ(appbox::LoaderEntryName(model), L"second.exe");

    const auto zip_path = temp.Get().parent_path() / (temp.Get().filename().wstring() + L"-order.zip");
    const auto result = appbox::Pack(model, registry, appbox::FilesystemIsolationModel(), appbox::NetworkModel(),
                                     appbox::EnvironmentModel(), "FAKE-LOADER", 11, zip_path.wstring(), nullptr);
    EXPECT_EQ(result, "") << result;

    ZipArchiveCloser closer(OpenArchive(zip_path.wstring()));
    zip_t*           archive = closer.archive;
    ASSERT_NE(archive, nullptr);

    const auto json_text = ReadEntry(archive, "second.exe.json");
    ASSERT_FALSE(json_text.empty());
    const auto config = nlohmann::json::parse(json_text).get<appbox::LoaderConfig>();

    /* Every startup file is written in the order of the model. */
    ASSERT_EQ(config.startups.size(), static_cast<std::size_t>(2));
    EXPECT_EQ(config.startups[0].trigger, "second");
    EXPECT_TRUE(config.startups[0].auto_start);
    EXPECT_EQ(config.startups[0].executable, "#ProgramFiles#\\MyApp\\second.exe");
    EXPECT_EQ(config.startups[1].trigger, "first");
    EXPECT_FALSE(config.startups[1].auto_start);
    EXPECT_EQ(config.startups[1].executable, "#ProgramFiles#\\MyApp\\first.exe");
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
                                     appbox::EnvironmentModel(), "FAKE", 4, zip_path.wstring(), progress);
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
                                     appbox::EnvironmentModel(), "FAKE", 4, zip_path.wstring(),
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
                                     appbox::EnvironmentModel(), "FAKE-LOADER", 11, zip_path.wstring(), nullptr);
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
                                     appbox::EnvironmentModel(), "FAKE", 4, zip_path.wstring(), nullptr);
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
                                     appbox::EnvironmentModel(), "FAKE", 4, zip_path.wstring(), progress);
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
