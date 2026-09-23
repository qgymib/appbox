#include "RegFile.hpp"
#include "WString.hpp"
#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>

namespace
{

/** Header line of the current registry file format. */
constexpr const wchar_t* kVersion5Header = L"Windows Registry Editor Version 5.00";

/** Header line of the legacy registry file format. */
constexpr const wchar_t* kLegacyHeader = L"REGEDIT4";

/**
 * @brief Build the error description of one line.
 * @param[in] line One based line number.
 * @param[in] message Error description.
 * @return The error text.
 */
std::string LineError(std::size_t line, const std::string& message)
{
    return "line " + std::to_string(line) + ": " + message;
}

/**
 * @brief Remove the leading and trailing whitespace of a text.
 * @param[in] text The text to trim.
 * @return The trimmed text.
 */
std::wstring Trim(const std::wstring& text)
{
    const auto is_space = [](wchar_t character) {
        return character == L' ' || character == L'\t' || character == L'\r' || character == L'\n';
    };

    std::size_t begin = 0;
    while (begin < text.size() && is_space(text[begin]))
    {
        ++begin;
    }

    std::size_t end = text.size();
    while (end > begin && is_space(text[end - 1]))
    {
        --end;
    }
    return text.substr(begin, end - begin);
}

/**
 * @brief Compare two texts ignoring the case.
 * @param[in] left Left text.
 * @param[in] right Right text.
 * @return true when both texts are equal ignoring the case.
 */
bool EqualsIgnoreCase(const std::wstring& left, const std::wstring& right)
{
    if (left.size() != right.size())
    {
        return false;
    }

    for (std::size_t index = 0; index < left.size(); ++index)
    {
        if (std::towlower(left[index]) != std::towlower(right[index]))
        {
            return false;
        }
    }
    return true;
}

/**
 * @brief Whether a text starts with a prefix, ignoring the case.
 * @param[in] text Text to inspect.
 * @param[in] prefix Prefix to look for.
 * @return true when the prefix is present.
 */
bool StartsWithIgnoreCase(const std::wstring& text, const std::wstring& prefix)
{
    if (prefix.size() > text.size())
    {
        return false;
    }
    return EqualsIgnoreCase(text.substr(0, prefix.size()), prefix);
}

/**
 * @brief Split a text into lines without the line breaks.
 * @param[in] text The text to split.
 * @return The lines in file order.
 */
std::vector<std::wstring> SplitLines(const std::wstring& text)
{
    std::vector<std::wstring> lines;
    std::wstring current;

    for (const wchar_t character : text)
    {
        if (character == L'\n')
        {
            lines.push_back(current);
            current.clear();
            continue;
        }
        if (character == L'\r')
        {
            continue;
        }
        current.push_back(character);
    }

    lines.push_back(current);
    return lines;
}

/**
 * @brief Whether a byte sequence is well formed UTF-8.
 *
 * Overlong encodings, surrogate code points and values above U+10FFFF are
 * rejected as well, so a text which passes the check can be decoded without a
 * replacement character being introduced.
 *
 * @param[in] text Text to check.
 * @return true when the text is valid UTF-8.
 */
bool IsValidUtf8(const std::string& text)
{
    std::size_t index = 0;
    while (index < text.size())
    {
        const auto lead = static_cast<unsigned char>(text[index]);

        if (lead <= 0x7F)
        {
            ++index;
            continue;
        }

        std::size_t trailing = 0;
        if (lead >= 0xC2 && lead <= 0xDF)
        {
            trailing = 1;
        }
        else if (lead >= 0xE0 && lead <= 0xEF)
        {
            trailing = 2;
        }
        else if (lead >= 0xF0 && lead <= 0xF4)
        {
            trailing = 3;
        }
        else
        {
            return false;
        }

        if (index + trailing >= text.size())
        {
            return false;
        }

        for (std::size_t offset = 1; offset <= trailing; ++offset)
        {
            const auto next = static_cast<unsigned char>(text[index + offset]);
            if (next < 0x80 || next > 0xBF)
            {
                return false;
            }
        }

        const auto second = static_cast<unsigned char>(text[index + 1]);
        if (trailing == 2 && ((lead == 0xE0 && second < 0xA0) || (lead == 0xED && second > 0x9F)))
        {
            return false;
        }
        if (trailing == 3 && ((lead == 0xF0 && second < 0x90) || (lead == 0xF4 && second > 0x8F)))
        {
            return false;
        }

        index += trailing + 1;
    }
    return true;
}

/**
 * @brief Decode a byte sequence with a code page.
 * @param[in] bytes The bytes to decode.
 * @param[in] code_page Code page used for the decoding.
 * @return The decoded text, empty when the decoding failed.
 */
std::wstring DecodeMultiByte(const std::string& bytes, UINT code_page)
{
    if (bytes.empty())
    {
        return {};
    }

    const auto size = static_cast<int>(bytes.size());
    const int needed = MultiByteToWideChar(code_page, 0, bytes.data(), size, nullptr, 0);
    if (needed <= 0)
    {
        return {};
    }

    std::wstring text(static_cast<std::size_t>(needed), L'\0');
    if (MultiByteToWideChar(code_page, 0, bytes.data(), size, text.data(), needed) != needed)
    {
        return {};
    }
    return text;
}

/**
 * @brief Decode the content of a registry file.
 *
 * @param[in] bytes Raw file content.
 * @param[out] text Decoded text.
 * @param[out] error Error description on failure.
 * @return true on success.
 */
bool DecodeRegFile(const std::string& bytes, std::wstring& text, std::string& error)
{
    const auto at = [&bytes](std::size_t index) { return static_cast<unsigned char>(bytes[index]); };

    /* UTF-16 little endian, the encoding the registry editor writes today. */
    if (bytes.size() >= 2 && at(0) == 0xFF && at(1) == 0xFE)
    {
        if (bytes.size() >= 4 && at(2) == 0x00 && at(3) == 0x00)
        {
            error = "the file is UTF-32 encoded";
            return false;
        }
        if ((bytes.size() - 2) % 2 != 0)
        {
            error = "the UTF-16 content has an odd number of bytes";
            return false;
        }

        std::wstring decoded;
        decoded.reserve((bytes.size() - 2) / 2);
        for (std::size_t index = 2; index + 1 < bytes.size(); index += 2)
        {
            const auto code = static_cast<std::uint16_t>(
                static_cast<std::uint16_t>(at(index)) | (static_cast<std::uint16_t>(at(index + 1)) << 8));
            decoded.push_back(static_cast<wchar_t>(code));
        }
        text = std::move(decoded);
        return true;
    }

    if (bytes.size() >= 2 && at(0) == 0xFE && at(1) == 0xFF)
    {
        error = "the file uses the big endian UTF-16 encoding";
        return false;
    }

    /* A UTF-8 mark and a well formed UTF-8 content are both accepted. */
    const bool has_utf8_mark = bytes.size() >= 3 && at(0) == 0xEF && at(1) == 0xBB && at(2) == 0xBF;
    const std::string content = has_utf8_mark ? bytes.substr(3) : bytes;

    if (has_utf8_mark || IsValidUtf8(content))
    {
        text = DecodeMultiByte(content, CP_UTF8);
        if (text.empty() && !content.empty())
        {
            error = "the file could not be decoded as UTF-8";
            return false;
        }
        return true;
    }

    /* The legacy format is written with the ANSI code page of the system. */
    text = DecodeMultiByte(content, CP_ACP);
    if (text.empty() && !content.empty())
    {
        error = "the file could not be decoded with the ANSI code page";
        return false;
    }
    return true;
}

/**
 * @brief Parse a quoted text starting at a position.
 *
 * The position has to point at the opening quote and is advanced behind the
 * closing quote. The escapes of the registry editor are resolved: `\\`, `\"`,
 * `\n`, `\r` and `\0`; an unknown escape keeps the escaped character.
 *
 * @param[in] text Text to parse.
 * @param[in,out] position Position of the opening quote.
 * @param[out] out The parsed text.
 * @param[out] error Error description on failure.
 * @param[in] line One based line number of the text.
 * @return true on success.
 */
bool ParseQuotedText(const std::wstring& text, std::size_t& position, std::wstring& out, std::string& error,
                     std::size_t line)
{
    if (position >= text.size() || text[position] != L'"')
    {
        error = LineError(line, "the value name is not quoted");
        return false;
    }

    std::wstring parsed;
    ++position;

    while (position < text.size())
    {
        const wchar_t character = text[position];
        if (character == L'"')
        {
            ++position;
            out = std::move(parsed);
            return true;
        }

        if (character == L'\\' && position + 1 < text.size())
        {
            const wchar_t escaped = text[position + 1];
            position += 2;
            switch (escaped)
            {
            case L'\\':
                parsed.push_back(L'\\');
                break;
            case L'"':
                parsed.push_back(L'"');
                break;
            case L'n':
                parsed.push_back(L'\n');
                break;
            case L'r':
                parsed.push_back(L'\r');
                break;
            case L'0':
                parsed.push_back(L'\0');
                break;
            default:
                parsed.push_back(escaped);
                break;
            }
            continue;
        }

        parsed.push_back(character);
        ++position;
    }

    error = LineError(line, "the quoted text is not closed");
    return false;
}

/**
 * @brief Resolve the root key name of a `.reg` key path.
 *
 * @param[in] name First component of the path.
 * @param[out] out The full name of the root key.
 * @return true when the component names a root key of the view.
 */
bool ResolveRootKeyName(const std::wstring& name, std::wstring& out)
{
    for (const auto& root : appbox::RegistryRootKeyNames())
    {
        if (EqualsIgnoreCase(root, name))
        {
            out = root;
            return true;
        }
    }

    /* The abbreviations the registry editor writes for the root keys. */
    struct Alias
    {
        const wchar_t* alias; ///< Abbreviated name.
        const wchar_t* root;  ///< Full name of the root key.
    };
    static const Alias aliases[] = {
        { L"HKCR", L"HKEY_CLASSES_ROOT" }, { L"HKCU", L"HKEY_CURRENT_USER" },
        { L"HKLM", L"HKEY_LOCAL_MACHINE" }, { L"HKU", L"HKEY_USERS" },
        { L"HKCC", L"HKEY_CURRENT_CONFIG" }
    };

    for (const auto& alias : aliases)
    {
        if (EqualsIgnoreCase(alias.alias, name))
        {
            out = alias.root;
            return true;
        }
    }
    return false;
}

/**
 * @brief Normalize a key path of a `.reg` file.
 * @param[in] text Key path as written in the file.
 * @param[out] out Normalized path of the key.
 * @param[out] error Error description on failure.
 * @param[in] line One based line number of the path.
 * @return true on success.
 */
bool ResolveRegKeyPath(const std::wstring& text, std::wstring& out, std::string& error, std::size_t line)
{
    const auto parts = appbox::SplitRegistryPath(text);
    if (parts.empty())
    {
        error = LineError(line, "the key line does not name a key");
        return false;
    }

    std::wstring root;
    if (!ResolveRootKeyName(parts.front(), root))
    {
        error = LineError(line, "the key path starts with an unknown root key: '" + appbox::WideToUTF8(parts.front())
                                    + "'");
        return false;
    }

    std::wstring path = root;
    for (std::size_t index = 1; index < parts.size(); ++index)
    {
        path = appbox::JoinRegistryPath(path, parts[index]);
    }

    out = std::move(path);
    return true;
}

/**
 * @brief Whether a path names one of the root keys itself.
 * @param[in] path Path to inspect.
 * @return true when the path is the path of a root key.
 */
bool IsRootKeyPath(const std::wstring& path)
{
    const auto parts = appbox::SplitRegistryPath(path);
    if (parts.size() != 1)
    {
        return false;
    }

    std::wstring root;
    return ResolveRootKeyName(parts.front(), root);
}

/**
 * @brief Parse a hexadecimal number.
 * @param[in] text Text to parse.
 * @param[out] out The parsed value.
 * @return true when the text holds at least one hexadecimal digit.
 */
bool ParseHexNumber(const std::wstring& text, std::uint64_t& out)
{
    if (text.empty())
    {
        return false;
    }

    std::uint64_t value = 0;
    for (const wchar_t character : text)
    {
        std::uint64_t digit = 0;
        if (character >= L'0' && character <= L'9')
        {
            digit = static_cast<std::uint64_t>(character - L'0');
        }
        else if (character >= L'a' && character <= L'f')
        {
            digit = static_cast<std::uint64_t>(character - L'a') + 10;
        }
        else if (character >= L'A' && character <= L'F')
        {
            digit = static_cast<std::uint64_t>(character - L'A') + 10;
        }
        else
        {
            return false;
        }

        if (value > (0xFFFFFFFFFFFFFFFFULL - digit) / 16)
        {
            return false;
        }
        value = value * 16 + digit;
    }

    out = value;
    return true;
}

/**
 * @brief Resolve the value type of a `hex(...)` data form.
 * @param[in] spec Text between the parentheses, empty for the untyped form.
 * @param[out] out The resolved type.
 * @param[out] error Error description on failure.
 * @param[in] line One based line number of the value.
 * @return true on success.
 */
bool ResolveHexType(const std::wstring& spec, appbox::RegistryValueType& out, std::string& error,
                    std::size_t line)
{
    if (spec.empty())
    {
        out = appbox::RegistryValueType::Binary;
        return true;
    }

    std::uint64_t code = 0;
    if (!ParseHexNumber(spec, code))
    {
        error = LineError(line, "the value line holds an invalid type index");
        return false;
    }

    if (!appbox::ResolveRegistryValueType(static_cast<std::uint32_t>(code), out))
    {
        error = LineError(line, "the value line holds an unsupported type index: hex(" + appbox::WideToUTF8(spec)
                                    + ")");
        return false;
    }
    return true;
}

/**
 * @brief Parse the data part of a value line.
 * @param[in] text Data part of the line, without the `=`.
 * @param[out] entry Entry filled with the parsed operation and data.
 * @param[out] error Error description on failure.
 * @param[in] line One based line number of the value.
 * @return true on success.
 */
bool ParseValueData(const std::wstring& text, appbox::RegFileEntry& entry, std::string& error, std::size_t line)
{
    if (text == L"-")
    {
        entry.operation = appbox::RegFileOperation::RemoveValue;
        return true;
    }

    entry.operation = appbox::RegFileOperation::SetValue;

    /* A quoted text is a string value. */
    if (!text.empty() && text.front() == L'"')
    {
        std::size_t position = 0;
        std::wstring parsed;
        if (!ParseQuotedText(text, position, parsed, error, line))
        {
            return false;
        }
        if (position != text.size())
        {
            error = LineError(line, "the value line holds data behind the quoted text");
            return false;
        }

        entry.type = appbox::RegistryValueType::String;
        entry.data = appbox::RegistryStringData(parsed);
        return true;
    }

    /* An empty data part is an empty string value. */
    if (text.empty())
    {
        entry.type = appbox::RegistryValueType::String;
        entry.data = appbox::RegistryStringData({});
        return true;
    }

    if (StartsWithIgnoreCase(text, L"dword:"))
    {
        std::uint64_t value = 0;
        if (!ParseHexNumber(text.substr(6), value) || value > 0xFFFFFFFFULL)
        {
            error = LineError(line, "the value line holds an invalid dword");
            return false;
        }

        entry.type = appbox::RegistryValueType::Dword;
        entry.data = appbox::RegistryDwordData(static_cast<std::uint32_t>(value));
        return true;
    }

    if (StartsWithIgnoreCase(text, L"hex"))
    {
        std::wstring spec;
        std::wstring data_text;

        const auto colon = text.find(L':');
        if (colon == std::wstring::npos)
        {
            error = LineError(line, "the value line holds no ':' behind the hex type");
            return false;
        }

        const auto head = text.substr(3, colon - 3);
        if (head.empty())
        {
            spec.clear();
        }
        else if (head.size() >= 2 && head.front() == L'(' && head.back() == L')')
        {
            spec = head.substr(1, head.size() - 2);
        }
        else
        {
            error = LineError(line, "the value line holds an invalid hex type");
            return false;
        }

        if (!ResolveHexType(spec, entry.type, error, line))
        {
            return false;
        }

        data_text = text.substr(colon + 1);
        if (!appbox::ParseRegistryHexText(data_text, entry.data, error))
        {
            error = LineError(line, error);
            return false;
        }
        return true;
    }

    error = LineError(line, "the value line holds an unsupported data form");
    return false;
}

/**
 * @brief Parse one value line of a key section.
 * @param[in] line Text of the line.
 * @param[in] key_path Path of the key the line belongs to.
 * @param[in] line_number One based line number.
 * @param[out] entry The parsed entry.
 * @param[out] error Error description on failure.
 * @return true on success.
 */
bool ParseValueLine(const std::wstring& line, const std::wstring& key_path, std::size_t line_number,
                    appbox::RegFileEntry& entry, std::string& error)
{
    std::size_t position = 0;
    std::wstring name;

    if (line.front() == L'@')
    {
        position = 1;
    }
    else if (line.front() == L'"')
    {
        if (!ParseQuotedText(line, position, name, error, line_number))
        {
            return false;
        }
    }
    else
    {
        error = LineError(line_number, "the value line does not start with a value name");
        return false;
    }

    if (position >= line.size() || line[position] != L'=')
    {
        error = LineError(line_number, "the value line holds no '=' behind the value name");
        return false;
    }

    entry.key_path = key_path;
    entry.value_name = name;
    entry.line = line_number;
    return ParseValueData(line.substr(position + 1), entry, error, line_number);
}

} // namespace

namespace appbox
{

bool ParseRegText(const std::wstring& text, std::vector<RegFileEntry>& entries, std::string& error)
{
    std::vector<RegFileEntry> parsed;
    const auto lines = SplitLines(text);

    bool header_seen = false;
    std::wstring current_key;
    std::size_t index = 0;

    while (index < lines.size())
    {
        const std::size_t line_number = index + 1;
        std::wstring line = Trim(lines[index]);
        ++index;

        if (line.empty() || line.front() == L';')
        {
            continue;
        }

        if (!header_seen)
        {
            if (line == kVersion5Header || line == kLegacyHeader)
            {
                header_seen = true;
                continue;
            }
            error = LineError(line_number, "the file does not start with a registry file header");
            return false;
        }

        if (line.front() == L'[')
        {
            /* A long key path may be split over several lines. */
            while (!line.empty() && line.back() != L']' && index < lines.size())
            {
                line += Trim(lines[index]);
                ++index;
            }

            if (line.size() < 2 || line.back() != L']')
            {
                error = LineError(line_number, "the key line is not closed");
                return false;
            }

            auto body = Trim(line.substr(1, line.size() - 2));
            bool remove = false;
            if (!body.empty() && body.front() == L'-')
            {
                remove = true;
                body = Trim(body.substr(1));
            }

            RegFileEntry entry;
            entry.line = line_number;
            if (!ResolveRegKeyPath(body, entry.key_path, error, line_number))
            {
                return false;
            }

            entry.operation = remove ? RegFileOperation::RemoveKey : RegFileOperation::AddKey;
            parsed.push_back(entry);

            /* Values behind a removed key line have no key to belong to. */
            current_key = remove ? std::wstring() : entry.key_path;
            continue;
        }

        if (current_key.empty())
        {
            error = LineError(line_number, "a value line appears before the first key line");
            return false;
        }

        /* A hexadecimal block may be split over several lines. */
        while (!line.empty() && line.back() == L'\\' && index < lines.size())
        {
            line.pop_back();
            line += Trim(lines[index]);
            ++index;
        }

        if (line.empty())
        {
            error = LineError(line_number, "the value line is empty");
            return false;
        }

        RegFileEntry entry;
        if (!ParseValueLine(line, current_key, line_number, entry, error))
        {
            return false;
        }
        parsed.push_back(entry);
    }

    if (!header_seen)
    {
        error = "the file does not hold a registry file header";
        return false;
    }

    entries = std::move(parsed);
    return true;
}

bool LoadRegFile(const std::wstring& path, std::vector<RegFileEntry>& entries, std::string& error)
{
    std::ifstream stream(std::filesystem::path(path), std::ios::binary);
    if (!stream)
    {
        error = "the file could not be opened: '" + WideToUTF8(path) + "'";
        return false;
    }

    const std::string bytes{ std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };

    std::wstring text;
    if (!DecodeRegFile(bytes, text, error))
    {
        error = "'" + WideToUTF8(path) + "': " + error;
        return false;
    }

    std::vector<RegFileEntry> parsed;
    if (!ParseRegText(text, parsed, error))
    {
        error = "'" + WideToUTF8(path) + "': " + error;
        return false;
    }

    entries = std::move(parsed);
    return true;
}

bool MergeRegFile(RegistryModel& model, const std::vector<RegFileEntry>& entries, std::string& error)
{
    /*
     * Validate every entry first: a file which cannot be applied completely
     * must not leave the model half updated.
     */
    for (const auto& entry : entries)
    {
        if (entry.operation == RegFileOperation::RemoveKey && IsRootKeyPath(entry.key_path))
        {
            error = LineError(entry.line, "a root key cannot be removed: '" + WideToUTF8(entry.key_path) + "'");
            return false;
        }
    }

    for (const auto& entry : entries)
    {
        std::string entry_error;

        switch (entry.operation)
        {
        case RegFileOperation::AddKey:
            if (!model.EnsureKey(entry.key_path, entry_error))
            {
                error = LineError(entry.line, entry_error);
                return false;
            }
            break;
        case RegFileOperation::RemoveKey:
            if (model.FindKey(entry.key_path) != nullptr)
            {
                std::string ignored;
                model.RemoveKey(entry.key_path, ignored);
            }
            break;
        case RegFileOperation::SetValue:
            if (!model.EnsureKey(entry.key_path, entry_error)
                || !model.SetValue(entry.key_path, entry.value_name, entry.type, entry.data, entry_error))
            {
                error = LineError(entry.line, entry_error);
                return false;
            }
            break;
        case RegFileOperation::RemoveValue:
            model.RemoveValue(entry.key_path, entry.value_name);
            break;
        }
    }

    return true;
}

} // namespace appbox
