#include <gtest/gtest.h>
#include "EnvironmentIsolation.hpp"
#include "FilesystemIsolation.hpp"
#include "NetworkIsolation.hpp"
#include "RegistryIsolation.hpp"
#include "environment/Configuration.hpp"
#include "network/DnsTable.hpp"
#include "network/ProxyConfig.hpp"
#include "src/core/EnvironmentIsolationFile.hpp"
#include "src/core/EnvironmentModel.hpp"
#include "src/core/NetworkIsolationFile.hpp"
#include "src/core/NetworkModel.hpp"
#include <nlohmann/json.hpp>
#include <string>

namespace
{

/**
 * @brief Read the text of a document and fail the test when it is refused.
 *
 * The helper is the reader of the five documents of the isolation files: the
 * text is read with the `ParseDocument()` of the schema, which is the mechanism
 * the packer and the sandbox share.
 *
 * @tparam Document Type of the document to read.
 * @tparam ParseFn Type of the reader.
 * @param[in] parse Reader of the document.
 * @param[in] text Text of the document.
 * @return The document of the text, a default one when the text was refused.
 */
template <typename Document, typename ParseFn>
Document ParseOrFail(ParseFn parse, const std::string& text)
{
    const auto document = parse(text);
    if (!document)
    {
        ADD_FAILURE() << "the document was refused with the code " << static_cast<int>(document.error())
                      << "\ntext: " << text;
        return Document{};
    }

    return *document;
}

/**
 * @brief Read the text of a document and report the code of its refusal.
 *
 * @tparam Document Type of the document to read.
 * @tparam ParseFn Type of the reader.
 * @param[in] parse Reader of the document.
 * @param[in] text Text of the document.
 * @return The code of the refusal, `appbox::ErrorCode::kInvalidArgument` when the
 *         text was accepted.
 */
template <typename Document, typename ParseFn>
appbox::ErrorCode RefusalOf(ParseFn parse, const std::string& text)
{
    const auto document = parse(text);
    if (document)
    {
        (void)sizeof(Document);
        return appbox::ErrorCode::kInvalidArgument;
    }

    return document.error();
}

/**
 * @brief Read a filesystem isolation document.
 * @param[in] text Text of the document.
 * @return The document of the text.
 */
appbox::filesystem_isolation::Document ReadFilesystem(const std::string& text)
{
    return ParseOrFail<appbox::filesystem_isolation::Document>(appbox::filesystem_isolation::ParseDocument, text);
}

/**
 * @brief Read a registry isolation document.
 * @param[in] text Text of the document.
 * @return The document of the text.
 */
appbox::registry_isolation::Document ReadRegistry(const std::string& text)
{
    return ParseOrFail<appbox::registry_isolation::Document>(appbox::registry_isolation::ParseDocument, text);
}

/**
 * @brief Read a network isolation document.
 * @param[in] text Text of the document.
 * @return The document of the text.
 */
appbox::network_isolation::Document ReadNetwork(const std::string& text)
{
    return ParseOrFail<appbox::network_isolation::Document>(appbox::network_isolation::ParseDocument, text);
}

/**
 * @brief Read an environment isolation document.
 * @param[in] text Text of the document.
 * @return The document of the text.
 */
appbox::environment_isolation::Document ReadEnvironment(const std::string& text)
{
    return ParseOrFail<appbox::environment_isolation::Document>(appbox::environment_isolation::ParseDocument, text);
}

/**
 * @brief Read an environment state document.
 * @param[in] text Text of the document.
 * @return The document of the text.
 */
appbox::environment_isolation::StateDocument ReadState(const std::string& text)
{
    return ParseOrFail<appbox::environment_isolation::StateDocument>(appbox::environment_isolation::ParseStateDocument,
                                                                     text);
}

/**
 * @brief Build a document which holds one entry of every kind of the schema.
 * @return The document.
 */
appbox::filesystem_isolation::Document FilesystemDocument()
{
    appbox::filesystem_isolation::Document document;

    appbox::filesystem_isolation::Entry folder;
    folder.path = "#ProgramFiles#\\MyApp";
    folder.kind = appbox::FilesystemEntryKind::Directory;
    folder.isolation = appbox::FilesystemIsolation::Full;
    document.entries.push_back(folder);

    appbox::filesystem_isolation::Entry file;
    file.path = "#ProgramFiles#\\MyApp\\app.exe";
    file.kind = appbox::FilesystemEntryKind::File;
    file.isolation = appbox::FilesystemIsolation::Whiteout;
    document.entries.push_back(file);

    /* The root of the view is the entry without a path. */
    appbox::filesystem_isolation::Entry root;
    root.path = "";
    root.kind = appbox::FilesystemEntryKind::Directory;
    root.isolation = appbox::FilesystemIsolation::Merge;
    document.entries.push_back(root);

    return document;
}

} // namespace

TEST(Unit_IsolationDocument, FilesystemDocumentRoundTrips)
{
    const auto text = nlohmann::json(FilesystemDocument()).dump(2);
    const auto back = ReadFilesystem(text);

    EXPECT_EQ(back.version, appbox::filesystem_isolation::kVersion);
    ASSERT_EQ(back.entries.size(), 3u);
    EXPECT_EQ(back.entries[0].path, "#ProgramFiles#\\MyApp");
    EXPECT_EQ(back.entries[0].kind, appbox::FilesystemEntryKind::Directory);
    EXPECT_EQ(back.entries[0].isolation, appbox::FilesystemIsolation::Full);
    EXPECT_EQ(back.entries[1].path, "#ProgramFiles#\\MyApp\\app.exe");
    EXPECT_EQ(back.entries[1].kind, appbox::FilesystemEntryKind::File);
    EXPECT_EQ(back.entries[1].isolation, appbox::FilesystemIsolation::Whiteout);

    /* The root of the view is stored as an entry without a path. */
    EXPECT_TRUE(back.entries[2].path.empty());
    EXPECT_EQ(back.entries[2].kind, appbox::FilesystemEntryKind::Directory);
    EXPECT_EQ(back.entries[2].isolation, appbox::FilesystemIsolation::Merge);

    /* The text keeps the members and the tokens of the schema of the file. */
    EXPECT_NE(text.find("\"path\""), std::string::npos);
    EXPECT_NE(text.find("\"kind\""), std::string::npos);
    EXPECT_NE(text.find("\"isolation\""), std::string::npos);
    EXPECT_NE(text.find("\"whiteout\""), std::string::npos);
    EXPECT_NE(text.find("\"merge\""), std::string::npos);
}

/**
 * @brief A document of another shape is refused with one code.
 *
 * The reader reports the refusal by code alone: the description of the member
 * which was refused is a diagnostic of the schema and never reaches the caller,
 * so every refusal of this schema answers the same code.
 */
TEST(Unit_IsolationDocument, FilesystemDocumentRefusesWhatTheSchemaDoesNotAllow)
{
    constexpr auto kInvalid = appbox::ErrorCode::kIsolationDocumentInvalid;

    EXPECT_EQ(RefusalOf<appbox::filesystem_isolation::Document>(appbox::filesystem_isolation::ParseDocument, "[]"),
              kInvalid);
    EXPECT_EQ(RefusalOf<appbox::filesystem_isolation::Document>(appbox::filesystem_isolation::ParseDocument,
                                                                R"({ "entries": [] })"),
              kInvalid);
    EXPECT_EQ(RefusalOf<appbox::filesystem_isolation::Document>(appbox::filesystem_isolation::ParseDocument,
                                                                R"({ "version": "1", "entries": [] })"),
              kInvalid);
    EXPECT_EQ(RefusalOf<appbox::filesystem_isolation::Document>(appbox::filesystem_isolation::ParseDocument,
                                                                R"({ "version": 1, "entries": {} })"),
              kInvalid);
    EXPECT_EQ(RefusalOf<appbox::filesystem_isolation::Document>(appbox::filesystem_isolation::ParseDocument,
                                                                R"({ "version": 1, "entries": [ 7 ] })"),
              kInvalid);
    EXPECT_EQ(RefusalOf<appbox::filesystem_isolation::Document>(
                  appbox::filesystem_isolation::ParseDocument,
                  R"({ "version": 1, "entries": [ { "kind": "directory", "isolation": "full" } ] })"),
              kInvalid);
    EXPECT_EQ(RefusalOf<appbox::filesystem_isolation::Document>(appbox::filesystem_isolation::ParseDocument,
                                                                R"({ "version": 1, "entries": [ { "path": 7 } ] })"),
              kInvalid);

    /* An entry without a path is the root of the view, which has to be a folder. */
    EXPECT_EQ(RefusalOf<appbox::filesystem_isolation::Document>(appbox::filesystem_isolation::ParseDocument,
                                                                R"({ "version": 1, "entries": [ { "path": "" } ] })"),
              kInvalid);
    EXPECT_EQ(RefusalOf<appbox::filesystem_isolation::Document>(
                  appbox::filesystem_isolation::ParseDocument,
                  R"({ "version": 1, "entries": [ { "path": "", "kind": "file", "isolation": "full" } ] })"),
              kInvalid);

    EXPECT_EQ(RefusalOf<appbox::filesystem_isolation::Document>(
                  appbox::filesystem_isolation::ParseDocument,
                  R"({ "version": 1, "entries": [ { "path": "a", "kind": "link", "isolation": "full" } ] })"),
              kInvalid);
    EXPECT_EQ(RefusalOf<appbox::filesystem_isolation::Document>(
                  appbox::filesystem_isolation::ParseDocument,
                  R"({ "version": 1, "entries": [ { "path": "a", "kind": "file", "isolation": "hide" } ] })"),
              kInvalid);
    EXPECT_EQ(RefusalOf<appbox::filesystem_isolation::Document>(
                  appbox::filesystem_isolation::ParseDocument,
                  R"({ "version": 1, "entries": [ { "path": "a", "kind": "file", "isolation": "write_copy" } ] })"),
              kInvalid);
    EXPECT_EQ(RefusalOf<appbox::filesystem_isolation::Document>(
                  appbox::filesystem_isolation::ParseDocument,
                  R"({ "version": 1, "entries": [ { "path": "a", "kind": "file", "isolation": "merge" } ] })"),
              kInvalid);

    /* A text which is no JSON document at all is the same code. */
    EXPECT_EQ(RefusalOf<appbox::filesystem_isolation::Document>(appbox::filesystem_isolation::ParseDocument,
                                                                "{ not a document"),
              kInvalid);

    /* A document without a mode at all describes a file which sets none. */
    const auto empty = ReadFilesystem(R"({ "version": 1 })");
    EXPECT_EQ(empty.version, appbox::filesystem_isolation::kVersion);
    EXPECT_TRUE(empty.entries.empty());
}

/**
 * @brief A document of another schema version is told apart from a broken one.
 *
 * The version is read before the members of the document, so a file of a newer
 * schema is never interpreted with the rules of this one and the caller does not
 * report it as a malformed file.
 */
TEST(Unit_IsolationDocument, DocumentOfAnotherVersionIsReportedAsUnsupported)
{
    using Document = appbox::filesystem_isolation::Document;
    using appbox::ErrorCode;

    EXPECT_EQ(RefusalOf<Document>(appbox::filesystem_isolation::ParseDocument, R"({ "version": 2, "entries": [] })"),
              ErrorCode::kIsolationDocumentUnsupportedVersion);

    /* The state document has a version of its own and reports the same code. */
    EXPECT_EQ(RefusalOf<appbox::environment_isolation::StateDocument>(appbox::environment_isolation::ParseStateDocument,
                                                                      R"({ "version": 0, "entries": [] })"),
              ErrorCode::kIsolationDocumentUnsupportedVersion);
}

TEST(Unit_IsolationDocument, RegistryDocumentRoundTrips)
{
    appbox::registry_isolation::Document document;

    appbox::registry_isolation::KeyEntry key;
    key.path = "HKEY_CURRENT_USER\\Software\\Vendor";
    key.isolation = appbox::RegistryKeyIsolation::Full;
    document.keys.push_back(key);

    appbox::registry_isolation::ValueEntry value;
    value.path = "HKEY_CURRENT_USER\\Software\\Vendor";
    value.name = "Server";
    value.isolation = appbox::RegistryValueIsolation::Hide;
    document.values.push_back(value);

    const auto text = nlohmann::json(document).dump(2);
    const auto back = ReadRegistry(text);

    EXPECT_EQ(back.version, appbox::registry_isolation::kVersion);
    ASSERT_EQ(back.keys.size(), 1u);
    EXPECT_EQ(back.keys[0].path, "HKEY_CURRENT_USER\\Software\\Vendor");
    EXPECT_EQ(back.keys[0].isolation, appbox::RegistryKeyIsolation::Full);
    ASSERT_EQ(back.values.size(), 1u);
    EXPECT_EQ(back.values[0].path, "HKEY_CURRENT_USER\\Software\\Vendor");
    EXPECT_EQ(back.values[0].name, "Server");
    EXPECT_EQ(back.values[0].isolation, appbox::RegistryValueIsolation::Hide);
}

TEST(Unit_IsolationDocument, RegistryDocumentRefusesWhatTheSchemaDoesNotAllow)
{
    constexpr auto kInvalid = appbox::ErrorCode::kIsolationDocumentInvalid;

    EXPECT_EQ(RefusalOf<appbox::registry_isolation::Document>(appbox::registry_isolation::ParseDocument, "[]"),
              kInvalid);
    EXPECT_EQ(
        RefusalOf<appbox::registry_isolation::Document>(appbox::registry_isolation::ParseDocument, R"({ "keys": [] })"),
        kInvalid);
    EXPECT_EQ(RefusalOf<appbox::registry_isolation::Document>(appbox::registry_isolation::ParseDocument,
                                                              R"({ "version": 1, "keys": {} })"),
              kInvalid);
    EXPECT_EQ(RefusalOf<appbox::registry_isolation::Document>(appbox::registry_isolation::ParseDocument,
                                                              R"({ "version": 1, "values": [ 7 ] })"),
              kInvalid);
    EXPECT_EQ(
        RefusalOf<appbox::registry_isolation::Document>(appbox::registry_isolation::ParseDocument,
                                                        R"({ "version": 1, "values": [ { "isolation": "full" } ] })"),
        kInvalid);
    EXPECT_EQ(RefusalOf<appbox::registry_isolation::Document>(
                  appbox::registry_isolation::ParseDocument,
                  R"({ "version": 1, "values": [ { "path": "HKEY_CURRENT_USER" } ] })"),
              kInvalid);
    EXPECT_EQ(RefusalOf<appbox::registry_isolation::Document>(appbox::registry_isolation::ParseDocument,
                                                              R"({ "version": 1, "keys": [ { "path": 7,
                                                                                            "isolation": "full" } ] })"),
              kInvalid);
    EXPECT_EQ(RefusalOf<appbox::registry_isolation::Document>(
                  appbox::registry_isolation::ParseDocument,
                  R"({ "version": 1, "keys": [ { "path": "", "isolation": "full" } ] })"),
              kInvalid);
    EXPECT_EQ(RefusalOf<appbox::registry_isolation::Document>(
                  appbox::registry_isolation::ParseDocument,
                  R"({ "version": 1, "keys": [ { "path": "a", "name": 7, "isolation": "full" } ] })"),
              kInvalid);
    EXPECT_EQ(RefusalOf<appbox::registry_isolation::Document>(
                  appbox::registry_isolation::ParseDocument,
                  R"({ "version": 1, "keys": [ { "path": "a", "isolation": "sandbox" } ] })"),
              kInvalid);

    /*
     * A value carries the two value modes alone: a value is never copied up and
     * a value write never reaches the host registry, so the modes of a key are
     * refused for a value entry.
     */
    EXPECT_EQ(RefusalOf<appbox::registry_isolation::Document>(
                  appbox::registry_isolation::ParseDocument,
                  R"({ "version": 1, "values": [ { "path": "a", "name": "b", "isolation": "merge" } ] })"),
              kInvalid);
    EXPECT_EQ(RefusalOf<appbox::registry_isolation::Document>(
                  appbox::registry_isolation::ParseDocument,
                  R"({ "version": 1, "values": [ { "path": "a", "name": "b", "isolation": "write_copy" } ] })"),
              kInvalid);

    /* A document which lists neither a key nor a value sets no mode. */
    const auto empty = ReadRegistry(R"({ "version": 1 })");
    EXPECT_TRUE(empty.keys.empty());
    EXPECT_TRUE(empty.values.empty());
}

TEST(Unit_IsolationDocument, NetworkDocumentRoundTrips)
{
    appbox::network_isolation::Document document;

    appbox::network_isolation::Entry entry;
    entry.hostname = "update.example.com";
    entry.redirect = "127.0.0.1";
    document.entries.push_back(entry);

    appbox::network_isolation::Proxy proxy;
    proxy.type = appbox::network_isolation::kSocks5Token;
    proxy.tcp = true;
    proxy.server = "proxy.example";
    proxy.port = "1080";
    proxy.username = "user";
    proxy.password = "secret";
    document.proxy = proxy;

    const auto text = nlohmann::json(document).dump(2);
    const auto back = ReadNetwork(text);

    EXPECT_EQ(back.version, appbox::network_isolation::kVersion);
    ASSERT_EQ(back.entries.size(), 1u);
    EXPECT_EQ(back.entries[0].hostname, "update.example.com");
    EXPECT_EQ(back.entries[0].redirect, "127.0.0.1");
    ASSERT_TRUE(back.proxy.has_value());
    EXPECT_EQ(back.proxy->type, "socks5");
    EXPECT_TRUE(back.proxy->tcp);
    EXPECT_FALSE(back.proxy->udp);
    EXPECT_EQ(back.proxy->server, "proxy.example");
    EXPECT_EQ(back.proxy->port, "1080");
    EXPECT_EQ(back.proxy->username, "user");
    EXPECT_EQ(back.proxy->password, "secret");
}

TEST(Unit_IsolationDocument, NetworkDocumentRefusesWhatTheSchemaDoesNotAllow)
{
    constexpr auto kInvalid = appbox::ErrorCode::kIsolationDocumentInvalid;

    EXPECT_EQ(RefusalOf<appbox::network_isolation::Document>(appbox::network_isolation::ParseDocument, "[]"), kInvalid);
    EXPECT_EQ(RefusalOf<appbox::network_isolation::Document>(appbox::network_isolation::ParseDocument,
                                                             R"({ "entries": [] })"),
              kInvalid);
    EXPECT_EQ(RefusalOf<appbox::network_isolation::Document>(appbox::network_isolation::ParseDocument,
                                                             R"({ "version": 1, "entries": {} })"),
              kInvalid);
    EXPECT_EQ(RefusalOf<appbox::network_isolation::Document>(appbox::network_isolation::ParseDocument,
                                                             R"({ "version": 1, "entries": [ 7 ] })"),
              kInvalid);
    EXPECT_EQ(RefusalOf<appbox::network_isolation::Document>(
                  appbox::network_isolation::ParseDocument,
                  R"({ "version": 1, "entries": [ { "redirect": "127.0.0.1" } ] })"),
              kInvalid);
    EXPECT_EQ(RefusalOf<appbox::network_isolation::Document>(appbox::network_isolation::ParseDocument,
                                                             R"({ "version": 1, "entries": [ { "hostname": "a" } ] })"),
              kInvalid);
    EXPECT_EQ(RefusalOf<appbox::network_isolation::Document>(
                  appbox::network_isolation::ParseDocument,
                  R"({ "version": 1, "entries": [ { "hostname": "", "redirect": "127.0.0.1" } ] })"),
              kInvalid);
}

/**
 * @brief A proxy which cannot be used never fails the document.
 *
 * The entries of the file redirect the names of the application either way, so
 * the proxy member is read leniently: a member of another type describes a
 * session without a proxy or leaves the default of the structure, and the rules
 * which decide whether the proxy can be used are applied by the reader of the
 * file.
 */
TEST(Unit_IsolationDocument, NetworkDocumentReadsTheProxyLeniently)
{
    /* A proxy member which is not an object describes a session without one. */
    EXPECT_FALSE(ReadNetwork(R"({ "version": 1, "proxy": "socks5" })").proxy.has_value());
    EXPECT_FALSE(ReadNetwork(R"({ "version": 1, "proxy": null })").proxy.has_value());

    /* A member of another type leaves the default of the proxy. */
    const auto document = ReadNetwork(R"({ "version": 1, "proxy": { "type": 7, "tcp": "yes", "udp": null,
                                                                  "server": null, "port": [], "username": 1 } })");
    ASSERT_TRUE(document.proxy.has_value());
    EXPECT_TRUE(document.proxy->type.empty());
    EXPECT_FALSE(document.proxy->tcp);
    EXPECT_FALSE(document.proxy->udp);
    EXPECT_TRUE(document.proxy->server.empty());
    EXPECT_TRUE(document.proxy->port.empty());
    EXPECT_TRUE(document.proxy->username.empty());
    EXPECT_TRUE(document.proxy->password.empty());
}

TEST(Unit_IsolationDocument, EnvironmentDocumentRoundTrips)
{
    appbox::environment_isolation::Document document;

    appbox::environment_isolation::Entry entry;
    entry.name = "PATH";
    entry.value = "C:/MyApp/bin";
    entry.isolation = appbox::EnvironmentIsolation::WriteCopy;
    entry.merge = appbox::EnvironmentMergeMode::Prepend;
    entry.merge_string = ";";
    document.entries.push_back(entry);

    const auto text = nlohmann::json(document).dump(2);
    const auto back = ReadEnvironment(text);

    EXPECT_EQ(back.version, appbox::environment_isolation::kVersion);
    ASSERT_EQ(back.entries.size(), 1u);
    EXPECT_EQ(back.entries[0].name, "PATH");
    EXPECT_EQ(back.entries[0].value, "C:/MyApp/bin");
    EXPECT_EQ(back.entries[0].isolation, appbox::EnvironmentIsolation::WriteCopy);
    EXPECT_EQ(back.entries[0].merge, appbox::EnvironmentMergeMode::Prepend);
    EXPECT_EQ(back.entries[0].merge_string, ";");
}

TEST(Unit_IsolationDocument, EnvironmentDocumentRefusesWhatTheSchemaDoesNotAllow)
{
    constexpr auto kInvalid = appbox::ErrorCode::kIsolationDocumentInvalid;

    EXPECT_EQ(RefusalOf<appbox::environment_isolation::Document>(appbox::environment_isolation::ParseDocument, "[]"),
              kInvalid);
    EXPECT_EQ(RefusalOf<appbox::environment_isolation::Document>(appbox::environment_isolation::ParseDocument,
                                                                 R"({ "version": 1 })"),
              kInvalid);
    EXPECT_EQ(RefusalOf<appbox::environment_isolation::Document>(appbox::environment_isolation::ParseDocument,
                                                                 R"({ "entries": [] })"),
              kInvalid);
    EXPECT_EQ(RefusalOf<appbox::environment_isolation::Document>(appbox::environment_isolation::ParseDocument,
                                                                 R"({ "version": 1, "entries": [ 7 ] })"),
              kInvalid);
    EXPECT_EQ(RefusalOf<appbox::environment_isolation::Document>(
                  appbox::environment_isolation::ParseDocument,
                  R"({ "version": 1, "entries": [ { "name": "A", "value": "1" } ] })"),
              kInvalid);
    EXPECT_EQ(RefusalOf<appbox::environment_isolation::Document>(
                  appbox::environment_isolation::ParseDocument,
                  R"({ "version": 1, "entries": [ { "name": "A", "value": "1", "isolation": "whiteout",
                                                    "merge": "replace", "merge_string": "" } ] })"),
              kInvalid);
    EXPECT_EQ(RefusalOf<appbox::environment_isolation::Document>(
                  appbox::environment_isolation::ParseDocument,
                  R"({ "version": 1, "entries": [ { "name": "", "value": "1", "isolation": "full",
                                                    "merge": "replace", "merge_string": "" } ] })"),
              kInvalid);
    EXPECT_EQ(RefusalOf<appbox::environment_isolation::Document>(
                  appbox::environment_isolation::ParseDocument,
                  R"({ "version": 1, "entries": [ { "name": "A=B", "value": "1", "isolation": "full",
                                                    "merge": "replace", "merge_string": "" } ] })"),
              kInvalid);
}

TEST(Unit_IsolationDocument, StateDocumentRoundTrips)
{
    appbox::environment_isolation::StateDocument document;

    appbox::environment_isolation::StateEntry stored;
    stored.name = "APPBOX_MODE";
    stored.value = "changed";
    document.entries.push_back(stored);

    appbox::environment_isolation::StateEntry removed;
    removed.name = "APPBOX_OLD";
    removed.deleted = true;
    document.entries.push_back(removed);

    const auto text = nlohmann::json(document).dump(2);
    const auto back = ReadState(text);

    EXPECT_EQ(back.version, appbox::environment_isolation::kStateVersion);
    ASSERT_EQ(back.entries.size(), 2u);
    EXPECT_EQ(back.entries[0].name, "APPBOX_MODE");
    EXPECT_EQ(back.entries[0].value, "changed");
    EXPECT_FALSE(back.entries[0].deleted);
    EXPECT_EQ(back.entries[1].name, "APPBOX_OLD");
    EXPECT_TRUE(back.entries[1].deleted);
}

TEST(Unit_IsolationDocument, StateDocumentRefusesWhatTheSchemaDoesNotAllow)
{
    constexpr auto kInvalid = appbox::ErrorCode::kIsolationDocumentInvalid;

    EXPECT_EQ(RefusalOf<appbox::environment_isolation::StateDocument>(appbox::environment_isolation::ParseStateDocument,
                                                                      "[]"),
              kInvalid);
    EXPECT_EQ(RefusalOf<appbox::environment_isolation::StateDocument>(appbox::environment_isolation::ParseStateDocument,
                                                                      R"({ "version": 1 })"),
              kInvalid);
    EXPECT_EQ(RefusalOf<appbox::environment_isolation::StateDocument>(
                  appbox::environment_isolation::ParseStateDocument,
                  R"({ "version": 1, "entries": [ { "name": "A", "value": "1" } ] })"),
              kInvalid);
    EXPECT_EQ(RefusalOf<appbox::environment_isolation::StateDocument>(
                  appbox::environment_isolation::ParseStateDocument,
                  R"({ "version": 1, "entries": [ { "name": "", "value": "1", "deleted": false } ] })"),
              kInvalid);
}

/**
 * @brief The network isolation file of the packer is read by the sandbox.
 *
 * The two sides of the network isolation live in different components, so the
 * test pins that the document of the workspace is accepted by the readers of
 * the sandbox and resolves to the configuration of the workspace.
 */
TEST(Unit_IsolationDocument, NetworkIsolationFileOfThePackerIsReadByTheSandbox)
{
    appbox::NetworkModel model;

    std::string error;
    ASSERT_TRUE(model.AddDnsEntry(L"update.example.com", L"127.0.0.1", error)) << error;

    appbox::ProxyConfig proxy;
    proxy.type = appbox::ProxyType::Socks5;
    proxy.tcp = true;
    proxy.server = L"127.0.0.1";
    proxy.port = L"1080";
    ASSERT_TRUE(model.SetProxy(proxy, error)) << error;

    std::string text;
    ASSERT_TRUE(appbox::BuildNetworkIsolationFile(model, text, error)) << error;

    appbox::network::DnsTable table;
    ASSERT_TRUE(table.Parse(text, error)) << error;
    ASSERT_EQ(table.Count(), 1u);

    const auto* entry = table.Find("Update.Example.COM.", appbox::network::RequestedFamily::IPv4);
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->redirect, "127.0.0.1");

    appbox::network::ProxyConfig read;
    ASSERT_TRUE(appbox::network::ParseProxyConfig(text, read));
    EXPECT_TRUE(read.IsEnabled());
    EXPECT_TRUE(read.tcp);
    EXPECT_FALSE(read.udp);
    EXPECT_EQ(read.server, "127.0.0.1");
    EXPECT_EQ(read.port, 1080);
}

/**
 * @brief The environment isolation file of the packer is read by the sandbox.
 *
 * The two sides of the environment isolation live in different components, so
 * the test pins that the document of the workspace is accepted by the reader of
 * the sandbox and resolves to the variables of the workspace.
 */
TEST(Unit_IsolationDocument, EnvironmentIsolationFileOfThePackerIsReadByTheSandbox)
{
    appbox::EnvironmentModel model;

    appbox::EnvironmentEntry entry;
    entry.name = L"PATH";
    entry.value = L"C:/MyApp/bin";
    entry.merge = appbox::EnvironmentMergeMode::Prepend;
    entry.merge_string = L";";

    std::string error;
    ASSERT_TRUE(model.AddEntry(entry, error)) << error;

    std::string text;
    ASSERT_TRUE(appbox::BuildEnvironmentIsolationFile(model, text, error)) << error;

    std::vector<appbox::environment::ConfiguredVariable> variables;
    ASSERT_TRUE(appbox::environment::ParseIsolationDocument(text, variables, error)) << error;
    ASSERT_EQ(variables.size(), 1u);
    EXPECT_EQ(variables[0].name, L"PATH");
    EXPECT_EQ(variables[0].value, L"C:/MyApp/bin");
    EXPECT_EQ(variables[0].isolation, appbox::EnvironmentIsolation::WriteCopy);
    EXPECT_EQ(variables[0].merge, appbox::EnvironmentMergeMode::Prepend);
    EXPECT_EQ(variables[0].merge_string, L";");
}
