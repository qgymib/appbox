#include "utils/WinAPI.h" /* Must be first include file */
#include <gtest/gtest.h>
#include "utils/VariableExpansion.hpp"
#include <cstring>
#include <string>
#include <vector>

namespace
{

using appbox::ExpandRegistryValueData;
using appbox::ExpandVariables;
using appbox::VariableMapping;

/**
 * @brief Build the variable table of a case.
 *
 * The paths are spelled like the ones of a real machine, so a case reads the
 * same way as the documentation of the feature.
 *
 * @return The variables of the case.
 */
std::vector<VariableMapping> Table()
{
    return {
        { L"ProgramFiles", L"C:\\Program Files"            },
        { L"USERPROFILE",  L"C:\\Users\\appbox"            },
        { L"Documents",    L"C:\\Users\\appbox\\Documents" }
    };
}

/**
 * @brief Build the data of a registry string value from a text.
 * @param[in] text Text of the value.
 * @return The data of the value, without a terminator.
 */
std::vector<BYTE> Bytes(const std::wstring& text)
{
    std::vector<BYTE> data(text.size() * sizeof(wchar_t));
    if (!data.empty())
    {
        memcpy(data.data(), text.data(), data.size());
    }
    return data;
}

/**
 * @brief Append the terminator of a registry string value.
 * @param[in] text Text of the value.
 * @return The text with a null character behind it.
 */
std::wstring Terminated(std::wstring text)
{
    text.push_back(L'\0');
    return text;
}

/**
 * @brief Build the data of a `REG_MULTI_SZ` value from its items.
 * @param[in] items Items of the list.
 * @return The text of the value, items separated and terminated by null
 *         characters.
 */
std::wstring MultiString(const std::vector<std::wstring>& items)
{
    std::wstring text;
    for (const auto& item : items)
    {
        text.append(item);
        text.push_back(L'\0');
    }
    text.push_back(L'\0');
    return text;
}

} // namespace

TEST(Unit_VariableExpansion, KnownReferenceIsReplaced)
{
    EXPECT_EQ(ExpandVariables(L"%APPBOX:ProgramFiles%\\Foo\\Bar", Table()), L"C:\\Program Files\\Foo\\Bar");
}

/**
 * @brief The prefix and the name of a reference are compared ignoring the
 *        case, because the layer keys of the packer are spelled in different
 *        cases.
 */
TEST(Unit_VariableExpansion, PrefixAndNameIgnoreTheCase)
{
    EXPECT_EQ(ExpandVariables(L"%appbox:programfiles%", Table()), L"C:\\Program Files");
    EXPECT_EQ(ExpandVariables(L"%APPBOX:userprofile%", Table()), L"C:\\Users\\appbox");
    EXPECT_EQ(ExpandVariables(L"%AppBox:Documents%", Table()), L"C:\\Users\\appbox\\Documents");
}

TEST(Unit_VariableExpansion, SeveralReferencesAreReplaced)
{
    EXPECT_EQ(ExpandVariables(L"%APPBOX:Documents%;%APPBOX:ProgramFiles%\\x", Table()),
              L"C:\\Users\\appbox\\Documents;C:\\Program Files\\x");
}

TEST(Unit_VariableExpansion, UnknownNameKeepsItsSpelling)
{
    EXPECT_EQ(ExpandVariables(L"%APPBOX:Missing%\\x", Table()), L"%APPBOX:Missing%\\x");
}

/**
 * @brief A `%NAME%` reference belongs to the shell and a name without the
 *        prefix of the sandbox is not a variable of the workspace.
 */
TEST(Unit_VariableExpansion, ShellReferenceIsKept)
{
    EXPECT_EQ(ExpandVariables(L"%PATH%", Table()), L"%PATH%");
    EXPECT_EQ(ExpandVariables(L"%APPBOX%", Table()), L"%APPBOX%");
}

TEST(Unit_VariableExpansion, IncompleteReferenceKeepsItsSpelling)
{
    EXPECT_EQ(ExpandVariables(L"100%", Table()), L"100%");
    EXPECT_EQ(ExpandVariables(L"%APPBOX:Documents", Table()), L"%APPBOX:Documents");
    EXPECT_EQ(ExpandVariables(L"%%", Table()), L"%%");
    EXPECT_EQ(ExpandVariables(L"%APPBOX:%", Table()), L"%APPBOX:%");
}

TEST(Unit_VariableExpansion, EmptyTextIsKept)
{
    EXPECT_EQ(ExpandVariables(L"", Table()), L"");
    EXPECT_EQ(ExpandVariables(L"", {}), L"");
}

TEST(Unit_VariableExpansion, AnEmptyTableKeepsTheText)
{
    EXPECT_EQ(ExpandVariables(L"%APPBOX:Documents%\\x", {}), L"%APPBOX:Documents%\\x");
}

/**
 * @brief An expansion is never resolved again: the text a reference was
 *        replaced with is copied as it is.
 */
TEST(Unit_VariableExpansion, AnExpansionIsNotResolvedAgain)
{
    const std::vector<VariableMapping> table = {
        { L"A", L"%APPBOX:B%" },
        { L"B", L"C:\\b"      },
    };

    EXPECT_EQ(ExpandVariables(L"%APPBOX:A%", table), L"%APPBOX:B%");
}

TEST(Unit_VariableExpansion, RegSzIsExpanded)
{
    EXPECT_EQ(ExpandRegistryValueData(REG_SZ, Bytes(L"%APPBOX:Documents%\\x"), Table()),
              Bytes(L"C:\\Users\\appbox\\Documents\\x"));
}

TEST(Unit_VariableExpansion, RegSzKeepsTheTerminator)
{
    EXPECT_EQ(ExpandRegistryValueData(REG_SZ, Bytes(Terminated(L"%APPBOX:Documents%")), Table()),
              Bytes(Terminated(L"C:\\Users\\appbox\\Documents")));
}

TEST(Unit_VariableExpansion, RegExpandSzIsExpanded)
{
    EXPECT_EQ(ExpandRegistryValueData(REG_EXPAND_SZ, Bytes(Terminated(L"%APPBOX:USERPROFILE%\\x")), Table()),
              Bytes(Terminated(L"C:\\Users\\appbox\\x")));
}

/**
 * @brief Every item of a `REG_MULTI_SZ` list is expanded on its own and the
 *        separators of the list are kept exactly as they are.
 */
TEST(Unit_VariableExpansion, RegMultiSzExpandsEveryItem)
{
    const std::wstring data = MultiString({ L"%APPBOX:Documents%\\a", L"plain" });
    const std::wstring expected = MultiString({ L"C:\\Users\\appbox\\Documents\\a", L"plain" });

    EXPECT_EQ(ExpandRegistryValueData(REG_MULTI_SZ, Bytes(data), Table()), Bytes(expected));
}

TEST(Unit_VariableExpansion, RegMultiSzKeepsAnEmptyItem)
{
    const std::wstring data = MultiString({ L"%APPBOX:Documents%", L"", L"x" });
    const std::wstring expected = MultiString({ L"C:\\Users\\appbox\\Documents", L"", L"x" });

    EXPECT_EQ(ExpandRegistryValueData(REG_MULTI_SZ, Bytes(data), Table()), Bytes(expected));
}

TEST(Unit_VariableExpansion, OtherTypesAreUntouched)
{
    const std::vector<BYTE> dword = { 0x78, 0x56, 0x34, 0x12 };
    EXPECT_EQ(ExpandRegistryValueData(REG_DWORD, dword, Table()), dword);

    /* The bytes of `%Doc%`, which no type but a string type reads as text. */
    const std::vector<BYTE> binary = { 0x25, 0x44, 0x6F, 0x63, 0x25 };
    EXPECT_EQ(ExpandRegistryValueData(REG_BINARY, binary, Table()), binary);

    EXPECT_TRUE(ExpandRegistryValueData(REG_NONE, {}, Table()).empty());
}

TEST(Unit_VariableExpansion, DataWhichIsNotMadeOfWholeWideCharactersIsUntouched)
{
    const std::vector<BYTE> odd = { 0x25, 0x41, 0x50 };
    EXPECT_EQ(ExpandRegistryValueData(REG_SZ, odd, Table()), odd);
}

TEST(Unit_VariableExpansion, AValueWithoutAReferenceIsUnchanged)
{
    const std::vector<BYTE> data = Bytes(L"C:\\plain\\x");
    EXPECT_EQ(ExpandRegistryValueData(REG_SZ, data, Table()), data);
}

TEST(Unit_VariableExpansion, AnEmptyStringValueIsUnchanged)
{
    EXPECT_TRUE(ExpandRegistryValueData(REG_SZ, {}, Table()).empty());
    EXPECT_TRUE(ExpandRegistryValueData(REG_EXPAND_SZ, {}, Table()).empty());
    EXPECT_TRUE(ExpandRegistryValueData(REG_MULTI_SZ, {}, Table()).empty());
}
