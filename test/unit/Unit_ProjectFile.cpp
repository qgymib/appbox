#include <gtest/gtest.h>
#include "src/core/ProjectFile.hpp"
#include <nlohmann/json.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

namespace
{

/**
 * @brief Generate a unique name fragment for temporary folders.
 * @return The unique fragment.
 */
std::wstring UniqueFragment()
{
    static unsigned counter = 0;
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
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
        path_ = base / (L"appbox-projectfile-" + UniqueFragment());
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

    /**
     * @brief Build a path inside the temporary folder.
     * @param[in] name File name.
     * @return The path of the file.
     */
    std::filesystem::path File(const std::wstring& name) const
    {
        return path_ / name;
    }

private:
    std::filesystem::path path_;
};

/**
 * @brief Write raw bytes to a file.
 * @param[in] path Destination file path.
 * @param[in] content Bytes to write.
 */
void WriteBytes(const std::filesystem::path& path, const std::string& content)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
}

/**
 * @brief Read the whole content of a file.
 * @param[in] path File path.
 * @return The file content.
 */
std::string ReadBytes(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

/**
 * @brief Build a model with one import, one imported file and a main program.
 * @param[out] model Model to fill.
 * @return true when every entry was accepted.
 */
bool BuildSampleModel(appbox::PackModel& model)
{
    std::string detail;
    return model.RestoreImportedFolder("program_files", L"MyApp", L"C:\\Program Files\\MyApp", detail)
           && model.RestoreImportedFile("program_files", L"MyApp\\data", L"settings.ini",
                                        L"C:\\tmp\\settings.ini", detail)
           && model.RestoreMainProgram("program_files", L"MyApp", L"bin\\app.exe", detail);
}

/*
 * The Chinese name U+6211 U+7684 U+5E94 U+7528, used to check that paths are
 * written as UTF-8 bytes and not as \uXXXX escapes. It is spelled with
 * universal character names so the test does not depend on the encoding the
 * compiler assumes for the source file.
 */
constexpr wchar_t kChineseName[] = L"\u6211\u7684\u5e94\u7528";

/* The same name as the UTF-8 byte sequence it has to appear as in the file. */
constexpr char kChineseNameUtf8[] = "\xE6\x88\x91\xE7\x9A\x84\xE5\xBA\x94\xE7\x94\xA8";

} // namespace

TEST(ProjectFile, RoundTripKeepsModelAndOutputPath)
{
    TempDir temp;
    const auto file = temp.File(L"project.json");

    appbox::PackModel saved;
    ASSERT_TRUE(BuildSampleModel(saved));
    const std::wstring output = std::wstring(L"D:\\out\\") + kChineseName + L".zip";

    std::string error;
    ASSERT_TRUE(appbox::SaveProject(saved, output, file.wstring(), error)) << error;

    appbox::PackModel loaded;
    std::wstring loaded_output;
    ASSERT_TRUE(appbox::LoadProject(file.wstring(), loaded, loaded_output, error)) << error;

    EXPECT_EQ(loaded_output, output);
    EXPECT_FALSE(loaded.IsEmpty());

    const auto imports = loaded.ImportsOf("program_files");
    ASSERT_EQ(imports.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(imports[0].import_name, L"MyApp");
    EXPECT_EQ(imports[0].source_path, L"C:\\Program Files\\MyApp");

    const auto files = loaded.AllImportedFiles();
    ASSERT_EQ(files.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(files[0].preset_id, "program_files");
    EXPECT_EQ(files[0].target_dir, L"MyApp\\data");
    EXPECT_EQ(files[0].file_name, L"settings.ini");
    EXPECT_EQ(files[0].source_path, L"C:\\tmp\\settings.ini");

    ASSERT_TRUE(loaded.HasMainProgram());
    EXPECT_EQ(loaded.MainProgramChoice().preset_id, "program_files");
    EXPECT_EQ(loaded.MainProgramChoice().import_name, L"MyApp");
    EXPECT_EQ(loaded.MainProgramChoice().relative_path, L"bin\\app.exe");
}

TEST(ProjectFile, ExportWritesStrictUtf8WithoutByteOrderMark)
{
    TempDir temp;
    const auto file = temp.File(L"project.json");

    appbox::PackModel model;
    std::string error;
    ASSERT_TRUE(model.RestoreImportedFolder("program_files", kChineseName,
                                            std::wstring(L"C:\\Program Files\\") + kChineseName,
                                            error))
        << error;

    ASSERT_TRUE(appbox::SaveProject(model, std::wstring(L"D:\\") + kChineseName + L".zip",
                                    file.wstring(), error))
        << error;

    const auto text = ReadBytes(file);
    ASSERT_FALSE(text.empty());

    /*
     * Strict UTF-8 means no byte order mark and no escaped code points: the
     * name has to appear as its raw UTF-8 bytes.
     */
    EXPECT_NE(text.compare(0, 3, "\xEF\xBB\xBF"), 0);
    EXPECT_EQ(text.find("\\u6211"), std::string::npos);
    EXPECT_NE(text.find(kChineseNameUtf8), std::string::npos);

    const auto root = nlohmann::json::parse(text);
    EXPECT_EQ(root.at("version").get<int>(), appbox::kProjectFileVersion);
    EXPECT_EQ(root.at("folders").at(0).at("name").get<std::string>(), kChineseNameUtf8);
}

TEST(ProjectFile, RoundTripOfEmptyConfiguration)
{
    TempDir temp;
    const auto file = temp.File(L"empty.json");

    appbox::PackModel empty;
    std::string error;
    ASSERT_TRUE(appbox::SaveProject(empty, L"", file.wstring(), error)) << error;

    appbox::PackModel loaded;
    std::wstring loaded_output = L"untouched";
    ASSERT_TRUE(appbox::LoadProject(file.wstring(), loaded, loaded_output, error)) << error;

    EXPECT_TRUE(loaded.IsEmpty());
    EXPECT_TRUE(loaded_output.empty());
}

TEST(ProjectFile, LoadKeepsSourcePathsWhichDoNotExist)
{
    TempDir temp;
    const auto file = temp.File(L"missing.json");

    appbox::PackModel saved;
    std::string error;
    ASSERT_TRUE(saved.RestoreImportedFolder("user_profile", L"Gone",
                                            L"C:\\appbox-no-such-folder\\Gone", error))
        << error;
    ASSERT_TRUE(saved.RestoreImportedFile("user_profile", L"Gone", L"tool.exe",
                                          L"C:\\appbox-no-such-folder\\tool.exe", error))
        << error;
    ASSERT_TRUE(appbox::SaveProject(saved, L"", file.wstring(), error)) << error;

    appbox::PackModel loaded;
    std::wstring loaded_output;
    ASSERT_TRUE(appbox::LoadProject(file.wstring(), loaded, loaded_output, error)) << error;

    const auto imports = loaded.ImportsOf("user_profile");
    ASSERT_EQ(imports.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(imports[0].source_path, L"C:\\appbox-no-such-folder\\Gone");
    EXPECT_EQ(loaded.AllImportedFiles().size(), static_cast<std::size_t>(1));
}

TEST(ProjectFile, LoadReplacesTheExistingConfiguration)
{
    TempDir temp;
    const auto file = temp.File(L"replace.json");

    appbox::PackModel saved;
    ASSERT_TRUE(BuildSampleModel(saved));

    std::string error;
    ASSERT_TRUE(appbox::SaveProject(saved, L"D:\\out\\MyApp.zip", file.wstring(), error)) << error;

    /* The target starts with a different, unrelated configuration. */
    appbox::PackModel loaded;
    ASSERT_TRUE(loaded.RestoreImportedFolder("user_profile", L"Other", L"C:\\Other", error)) << error;
    ASSERT_TRUE(loaded.RestoreImportedFile("user_profile", L"Other", L"other.exe", L"C:\\other.exe",
                                           error))
        << error;
    ASSERT_TRUE(loaded.RestoreMainProgram("user_profile", L"Other", L"other.exe", error)) << error;

    std::wstring loaded_output;
    ASSERT_TRUE(appbox::LoadProject(file.wstring(), loaded, loaded_output, error)) << error;

    /* Loading twice must not append either. */
    ASSERT_TRUE(appbox::LoadProject(file.wstring(), loaded, loaded_output, error)) << error;

    EXPECT_EQ(loaded.ImportsOf("user_profile").size(), static_cast<std::size_t>(0));
    EXPECT_EQ(loaded.ImportsOf("program_files").size(), static_cast<std::size_t>(1));
    EXPECT_EQ(loaded.AllImportedFiles().size(), static_cast<std::size_t>(1));
    EXPECT_EQ(loaded.MainProgramChoice().import_name, L"MyApp");
    EXPECT_EQ(loaded_output, L"D:\\out\\MyApp.zip");
}

TEST(ProjectFile, LoadKeepsTheModelUntouchedOnFailure)
{
    TempDir temp;

    appbox::PackModel model;
    ASSERT_TRUE(BuildSampleModel(model));
    const std::wstring output = L"D:\\out\\MyApp.zip";

    const auto broken = temp.File(L"broken.json");
    WriteBytes(broken, "{ \"version\": 1, \"folders\": [ { \"preset\": \"program_files\" } ] }");

    std::wstring loaded_output = output;
    std::string error;
    EXPECT_FALSE(appbox::LoadProject(broken.wstring(), model, loaded_output, error));
    EXPECT_FALSE(error.empty());

    EXPECT_EQ(model.ImportsOf("program_files").size(), static_cast<std::size_t>(1));
    EXPECT_EQ(model.AllImportedFiles().size(), static_cast<std::size_t>(1));
    EXPECT_TRUE(model.HasMainProgram());
    EXPECT_EQ(loaded_output, output);
}

TEST(ProjectFile, SaveAndLoadRejectAnEmptyPath)
{
    appbox::PackModel model;
    std::string error;
    EXPECT_FALSE(appbox::SaveProject(model, L"", L"", error));
    EXPECT_FALSE(error.empty());

    std::wstring output;
    EXPECT_FALSE(appbox::LoadProject(L"", model, output, error));
    EXPECT_FALSE(error.empty());
}

TEST(ProjectFile, LoadRejectsAMissingFile)
{
    TempDir temp;
    const auto missing = temp.File(L"does-not-exist.json");

    appbox::PackModel model;
    std::wstring output;
    std::string error;
    EXPECT_FALSE(appbox::LoadProject(missing.wstring(), model, output, error));
    EXPECT_NE(error.find("cannot open"), std::string::npos);
    EXPECT_TRUE(model.IsEmpty());
}

TEST(ProjectFile, LoadRejectsAnEmptyFile)
{
    TempDir temp;
    const auto file = temp.File(L"empty.json");
    WriteBytes(file, "");

    appbox::PackModel model;
    std::wstring output;
    std::string error;
    EXPECT_FALSE(appbox::LoadProject(file.wstring(), model, output, error));
    EXPECT_NE(error.find("empty"), std::string::npos);
}

TEST(ProjectFile, LoadRejectsInvalidJson)
{
    TempDir temp;
    const auto file = temp.File(L"invalid.json");
    WriteBytes(file, "{ \"version\": 1, \"folders\": [ }");

    appbox::PackModel model;
    std::wstring output;
    std::string error;
    EXPECT_FALSE(appbox::LoadProject(file.wstring(), model, output, error));
    EXPECT_NE(error.find("not valid JSON"), std::string::npos);
    EXPECT_TRUE(model.IsEmpty());
}

TEST(ProjectFile, LoadRejectsANonObjectDocument)
{
    TempDir temp;
    const auto file = temp.File(L"array.json");
    WriteBytes(file, "[1, 2, 3]");

    appbox::PackModel model;
    std::wstring output;
    std::string error;
    EXPECT_FALSE(appbox::LoadProject(file.wstring(), model, output, error));
    EXPECT_NE(error.find("JSON object"), std::string::npos);
}

TEST(ProjectFile, LoadRejectsUtf16AndUtf32Content)
{
    TempDir temp;

    const auto utf16 = temp.File(L"utf16.json");
    WriteBytes(utf16, std::string("\xFF\xFE", 2) + std::string("{\0}\0", 4));

    const auto utf32 = temp.File(L"utf32.json");
    WriteBytes(utf32, std::string("\xFF\xFE\x00\x00", 4) + std::string("{\0\0\0}\0\0\0", 8));

    appbox::PackModel model;
    std::wstring output;
    std::string error;
    EXPECT_FALSE(appbox::LoadProject(utf16.wstring(), model, output, error));
    EXPECT_NE(error.find("UTF-16"), std::string::npos);

    EXPECT_FALSE(appbox::LoadProject(utf32.wstring(), model, output, error));
    EXPECT_NE(error.find("UTF-32"), std::string::npos);
}

TEST(ProjectFile, LoadRejectsBytesWhichAreNotUtf8)
{
    TempDir temp;
    const auto file = temp.File(L"ansi.json");

    /*
     * 0xB0 0xA1 is a two byte character of the local code page; 0xB0 is below
     * the range of a UTF-8 lead byte, so the text is not valid UTF-8 even
     * though it forms valid JSON.
     */
    WriteBytes(file, "{ \"version\": 1, \"output_path\": \"\xB0\xA1\" }");

    appbox::PackModel model;
    std::wstring output;
    std::string error;
    EXPECT_FALSE(appbox::LoadProject(file.wstring(), model, output, error));
    EXPECT_NE(error.find("UTF-8"), std::string::npos);
}

TEST(ProjectFile, LoadAcceptsAUtf8ByteOrderMark)
{
    TempDir temp;
    const auto file = temp.File(L"bom.json");
    WriteBytes(file, "\xEF\xBB\xBF{ \"version\": 1, \"output_path\": \"D:\\\\out\\\\a.zip\" }");

    appbox::PackModel model;
    std::wstring output;
    std::string error;
    ASSERT_TRUE(appbox::LoadProject(file.wstring(), model, output, error)) << error;
    EXPECT_EQ(output, L"D:\\out\\a.zip");
}

TEST(ProjectFile, LoadRejectsAnUnsupportedVersion)
{
    TempDir temp;

    const auto future = temp.File(L"future.json");
    WriteBytes(future, "{ \"version\": 2 }");

    const auto missing = temp.File(L"missing.json");
    WriteBytes(missing, "{ \"output_path\": \"D:\\\\out\\\\a.zip\" }");

    const auto text = temp.File(L"text.json");
    WriteBytes(text, "{ \"version\": \"1\" }");

    appbox::PackModel model;
    std::wstring output;
    std::string error;

    EXPECT_FALSE(appbox::LoadProject(future.wstring(), model, output, error));
    EXPECT_NE(error.find("unsupported project file version 2"), std::string::npos);

    EXPECT_FALSE(appbox::LoadProject(missing.wstring(), model, output, error));
    EXPECT_NE(error.find("version"), std::string::npos);

    EXPECT_FALSE(appbox::LoadProject(text.wstring(), model, output, error));
    EXPECT_FALSE(error.empty());
}

TEST(ProjectFile, LoadRejectsUnknownPresetDirectories)
{
    TempDir temp;
    const auto file = temp.File(L"preset.json");
    WriteBytes(file, "{ \"version\": 1, \"folders\": [ { \"preset\": \"does_not_exist\", "
                     "\"name\": \"MyApp\", \"source\": \"C:\\\\MyApp\" } ] }");

    appbox::PackModel model;
    std::wstring output;
    std::string error;
    EXPECT_FALSE(appbox::LoadProject(file.wstring(), model, output, error));
    EXPECT_NE(error.find("folders[0]"), std::string::npos);
    EXPECT_NE(error.find("unknown preset directory"), std::string::npos);
    EXPECT_TRUE(model.IsEmpty());
}

TEST(ProjectFile, LoadRejectsMalformedMembers)
{
    TempDir temp;

    const auto folders = temp.File(L"folders.json");
    WriteBytes(folders, "{ \"version\": 1, \"folders\": {} }");

    const auto entry = temp.File(L"entry.json");
    WriteBytes(entry, "{ \"version\": 1, \"folders\": [ { \"preset\": \"program_files\", "
                      "\"name\": \"MyApp\" } ] }");

    const auto files = temp.File(L"files.json");
    WriteBytes(files, "{ \"version\": 1, \"files\": [ \"MyApp\" ] }");

    const auto program = temp.File(L"program.json");
    WriteBytes(program, "{ \"version\": 1, \"main_program\": 7 }");

    const auto duplicates = temp.File(L"duplicates.json");
    WriteBytes(duplicates,
               "{ \"version\": 1, \"folders\": [ "
               "{ \"preset\": \"program_files\", \"name\": \"MyApp\", \"source\": \"C:\\\\MyApp\" }, "
               "{ \"preset\": \"program_files\", \"name\": \"myapp\", \"source\": \"C:\\\\Other\" } ] }");

    appbox::PackModel model;
    std::wstring output;
    std::string error;

    EXPECT_FALSE(appbox::LoadProject(folders.wstring(), model, output, error));
    EXPECT_NE(error.find("not an array"), std::string::npos);

    EXPECT_FALSE(appbox::LoadProject(entry.wstring(), model, output, error));
    EXPECT_NE(error.find("'source' member"), std::string::npos);

    EXPECT_FALSE(appbox::LoadProject(files.wstring(), model, output, error));
    EXPECT_NE(error.find("files[0]"), std::string::npos);

    EXPECT_FALSE(appbox::LoadProject(program.wstring(), model, output, error));
    EXPECT_NE(error.find("main_program"), std::string::npos);

    EXPECT_FALSE(appbox::LoadProject(duplicates.wstring(), model, output, error));
    EXPECT_NE(error.find("folders[1]"), std::string::npos);
    EXPECT_TRUE(model.IsEmpty());
}

TEST(ProjectFile, LoadRejectsAFileOutsideAnImportedFolder)
{
    TempDir temp;
    const auto file = temp.File(L"target.json");
    WriteBytes(file, "{ \"version\": 1, \"folders\": [ { \"preset\": \"program_files\", "
                     "\"name\": \"MyApp\", \"source\": \"C:\\\\MyApp\" } ], "
                     "\"files\": [ { \"preset\": \"program_files\", \"target_dir\": \"Other\", "
                     "\"name\": \"tool.exe\", \"source\": \"C:\\\\tool.exe\" } ] }");

    appbox::PackModel model;
    std::wstring output;
    std::string error;
    EXPECT_FALSE(appbox::LoadProject(file.wstring(), model, output, error));
    EXPECT_NE(error.find("not an imported folder"), std::string::npos);
    EXPECT_TRUE(model.IsEmpty());
}

TEST(ProjectFile, RoundTripKeepsTheVirtualRegistry)
{
    TempDir temp;
    const auto file = temp.File(L"registry.json");

    appbox::PackModel model;
    ASSERT_TRUE(BuildSampleModel(model));

    appbox::RegistryModel registry;
    std::string           error;
    ASSERT_TRUE(registry.EnsureKey(L"HKEY_CURRENT_USER\\Software\\Vendor\\Deep", error)) << error;
    ASSERT_TRUE(registry.EnsureKey(L"HKEY_LOCAL_MACHINE\\Software\\AppBox", error)) << error;
    ASSERT_TRUE(registry.SetValue(L"HKEY_CURRENT_USER\\Software\\Vendor", L"Server",
                                  appbox::RegistryValueType::String, appbox::RegistryStringData(L"host"),
                                  error))
        << error;
    ASSERT_TRUE(registry.SetValue(L"HKEY_CURRENT_USER\\Software\\Vendor", L"Count",
                                  appbox::RegistryValueType::Dword, appbox::RegistryDwordData(42), error))
        << error;
    ASSERT_TRUE(registry.SetValue(L"HKEY_LOCAL_MACHINE\\Software\\AppBox", L"", appbox::RegistryValueType::None,
                                  { 0x01, 0x02, 0x03 }, error))
        << error;

    ASSERT_TRUE(registry.SetKeyIsolation(L"HKEY_CURRENT_USER\\Software\\Vendor", appbox::RegistryIsolation::Full));
    ASSERT_TRUE(registry.SetValueIsolation(L"HKEY_CURRENT_USER\\Software\\Vendor", L"Server",
                                           appbox::RegistryIsolation::Hide));

    ASSERT_TRUE(appbox::SaveProject(model, registry, L"D:\\out\\app.zip", file.wstring(), error)) << error;

    appbox::PackModel     loaded;
    appbox::RegistryModel loaded_registry;
    std::wstring          loaded_output;
    ASSERT_TRUE(appbox::LoadProject(file.wstring(), loaded, loaded_registry, loaded_output, error)) << error;

    EXPECT_EQ(loaded_output, L"D:\\out\\app.zip");
    EXPECT_FALSE(loaded.IsEmpty());

    /* The key tree and the modes come back unchanged. */
    const auto* vendor = loaded_registry.FindKey(L"HKEY_CURRENT_USER\\Software\\Vendor");
    ASSERT_NE(vendor, nullptr);
    EXPECT_EQ(vendor->isolation, appbox::RegistryIsolation::Full);
    EXPECT_NE(loaded_registry.FindKey(L"HKEY_CURRENT_USER\\Software\\Vendor\\Deep"), nullptr);
    EXPECT_NE(loaded_registry.FindKey(L"HKEY_LOCAL_MACHINE\\Software\\AppBox"), nullptr);

    /* The sub key comes first, then the values in name order. */
    const auto rows = loaded_registry.Rows(L"HKEY_CURRENT_USER\\Software\\Vendor");
    ASSERT_EQ(rows.size(), static_cast<std::size_t>(3));

    EXPECT_EQ(rows[0].kind, appbox::RegistryRow::Kind::Key);
    EXPECT_EQ(rows[0].name, L"Deep");
    /* The sub key keeps the mode it was created with: the change of the key
     * above it never reached it. */
    EXPECT_EQ(rows[0].isolation, appbox::RegistryIsolation::WriteCopy);

    EXPECT_EQ(rows[1].name, L"Count");
    EXPECT_EQ(rows[1].type, appbox::RegistryValueType::Dword);
    EXPECT_EQ(rows[1].isolation, appbox::RegistryIsolation::WriteCopy);
    uint32_t count = 0;
    ASSERT_TRUE(appbox::RegistryDwordValue(rows[1].data, count));
    EXPECT_EQ(count, 42u);

    EXPECT_EQ(rows[2].name, L"Server");
    EXPECT_EQ(rows[2].type, appbox::RegistryValueType::String);
    EXPECT_EQ(rows[2].isolation, appbox::RegistryIsolation::Hide);
    EXPECT_EQ(appbox::RegistryStringValue(rows[2].data), L"host");

    /* The default value of another root keeps its type and its bytes. */
    const auto appbox_rows = loaded_registry.Rows(L"HKEY_LOCAL_MACHINE\\Software\\AppBox");
    ASSERT_EQ(appbox_rows.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(appbox_rows[0].name, L"");
    EXPECT_EQ(appbox_rows[0].type, appbox::RegistryValueType::None);
    EXPECT_EQ(appbox_rows[0].data, (std::vector<std::uint8_t>{ 0x01, 0x02, 0x03 }));
}

TEST(ProjectFile, LoadWithoutARegistryMemberRestoresAnEmptyRegistry)
{
    TempDir temp;
    const auto file = temp.File(L"legacy.json");
    WriteBytes(file, "{ \"version\": 1, \"output_path\": \"D:\\\\out\\\\a.zip\" }");

    std::string error;
    appbox::RegistryModel registry;
    ASSERT_TRUE(registry.EnsureKey(L"HKEY_CURRENT_USER\\Software\\Old", error)) << error;

    appbox::PackModel model;
    std::wstring      output;
    ASSERT_TRUE(appbox::LoadProject(file.wstring(), model, registry, output, error)) << error;

    /* The file does not describe a registry, so the workspace starts empty. */
    EXPECT_EQ(registry.Root().children.size(), static_cast<std::size_t>(5));
    EXPECT_EQ(registry.FindKey(L"HKEY_CURRENT_USER\\Software\\Old"), nullptr);
}

TEST(ProjectFile, LoadIgnoresTheIsolationSetMemberOfAnOlderFile)
{
    TempDir temp;
    const auto file = temp.File(L"isolation-set.json");
    WriteBytes(file, "{ \"version\": 1, \"registry\": { \"keys\": [ { \"name\": \"HKEY_CURRENT_USER\", "
                      "\"isolation\": \"write_copy\", \"isolation_set\": false, \"values\": [ { \"name\": "
                      "\"Server\", \"type\": \"REG_SZ\", \"data\": \"\", \"isolation\": \"hide\", "
                      "\"isolation_set\": true } ], \"children\": [ { \"name\": \"Deep\", "
                      "\"isolation\": \"full\", \"isolation_set\": false } ] } ] } }");

    appbox::PackModel     model;
    appbox::RegistryModel registry;
    std::wstring          output;
    std::string           error;
    ASSERT_TRUE(appbox::LoadProject(file.wstring(), model, registry, output, error)) << error;

    /*
     * The member does not name a mode of its own, so every mode of the file is
     * read as it is stored.
     */
    const auto* current_user = registry.FindKey(L"HKEY_CURRENT_USER");
    ASSERT_NE(current_user, nullptr);
    EXPECT_EQ(current_user->isolation, appbox::RegistryIsolation::WriteCopy);

    const auto* deep = registry.FindKey(L"HKEY_CURRENT_USER\\Deep");
    ASSERT_NE(deep, nullptr);
    EXPECT_EQ(deep->isolation, appbox::RegistryIsolation::Full);

    ASSERT_EQ(current_user->values.size(), 1u);
    EXPECT_EQ(current_user->values[0].name, L"Server");
    EXPECT_EQ(current_user->values[0].isolation, appbox::RegistryIsolation::Hide);
}

TEST(ProjectFile, LoadRejectsABrokenRegistry)
{
    TempDir temp;

    const auto broken = temp.File(L"broken-registry.json");
    WriteBytes(broken, "{ \"version\": 1, \"registry\": { \"keys\": [ { \"name\": \"HKEY_CURRENT_USER\", "
                       "\"isolation\": \"sandbox\" } ] } }");

    const auto unknown_root = temp.File(L"unknown-root.json");
    WriteBytes(unknown_root, "{ \"version\": 1, \"registry\": { \"keys\": [ { \"name\": \"HKEY_OTHER\", "
                             "\"isolation\": \"full\" } ] } }");

    const auto bad_value = temp.File(L"bad-value.json");
    WriteBytes(bad_value, "{ \"version\": 1, \"registry\": { \"keys\": [ { \"name\": \"HKEY_CURRENT_USER\", "
                          "\"isolation\": \"full\", \"values\": [ { \"name\": \"Server\", "
                          "\"type\": \"REG_SOMETHING\", \"data\": \"\", \"isolation\": \"full\" } ] } ] } }");

    appbox::PackModel model;
    ASSERT_TRUE(BuildSampleModel(model));

    std::string error;
    appbox::RegistryModel registry;
    ASSERT_TRUE(registry.EnsureKey(L"HKEY_CURRENT_USER\\Software\\Keep", error)) << error;

    std::wstring output;

    EXPECT_FALSE(appbox::LoadProject(broken.wstring(), model, registry, output, error));
    EXPECT_NE(error.find("unknown isolation mode"), std::string::npos);

    EXPECT_FALSE(appbox::LoadProject(unknown_root.wstring(), model, registry, output, error));
    EXPECT_NE(error.find("unknown root key"), std::string::npos);

    EXPECT_FALSE(appbox::LoadProject(bad_value.wstring(), model, registry, output, error));
    EXPECT_NE(error.find("unknown value type"), std::string::npos);

    /* A rejected file leaves both models untouched. */
    EXPECT_FALSE(model.IsEmpty());
    EXPECT_NE(registry.FindKey(L"HKEY_CURRENT_USER\\Software\\Keep"), nullptr);
}
