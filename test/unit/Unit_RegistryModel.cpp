#include <gtest/gtest.h>
#include "src/core/RegistryModel.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace
{

/**
 * @brief Whether a row list holds a row of a kind and a name.
 * @param[in] rows Rows to search.
 * @param[in] kind Kind of the row.
 * @param[in] name Name of the row.
 * @return true when the row exists.
 */
bool HasRow(const std::vector<appbox::RegistryRow>& rows, appbox::RegistryRow::Kind kind,
            const std::wstring& name)
{
    for (const auto& row : rows)
    {
        if (row.kind == kind && row.name == name)
        {
            return true;
        }
    }
    return false;
}

} // namespace

TEST(UnitRegistryModel, ResetCreatesTheFiveRootKeysInDisplayOrder)
{
    appbox::RegistryModel model;

    const auto& roots = appbox::RegistryRootKeyNames();
    ASSERT_EQ(roots.size(), 5u);

    const auto& container = model.Root();
    EXPECT_EQ(container.name, appbox::kRegistryContainerLabel);
    EXPECT_FALSE(container.removable);
    ASSERT_EQ(container.children.size(), roots.size());

    for (std::size_t index = 0; index < roots.size(); ++index)
    {
        EXPECT_EQ(container.children[index].name, roots[index]);
        EXPECT_FALSE(container.children[index].removable);
        EXPECT_EQ(container.children[index].isolation, appbox::RegistryIsolation::WriteCopy);
    }
}

TEST(UnitRegistryModel, RowsOfTheContainerListTheRootKeys)
{
    appbox::RegistryModel model;

    const auto rows = model.Rows(L"");
    ASSERT_EQ(rows.size(), appbox::RegistryRootKeyNames().size());

    for (std::size_t index = 0; index < rows.size(); ++index)
    {
        EXPECT_EQ(rows[index].kind, appbox::RegistryRow::Kind::Key);
        EXPECT_EQ(rows[index].name, appbox::RegistryRootKeyNames()[index]);
        EXPECT_TRUE(rows[index].key_path.empty());
    }
}

TEST(UnitRegistryModel, FindKeyIgnoresTheCase)
{
    appbox::RegistryModel model;
    std::string error;

    ASSERT_TRUE(model.AddKey(L"HKEY_CURRENT_USER", L"Vendor", error)) << error;

    EXPECT_NE(model.FindKey(L"hkey_current_user\\vendor"), nullptr);
    EXPECT_NE(model.FindKey(L"HKEY_CURRENT_USER"), nullptr);
    EXPECT_EQ(model.FindKey(L"HKEY_CURRENT_USER\\Missing"), nullptr);
}

TEST(UnitRegistryModel, AddKeyCreatesAndSortsSubKeys)
{
    appbox::RegistryModel model;
    std::string error;

    ASSERT_TRUE(model.AddKey(L"HKEY_CURRENT_USER", L"Vendor", error)) << error;
    ASSERT_TRUE(model.AddKey(L"HKEY_CURRENT_USER", L"Alpha", error)) << error;

    const auto* key = model.FindKey(L"HKEY_CURRENT_USER");
    ASSERT_NE(key, nullptr);
    ASSERT_EQ(key->children.size(), 2u);
    EXPECT_EQ(key->children[0].name, L"Alpha");
    EXPECT_EQ(key->children[1].name, L"Vendor");

    EXPECT_EQ(key->children[0].isolation, appbox::RegistryIsolation::WriteCopy);
}

TEST(UnitRegistryModel, AddKeyRejectsInvalidAndDuplicateNames)
{
    appbox::RegistryModel model;
    std::string error;

    ASSERT_TRUE(model.AddKey(L"HKEY_CURRENT_USER", L"Vendor", error)) << error;

    EXPECT_FALSE(model.AddKey(L"HKEY_CURRENT_USER", L"vendor", error));
    EXPECT_FALSE(error.empty());

    error.clear();
    EXPECT_FALSE(model.AddKey(L"HKEY_CURRENT_USER", L"", error));
    EXPECT_FALSE(error.empty());

    error.clear();
    EXPECT_FALSE(model.AddKey(L"HKEY_CURRENT_USER", L"Sub\\Key", error));
    EXPECT_FALSE(error.empty());

    error.clear();
    EXPECT_FALSE(model.AddKey(L"HKEY_CURRENT_USER\\Missing", L"Sub", error));
    EXPECT_FALSE(error.empty());

    /* The virtual container holds the root keys only. */
    error.clear();
    EXPECT_FALSE(model.AddKey(L"", L"Extra", error));
    EXPECT_FALSE(error.empty());
}

TEST(UnitRegistryModel, EnsureKeyCreatesTheIntermediateKeys)
{
    appbox::RegistryModel model;
    std::string error;

    ASSERT_TRUE(model.EnsureKey(L"HKEY_CURRENT_USER\\Software\\Vendor\\App", error)) << error;

    EXPECT_NE(model.FindKey(L"HKEY_CURRENT_USER\\Software"), nullptr);
    EXPECT_NE(model.FindKey(L"HKEY_CURRENT_USER\\Software\\Vendor"), nullptr);
    EXPECT_NE(model.FindKey(L"HKEY_CURRENT_USER\\Software\\Vendor\\App"), nullptr);

    /* A second call is a no-op and must not duplicate the keys. */
    ASSERT_TRUE(model.EnsureKey(L"HKEY_CURRENT_USER\\Software\\Vendor\\App", error)) << error;
    const auto* software = model.FindKey(L"HKEY_CURRENT_USER\\Software");
    ASSERT_NE(software, nullptr);
    EXPECT_EQ(software->children.size(), 1u);
}

TEST(UnitRegistryModel, EnsureKeyRejectsAnUnknownRootKey)
{
    appbox::RegistryModel model;
    std::string error;

    EXPECT_FALSE(model.EnsureKey(L"HKEY_DYN_DATA\\Software", error));
    EXPECT_FALSE(error.empty());

    error.clear();
    EXPECT_FALSE(model.EnsureKey(L"", error));
    EXPECT_FALSE(error.empty());
}

TEST(UnitRegistryModel, RenameKeyKeepsTheSubtreeAndRejectsRoots)
{
    appbox::RegistryModel model;
    std::string error;

    ASSERT_TRUE(model.EnsureKey(L"HKEY_CURRENT_USER\\Vendor\\App", error)) << error;
    ASSERT_TRUE(model.SetValue(L"HKEY_CURRENT_USER\\Vendor\\App", L"Path", appbox::RegistryValueType::String,
                               appbox::RegistryStringData(L"C:\\App"), error))
        << error;

    ASSERT_TRUE(model.RenameKey(L"HKEY_CURRENT_USER\\Vendor", L"Vendor2", error)) << error;

    const auto* renamed = model.FindKey(L"HKEY_CURRENT_USER\\Vendor2");
    ASSERT_NE(renamed, nullptr);
    ASSERT_EQ(renamed->children.size(), 1u);
    EXPECT_EQ(renamed->children[0].name, L"App");
    EXPECT_EQ(appbox::RegistryStringValue(renamed->children[0].values[0].data), L"C:\\App");
    EXPECT_EQ(model.FindKey(L"HKEY_CURRENT_USER\\Vendor"), nullptr);
    EXPECT_NE(model.FindKey(L"hkey_current_user\\vendor2\\app"), nullptr) << "the match ignores the case";

    error.clear();
    EXPECT_FALSE(model.RenameKey(L"HKEY_CURRENT_USER", L"Other", error));
    EXPECT_FALSE(error.empty());
}

TEST(UnitRegistryModel, RenameKeyRejectsEveryFixedKeyWithoutChangingTheModel)
{
    appbox::RegistryModel model;
    std::string error;

    ASSERT_TRUE(model.EnsureKey(L"HKEY_CURRENT_USER\\Vendor", error)) << error;
    ASSERT_TRUE(model.SetValue(L"HKEY_CURRENT_USER\\Vendor", L"Path", appbox::RegistryValueType::String,
                               appbox::RegistryStringData(L"C:\\App"), error))
        << error;

    const auto& roots = appbox::RegistryRootKeyNames();
    for (std::size_t index = 0; index < roots.size(); ++index)
    {
        error.clear();
        EXPECT_FALSE(model.RenameKey(roots[index], L"Renamed", error)) << "root key " << index;
        EXPECT_FALSE(error.empty()) << "root key " << index;
    }

    /* The container is fixed as well, so it cannot be renamed either. */
    error.clear();
    EXPECT_FALSE(model.RenameKey(L"", L"Renamed", error));
    EXPECT_FALSE(error.empty());

    /* A rejected rename leaves the registry untouched. */
    EXPECT_EQ(model.FindKey(L"Renamed"), nullptr);

    const auto& container = model.Root();
    ASSERT_EQ(container.children.size(), roots.size());
    for (std::size_t index = 0; index < roots.size(); ++index)
    {
        EXPECT_EQ(container.children[index].name, roots[index]);
        EXPECT_FALSE(container.children[index].removable);
    }

    const auto* vendor = model.FindKey(L"HKEY_CURRENT_USER\\Vendor");
    ASSERT_NE(vendor, nullptr);
    ASSERT_EQ(vendor->values.size(), 1u);
    EXPECT_EQ(vendor->values[0].name, L"Path");
    EXPECT_EQ(appbox::RegistryStringValue(vendor->values[0].data), L"C:\\App");
}

TEST(UnitRegistryModel, RenameKeyRejectsAnExistingSiblingAndKeepsTheCaseChange)
{
    appbox::RegistryModel model;
    std::string error;

    ASSERT_TRUE(model.AddKey(L"HKEY_CURRENT_USER", L"Alpha", error)) << error;
    ASSERT_TRUE(model.AddKey(L"HKEY_CURRENT_USER", L"Beta", error)) << error;

    error.clear();
    EXPECT_FALSE(model.RenameKey(L"HKEY_CURRENT_USER\\Alpha", L"beta", error));
    EXPECT_FALSE(error.empty());

    /* Renaming a key onto its own name with another case is allowed. */
    error.clear();
    ASSERT_TRUE(model.RenameKey(L"HKEY_CURRENT_USER\\Alpha", L"ALPHA", error)) << error;
    const auto* key = model.FindKey(L"HKEY_CURRENT_USER");
    ASSERT_NE(key, nullptr);
    EXPECT_EQ(key->children[0].name, L"ALPHA");
}

TEST(UnitRegistryModel, RemoveKeyDropsTheSubtreeAndRejectsRoots)
{
    appbox::RegistryModel model;
    std::string error;

    ASSERT_TRUE(model.EnsureKey(L"HKEY_CURRENT_USER\\Vendor\\App", error)) << error;
    ASSERT_TRUE(model.RemoveKey(L"HKEY_CURRENT_USER\\Vendor", error)) << error;

    EXPECT_EQ(model.FindKey(L"HKEY_CURRENT_USER\\Vendor"), nullptr);
    EXPECT_EQ(model.FindKey(L"HKEY_CURRENT_USER\\Vendor\\App"), nullptr);

    error.clear();
    EXPECT_FALSE(model.RemoveKey(L"HKEY_CURRENT_USER", error));
    EXPECT_FALSE(error.empty());

    error.clear();
    EXPECT_FALSE(model.RemoveKey(L"HKEY_CURRENT_USER\\Missing", error));
    EXPECT_FALSE(error.empty());
}

TEST(UnitRegistryModel, RowsListTheSubKeysBeforeTheValues)
{
    appbox::RegistryModel model;
    std::string error;

    ASSERT_TRUE(model.AddKey(L"HKEY_CURRENT_USER", L"Vendor", error)) << error;
    ASSERT_TRUE(model.AddValue(L"HKEY_CURRENT_USER", L"Beta", appbox::RegistryValueType::Dword,
                               appbox::RegistryDwordData(30), error))
        << error;
    ASSERT_TRUE(model.AddValue(L"HKEY_CURRENT_USER", L"Alpha", appbox::RegistryValueType::String,
                               appbox::RegistryStringData(L"text"), error))
        << error;

    const auto rows = model.Rows(L"HKEY_CURRENT_USER");
    ASSERT_EQ(rows.size(), 3u);

    EXPECT_EQ(rows[0].kind, appbox::RegistryRow::Kind::Key);
    EXPECT_EQ(rows[0].name, L"Vendor");
    EXPECT_EQ(rows[0].key_path, L"HKEY_CURRENT_USER");

    EXPECT_EQ(rows[1].kind, appbox::RegistryRow::Kind::Value);
    EXPECT_EQ(rows[1].name, L"Alpha");
    EXPECT_EQ(rows[1].type, appbox::RegistryValueType::String);
    EXPECT_EQ(appbox::RegistryStringValue(rows[1].data), L"text");

    EXPECT_EQ(rows[2].name, L"Beta");
    EXPECT_EQ(rows[2].type, appbox::RegistryValueType::Dword);

    EXPECT_TRUE(model.Rows(L"HKEY_CURRENT_USER\\Missing").empty());
}

TEST(UnitRegistryModel, AddValueAcceptsTheDefaultValueAndRejectsDuplicates)
{
    appbox::RegistryModel model;
    std::string error;

    ASSERT_TRUE(model.AddValue(L"HKEY_CURRENT_USER", L"", appbox::RegistryValueType::String,
                               appbox::RegistryStringData(L"default"), error))
        << error;
    EXPECT_EQ(model.FindKey(L"HKEY_CURRENT_USER")->values.size(), 1u);

    error.clear();
    EXPECT_FALSE(model.AddValue(L"HKEY_CURRENT_USER", L"", appbox::RegistryValueType::String, {}, error));
    EXPECT_FALSE(error.empty());

    error.clear();
    EXPECT_FALSE(model.AddValue(L"HKEY_CURRENT_USER", L"a\\b", appbox::RegistryValueType::String, {}, error));
    EXPECT_FALSE(error.empty());

    error.clear();
    EXPECT_FALSE(model.AddValue(L"", L"Value", appbox::RegistryValueType::String, {}, error));
    EXPECT_FALSE(error.empty());
}

TEST(UnitRegistryModel, SetValueOverwritesAndKeepsTheIsolationMode)
{
    appbox::RegistryModel model;
    std::string error;

    ASSERT_TRUE(model.SetValue(L"HKEY_CURRENT_USER", L"Path", appbox::RegistryValueType::String,
                               appbox::RegistryStringData(L"C:\\Old"), error))
        << error;
    ASSERT_TRUE(model.SetValueIsolation(L"HKEY_CURRENT_USER", L"Path", appbox::RegistryIsolation::Hide));

    ASSERT_TRUE(model.SetValue(L"HKEY_CURRENT_USER", L"Path", appbox::RegistryValueType::String,
                               appbox::RegistryStringData(L"C:\\New"), error))
        << error;

    const auto& values = model.FindKey(L"HKEY_CURRENT_USER")->values;
    ASSERT_EQ(values.size(), 1u);
    EXPECT_EQ(appbox::RegistryStringValue(values[0].data), L"C:\\New");
    EXPECT_EQ(values[0].isolation, appbox::RegistryIsolation::Hide);
}

TEST(UnitRegistryModel, UpdateValueReplacesNameTypeAndData)
{
    appbox::RegistryModel model;
    std::string error;

    ASSERT_TRUE(model.AddValue(L"HKEY_CURRENT_USER", L"Old", appbox::RegistryValueType::String,
                               appbox::RegistryStringData(L"text"), error))
        << error;
    ASSERT_TRUE(model.UpdateValue(L"HKEY_CURRENT_USER", L"Old", L"New", appbox::RegistryValueType::Dword,
                                  appbox::RegistryDwordData(7), error))
        << error;

    const auto& values = model.FindKey(L"HKEY_CURRENT_USER")->values;
    ASSERT_EQ(values.size(), 1u);
    EXPECT_EQ(values[0].name, L"New");
    EXPECT_EQ(values[0].type, appbox::RegistryValueType::Dword);

    std::uint32_t number = 0;
    ASSERT_TRUE(appbox::RegistryDwordValue(values[0].data, number));
    EXPECT_EQ(number, 7u);

    error.clear();
    EXPECT_FALSE(model.UpdateValue(L"HKEY_CURRENT_USER", L"Missing", L"Other",
                                   appbox::RegistryValueType::Dword, {}, error));
    EXPECT_FALSE(error.empty());
}

TEST(UnitRegistryModel, UpdateValueRejectsTheNameOfAnotherValue)
{
    appbox::RegistryModel model;
    std::string error;

    ASSERT_TRUE(model.AddValue(L"HKEY_CURRENT_USER", L"Alpha", appbox::RegistryValueType::String, {}, error))
        << error;
    ASSERT_TRUE(model.AddValue(L"HKEY_CURRENT_USER", L"Beta", appbox::RegistryValueType::String, {}, error))
        << error;

    error.clear();
    EXPECT_FALSE(model.UpdateValue(L"HKEY_CURRENT_USER", L"Alpha", L"beta",
                                   appbox::RegistryValueType::String, {}, error));
    EXPECT_FALSE(error.empty());

    /* Renaming a value onto its own name is allowed. */
    error.clear();
    ASSERT_TRUE(model.UpdateValue(L"HKEY_CURRENT_USER", L"Alpha", L"Alpha",
                                  appbox::RegistryValueType::String, {}, error))
        << error;
}

TEST(UnitRegistryModel, RemoveValueReportsWhetherTheValueExisted)
{
    appbox::RegistryModel model;
    std::string error;

    ASSERT_TRUE(model.AddValue(L"HKEY_CURRENT_USER", L"Alpha", appbox::RegistryValueType::String, {}, error))
        << error;

    EXPECT_TRUE(model.RemoveValue(L"HKEY_CURRENT_USER", L"alpha"));
    EXPECT_FALSE(model.RemoveValue(L"HKEY_CURRENT_USER", L"Alpha"));
    EXPECT_FALSE(model.RemoveValue(L"HKEY_CURRENT_USER\\Missing", L"Alpha"));
}

TEST(UnitRegistryModel, SetKeyIsolationOnlyChangesTheKey)
{
    appbox::RegistryModel model;
    std::string error;

    ASSERT_TRUE(model.EnsureKey(L"HKEY_CURRENT_USER\\Vendor\\App", error)) << error;
    ASSERT_TRUE(model.SetValue(L"HKEY_CURRENT_USER\\Vendor\\App", L"Path",
                               appbox::RegistryValueType::String, {}, error))
        << error;
    ASSERT_TRUE(model.SetValue(L"HKEY_CURRENT_USER\\Vendor", L"Name", appbox::RegistryValueType::String, {},
                               error))
        << error;

    ASSERT_TRUE(model.SetKeyIsolation(L"HKEY_CURRENT_USER\\Vendor", appbox::RegistryIsolation::Full));

    const auto* vendor = model.FindKey(L"HKEY_CURRENT_USER\\Vendor");
    ASSERT_NE(vendor, nullptr);
    EXPECT_EQ(vendor->isolation, appbox::RegistryIsolation::Full);

    /* The sub key and every value keep the mode they held before. */
    ASSERT_EQ(vendor->children.size(), 1u);
    EXPECT_EQ(vendor->children[0].isolation, appbox::RegistryIsolation::WriteCopy);
    ASSERT_EQ(vendor->children[0].values.size(), 1u);
    EXPECT_EQ(vendor->children[0].values[0].isolation, appbox::RegistryIsolation::WriteCopy);

    ASSERT_EQ(vendor->values.size(), 1u);
    EXPECT_EQ(vendor->values[0].isolation, appbox::RegistryIsolation::WriteCopy);

    /* The key above the changed key is untouched as well. */
    const auto* current_user = model.FindKey(L"HKEY_CURRENT_USER");
    ASSERT_NE(current_user, nullptr);
    EXPECT_EQ(current_user->isolation, appbox::RegistryIsolation::WriteCopy);

    EXPECT_FALSE(model.SetKeyIsolation(L"HKEY_CURRENT_USER\\Missing", appbox::RegistryIsolation::Full));
}

TEST(UnitRegistryModel, ApplyIsolationToSubtreeOverwritesEveryKeyAndValue)
{
    appbox::RegistryModel model;
    std::string error;

    ASSERT_TRUE(model.EnsureKey(L"HKEY_CURRENT_USER\\Vendor\\App\\Sub", error)) << error;
    ASSERT_TRUE(model.SetValue(L"HKEY_CURRENT_USER\\Vendor", L"Name", appbox::RegistryValueType::String, {},
                               error))
        << error;
    ASSERT_TRUE(model.SetValue(L"HKEY_CURRENT_USER\\Vendor\\App", L"Path",
                               appbox::RegistryValueType::String, {}, error))
        << error;

    /* Entries below the key are changed before the subtree is overwritten. */
    ASSERT_TRUE(model.SetKeyIsolation(L"HKEY_CURRENT_USER\\Vendor\\App", appbox::RegistryIsolation::Hide));
    ASSERT_TRUE(model.SetValueIsolation(L"HKEY_CURRENT_USER\\Vendor\\App", L"Path",
                                        appbox::RegistryIsolation::WriteCopy));

    ASSERT_TRUE(model.ApplyIsolationToSubtree(L"HKEY_CURRENT_USER\\Vendor", appbox::RegistryIsolation::Full,
                                              true));

    const auto* vendor = model.FindKey(L"HKEY_CURRENT_USER\\Vendor");
    ASSERT_NE(vendor, nullptr);
    EXPECT_EQ(vendor->isolation, appbox::RegistryIsolation::Full);
    ASSERT_EQ(vendor->values.size(), 1u);
    EXPECT_EQ(vendor->values[0].isolation, appbox::RegistryIsolation::Full);

    /* A mode which was changed before is overwritten without an exception. */
    ASSERT_EQ(vendor->children.size(), 1u);
    const auto& app = vendor->children[0];
    EXPECT_EQ(app.isolation, appbox::RegistryIsolation::Full);
    ASSERT_EQ(app.values.size(), 1u);
    EXPECT_EQ(app.values[0].isolation, appbox::RegistryIsolation::Full);

    ASSERT_EQ(app.children.size(), 1u);
    EXPECT_EQ(app.children[0].isolation, appbox::RegistryIsolation::Full);

    EXPECT_FALSE(model.ApplyIsolationToSubtree(L"HKEY_CURRENT_USER\\Missing",
                                               appbox::RegistryIsolation::Full, true));
}

TEST(UnitRegistryModel, ApplyIsolationToSubtreeKeepsTheValuesWhenTheyAreNotIncluded)
{
    appbox::RegistryModel model;
    std::string error;

    ASSERT_TRUE(model.EnsureKey(L"HKEY_CURRENT_USER\\Vendor\\App", error)) << error;
    ASSERT_TRUE(model.SetValue(L"HKEY_CURRENT_USER\\Vendor", L"Name", appbox::RegistryValueType::String, {},
                               error))
        << error;
    ASSERT_TRUE(model.SetValue(L"HKEY_CURRENT_USER\\Vendor\\App", L"Path",
                               appbox::RegistryValueType::String, {}, error))
        << error;

    ASSERT_TRUE(model.ApplyIsolationToSubtree(L"HKEY_CURRENT_USER\\Vendor", appbox::RegistryIsolation::Hide,
                                              false));

    const auto* vendor = model.FindKey(L"HKEY_CURRENT_USER\\Vendor");
    ASSERT_NE(vendor, nullptr);
    EXPECT_EQ(vendor->isolation, appbox::RegistryIsolation::Hide);
    ASSERT_EQ(vendor->values.size(), 1u);
    EXPECT_EQ(vendor->values[0].isolation, appbox::RegistryIsolation::WriteCopy);

    ASSERT_EQ(vendor->children.size(), 1u);
    EXPECT_EQ(vendor->children[0].isolation, appbox::RegistryIsolation::Hide);
    ASSERT_EQ(vendor->children[0].values.size(), 1u);
    EXPECT_EQ(vendor->children[0].values[0].isolation, appbox::RegistryIsolation::WriteCopy);
}

TEST(UnitRegistryModel, NewEntriesFollowTheKeyTheyAreCreatedIn)
{
    appbox::RegistryModel model;
    std::string error;

    ASSERT_TRUE(model.EnsureKey(L"HKEY_CURRENT_USER\\Vendor", error)) << error;
    ASSERT_TRUE(model.SetKeyIsolation(L"HKEY_CURRENT_USER\\Vendor", appbox::RegistryIsolation::Hide));

    /* The intermediate keys of an import follow the closest key above them. */
    ASSERT_TRUE(model.EnsureKey(L"HKEY_CURRENT_USER\\Vendor\\App\\Sub", error)) << error;

    const auto* app = model.FindKey(L"HKEY_CURRENT_USER\\Vendor\\App");
    ASSERT_NE(app, nullptr);
    EXPECT_EQ(app->isolation, appbox::RegistryIsolation::Hide);
    ASSERT_EQ(app->children.size(), 1u);
    EXPECT_EQ(app->children[0].isolation, appbox::RegistryIsolation::Hide);

    /* Keys and values added by hand follow the key they are added to as well. */
    ASSERT_TRUE(model.AddKey(L"HKEY_CURRENT_USER\\Vendor\\App", L"Child", error)) << error;
    ASSERT_TRUE(model.AddValue(L"HKEY_CURRENT_USER\\Vendor", L"Name", appbox::RegistryValueType::String, {},
                               error))
        << error;
    ASSERT_TRUE(model.SetValue(L"HKEY_CURRENT_USER\\Vendor", L"Other", appbox::RegistryValueType::String, {},
                               error))
        << error;

    const auto* vendor = model.FindKey(L"HKEY_CURRENT_USER\\Vendor");
    ASSERT_NE(vendor, nullptr);
    ASSERT_EQ(vendor->values.size(), 2u);
    EXPECT_EQ(vendor->values[0].name, L"Name");
    EXPECT_EQ(vendor->values[0].isolation, appbox::RegistryIsolation::Hide);
    EXPECT_EQ(vendor->values[1].name, L"Other");
    EXPECT_EQ(vendor->values[1].isolation, appbox::RegistryIsolation::Hide);

    const auto* child = model.FindKey(L"HKEY_CURRENT_USER\\Vendor\\App\\Child");
    ASSERT_NE(child, nullptr);
    EXPECT_EQ(child->isolation, appbox::RegistryIsolation::Hide);
}

TEST(UnitRegistryModel, SetValueIsolationOnlyChangesTheValue)
{
    appbox::RegistryModel model;
    std::string error;

    ASSERT_TRUE(model.EnsureKey(L"HKEY_CURRENT_USER\\Vendor", error)) << error;
    ASSERT_TRUE(model.SetValue(L"HKEY_CURRENT_USER\\Vendor", L"Name", appbox::RegistryValueType::String, {},
                               error))
        << error;
    ASSERT_TRUE(model.SetValue(L"HKEY_CURRENT_USER\\Vendor", L"Other", appbox::RegistryValueType::String, {},
                               error))
        << error;

    ASSERT_TRUE(model.SetValueIsolation(L"HKEY_CURRENT_USER\\Vendor", L"Name",
                                        appbox::RegistryIsolation::Full));

    const auto* vendor = model.FindKey(L"HKEY_CURRENT_USER\\Vendor");
    ASSERT_NE(vendor, nullptr);
    EXPECT_EQ(vendor->isolation, appbox::RegistryIsolation::WriteCopy);
    ASSERT_EQ(vendor->values.size(), 2u);
    EXPECT_EQ(vendor->values[0].name, L"Name");
    EXPECT_EQ(vendor->values[0].isolation, appbox::RegistryIsolation::Full);
    EXPECT_EQ(vendor->values[1].isolation, appbox::RegistryIsolation::WriteCopy);

    EXPECT_FALSE(model.SetKeyIsolation(L"HKEY_CURRENT_USER\\Missing", appbox::RegistryIsolation::Full));
    EXPECT_FALSE(model.SetValueIsolation(L"HKEY_CURRENT_USER\\Vendor", L"Missing",
                                         appbox::RegistryIsolation::Full));
}

TEST(UnitRegistryModel, IsolationNamesCoverEveryMode)
{
    const auto& names = appbox::RegistryIsolationNames();
    ASSERT_EQ(names.size(), 3u);
    EXPECT_EQ(names[0], L"Full");
    EXPECT_EQ(names[1], L"Write Copy");
    EXPECT_EQ(names[2], L"Hide");

    EXPECT_EQ(appbox::RegistryIsolationName(appbox::RegistryIsolation::WriteCopy), L"Write Copy");
}

TEST(UnitRegistryModel, ValueTypeNamesResolveBothWays)
{
    const auto& types = appbox::RegistryValueTypes();
    ASSERT_EQ(types.size(), 7u);

    for (const auto type : types)
    {
        appbox::RegistryValueType parsed = appbox::RegistryValueType::None;
        EXPECT_TRUE(appbox::ParseRegistryValueType(appbox::RegistryValueTypeName(type), parsed));
        EXPECT_EQ(parsed, type);

        appbox::RegistryValueType resolved = appbox::RegistryValueType::None;
        EXPECT_TRUE(appbox::ResolveRegistryValueType(static_cast<std::uint32_t>(type), resolved));
        EXPECT_EQ(resolved, type);
    }

    EXPECT_EQ(appbox::RegistryValueTypeName(appbox::RegistryValueType::Qword), L"REG_QWORD");

    appbox::RegistryValueType parsed = appbox::RegistryValueType::None;
    EXPECT_TRUE(appbox::ParseRegistryValueType(L"reg_dword", parsed));
    EXPECT_EQ(parsed, appbox::RegistryValueType::Dword);

    EXPECT_FALSE(appbox::ParseRegistryValueType(L"REG_UNKNOWN", parsed));
    EXPECT_FALSE(appbox::ResolveRegistryValueType(5, parsed));
}

TEST(UnitRegistryModel, StringAndMultiStringDataRoundTrip)
{
    const auto text = appbox::RegistryStringValue(appbox::RegistryStringData(L"Hello"));
    EXPECT_EQ(text, L"Hello");

    const std::vector<std::wstring> parts = { L"one", L"two" };
    const auto data = appbox::RegistryMultiStringData(parts);
    EXPECT_EQ(appbox::RegistryMultiStringValue(data), parts);

    EXPECT_TRUE(appbox::RegistryMultiStringValue(appbox::RegistryMultiStringData({})).empty());
}

TEST(UnitRegistryModel, NumberDataRoundTrip)
{
    std::uint32_t dword = 0;
    ASSERT_TRUE(appbox::RegistryDwordValue(appbox::RegistryDwordData(0x12345678u), dword));
    EXPECT_EQ(dword, 0x12345678u);

    std::uint64_t qword = 0;
    ASSERT_TRUE(appbox::RegistryQwordValue(appbox::RegistryQwordData(0x1122334455667788ULL), qword));
    EXPECT_EQ(qword, 0x1122334455667788ULL);

    EXPECT_FALSE(appbox::RegistryDwordValue({ 1, 2 }, dword));
    EXPECT_FALSE(appbox::RegistryQwordValue({ 1, 2 }, qword));
}

TEST(UnitRegistryModel, HexTextRoundTripAndErrors)
{
    const std::vector<std::uint8_t> bytes = { 0x0A, 0xB1, 0x00 };
    const auto text = appbox::FormatRegistryHexText(bytes);
    EXPECT_EQ(text, L"0A B1 00");

    std::vector<std::uint8_t> parsed;
    std::string error;
    ASSERT_TRUE(appbox::ParseRegistryHexText(L"0a,b1, 00", parsed, error)) << error;
    EXPECT_EQ(parsed, bytes);

    error.clear();
    EXPECT_FALSE(appbox::ParseRegistryHexText(L"0a b", parsed, error));
    EXPECT_FALSE(error.empty());

    error.clear();
    EXPECT_FALSE(appbox::ParseRegistryHexText(L"zz", parsed, error));
    EXPECT_FALSE(error.empty());
}

TEST(UnitRegistryModel, ValueTextParsingFollowsTheType)
{
    std::vector<std::uint8_t> data;
    std::string error;

    ASSERT_TRUE(appbox::ParseRegistryValueText(appbox::RegistryValueType::Dword, L"0x10", data, error))
        << error;
    std::uint32_t number = 0;
    ASSERT_TRUE(appbox::RegistryDwordValue(data, number));
    EXPECT_EQ(number, 16u);

    error.clear();
    ASSERT_TRUE(appbox::ParseRegistryValueText(appbox::RegistryValueType::Qword, L"123", data, error))
        << error;
    std::uint64_t wide = 0;
    ASSERT_TRUE(appbox::RegistryQwordValue(data, wide));
    EXPECT_EQ(wide, 123u);

    error.clear();
    ASSERT_TRUE(appbox::ParseRegistryValueText(appbox::RegistryValueType::MultiString, L"one\r\ntwo", data,
                                               error))
        << error;
    const std::vector<std::wstring> parts = { L"one", L"two" };
    EXPECT_EQ(appbox::RegistryMultiStringValue(data), parts);

    error.clear();
    ASSERT_TRUE(appbox::ParseRegistryValueText(appbox::RegistryValueType::Binary, L"01,02", data, error))
        << error;
    EXPECT_EQ(data, (std::vector<std::uint8_t>{ 0x01, 0x02 }));

    error.clear();
    EXPECT_FALSE(appbox::ParseRegistryValueText(appbox::RegistryValueType::Dword, L"abc", data, error));
    EXPECT_FALSE(error.empty());

    error.clear();
    EXPECT_FALSE(appbox::ParseRegistryValueText(appbox::RegistryValueType::Binary, L"0", data, error));
    EXPECT_FALSE(error.empty());
}

TEST(UnitRegistryModel, ValueTextFormattingFollowsTheType)
{
    EXPECT_EQ(appbox::FormatRegistryValueText(appbox::RegistryValueType::String,
                                              appbox::RegistryStringData(L"text")),
              L"text");

    const auto parts = appbox::RegistryMultiStringData({ L"one", L"two" });
    EXPECT_EQ(appbox::FormatRegistryValueText(appbox::RegistryValueType::MultiString, parts), L"one\r\ntwo");
    EXPECT_EQ(appbox::FormatRegistryValueData(appbox::RegistryValueType::MultiString, parts, 100),
              L"one; two");

    EXPECT_EQ(appbox::FormatRegistryValueData(appbox::RegistryValueType::Dword,
                                              appbox::RegistryDwordData(30), 100),
              L"0x0000001E (30)");

    /* The value column truncates long data. */
    const auto long_text = appbox::FormatRegistryValueData(appbox::RegistryValueType::String,
                                                           appbox::RegistryStringData(L"0123456789"), 8);
    EXPECT_EQ(long_text, L"01234...");
}

TEST(UnitRegistryModel, PathHelpersNormalizeAndSplit)
{
    const auto parts = appbox::SplitRegistryPath(L"HKEY_CURRENT_USER\\Software\\\\Vendor\\");
    ASSERT_EQ(parts.size(), 3u);
    EXPECT_EQ(parts[0], L"HKEY_CURRENT_USER");
    EXPECT_EQ(parts[2], L"Vendor");

    EXPECT_TRUE(appbox::SplitRegistryPath(L"").empty());

    EXPECT_EQ(appbox::JoinRegistryPath(L"HKEY_CURRENT_USER", L"Software"), L"HKEY_CURRENT_USER\\Software");
    EXPECT_EQ(appbox::JoinRegistryPath(L"", L"HKEY_USERS"), L"HKEY_USERS");
    EXPECT_EQ(appbox::JoinRegistryPath(L"HKEY_USERS", L""), L"HKEY_USERS");

    EXPECT_EQ(appbox::NormalizeRegistryPath(L"hkey_users//Software/"), L"hkey_users\\Software");
    EXPECT_EQ(appbox::RegistryParentPath(L"HKEY_USERS\\Software\\Vendor"), L"HKEY_USERS\\Software");
    EXPECT_EQ(appbox::RegistryParentPath(L"HKEY_USERS"), L"");
    EXPECT_EQ(appbox::RegistryLeafName(L"HKEY_USERS\\Software"), L"Software");

    EXPECT_TRUE(appbox::RegistryPathEquals(L"HKLM\\Software", L"hklm\\software"));
    EXPECT_FALSE(appbox::RegistryPathEquals(L"HKLM\\Software", L"HKLM\\Software\\Vendor"));

    EXPECT_TRUE(appbox::IsValidRegistryName(L"Name", false));
    EXPECT_TRUE(appbox::IsValidRegistryName(L"", true));
    EXPECT_FALSE(appbox::IsValidRegistryName(L"", false));
    EXPECT_FALSE(appbox::IsValidRegistryName(L"A\\B", true));
}
