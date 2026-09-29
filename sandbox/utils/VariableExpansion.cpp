#include "utils/WinAPI.h" /* Must be first include file */
#include "utils/Log.hpp"
#include "WString.hpp"
#include <cstring>
#include <string>
#include <string_view>
#include <vector>
#include "VariableExpansion.hpp"

namespace
{

/**
 * @brief Whether two texts are the same, ignoring the case.
 *
 * The comparison is the ordinal one of the operating system, which is the
 * comparison the registry and the environment of a process use.
 *
 * @param[in] left Left text.
 * @param[in] right Right text.
 * @return true when both texts name the same thing.
 */
bool EqualsIgnoreCase(std::wstring_view left, std::wstring_view right)
{
    if (left.size() != right.size())
    {
        return false;
    }

    if (left.empty())
    {
        return true;
    }

    return ::CompareStringOrdinal(left.data(), static_cast<int>(left.size()), right.data(),
                                  static_cast<int>(right.size()), TRUE) == CSTR_EQUAL;
}

/**
 * @brief Whether a text starts with a prefix, ignoring the case.
 * @param[in] text Text to inspect.
 * @param[in] prefix Prefix to look for.
 * @return true when the text starts with the prefix.
 */
bool StartsWithIgnoreCase(std::wstring_view text, std::wstring_view prefix)
{
    return text.size() >= prefix.size() && EqualsIgnoreCase(text.substr(0, prefix.size()), prefix);
}

/**
 * @brief Find the variable a name references.
 * @param[in] name Name of a reference, without the `%APPBOX:` prefix.
 * @param[in] variables Variables the sandbox knows.
 * @return The variable, null when the name is not listed.
 */
const appbox::VariableMapping* FindVariable(std::wstring_view                           name,
                                            const std::vector<appbox::VariableMapping>& variables)
{
    for (const auto& variable : variables)
    {
        if (EqualsIgnoreCase(variable.name, name))
        {
            return &variable;
        }
    }

    return nullptr;
}

/**
 * @brief Expand the items of a `REG_MULTI_SZ` list.
 *
 * Every item is expanded on its own and every null character of the list is
 * written back where it was, so an empty item and the terminator of the list
 * survive the expansion.
 *
 * @param[in] text Text of the value, items separated by null characters.
 * @param[in] variables Variables the sandbox knows.
 * @return The text with every known reference replaced.
 */
std::wstring ExpandMultiString(std::wstring_view text, const std::vector<appbox::VariableMapping>& variables)
{
    std::wstring result;
    result.reserve(text.size());

    std::size_t start = 0;
    while (start <= text.size())
    {
        const std::size_t end = text.find(L'\0', start);
        if (end == std::wstring_view::npos)
        {
            /* The last item of the list carries no terminator. */
            result.append(appbox::ExpandVariables(text.substr(start), variables));
            break;
        }

        result.append(appbox::ExpandVariables(text.substr(start, end - start), variables));
        result.push_back(L'\0');
        start = end + 1;
    }

    return result;
}

} // namespace

std::wstring appbox::ExpandVariables(std::wstring_view text, const std::vector<VariableMapping>& variables)
{
    std::wstring result;
    result.reserve(text.size());

    std::size_t index = 0;
    while (index < text.size())
    {
        if (text[index] != L'%')
        {
            result.push_back(text[index]);
            ++index;
            continue;
        }

        const std::size_t end = text.find(L'%', index + 1);
        if (end == std::wstring_view::npos)
        {
            /* A lone percent sign is copied, like the operating system does. */
            result.push_back(text[index]);
            ++index;
            continue;
        }

        const std::wstring_view reference = text.substr(index + 1, end - index - 1);
        if (!StartsWithIgnoreCase(reference, kVariablePrefix))
        {
            /* A `%NAME%` reference belongs to the shell, not to the sandbox. */
            result.append(text.substr(index, end - index + 1));
            index = end + 1;
            continue;
        }

        const std::wstring_view name = reference.substr(kVariablePrefix.size());
        const VariableMapping*  variable = FindVariable(name, variables);
        if (variable == nullptr)
        {
            /*
             * A reference the configuration does not list keeps its own
             * spelling, like an unknown `%NAME%` reference of the shell does.
             */
            LOG_W("the value references the variable '{}', which the sandbox does not know, the reference is kept",
                  WideToUTF8(std::wstring(name)));
            result.append(text.substr(index, end - index + 1));
            index = end + 1;
            continue;
        }

        result.append(variable->path);
        index = end + 1;
    }

    return result;
}

std::vector<BYTE> appbox::ExpandRegistryValueData(DWORD type, const std::vector<BYTE>& data,
                                                  const std::vector<VariableMapping>& variables)
{
    /* Only the string types of the registry carry references. */
    if (type != REG_SZ && type != REG_EXPAND_SZ && type != REG_MULTI_SZ)
    {
        return data;
    }

    /* A string value is a sequence of whole wide characters. */
    if ((data.size() % sizeof(wchar_t)) != 0)
    {
        return data;
    }

    std::wstring text(data.size() / sizeof(wchar_t), L'\0');
    if (!text.empty())
    {
        memcpy(text.data(), data.data(), data.size());
    }

    const std::wstring expanded =
        type == REG_MULTI_SZ ? ExpandMultiString(text, variables) : ExpandVariables(text, variables);
    if (expanded == text)
    {
        return data;
    }

    std::vector<BYTE> result(expanded.size() * sizeof(wchar_t));
    if (!result.empty())
    {
        memcpy(result.data(), expanded.data(), result.size());
    }

    return result;
}
