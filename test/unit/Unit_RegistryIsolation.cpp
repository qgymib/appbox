#include "RegistryIsolation.hpp"
#include "registry/IsolationTable.hpp"
#include <gtest/gtest.h>
#include <string>
#include <vector>

/**
 * @brief Every mode has its canonical lower case token.
 */
TEST(UnitRegistryIsolation, IsolationToken)
{
    ASSERT_STREQ(appbox::registry_isolation::IsolationToken(appbox::RegistryIsolation::Full), "full");
    ASSERT_STREQ(appbox::registry_isolation::IsolationToken(appbox::RegistryIsolation::WriteCopy), "write_copy");
    ASSERT_STREQ(appbox::registry_isolation::IsolationToken(appbox::RegistryIsolation::Hide), "hide");
}

/**
 * @brief The canonical token resolves back to its mode.
 */
TEST(UnitRegistryIsolation, ParseIsolationTokenCanonical)
{
    appbox::RegistryIsolation mode = appbox::RegistryIsolation::Hide;

    ASSERT_TRUE(appbox::registry_isolation::ParseIsolationToken("full", mode));
    ASSERT_EQ(mode, appbox::RegistryIsolation::Full);

    ASSERT_TRUE(appbox::registry_isolation::ParseIsolationToken("write_copy", mode));
    ASSERT_EQ(mode, appbox::RegistryIsolation::WriteCopy);

    ASSERT_TRUE(appbox::registry_isolation::ParseIsolationToken("hide", mode));
    ASSERT_EQ(mode, appbox::RegistryIsolation::Hide);
}

/**
 * @brief The comparison ignores the case, so a hand written file resolves too.
 */
TEST(UnitRegistryIsolation, ParseIsolationTokenIgnoreCase)
{
    appbox::RegistryIsolation mode = appbox::RegistryIsolation::Hide;

    ASSERT_TRUE(appbox::registry_isolation::ParseIsolationToken("FULL", mode));
    ASSERT_EQ(mode, appbox::RegistryIsolation::Full);

    ASSERT_TRUE(appbox::registry_isolation::ParseIsolationToken("Write_Copy", mode));
    ASSERT_EQ(mode, appbox::RegistryIsolation::WriteCopy);
}

/**
 * @brief Spaces, dashes and the missing underscore are accepted as well.
 */
TEST(UnitRegistryIsolation, ParseIsolationTokenSeparators)
{
    appbox::RegistryIsolation mode = appbox::RegistryIsolation::Full;

    ASSERT_TRUE(appbox::registry_isolation::ParseIsolationToken("Write Copy", mode));
    ASSERT_EQ(mode, appbox::RegistryIsolation::WriteCopy);

    ASSERT_TRUE(appbox::registry_isolation::ParseIsolationToken("write-copy", mode));
    ASSERT_EQ(mode, appbox::RegistryIsolation::WriteCopy);

    ASSERT_TRUE(appbox::registry_isolation::ParseIsolationToken("writecopy", mode));
    ASSERT_EQ(mode, appbox::RegistryIsolation::WriteCopy);
}

/**
 * @brief An unknown token is rejected and leaves the output untouched.
 *
 * `merge` is no longer a mode of the isolation: an isolation file or a project
 * file which still carries it is rejected instead of being interpreted with
 * the rules of a mode which does not exist anymore.
 */
TEST(UnitRegistryIsolation, ParseIsolationTokenUnknown)
{
    appbox::RegistryIsolation mode = appbox::RegistryIsolation::Hide;

    ASSERT_FALSE(appbox::registry_isolation::ParseIsolationToken("", mode));
    ASSERT_FALSE(appbox::registry_isolation::ParseIsolationToken("sandbox", mode));
    ASSERT_FALSE(appbox::registry_isolation::ParseIsolationToken("full copy", mode));
    ASSERT_FALSE(appbox::registry_isolation::ParseIsolationToken("merge", mode));
    ASSERT_FALSE(appbox::registry_isolation::ParseIsolationToken("Merge", mode));
    ASSERT_EQ(mode, appbox::RegistryIsolation::Hide);
}

/**
 * @brief An empty table reports the default mode for every entry.
 */
TEST(UnitRegistryIsolationTable, EmptyTable)
{
    appbox::registry::IsolationTable table;

    ASSERT_TRUE(table.Empty());
    ASSERT_EQ(table.KeyCount(), 0u);
    ASSERT_EQ(table.ValueCount(), 0u);
    ASSERT_EQ(table.KeyMode(L"HKEY_CURRENT_USER\\Software"), appbox::RegistryIsolation::WriteCopy);
    ASSERT_EQ(table.ValueMode(L"HKEY_CURRENT_USER\\Software", L"Server"), appbox::RegistryIsolation::WriteCopy);
}

/**
 * @brief The listed key and value modes are read back.
 */
TEST(UnitRegistryIsolationTable, ParseEntries)
{
    const std::string text = R"({
        "version": 1,
        "keys": [ { "path": "HKEY_CURRENT_USER\\Software\\Vendor", "isolation": "full" } ],
        "values": [ { "path": "HKEY_CURRENT_USER\\Software\\Vendor", "name": "Server", "isolation": "hide" } ]
    })";

    appbox::registry::IsolationTable table;
    std::string                      error;
    ASSERT_TRUE(table.Parse(text, error)) << error;
    ASSERT_FALSE(table.Empty());
    ASSERT_EQ(table.KeyCount(), 1u);
    ASSERT_EQ(table.ValueCount(), 1u);
    ASSERT_EQ(table.KeyMode(L"HKEY_CURRENT_USER\\Software\\Vendor"), appbox::RegistryIsolation::Full);
    ASSERT_EQ(table.ValueMode(L"HKEY_CURRENT_USER\\Software\\Vendor", L"Server"), appbox::RegistryIsolation::Hide);
}

/**
 * @brief A listed key covers its whole subtree, including entries the file does
 *        not know about.
 */
TEST(UnitRegistryIsolationTable, KeyModeInheritsFromAncestor)
{
    const std::string text = R"({
        "version": 1,
        "keys": [ { "path": "HKEY_CURRENT_USER\\Software\\Vendor", "isolation": "full" } ]
    })";

    appbox::registry::IsolationTable table;
    std::string                      error;
    ASSERT_TRUE(table.Parse(text, error)) << error;

    ASSERT_EQ(table.KeyMode(L"HKEY_CURRENT_USER\\Software\\Vendor"), appbox::RegistryIsolation::Full);
    ASSERT_EQ(table.KeyMode(L"HKEY_CURRENT_USER\\Software\\Vendor\\Deep\\Key"), appbox::RegistryIsolation::Full);
    ASSERT_EQ(table.ValueMode(L"HKEY_CURRENT_USER\\Software\\Vendor\\Deep", L"Value"),
              appbox::RegistryIsolation::Full);

    /* The siblings above the listed key are untouched. */
    ASSERT_EQ(table.KeyMode(L"HKEY_CURRENT_USER\\Software\\Other"), appbox::RegistryIsolation::WriteCopy);
    ASSERT_EQ(table.KeyMode(L"HKEY_LOCAL_MACHINE"), appbox::RegistryIsolation::WriteCopy);
}

/**
 * @brief A listed value wins over the mode of its key.
 */
TEST(UnitRegistryIsolationTable, ValueModeOfKeyAndAncestor)
{
    const std::string text = R"({
        "version": 1,
        "keys": [ { "path": "HKEY_LOCAL_MACHINE\\Software", "isolation": "full" } ],
        "values": [ { "path": "HKEY_LOCAL_MACHINE\\Software\\Vendor", "name": "Server",
                      "isolation": "write_copy" } ]
    })";

    appbox::registry::IsolationTable table;
    std::string                      error;
    ASSERT_TRUE(table.Parse(text, error)) << error;

    /* The key follows its ancestor, the value overrides the key. */
    ASSERT_EQ(table.KeyMode(L"HKEY_LOCAL_MACHINE\\Software\\Vendor"), appbox::RegistryIsolation::Full);
    ASSERT_EQ(table.ValueMode(L"HKEY_LOCAL_MACHINE\\Software\\Vendor", L"Server"),
              appbox::RegistryIsolation::WriteCopy);
    ASSERT_EQ(table.ValueMode(L"HKEY_LOCAL_MACHINE\\Software\\Vendor", L"Other"), appbox::RegistryIsolation::Full);
}

/**
 * @brief The default value of a key can carry a mode of its own.
 */
TEST(UnitRegistryIsolationTable, DefaultValueEntry)
{
    const std::string text = R"({
        "version": 1,
        "values": [ { "path": "HKEY_CURRENT_USER\\Software\\Vendor", "name": "", "isolation": "full" } ]
    })";

    appbox::registry::IsolationTable table;
    std::string                      error;
    ASSERT_TRUE(table.Parse(text, error)) << error;

    ASSERT_EQ(table.ValueMode(L"HKEY_CURRENT_USER\\Software\\Vendor", L""), appbox::RegistryIsolation::Full);
    ASSERT_EQ(table.ValueMode(L"HKEY_CURRENT_USER\\Software\\Vendor", L"Server"),
              appbox::RegistryIsolation::WriteCopy);
}

/**
 * @brief The lookup ignores the case of the key path and of the value name.
 */
TEST(UnitRegistryIsolationTable, LookupIgnoreCase)
{
    const std::string text = R"({
        "version": 1,
        "keys": [ { "path": "HKEY_CURRENT_USER\\Software\\Vendor", "isolation": "full" } ],
        "values": [ { "path": "HKEY_CURRENT_USER\\Software\\Vendor", "name": "Server", "isolation": "hide" } ]
    })";

    appbox::registry::IsolationTable table;
    std::string                      error;
    ASSERT_TRUE(table.Parse(text, error)) << error;

    ASSERT_EQ(table.KeyMode(L"hkey_current_user\\SOFTWARE\\vendor"), appbox::RegistryIsolation::Full);
    ASSERT_EQ(table.ValueMode(L"HKEY_CURRENT_USER\\SOFTWARE\\VENDOR", L"SERVER"),
              appbox::RegistryIsolation::Hide);
}

/**
 * @brief `Full` and `Hide` hide the host entry, `WriteCopy` does not.
 */
TEST(UnitRegistryIsolationTable, HidesHost)
{
    ASSERT_TRUE(appbox::registry::IsolationTable::HidesHost(appbox::RegistryIsolation::Full));
    ASSERT_TRUE(appbox::registry::IsolationTable::HidesHost(appbox::RegistryIsolation::Hide));
    ASSERT_FALSE(appbox::registry::IsolationTable::HidesHost(appbox::RegistryIsolation::WriteCopy));
}

/**
 * @brief A malformed document is rejected with an error description.
 */
TEST(UnitRegistryIsolationTable, ParseErrors)
{
    appbox::registry::IsolationTable table;
    std::string                      error;

    ASSERT_FALSE(table.Parse("[1, 2]", error));
    ASSERT_FALSE(error.empty());

    ASSERT_FALSE(table.Parse(R"({ "version": 2 })", error));
    ASSERT_FALSE(error.empty());

    ASSERT_FALSE(table.Parse(R"({ "version": 1, "keys": {} })", error));
    ASSERT_FALSE(error.empty());

    ASSERT_FALSE(table.Parse(R"({ "version": 1, "keys": [ { "isolation": "full" } ] })", error));
    ASSERT_FALSE(error.empty());

    ASSERT_FALSE(table.Parse(R"({ "version": 1, "keys": [ { "path": "", "isolation": "full" } ] })", error));
    ASSERT_FALSE(error.empty());

    ASSERT_FALSE(table.Parse(R"({ "version": 1, "keys": [ { "path": "HKEY_CURRENT_USER",
                                                           "isolation": "sandbox" } ] })",
                             error));
    ASSERT_FALSE(error.empty());

    /* The removed mode is an unknown token as well. */
    ASSERT_FALSE(table.Parse(R"({ "version": 1, "keys": [ { "path": "HKEY_CURRENT_USER",
                                                           "isolation": "merge" } ] })",
                             error));
    ASSERT_FALSE(error.empty());

    ASSERT_FALSE(table.Parse(R"({ "version": 1, "values": [ { "path": "HKEY_CURRENT_USER" } ] })", error));
    ASSERT_FALSE(error.empty());

    /* A rejected document leaves the table unchanged. */
    ASSERT_TRUE(table.Empty());
}

/**
 * @brief A document without a version is rejected, an empty list is accepted.
 */
TEST(UnitRegistryIsolationTable, ParseVersionAndEmptyLists)
{
    appbox::registry::IsolationTable table;
    std::string                      error;

    ASSERT_FALSE(table.Parse(R"({ "keys": [] })", error));
    ASSERT_FALSE(error.empty());

    ASSERT_TRUE(table.Parse(R"({ "version": 1, "keys": [], "values": [] })", error)) << error;
    ASSERT_TRUE(table.Empty());
}
