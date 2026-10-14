#include "KnownFolderReference.hpp"
#include "RegistryModel.hpp"
#include "WString.hpp"
#include <cstring>
#include <cwctype>
#include <string>
#include <utility>
#include <vector>

namespace
{

/**
 * @brief Whether a character is part of a word.
 *
 * A path of a preset directory may only start behind a character which is none
 * of these, and it may only end in front of a character which is none of them
 * either: `xC:\Program Files` and `C:\Program FilesX` are not the folder,
 * while `C:\Program Files\Foo` and `C:\Program Files;` are.
 *
 * @param[in] character The character to inspect.
 * @return true when the character belongs to a word.
 */
bool IsWordCharacter(wchar_t character)
{
    return std::iswalnum(static_cast<std::wint_t>(character)) != 0 || character == L'_' || character == L'.';
}

/**
 * @brief Whether a text holds a path at a position, ignoring the case.
 * @param[in] text Text to inspect.
 * @param[in] position Position to start at.
 * @param[in] path Path to compare with.
 * @return true when the text holds the path at the position.
 */
bool MatchesAt(const std::wstring& text, std::size_t position, const std::wstring& path)
{
    if (position + path.size() > text.size())
    {
        return false;
    }

    for (std::size_t index = 0; index < path.size(); ++index)
    {
        const auto left = std::towupper(static_cast<std::wint_t>(text[position + index]));
        const auto right = std::towupper(static_cast<std::wint_t>(path[index]));
        if (left != right)
        {
            return false;
        }
    }

    return true;
}

/**
 * @brief Whether a path may start at a position of a text.
 * @param[in] text Text to inspect.
 * @param[in] position Position of the first character of the path.
 * @return true when the position is a boundary.
 */
bool StartsAtBoundary(const std::wstring& text, std::size_t position)
{
    return position == 0 || !IsWordCharacter(text[position - 1]);
}

/**
 * @brief Whether a path may end at a position of a text.
 *
 * The character behind the path decides whether the text names the folder
 * itself or a longer name which happens to start with its path, so
 * `C:\Program FilesX` is not the folder `C:\Program Files`.
 *
 * @param[in] text Text to inspect.
 * @param[in] position Position behind the last character of the path.
 * @return true when the position is a boundary.
 */
bool EndsAtBoundary(const std::wstring& text, std::size_t position)
{
    return position >= text.size() || !IsWordCharacter(text[position]);
}

/**
 * @brief Find the preset directory whose path matches a text at a position.
 *
 * The longest path wins, which is what tells the nested preset directories
 * apart: `C:\Program Files\Common Files` is the folder of
 * `#ProgramFilesCommon#` and not a folder of `#ProgramFiles#`.
 *
 * @param[in] text Text to inspect.
 * @param[in] position Position to start at.
 * @param[in] presets Preset directories which are resolved on this machine.
 * @return The matching preset directory, null when none matches.
 */
const appbox::PresetDirectory* LongestMatchAt(const std::wstring& text, std::size_t position,
                                              const std::vector<appbox::PresetDirectory>& presets)
{
    const appbox::PresetDirectory* best = nullptr;
    std::size_t                    best_length = 0;

    for (const auto& preset : presets)
    {
        if (preset.real_path.empty() || preset.real_path.size() <= best_length)
        {
            continue;
        }
        if (!MatchesAt(text, position, preset.real_path))
        {
            continue;
        }
        if (!EndsAtBoundary(text, position + preset.real_path.size()))
        {
            continue;
        }

        best = &preset;
        best_length = preset.real_path.size();
    }

    return best;
}

} // namespace

namespace appbox
{

std::wstring KnownFolderReferenceName(const std::wstring& layer_key)
{
    std::wstring name = layer_key;

    if (!name.empty() && name.front() == L'#')
    {
        name.erase(name.begin());
    }
    if (!name.empty() && name.back() == L'#')
    {
        name.pop_back();
    }

    return name;
}

std::wstring RewriteKnownFolderReferences(const std::wstring& text, const std::vector<PresetDirectory>& presets,
                                          bool& changed)
{
    changed = false;
    if (text.empty() || presets.empty())
    {
        return text;
    }

    std::wstring result;
    result.reserve(text.size());

    std::size_t position = 0;
    while (position < text.size())
    {
        const auto* match = StartsAtBoundary(text, position) ? LongestMatchAt(text, position, presets) : nullptr;
        if (match == nullptr)
        {
            result.push_back(text[position]);
            ++position;
            continue;
        }

        /*
         * The remainder of the path keeps its own separator, so the reference
         * and the expansion of the sandbox rebuild the same path.
         */
        result += kKnownFolderReferencePrefix;
        result += KnownFolderReferenceName(match->layer_key);
        result.push_back(L'%');

        position += match->real_path.size();
        changed = true;
    }

    return result;
}

std::vector<std::uint8_t> RewriteRegistryValueReferences(std::uint32_t type, const std::vector<std::uint8_t>& data,
                                                         const std::vector<PresetDirectory>& presets, bool& changed)
{
    changed = false;

    /* Only the string types carry a text which the sandbox expands. */
    if (type != REG_SZ && type != REG_EXPAND_SZ && type != REG_MULTI_SZ)
    {
        return data;
    }
    if (data.size() % sizeof(wchar_t) != 0)
    {
        return data;
    }

    if (type == REG_MULTI_SZ)
    {
        const auto parts = RegistryMultiStringValue(data);

        std::vector<std::wstring> rewritten;
        rewritten.reserve(parts.size());
        for (const auto& part : parts)
        {
            bool       part_changed = false;
            const auto text = RewriteKnownFolderReferences(part, presets, part_changed);
            changed = changed || part_changed;
            rewritten.push_back(text);
        }

        return changed ? RegistryMultiStringData(rewritten) : data;
    }

    const auto  chars = data.size() / sizeof(wchar_t);
    const auto* text = reinterpret_cast<const wchar_t*>(data.data());

    /* The terminator of the value is kept, so the shape of the value survives. */
    std::size_t terminators = 0;
    while (terminators < chars && text[chars - 1 - terminators] == L'\0')
    {
        ++terminators;
    }

    const auto content_chars = chars - terminators;

    std::wstring content;
    if (content_chars > 0)
    {
        content.assign(text, content_chars);
    }

    const auto rewritten = RewriteKnownFolderReferences(content, presets, changed);
    if (!changed)
    {
        return data;
    }

    std::vector<std::uint8_t> result(rewritten.size() * sizeof(wchar_t));
    if (!result.empty())
    {
        std::memcpy(result.data(), rewritten.data(), result.size());
    }
    result.resize(result.size() + terminators * sizeof(wchar_t), 0);
    return result;
}

} // namespace appbox
