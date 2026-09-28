#include <gtest/gtest.h>
#include "src/core/ProjectDocument.hpp"
#include <nlohmann/json.hpp>
#include <cstdint>
#include <exception>
#include <string>
#include <vector>

namespace
{

/*
 * The Chinese name U+6211 U+7684 U+5E94 U+7528, used to check that paths are
 * written as UTF-8 bytes and not as \uXXXX escapes. It is spelled with
 * universal character names so the test does not depend on the encoding the
 * compiler assumes for the source file.
 */
constexpr wchar_t kChineseName[] = L"\u6211\u7684\u5e94\u7528";

/* The same name as the UTF-8 byte sequence it has to appear as in the file. */
constexpr char kChineseNameUtf8[] = "\xE6\x88\x91\xE7\x9A\x84\xE5\xBA\x94\xE7\x94\xA8";

/**
 * @brief Decode a document from the text of a project file.
 * @param[in] text JSON text.
 * @return The decoded document.
 */
appbox::ProjectDocument ParseDocument(const std::string& text)
{
    return nlohmann::ordered_json::parse(text).get<appbox::ProjectDocument>();
}

/**
 * @brief Read the error of a document which is expected to be rejected.
 * @param[in] text JSON text.
 * @return The description of the failure, empty when the text was accepted.
 */
std::string ParseError(const std::string& text)
{
    try
    {
        (void)ParseDocument(text);
    }
    catch (const std::exception& error)
    {
        return error.what();
    }

    return {};
}

/**
 * @brief Build a document which holds every member of the schema.
 * @return The document.
 */
appbox::ProjectDocument BuildSampleDocument()
{
    appbox::ProjectDocument document;
    document.output_path = L"D:\\out\\MyApp.zip";

    appbox::ProjectFolderRecord folder;
    folder.preset_id = "program_files";
    folder.name = L"MyApp";
    folder.source_path = L"C:\\Program Files\\MyApp";
    document.folders.push_back(folder);

    appbox::ProjectFileRecord file;
    file.preset_id = "user_profile";
    file.target_dir = L"MyApp\\data";
    file.name = L"settings.ini";
    file.source_path = L"C:\\tmp\\settings.ini";
    document.files.push_back(file);

    appbox::ProjectStartupRecord startup;
    startup.preset_id = "program_files";
    startup.folder = L"MyApp";
    startup.relative_path = L"bin\\app.exe";
    startup.trigger = L"app";
    startup.auto_start = true;
    document.startup_files.push_back(startup);

    appbox::ProjectRegistryKeyRecord root;
    root.name = L"HKEY_CURRENT_USER";
    root.isolation = appbox::RegistryIsolation::Full;

    appbox::ProjectRegistryValueRecord text_value;
    text_value.name = L"Server";
    text_value.type = appbox::RegistryValueType::String;
    text_value.data = appbox::RegistryStringData(L"host");
    text_value.isolation = appbox::RegistryIsolation::Hide;
    root.values.push_back(text_value);

    appbox::ProjectRegistryValueRecord binary_value;
    binary_value.name = L"";
    binary_value.type = appbox::RegistryValueType::None;
    binary_value.data = { 0x01, 0x02, 0xFF };
    binary_value.isolation = appbox::RegistryIsolation::WriteCopy;
    root.values.push_back(binary_value);

    appbox::ProjectRegistryKeyRecord child;
    child.name = L"Software";
    child.isolation = appbox::RegistryIsolation::WriteCopy;
    root.children.push_back(child);

    document.registry.push_back(root);

    appbox::ProjectFilesystemRecord entry;
    entry.path = L"#ProgramFiles#\\MyApp\\app.exe";
    entry.kind = appbox::FilesystemEntryKind::File;
    entry.isolation = appbox::FilesystemIsolation::Whiteout;
    document.filesystem.push_back(entry);

    appbox::ProjectDnsRecord dns;
    dns.hostname = L"update.example.com";
    dns.redirect = L"127.0.0.1";
    document.network.push_back(dns);

    appbox::ProjectProxyRecord proxy;
    proxy.type = appbox::ProxyType::Socks5;
    proxy.tcp = true;
    proxy.udp = false;
    proxy.server = L"127.0.0.1";
    proxy.port = L"1080";
    proxy.username = L"user";
    proxy.password = L"secret";
    document.proxy = proxy;

    return document;
}

} // namespace

TEST(Unit_ProjectDocument, RoundTripKeepsEveryMember)
{
    const auto document = BuildSampleDocument();
    const auto back = nlohmann::ordered_json(document).get<appbox::ProjectDocument>();

    EXPECT_EQ(back.output_path, document.output_path);

    ASSERT_EQ(back.folders.size(), 1u);
    EXPECT_EQ(back.folders[0].preset_id, "program_files");
    EXPECT_EQ(back.folders[0].name, L"MyApp");
    EXPECT_EQ(back.folders[0].source_path, L"C:\\Program Files\\MyApp");

    ASSERT_EQ(back.files.size(), 1u);
    EXPECT_EQ(back.files[0].preset_id, "user_profile");
    EXPECT_EQ(back.files[0].target_dir, L"MyApp\\data");
    EXPECT_EQ(back.files[0].name, L"settings.ini");
    EXPECT_EQ(back.files[0].source_path, L"C:\\tmp\\settings.ini");

    ASSERT_EQ(back.startup_files.size(), 1u);
    EXPECT_EQ(back.startup_files[0].preset_id, "program_files");
    EXPECT_EQ(back.startup_files[0].folder, L"MyApp");
    EXPECT_EQ(back.startup_files[0].relative_path, L"bin\\app.exe");
    EXPECT_EQ(back.startup_files[0].trigger, L"app");
    EXPECT_TRUE(back.startup_files[0].auto_start);

    ASSERT_EQ(back.registry.size(), 1u);
    const auto& root = back.registry[0];
    EXPECT_EQ(root.name, L"HKEY_CURRENT_USER");
    EXPECT_EQ(root.isolation, appbox::RegistryIsolation::Full);

    /* The values keep their name, their type, their bytes and their mode. */
    ASSERT_EQ(root.values.size(), 2u);
    EXPECT_EQ(root.values[0].name, L"Server");
    EXPECT_EQ(root.values[0].type, appbox::RegistryValueType::String);
    EXPECT_EQ(root.values[0].isolation, appbox::RegistryIsolation::Hide);
    EXPECT_EQ(appbox::RegistryStringValue(root.values[0].data), L"host");

    /* The default value of a key is an ordinary entry with an empty name. */
    EXPECT_EQ(root.values[1].name, L"");
    EXPECT_EQ(root.values[1].type, appbox::RegistryValueType::None);
    EXPECT_EQ(root.values[1].isolation, appbox::RegistryIsolation::WriteCopy);
    EXPECT_EQ(root.values[1].data, (std::vector<std::uint8_t>{ 0x01, 0x02, 0xFF }));

    ASSERT_EQ(root.children.size(), 1u);
    EXPECT_EQ(root.children[0].name, L"Software");
    EXPECT_EQ(root.children[0].isolation, appbox::RegistryIsolation::WriteCopy);

    ASSERT_EQ(back.filesystem.size(), 1u);
    EXPECT_EQ(back.filesystem[0].path, L"#ProgramFiles#\\MyApp\\app.exe");
    EXPECT_EQ(back.filesystem[0].kind, appbox::FilesystemEntryKind::File);
    EXPECT_EQ(back.filesystem[0].isolation, appbox::FilesystemIsolation::Whiteout);

    ASSERT_EQ(back.network.size(), 1u);
    EXPECT_EQ(back.network[0].hostname, L"update.example.com");
    EXPECT_EQ(back.network[0].redirect, L"127.0.0.1");

    ASSERT_TRUE(back.proxy.has_value());
    EXPECT_EQ(back.proxy->type, appbox::ProxyType::Socks5);
    EXPECT_TRUE(back.proxy->tcp);
    EXPECT_FALSE(back.proxy->udp);
    EXPECT_EQ(back.proxy->server, L"127.0.0.1");
    EXPECT_EQ(back.proxy->port, L"1080");
    EXPECT_EQ(back.proxy->username, L"user");
    EXPECT_EQ(back.proxy->password, L"secret");
}

TEST(Unit_ProjectDocument, RejectsAnIncompleteDnsRecord)
{
    const auto missing = ParseError(R"({"version":1,"network":[{"hostname":"example.com"}]})");
    EXPECT_NE(missing.find("network[0]"), std::string::npos) << missing;
    EXPECT_NE(missing.find("redirect"), std::string::npos) << missing;

    const auto mistyped = ParseError(R"({"version":1,"network":[{"hostname":7,"redirect":"127.0.0.1"}]})");
    EXPECT_NE(mistyped.find("network[0]"), std::string::npos) << mistyped;

    const auto not_an_array = ParseError(R"({"version":1,"network":{}})");
    EXPECT_NE(not_an_array.find("network"), std::string::npos) << not_an_array;
}

TEST(Unit_ProjectDocument, WritesTheSchemaInAFixedOrder)
{
    const auto json = nlohmann::ordered_json(BuildSampleDocument());

    std::vector<std::string> members;
    for (auto member = json.begin(); member != json.end(); ++member)
    {
        members.push_back(member.key());
    }

    /*
     * The version comes first and the members follow the order of the schema,
     * so the text of a given document is stable and easy to diff.
     */
    const std::vector<std::string> expected{ "version",  "output_path", "folders", "files", "startup_files",
                                             "registry", "filesystem",  "network", "proxy" };
    EXPECT_EQ(members, expected);
    EXPECT_EQ(json.at("version").get<int>(), appbox::kProjectFileVersion);

    /* The proxy names its protocol, the traffic it carries and the server. */
    const auto& proxy = json.at("proxy");
    EXPECT_EQ(proxy.at("type").get<std::string>(), "socks5");
    EXPECT_EQ(proxy.at("tcp").get<bool>(), true);
    EXPECT_EQ(proxy.at("udp").get<bool>(), false);
    EXPECT_EQ(proxy.at("server").get<std::string>(), "127.0.0.1");
    EXPECT_EQ(proxy.at("port").get<std::string>(), "1080");
    EXPECT_EQ(proxy.at("username").get<std::string>(), "user");
    EXPECT_EQ(proxy.at("password").get<std::string>(), "secret");

    const auto& root = json.at("registry").at(0);
    EXPECT_EQ(root.at("name").get<std::string>(), "HKEY_CURRENT_USER");
    EXPECT_EQ(root.at("isolation").get<std::string>(), "full");

    /* The raw bytes of a value are stored as a hexadecimal byte string. */
    EXPECT_EQ(root.at("values").at(0).at("type").get<std::string>(), "REG_SZ");
    EXPECT_EQ(root.at("values").at(0).at("isolation").get<std::string>(), "hide");
    EXPECT_EQ(root.at("values").at(1).at("data").get<std::string>(), "01 02 FF");

    /* A sub key is nested below the key which holds it. */
    EXPECT_EQ(root.at("children").at(0).at("name").get<std::string>(), "Software");

    EXPECT_EQ(json.at("filesystem").at(0).at("kind").get<std::string>(), "file");
    EXPECT_EQ(json.at("filesystem").at(0).at("isolation").get<std::string>(), "whiteout");
}

TEST(Unit_ProjectDocument, WritesPathsAsUtf8Bytes)
{
    appbox::ProjectDocument document;

    appbox::ProjectFolderRecord folder;
    folder.preset_id = "program_files";
    folder.name = kChineseName;
    folder.source_path = std::wstring(L"C:\\Program Files\\") + kChineseName;
    document.folders.push_back(folder);

    const auto text = nlohmann::ordered_json(document).dump(2);

    /* Strict UTF-8 means no escaped code points. */
    EXPECT_EQ(text.find("\\u6211"), std::string::npos);
    EXPECT_NE(text.find(kChineseNameUtf8), std::string::npos);
}

TEST(Unit_ProjectDocument, WritesOneRecordAtATime)
{
    appbox::ProjectFilesystemRecord record;
    record.path = L"#Windows#";
    record.kind = appbox::FilesystemEntryKind::Directory;
    record.isolation = appbox::FilesystemIsolation::Whiteout;

    const auto json = nlohmann::ordered_json(record);
    EXPECT_EQ(json.at("path").get<std::string>(), "#Windows#");
    EXPECT_EQ(json.at("kind").get<std::string>(), "directory");
    EXPECT_EQ(json.at("isolation").get<std::string>(), "whiteout");

    const auto back = json.get<appbox::ProjectFilesystemRecord>();
    EXPECT_EQ(back.path, record.path);
    EXPECT_EQ(back.kind, record.kind);
    EXPECT_EQ(back.isolation, record.isolation);
}

TEST(Unit_ProjectDocument, WritesTheStartupFilesAsAnArray)
{
    const auto json = nlohmann::ordered_json(BuildSampleDocument());
    ASSERT_TRUE(json.contains("startup_files"));
    ASSERT_TRUE(json.at("startup_files").is_array());
    ASSERT_EQ(json.at("startup_files").size(), 1u);

    const auto& startup = json.at("startup_files").at(0);
    EXPECT_EQ(startup.at("preset").get<std::string>(), "program_files");
    EXPECT_EQ(startup.at("folder").get<std::string>(), "MyApp");
    EXPECT_EQ(startup.at("path").get<std::string>(), "bin\\app.exe");
    EXPECT_EQ(startup.at("trigger").get<std::string>(), "app");
    EXPECT_EQ(startup.at("auto_start").get<bool>(), true);
}

TEST(Unit_ProjectDocument, WritesAnEmptyStartupFileArray)
{
    const auto json = nlohmann::ordered_json(appbox::ProjectDocument{});
    ASSERT_TRUE(json.contains("startup_files"));
    EXPECT_TRUE(json.at("startup_files").empty());

    /* A document without startup files is read back as an empty list. */
    const auto back = ParseDocument(R"({ "version": 1, "startup_files": [] })");
    EXPECT_TRUE(back.startup_files.empty());
}

TEST(Unit_ProjectDocument, OmitsTheProxyWhileNoneIsConfigured)
{
    const auto json = nlohmann::ordered_json(appbox::ProjectDocument{});
    EXPECT_FALSE(json.contains("proxy"));

    /* A hand written document may spell the absent member as a null value. */
    const auto back = ParseDocument(R"({ "version": 1, "proxy": null })");
    EXPECT_FALSE(back.proxy.has_value());
}

TEST(Unit_ProjectDocument, RoundTripsTheProxyOfTheWorkspace)
{
    const auto document = ParseDocument(R"({ "version": 1, "proxy": { "type": "SOCKS5", "tcp": false, "udp": true,)"
                                        R"( "server": "proxy.example.com", "port": "1080",)"
                                        R"( "username": "", "password": "" } })");

    ASSERT_TRUE(document.proxy.has_value());
    EXPECT_EQ(document.proxy->type, appbox::ProxyType::Socks5);
    EXPECT_FALSE(document.proxy->tcp);
    EXPECT_TRUE(document.proxy->udp);
    EXPECT_EQ(document.proxy->server, L"proxy.example.com");
    EXPECT_EQ(document.proxy->port, L"1080");
    EXPECT_TRUE(document.proxy->username.empty());
    EXPECT_TRUE(document.proxy->password.empty());

    const auto back = nlohmann::ordered_json(document).get<appbox::ProjectDocument>();
    ASSERT_TRUE(back.proxy.has_value());
    EXPECT_EQ(back.proxy->type, appbox::ProxyType::Socks5);
    EXPECT_TRUE(back.proxy->udp);
    EXPECT_EQ(back.proxy->server, L"proxy.example.com");
}

TEST(Unit_ProjectDocument, RejectsAnIncompleteProxyRecord)
{
    const auto missing = ParseError(R"({ "version": 1, "proxy": { "type": "socks5", "tcp": true, "udp": false,)"
                                    R"( "server": "127.0.0.1", "port": "1080", "username": "user" } })");
    EXPECT_NE(missing.find("proxy"), std::string::npos) << missing;
    EXPECT_NE(missing.find("'password'"), std::string::npos) << missing;

    const auto mistyped = ParseError(R"({ "version": 1, "proxy": { "type": "socks5", "tcp": "yes", "udp": false,)"
                                     R"( "server": "127.0.0.1", "port": "1080", "username": "", "password": "" } })");
    EXPECT_NE(mistyped.find("proxy"), std::string::npos) << mistyped;
    EXPECT_NE(mistyped.find("'tcp'"), std::string::npos) << mistyped;

    const auto not_an_object = ParseError(R"({ "version": 1, "proxy": 7 })");
    EXPECT_NE(not_an_object.find("proxy"), std::string::npos) << not_an_object;
    EXPECT_NE(not_an_object.find("not a JSON object"), std::string::npos) << not_an_object;
}

TEST(Unit_ProjectDocument, RejectsAnUnknownProxyType)
{
    const auto error = ParseError(R"({ "version": 1, "proxy": { "type": "socks4", "tcp": true, "udp": false,)"
                                  R"( "server": "127.0.0.1", "port": "1080", "username": "", "password": "" } })");
    EXPECT_NE(error.find("proxy"), std::string::npos) << error;
    EXPECT_NE(error.find("unknown proxy type 'socks4'"), std::string::npos) << error;
}

TEST(Unit_ProjectDocument, ReadsMissingMembersAsEmpty)
{
    const auto document = ParseDocument(R"({ "version": 1 })");

    EXPECT_TRUE(document.output_path.empty());
    EXPECT_TRUE(document.folders.empty());
    EXPECT_TRUE(document.files.empty());
    EXPECT_TRUE(document.startup_files.empty());
    EXPECT_TRUE(document.registry.empty());
    EXPECT_TRUE(document.filesystem.empty());
    EXPECT_TRUE(document.network.empty());
    EXPECT_FALSE(document.proxy.has_value());
}

TEST(Unit_ProjectDocument, ReadingReplacesTheWholeDocument)
{
    appbox::ProjectDocument document = BuildSampleDocument();

    nlohmann::ordered_json::parse(R"({ "version": 1, "output_path": "D:\\out\\a.zip" })").get_to(document);

    /* Reading must not append to the document which was already there. */
    EXPECT_EQ(document.output_path, L"D:\\out\\a.zip");
    EXPECT_TRUE(document.folders.empty());
    EXPECT_TRUE(document.files.empty());
    EXPECT_TRUE(document.startup_files.empty());
    EXPECT_TRUE(document.registry.empty());
    EXPECT_TRUE(document.filesystem.empty());
    EXPECT_FALSE(document.proxy.has_value());
}

TEST(Unit_ProjectDocument, ReadingLeavesTheDocumentUntouchedOnFailure)
{
    appbox::ProjectDocument document = BuildSampleDocument();

    const auto broken = nlohmann::ordered_json::parse(R"({ "version": 1, "folders": {} })");
    EXPECT_THROW(broken.get_to(document), appbox::ProjectDocumentError);

    EXPECT_EQ(document.folders.size(), 1u);
    EXPECT_EQ(document.files.size(), 1u);
    EXPECT_EQ(document.startup_files.size(), 1u);
    EXPECT_TRUE(document.proxy.has_value());
}

TEST(Unit_ProjectDocument, RejectsADocumentWhichIsNotAnObject)
{
    EXPECT_NE(ParseError("[1, 2, 3]").find("the project file does not hold a JSON object"), std::string::npos);
}

TEST(Unit_ProjectDocument, RejectsAVersionItDoesNotSupport)
{
    EXPECT_NE(ParseError(R"({ "version": 2 })").find("unsupported project file version 2 (expected 1)"),
              std::string::npos);

    /* The version is required and has to be a whole number. */
    EXPECT_NE(ParseError(R"({})").find("'version'"), std::string::npos);
    EXPECT_NE(ParseError(R"({ "version": "1" })").find("'version'"), std::string::npos);
    EXPECT_NE(ParseError(R"({ "version": 1.5 })").find("'version'"), std::string::npos);
}

TEST(Unit_ProjectDocument, RejectsMalformedMembers)
{
    EXPECT_NE(ParseError(R"({ "version": 1, "output_path": 7 })").find("the 'output_path' member is not a string"),
              std::string::npos);
    EXPECT_NE(ParseError(R"({ "version": 1, "folders": {} })").find("the 'folders' member is not an array"),
              std::string::npos);
    EXPECT_NE(ParseError(R"({ "version": 1, "files": {} })").find("the 'files' member is not an array"),
              std::string::npos);
    EXPECT_NE(ParseError(R"({ "version": 1, "registry": {} })").find("the 'registry' member is not an array"),
              std::string::npos);
    EXPECT_NE(ParseError(R"({ "version": 1, "filesystem": {} })").find("the 'filesystem' member is not an array"),
              std::string::npos);

    /* An element of an array has to be an object with every member of a record. */
    EXPECT_NE(ParseError(R"({ "version": 1, "files": [ "MyApp" ] })").find("the entry is not a JSON object"),
              std::string::npos);
    EXPECT_NE(ParseError(R"({ "version": 1, "folders": [ { "preset": "program_files", "name": "MyApp" } ] })")
                  .find("the 'source' member is missing or not a string"),
              std::string::npos);
    EXPECT_NE(ParseError(R"({ "version": 1, "files": [ { "preset": "user_profile", "name": "a.ini" } ] })")
                  .find("the 'target_dir' member is missing or not a string"),
              std::string::npos);
    EXPECT_NE(ParseError(R"({ "version": 1, "startup_files": 7 })").find("startup_files"), std::string::npos);
    EXPECT_NE(ParseError(R"({ "version": 1, "startup_files": [ { "preset": "program_files", "folder": "MyApp", )"
                         R"("path": "app.exe", "trigger": "app" } ] })")
                  .find("the 'auto_start' member is missing or not a boolean"),
              std::string::npos);
    EXPECT_NE(ParseError(R"({ "version": 1, "startup_files": [ { "preset": "program_files", "folder": "MyApp", )"
                         R"("path": "app.exe", "trigger": "app", "auto_start": "yes" } ] })")
                  .find("the 'auto_start' member is missing or not a boolean"),
              std::string::npos);
}

TEST(Unit_ProjectDocument, ReportsThePathOfTheRejectedEntry)
{
    const auto folders = ParseError(R"({ "version": 1, "folders": [)"
                                    R"( { "preset": "program_files", "name": "MyApp", "source": "C:\\MyApp" },)"
                                    R"( { "preset": "program_files", "name": "Other", "source": "C:\\Other" },)"
                                    R"( { "preset": "program_files" } ] })");
    EXPECT_NE(folders.find("folders[2]"), std::string::npos);

    const auto filesystem = ParseError(R"({ "version": 1, "filesystem": [)"
                                       R"( { "path": "#Windows#", "kind": "directory", "isolation": "full" },)"
                                       R"( { "path": "#Windows#" } ] })");
    EXPECT_NE(filesystem.find("filesystem[1]"), std::string::npos);
    EXPECT_NE(filesystem.find("'kind'"), std::string::npos);

    const auto startup = ParseError(R"({ "version": 1, "startup_files": [)"
                                    R"( { "preset": "program_files", "folder": "MyApp", "path": "one.exe",)"
                                    R"( "trigger": "one", "auto_start": true },)"
                                    R"( { "preset": "program_files", "folder": "MyApp", "path": "two.exe",)"
                                    R"( "trigger": "two" } ] })");
    EXPECT_NE(startup.find("startup_files[1]"), std::string::npos);
    EXPECT_NE(startup.find("'auto_start'"), std::string::npos);

    /*
     * The path of a nested entry names the key it belongs to, so a value which
     * does not fit the schema can be found in the file.
     */
    const auto value = ParseError(R"({ "version": 1, "registry": [ { "name": "HKEY_CURRENT_USER", "isolation": "full",)"
                                  R"( "values": [ { "name": "Server" } ] } ] })");
    EXPECT_NE(value.find("registry[0]"), std::string::npos);
    EXPECT_NE(value.find("values[0]"), std::string::npos);
}

TEST(Unit_ProjectDocument, RejectsUnknownTokens)
{
    EXPECT_NE(ParseError(R"({ "version": 1, "registry": [ { "name": "HKEY_CURRENT_USER", "isolation": "sandbox" } ] })")
                  .find("unknown isolation mode 'sandbox'"),
              std::string::npos);
    EXPECT_NE(ParseError(R"({ "version": 1, "registry": [ { "name": "HKEY_CURRENT_USER",)"
                         R"( "isolation": "full", "values": [ { "name": "Server", "type": "REG_SOMETHING",)"
                         R"( "data": "", "isolation": "full" } ] } ] })")
                  .find("unknown value type 'REG_SOMETHING'"),
              std::string::npos);
    EXPECT_NE(ParseError(R"({ "version": 1, "registry": [ { "name": "HKEY_CURRENT_USER",)"
                         R"( "isolation": "full", "values": [ { "name": "Server", "type": "REG_SZ",)"
                         R"( "data": "", "isolation": "sandbox" } ] } ] })")
                  .find("unknown isolation mode 'sandbox'"),
              std::string::npos);
    EXPECT_NE(ParseError(
                  R"({ "version": 1, "filesystem": [ { "path": "#Windows#", "kind": "link", "isolation": "full" } ] })")
                  .find("unknown entry kind 'link'"),
              std::string::npos);
    EXPECT_NE(
        ParseError(
            R"({ "version": 1, "filesystem": [ { "path": "#Windows#", "kind": "directory", "isolation": "hide" } ] })")
            .find("unknown isolation mode 'hide'"),
        std::string::npos);
}

TEST(Unit_ProjectDocument, RejectsMalformedValueData)
{
    EXPECT_NE(ParseError(R"({ "version": 1, "registry": [ { "name": "HKEY_CURRENT_USER", "isolation": "full",)"
                         R"( "values": [ { "name": "Server", "type": "REG_BINARY", "data": "0",)"
                         R"( "isolation": "full" } ] } ] })")
                  .find("odd number of digits"),
              std::string::npos);
    EXPECT_NE(ParseError(R"({ "version": 1, "registry": [ { "name": "HKEY_CURRENT_USER", "isolation": "full",)"
                         R"( "values": [ { "name": "Server", "type": "REG_BINARY", "data": "zz",)"
                         R"( "isolation": "full" } ] } ] })")
                  .find("invalid character"),
              std::string::npos);
}

TEST(Unit_ProjectDocument, AcceptsTheTokensOfTheModels)
{
    /*
     * The tokens of the schema are the ones the models use, so a document
     * which is written from a model is read back unchanged.
     */
    const auto document =
        ParseDocument(R"({ "version": 1, "registry": [ { "name": "HKEY_CURRENT_USER", "isolation": "write copy",)"
                      R"( "values": [ { "name": "Server", "type": "reg_dword", "data": "2A 00 00 00",)"
                      R"( "isolation": "FULL" } ] } ],)"
                      R"( "filesystem": [ { "path": "#Windows#", "kind": "folder", "isolation": "write-copy" } ] })");

    ASSERT_EQ(document.registry.size(), 1u);
    EXPECT_EQ(document.registry[0].isolation, appbox::RegistryIsolation::WriteCopy);
    ASSERT_EQ(document.registry[0].values.size(), 1u);
    EXPECT_EQ(document.registry[0].values[0].type, appbox::RegistryValueType::Dword);
    EXPECT_EQ(document.registry[0].values[0].isolation, appbox::RegistryIsolation::Full);
    EXPECT_EQ(document.registry[0].values[0].data, (std::vector<std::uint8_t>{ 0x2A, 0x00, 0x00, 0x00 }));

    ASSERT_EQ(document.filesystem.size(), 1u);
    EXPECT_EQ(document.filesystem[0].kind, appbox::FilesystemEntryKind::Directory);
    EXPECT_EQ(document.filesystem[0].isolation, appbox::FilesystemIsolation::WriteCopy);
}
