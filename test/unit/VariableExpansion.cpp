#include "WinAPI.h" /* Must be first include file */
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
 * @brief The prefix and the name of a reference are compared with the case of
 *        the table, so a reference which is spelled with another case keeps its
 *        own spelling, exactly like a reference to a name the table does not
 *        list.
 */
TEST(Unit_VariableExpansion, PrefixAndNameAreCaseSensitive)
{
    EXPECT_EQ(ExpandVariables(L"%appbox:ProgramFiles%", Table()), L"%appbox:ProgramFiles%");
    EXPECT_EQ(ExpandVariables(L"%AppBox:Documents%", Table()), L"%AppBox:Documents%");
    EXPECT_EQ(ExpandVariables(L"%APPBOX:programfiles%", Table()), L"%APPBOX:programfiles%");
    EXPECT_EQ(ExpandVariables(L"%APPBOX:userprofile%", Table()), L"%APPBOX:userprofile%");
}

/**
 * @brief A name which the table spells in upper case is expanded in that
 *        spelling alone, which is the spelling the packer writes.
 */
TEST(Unit_VariableExpansion, TheSpellingOfTheTableIsExpanded)
{
    EXPECT_EQ(ExpandVariables(L"%APPBOX:ProgramFiles%", Table()), L"C:\\Program Files");
    EXPECT_EQ(ExpandVariables(L"%APPBOX:USERPROFILE%", Table()), L"C:\\Users\\appbox");
    EXPECT_EQ(ExpandVariables(L"%APPBOX:Documents%", Table()), L"C:\\Users\\appbox\\Documents");
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

/**
 * @brief The registry side compares the case as well, so a value which spells
 *        the reference with another case is served as it is stored.
 */
TEST(Unit_VariableExpansion, RegSzKeepsAReferenceSpelledWithAnotherCase)
{
    const std::vector<BYTE> data = Bytes(Terminated(L"%appbox:documents%\\x"));
    EXPECT_EQ(ExpandRegistryValueData(REG_SZ, data, Table()), data);
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

/**
 * @brief The table of a process carries the view of its own bitness.
 *
 * The launcher resolves every known folder twice, because the operating system
 * shows a different folder to a 32 bit process than to a 64 bit one:
 * `%ProgramFiles%` is `C:\Program Files` in a 64 bit process and
 * `C:\Program Files (x86)` in a 32 bit one. A folder which a 32 bit process does
 * not see at all (`ProgramFilesX64`) is not part of its table, so a reference to
 * it keeps its own spelling there.
 */
TEST(Unit_VariableExpansion, SelectVariablesPicksTheViewOfTheProcess)
{
    const std::vector<appbox::SandboxVariable> injected = {
        { "ProgramFiles",    "C:\\Program Files",            "C:\\Program Files (x86)"      },
        { "ProgramFilesX64", "C:\\Program Files",            ""                             },
        { "Documents",       "C:\\Users\\appbox\\Documents", "C:\\Users\\appbox\\Documents" }
    };

    const auto variables64 = appbox::SelectVariables(injected, true);
    ASSERT_EQ(variables64.size(), 3u);
    EXPECT_EQ(variables64[0].name, L"ProgramFiles");
    EXPECT_EQ(variables64[0].path, L"C:\\Program Files");
    EXPECT_EQ(variables64[1].path, L"C:\\Program Files");
    EXPECT_EQ(variables64[2].path, L"C:\\Users\\appbox\\Documents");

    const auto variables32 = appbox::SelectVariables(injected, false);
    ASSERT_EQ(variables32.size(), 2u);
    EXPECT_EQ(variables32[0].path, L"C:\\Program Files (x86)");
    EXPECT_EQ(variables32[1].name, L"Documents");

    /* The folder a 32 bit process does not see keeps the reference as it is spelled. */
    EXPECT_EQ(ExpandVariables(L"%APPBOX:ProgramFilesX64%\\Foo", variables32), L"%APPBOX:ProgramFilesX64%\\Foo");
    EXPECT_EQ(ExpandVariables(L"%APPBOX:ProgramFiles%\\Foo", variables32), L"C:\\Program Files (x86)\\Foo");
}

/**
 * @brief A variable without a view names no folder in the process.
 *
 * The launcher never sends a variable without the view of a 64 bit process, so
 * the case describes a table which cannot come from a run; the sandbox still
 * leaves such a variable out instead of expanding it to an empty path.
 */
TEST(Unit_VariableExpansion, SelectVariablesLeavesAnEmptyViewOut)
{
    const std::vector<appbox::SandboxVariable> injected = {
        { "Empty", "", "" }
    };

    EXPECT_TRUE(appbox::SelectVariables(injected, true).empty());
    EXPECT_TRUE(appbox::SelectVariables(injected, false).empty());
}
