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
 * text is parsed and converted with the `from_json()` of the schema, which is
 * the mechanism the packer and the sandbox share.
 *
 * @tparam Document Type of the document to read.
 * @param[in] text Text of the document.
 * @return The document of the text, a default one when the text was refused.
 */
template <typename Document>
Document ParseOrFail(const std::string& text)
{
    try
    {
        return nlohmann::json::parse(text).get<Document>();
    }
    catch (const std::exception& e)
    {
        ADD_FAILURE() << "the document was refused: " << e.what() << "\ntext: " << text;
        return Document{};
    }
}

/**
 * @brief Read the text of a document and report why it was refused.
 *
 * @tparam Document Type of the document to read.
 * @param[in] text Text of the document.
 * @return The description of the refusal, empty when the text was accepted.
 */
template <typename Document>
std::string RefusalOf(const std::string& text)
{
    try
    {
        (void)nlohmann::json::parse(text).get<Document>();
    }
    catch (const appbox::IsolationDocumentError& e)
    {
        return e.what();
    }
    catch (const std::exception& e)
    {
        return std::string("the text was not read as a document: ") + e.what();
    }

    return {};
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

    return document;
}

} // namespace

TEST(Unit_IsolationDocument, FilesystemDocumentRoundTrips)
{
    const auto text = nlohmann::json(FilesystemDocument()).dump(2);
    const auto back = ParseOrFail<appbox::filesystem_isolation::Document>(text);

    EXPECT_EQ(back.version, appbox::filesystem_isolation::kVersion);
    ASSERT_EQ(back.entries.size(), 2u);
    EXPECT_EQ(back.entries[0].path, "#ProgramFiles#\\MyApp");
    EXPECT_EQ(back.entries[0].kind, appbox::FilesystemEntryKind::Directory);
    EXPECT_EQ(back.entries[0].isolation, appbox::FilesystemIsolation::Full);
    EXPECT_EQ(back.entries[1].path, "#ProgramFiles#\\MyApp\\app.exe");
    EXPECT_EQ(back.entries[1].kind, appbox::FilesystemEntryKind::File);
    EXPECT_EQ(back.entries[1].isolation, appbox::FilesystemIsolation::Whiteout);

    /* The text keeps the members and the tokens of the schema of the file. */
    EXPECT_NE(text.find("\"path\""), std::string::npos);
    EXPECT_NE(text.find("\"kind\""), std::string::npos);
    EXPECT_NE(text.find("\"isolation\""), std::string::npos);
    EXPECT_NE(text.find("\"whiteout\""), std::string::npos);
}

TEST(Unit_IsolationDocument, FilesystemDocumentRefusesWhatTheSchemaDoesNotAllow)
{
    using Document = appbox::filesystem_isolation::Document;

    EXPECT_EQ(RefusalOf<Document>("[]"), "the filesystem isolation file is not a JSON object");
    EXPECT_EQ(RefusalOf<Document>(R"({ "entries": [] })"), "the filesystem isolation file has no 'version' member");
    EXPECT_EQ(RefusalOf<Document>(R"({ "version": "1", "entries": [] })"),
              "the 'version' member of the filesystem isolation file is not a whole number");
    EXPECT_EQ(RefusalOf<Document>(R"({ "version": 1, "entries": {} })"),
              "the 'entries' member of the filesystem isolation file is not a list");
    EXPECT_EQ(RefusalOf<Document>(R"({ "version": 1, "entries": [ 7 ] })"),
              "a filesystem isolation file entry is not a JSON object");
    EXPECT_EQ(RefusalOf<Document>(R"({ "version": 1, "entries": [ { "kind": "directory", "isolation": "full" } ] })"),
              "a filesystem isolation file entry has no 'path' member");
    EXPECT_EQ(RefusalOf<Document>(R"({ "version": 1, "entries": [ { "path": 7 } ] })"),
              "the 'path' member of a filesystem isolation file entry is not a string");
    EXPECT_EQ(RefusalOf<Document>(R"({ "version": 1, "entries": [ { "path": "" } ] })"),
              "a filesystem isolation file entry has an empty path");
    EXPECT_EQ(
        RefusalOf<Document>(R"({ "version": 1, "entries": [ { "path": "a", "kind": "link", "isolation": "full" } ] })"),
        "unknown entry kind 'link' in the filesystem isolation file");
    EXPECT_EQ(
        RefusalOf<Document>(R"({ "version": 1, "entries": [ { "path": "a", "kind": "file", "isolation": "hide" } ] })"),
        "unknown isolation mode 'hide' in the filesystem isolation file");
    EXPECT_EQ(RefusalOf<Document>(
                  R"({ "version": 1, "entries": [ { "path": "a", "kind": "file", "isolation": "write_copy" } ] })"),
              "the isolation mode 'write_copy' cannot be used for a file in the filesystem isolation file");

    /* A document without a mode at all describes a file which sets none. */
    const auto empty = ParseOrFail<Document>(R"({ "version": 1 })");
    EXPECT_EQ(empty.version, appbox::filesystem_isolation::kVersion);
    EXPECT_TRUE(empty.entries.empty());
}

TEST(Unit_IsolationDocument, RegistryDocumentRoundTrips)
{
    appbox::registry_isolation::Document document;

    appbox::registry_isolation::KeyEntry key;
    key.path = "HKEY_CURRENT_USER\\Software\\Vendor";
    key.isolation = appbox::RegistryIsolation::Full;
    document.keys.push_back(key);

    appbox::registry_isolation::ValueEntry value;
    value.path = "HKEY_CURRENT_USER\\Software\\Vendor";
    value.name = "Server";
    value.isolation = appbox::RegistryIsolation::Hide;
    document.values.push_back(value);

    const auto text = nlohmann::json(document).dump(2);
    const auto back = ParseOrFail<appbox::registry_isolation::Document>(text);

    EXPECT_EQ(back.version, appbox::registry_isolation::kVersion);
    ASSERT_EQ(back.keys.size(), 1u);
    EXPECT_EQ(back.keys[0].path, "HKEY_CURRENT_USER\\Software\\Vendor");
    EXPECT_EQ(back.keys[0].isolation, appbox::RegistryIsolation::Full);
    ASSERT_EQ(back.values.size(), 1u);
    EXPECT_EQ(back.values[0].path, "HKEY_CURRENT_USER\\Software\\Vendor");
    EXPECT_EQ(back.values[0].name, "Server");
    EXPECT_EQ(back.values[0].isolation, appbox::RegistryIsolation::Hide);
}

TEST(Unit_IsolationDocument, RegistryDocumentRefusesWhatTheSchemaDoesNotAllow)
{
    using Document = appbox::registry_isolation::Document;

    EXPECT_EQ(RefusalOf<Document>("[]"), "the isolation file is not a JSON object");
    EXPECT_EQ(RefusalOf<Document>(R"({ "keys": [] })"), "the isolation file has no 'version' member");
    EXPECT_EQ(RefusalOf<Document>(R"({ "version": 1, "keys": {} })"),
              "the 'keys' member of the isolation file is not a list");
    EXPECT_EQ(RefusalOf<Document>(R"({ "version": 1, "values": [ 7 ] })"),
              "an isolation file entry is not a JSON object");
    EXPECT_EQ(RefusalOf<Document>(R"({ "version": 1, "values": [ { "isolation": "full" } ] })"),
              "an isolation file entry has no 'path' member");
    EXPECT_EQ(RefusalOf<Document>(R"({ "version": 1, "values": [ { "path": "HKEY_CURRENT_USER" } ] })"),
              "an isolation file entry has no 'name' member");
    EXPECT_EQ(RefusalOf<Document>(R"({ "version": 1, "keys": [ { "path": 7, "isolation": "full" } ] })"),
              "the 'path' member of an isolation file entry is not a string");
    EXPECT_EQ(RefusalOf<Document>(R"({ "version": 1, "keys": [ { "path": "", "isolation": "full" } ] })"),
              "an isolation file entry has an empty key path");
    EXPECT_EQ(RefusalOf<Document>(R"({ "version": 1, "keys": [ { "path": "a", "name": 7, "isolation": "full" } ] })"),
              "the 'name' member of an isolation file entry is not a string");
    EXPECT_EQ(RefusalOf<Document>(R"({ "version": 1, "keys": [ { "path": "a", "isolation": "sandbox" } ] })"),
              "unknown isolation mode 'sandbox' in the isolation file");

    /* A document which lists neither a key nor a value sets no mode. */
    const auto empty = ParseOrFail<Document>(R"({ "version": 1 })");
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
    const auto back = ParseOrFail<appbox::network_isolation::Document>(text);

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
    using Document = appbox::network_isolation::Document;

    EXPECT_EQ(RefusalOf<Document>("[]"), "the network isolation file is not a JSON object");
    EXPECT_EQ(RefusalOf<Document>(R"({ "entries": [] })"), "the network isolation file has no 'version' member");
    EXPECT_EQ(RefusalOf<Document>(R"({ "version": 1, "entries": {} })"),
              "the 'entries' member of the network isolation file is not a list");
    EXPECT_EQ(RefusalOf<Document>(R"({ "version": 1, "entries": [ 7 ] })"),
              "a network isolation file entry is not a JSON object");
    EXPECT_EQ(RefusalOf<Document>(R"({ "version": 1, "entries": [ { "redirect": "127.0.0.1" } ] })"),
              "a network isolation file entry has no 'hostname' member");
    EXPECT_EQ(RefusalOf<Document>(R"({ "version": 1, "entries": [ { "hostname": "a" } ] })"),
              "a network isolation file entry has no 'redirect' member");
    EXPECT_EQ(RefusalOf<Document>(R"({ "version": 1, "entries": [ { "hostname": "", "redirect": "127.0.0.1" } ] })"),
              "a network isolation file entry has an empty hostname");
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
    using Document = appbox::network_isolation::Document;

    /* A proxy member which is not an object describes a session without one. */
    EXPECT_FALSE(ParseOrFail<Document>(R"({ "version": 1, "proxy": "socks5" })").proxy.has_value());
    EXPECT_FALSE(ParseOrFail<Document>(R"({ "version": 1, "proxy": null })").proxy.has_value());

    /* A member of another type leaves the default of the proxy. */
    const auto document = ParseOrFail<Document>(R"({ "version": 1, "proxy": { "type": 7, "tcp": "yes", "udp": null,
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
    const auto back = ParseOrFail<appbox::environment_isolation::Document>(text);

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
    using Document = appbox::environment_isolation::Document;

    EXPECT_EQ(RefusalOf<Document>("[]"), "the environment isolation file is not a JSON object");
    EXPECT_EQ(RefusalOf<Document>(R"({ "version": 1 })"), "the environment isolation file carries no entry list");
    EXPECT_EQ(RefusalOf<Document>(R"({ "entries": [] })"), "the environment isolation file has no 'version' member");
    EXPECT_EQ(RefusalOf<Document>(R"({ "version": 1, "entries": [ 7 ] })"), "entries[0]: the entry is not an object");
    EXPECT_EQ(RefusalOf<Document>(R"({ "version": 1, "entries": [ { "name": "A", "value": "1" } ] })"),
              "entries[0]: the member 'merge_string' is missing or is not a string");
    EXPECT_EQ(RefusalOf<Document>(R"({ "version": 1, "entries": [ { "name": "A", "value": "1", "isolation": "whiteout",
                                                                   "merge": "replace", "merge_string": "" } ] })"),
              "entries[0]: the member 'isolation' is not a known mode: 'whiteout'");
    EXPECT_EQ(RefusalOf<Document>(R"({ "version": 1, "entries": [ { "name": "", "value": "1", "isolation": "full",
                                                                   "merge": "replace", "merge_string": "" } ] })"),
              "entries[0]: the variable carries no name");
    EXPECT_EQ(RefusalOf<Document>(R"({ "version": 1, "entries": [ { "name": "A=B", "value": "1", "isolation": "full",
                                                                   "merge": "replace", "merge_string": "" } ] })"),
              "entries[0]: the name of the variable carries an equals sign");
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
    const auto back = ParseOrFail<appbox::environment_isolation::StateDocument>(text);

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
    using Document = appbox::environment_isolation::StateDocument;

    EXPECT_EQ(RefusalOf<Document>("[]"), "the environment state file is not a JSON object");
    EXPECT_EQ(RefusalOf<Document>(R"({ "version": 1 })"), "the environment state file carries no entry list");
    EXPECT_EQ(RefusalOf<Document>(R"({ "version": 1, "entries": [ { "name": "A", "value": "1" } ] })"),
              "entries[0]: the member 'deleted' is missing or is not a boolean");
    EXPECT_EQ(RefusalOf<Document>(R"({ "version": 1, "entries": [ { "name": "", "value": "1", "deleted": false } ] })"),
              "entries[0]: the variable carries no name");
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
