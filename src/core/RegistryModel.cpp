#include "RegistryModel.hpp"
#include "WString.hpp"
#include <algorithm>
#include <cwctype>
#include <utility>

namespace
{

/**
 * @brief Compare two names ignoring the case.
 * @param[in] left Left name.
 * @param[in] right Right name.
 * @return true when both names are equal ignoring the case.
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
 * @brief Order two names ignoring the case.
 * @param[in] left Left name.
 * @param[in] right Right name.
 * @return true when left has to be placed before right.
 */
bool LessIgnoreCase(const std::wstring& left, const std::wstring& right)
{
    const std::size_t shared = left.size() < right.size() ? left.size() : right.size();
    for (std::size_t index = 0; index < shared; ++index)
    {
        const wchar_t left_char = std::towlower(left[index]);
        const wchar_t right_char = std::towlower(right[index]);
        if (left_char != right_char)
        {
            return left_char < right_char;
        }
    }
    return left.size() < right.size();
}

/**
 * @brief Quote a wide text for an English error description.
 * @param[in] text The text to quote.
 * @return The quoted UTF-8 text.
 */
std::string Quote(const std::wstring& text)
{
    return "'" + appbox::WideToUTF8(text) + "'";
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
 * @brief Parse an unsigned number written in decimal or hexadecimal form.
 * @param[in] text The text to parse.
 * @param[in] maximum Largest accepted value.
 * @param[out] out The parsed value.
 * @return true when the whole text forms a number within the range.
 */
bool ParseUnsigned(const std::wstring& text, std::uint64_t maximum, std::uint64_t& out)
{
    const auto trimmed = Trim(text);
    if (trimmed.empty())
    {
        return false;
    }

    bool hexadecimal = false;
    std::size_t begin = 0;
    if (trimmed.size() > 2 && trimmed[0] == L'0' && (trimmed[1] == L'x' || trimmed[1] == L'X'))
    {
        hexadecimal = true;
        begin = 2;
    }

    if (begin >= trimmed.size())
    {
        return false;
    }

    std::uint64_t value = 0;
    for (std::size_t index = begin; index < trimmed.size(); ++index)
    {
        const wchar_t character = trimmed[index];
        std::uint64_t digit = 0;

        if (character >= L'0' && character <= L'9')
        {
            digit = static_cast<std::uint64_t>(character - L'0');
        }
        else if (hexadecimal && character >= L'a' && character <= L'f')
        {
            digit = static_cast<std::uint64_t>(character - L'a') + 10;
        }
        else if (hexadecimal && character >= L'A' && character <= L'F')
        {
            digit = static_cast<std::uint64_t>(character - L'A') + 10;
        }
        else
        {
            return false;
        }

        const std::uint64_t base = hexadecimal ? 16 : 10;
        if (value > (maximum - digit) / base)
        {
            return false;
        }
        value = value * base + digit;
    }

    out = value;
    return true;
}

/**
 * @brief Append a number to a text.
 * @param[in] text Text to append to.
 * @param[in] value The value to append.
 * @param[in] base Number base of the representation.
 * @param[in] digits Minimum number of digits.
 */
std::wstring FormatUnsigned(std::uint64_t value, unsigned base, std::size_t digits)
{
    static const wchar_t* const alphabet = L"0123456789ABCDEF";

    std::wstring text;
    while (value > 0)
    {
        text.push_back(alphabet[value % base]);
        value /= base;
    }
    while (text.size() < digits)
    {
        text.push_back(L'0');
    }
    if (text.empty())
    {
        text.push_back(L'0');
    }

    std::reverse(text.begin(), text.end());
    return text;
}

} // namespace

namespace appbox
{

const std::vector<std::wstring>& RegistryRootKeyNames()
{
    static const std::vector<std::wstring> names = {
        L"HKEY_CLASSES_ROOT", L"HKEY_CURRENT_USER", L"HKEY_LOCAL_MACHINE", L"HKEY_USERS",
        L"HKEY_CURRENT_CONFIG"
    };
    return names;
}

const std::vector<std::wstring>& RegistryIsolationNames()
{
    static const std::vector<std::wstring> names = { L"Full", L"Write Copy", L"Hide" };
    return names;
}

std::wstring RegistryIsolationName(RegistryIsolation isolation)
{
    const auto& names = RegistryIsolationNames();
    const auto index = static_cast<std::size_t>(isolation);
    if (index >= names.size())
    {
        return names.front();
    }
    return names[index];
}

const std::vector<RegistryValueType>& RegistryValueTypes()
{
    static const std::vector<RegistryValueType> types = {
        RegistryValueType::None,      RegistryValueType::String, RegistryValueType::ExpandString,
        RegistryValueType::Binary,    RegistryValueType::Dword,  RegistryValueType::MultiString,
        RegistryValueType::Qword
    };
    return types;
}

std::wstring RegistryValueTypeName(RegistryValueType type)
{
    switch (type)
    {
    case RegistryValueType::None:
        return L"REG_NONE";
    case RegistryValueType::String:
        return L"REG_SZ";
    case RegistryValueType::ExpandString:
        return L"REG_EXPAND_SZ";
    case RegistryValueType::Binary:
        return L"REG_BINARY";
    case RegistryValueType::Dword:
        return L"REG_DWORD";
    case RegistryValueType::MultiString:
        return L"REG_MULTI_SZ";
    case RegistryValueType::Qword:
        return L"REG_QWORD";
    }
    return L"REG_NONE";
}

bool ParseRegistryValueType(const std::wstring& name, RegistryValueType& out)
{
    for (const auto type : RegistryValueTypes())
    {
        if (EqualsIgnoreCase(RegistryValueTypeName(type), name))
        {
            out = type;
            return true;
        }
    }
    return false;
}

bool ResolveRegistryValueType(std::uint32_t code, RegistryValueType& out)
{
    for (const auto type : RegistryValueTypes())
    {
        if (static_cast<std::uint32_t>(type) == code)
        {
            out = type;
            return true;
        }
    }
    return false;
}

std::vector<std::wstring> SplitRegistryPath(const std::wstring& path)
{
    std::vector<std::wstring> parts;
    std::wstring current;

    for (const wchar_t character : path)
    {
        if (character == L'\\' || character == L'/')
        {
            if (!current.empty())
            {
                parts.push_back(current);
                current.clear();
            }
            continue;
        }
        current.push_back(character);
    }

    if (!current.empty())
    {
        parts.push_back(current);
    }
    return parts;
}

std::wstring JoinRegistryPath(const std::wstring& parent, const std::wstring& name)
{
    if (parent.empty())
    {
        return name;
    }
    if (name.empty())
    {
        return parent;
    }
    return parent + kRegistryPathSeparator + name;
}

std::wstring RegistryParentPath(const std::wstring& path)
{
    const auto normalized = NormalizeRegistryPath(path);
    const auto position = normalized.find_last_of(kRegistryPathSeparator);
    if (position == std::wstring::npos)
    {
        return {};
    }
    return normalized.substr(0, position);
}

std::wstring RegistryLeafName(const std::wstring& path)
{
    const auto normalized = NormalizeRegistryPath(path);
    const auto position = normalized.find_last_of(kRegistryPathSeparator);
    if (position == std::wstring::npos)
    {
        return normalized;
    }
    return normalized.substr(position + 1);
}

std::wstring NormalizeRegistryPath(const std::wstring& path)
{
    std::wstring normalized;
    for (const auto& part : SplitRegistryPath(path))
    {
        normalized = JoinRegistryPath(normalized, part);
    }
    return normalized;
}

bool RegistryPathEquals(const std::wstring& left, const std::wstring& right)
{
    return EqualsIgnoreCase(NormalizeRegistryPath(left), NormalizeRegistryPath(right));
}

bool IsValidRegistryName(const std::wstring& name, bool allow_empty)
{
    if (name.empty())
    {
        return allow_empty;
    }
    return name.find(L'\\') == std::wstring::npos && name.find(L'/') == std::wstring::npos;
}

std::vector<std::uint8_t> RegistryStringData(const std::wstring& text)
{
    std::vector<std::uint8_t> data;
    data.reserve((text.size() + 1) * 2);

    for (const wchar_t character : text)
    {
        const auto code = static_cast<std::uint16_t>(character);
        data.push_back(static_cast<std::uint8_t>(code & 0xFF));
        data.push_back(static_cast<std::uint8_t>((code >> 8) & 0xFF));
    }

    data.push_back(0);
    data.push_back(0);
    return data;
}

std::wstring RegistryStringValue(const std::vector<std::uint8_t>& data)
{
    std::wstring text;
    for (std::size_t index = 0; index + 1 < data.size(); index += 2)
    {
        const auto code = static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(data[index]) | (static_cast<std::uint16_t>(data[index + 1]) << 8));
        if (code == 0)
        {
            break;
        }
        text.push_back(static_cast<wchar_t>(code));
    }
    return text;
}

std::vector<std::uint8_t> RegistryMultiStringData(const std::vector<std::wstring>& parts)
{
    std::vector<std::uint8_t> data;
    for (const auto& part : parts)
    {
        const auto encoded = RegistryStringData(part);
        data.insert(data.end(), encoded.begin(), encoded.end());
    }

    /* The empty list and the last entry are terminated by a second NUL. */
    data.push_back(0);
    data.push_back(0);
    return data;
}

std::vector<std::wstring> RegistryMultiStringValue(const std::vector<std::uint8_t>& data)
{
    std::vector<std::wstring> parts;
    std::wstring current;

    for (std::size_t index = 0; index + 1 < data.size(); index += 2)
    {
        const auto code = static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(data[index]) | (static_cast<std::uint16_t>(data[index + 1]) << 8));
        if (code == 0)
        {
            if (current.empty())
            {
                /* The terminator of the list or a separator of an empty entry. */
                continue;
            }
            parts.push_back(current);
            current.clear();
            continue;
        }
        current.push_back(static_cast<wchar_t>(code));
    }

    if (!current.empty())
    {
        parts.push_back(current);
    }
    return parts;
}

std::vector<std::uint8_t> RegistryDwordData(std::uint32_t value)
{
    return { static_cast<std::uint8_t>(value & 0xFF), static_cast<std::uint8_t>((value >> 8) & 0xFF),
             static_cast<std::uint8_t>((value >> 16) & 0xFF),
             static_cast<std::uint8_t>((value >> 24) & 0xFF) };
}

bool RegistryDwordValue(const std::vector<std::uint8_t>& data, std::uint32_t& out)
{
    if (data.size() != 4)
    {
        return false;
    }

    out = static_cast<std::uint32_t>(data[0]) | (static_cast<std::uint32_t>(data[1]) << 8)
          | (static_cast<std::uint32_t>(data[2]) << 16) | (static_cast<std::uint32_t>(data[3]) << 24);
    return true;
}

std::vector<std::uint8_t> RegistryQwordData(std::uint64_t value)
{
    std::vector<std::uint8_t> data;
    data.reserve(8);
    for (unsigned index = 0; index < 8; ++index)
    {
        data.push_back(static_cast<std::uint8_t>((value >> (index * 8)) & 0xFF));
    }
    return data;
}

bool RegistryQwordValue(const std::vector<std::uint8_t>& data, std::uint64_t& out)
{
    if (data.size() != 8)
    {
        return false;
    }

    std::uint64_t value = 0;
    for (unsigned index = 0; index < 8; ++index)
    {
        value |= static_cast<std::uint64_t>(data[index]) << (index * 8);
    }
    out = value;
    return true;
}

bool ParseRegistryHexText(const std::wstring& text, std::vector<std::uint8_t>& out, std::string& error)
{
    std::vector<std::uint8_t> bytes;
    int high = -1;

    for (const wchar_t character : text)
    {
        /* The registry editor separates the bytes of a `.reg` block by commas. */
        if (character == L' ' || character == L'\t' || character == L'\r' || character == L'\n'
            || character == L',')
        {
            continue;
        }

        int digit = -1;
        if (character >= L'0' && character <= L'9')
        {
            digit = static_cast<int>(character - L'0');
        }
        else if (character >= L'a' && character <= L'f')
        {
            digit = static_cast<int>(character - L'a') + 10;
        }
        else if (character >= L'A' && character <= L'F')
        {
            digit = static_cast<int>(character - L'A') + 10;
        }
        else
        {
            error = "the hexadecimal data holds an invalid character";
            return false;
        }

        if (high < 0)
        {
            high = digit;
            continue;
        }

        bytes.push_back(static_cast<std::uint8_t>((high << 4) | digit));
        high = -1;
    }

    if (high >= 0)
    {
        error = "the hexadecimal data holds an odd number of digits";
        return false;
    }

    out = std::move(bytes);
    return true;
}

std::wstring FormatRegistryHexText(const std::vector<std::uint8_t>& data)
{
    static const wchar_t* const alphabet = L"0123456789ABCDEF";

    std::wstring text;
    text.reserve(data.size() * 3);
    for (std::size_t index = 0; index < data.size(); ++index)
    {
        if (index > 0)
        {
            text.push_back(L' ');
        }
        text.push_back(alphabet[(data[index] >> 4) & 0x0F]);
        text.push_back(alphabet[data[index] & 0x0F]);
    }
    return text;
}

std::wstring FormatRegistryValueData(RegistryValueType type, const std::vector<std::uint8_t>& data,
                                     std::size_t max_chars)
{
    std::wstring text;

    switch (type)
    {
    case RegistryValueType::String:
    case RegistryValueType::ExpandString:
        text = RegistryStringValue(data);
        break;
    case RegistryValueType::Dword:
    {
        std::uint32_t value = 0;
        if (RegistryDwordValue(data, value))
        {
            text = L"0x" + FormatUnsigned(value, 16, 8) + L" (" + FormatUnsigned(value, 10, 0) + L")";
        }
        else
        {
            text = FormatRegistryHexText(data);
        }
        break;
    }
    case RegistryValueType::Qword:
    {
        std::uint64_t value = 0;
        if (RegistryQwordValue(data, value))
        {
            text = L"0x" + FormatUnsigned(value, 16, 16) + L" (" + FormatUnsigned(value, 10, 0) + L")";
        }
        else
        {
            text = FormatRegistryHexText(data);
        }
        break;
    }
    case RegistryValueType::MultiString:
    {
        const auto parts = RegistryMultiStringValue(data);
        for (std::size_t index = 0; index < parts.size(); ++index)
        {
            if (index > 0)
            {
                text += L"; ";
            }
            text += parts[index];
        }
        break;
    }
    case RegistryValueType::None:
    case RegistryValueType::Binary:
        text = FormatRegistryHexText(data);
        break;
    }

    if (text.size() > max_chars)
    {
        if (max_chars <= 3)
        {
            return text.substr(0, max_chars);
        }
        return text.substr(0, max_chars - 3) + L"...";
    }
    return text;
}

std::wstring FormatRegistryValueText(RegistryValueType type, const std::vector<std::uint8_t>& data)
{
    switch (type)
    {
    case RegistryValueType::String:
    case RegistryValueType::ExpandString:
        return RegistryStringValue(data);
    case RegistryValueType::Dword:
    {
        std::uint32_t value = 0;
        if (RegistryDwordValue(data, value))
        {
            return FormatUnsigned(value, 10, 0);
        }
        return FormatRegistryHexText(data);
    }
    case RegistryValueType::Qword:
    {
        std::uint64_t value = 0;
        if (RegistryQwordValue(data, value))
        {
            return FormatUnsigned(value, 10, 0);
        }
        return FormatRegistryHexText(data);
    }
    case RegistryValueType::MultiString:
    {
        const auto parts = RegistryMultiStringValue(data);
        std::wstring text;
        for (std::size_t index = 0; index < parts.size(); ++index)
        {
            if (index > 0)
            {
                text += L"\r\n";
            }
            text += parts[index];
        }
        return text;
    }
    case RegistryValueType::None:
    case RegistryValueType::Binary:
        return FormatRegistryHexText(data);
    }
    return {};
}

bool ParseRegistryValueText(RegistryValueType type, const std::wstring& text,
                            std::vector<std::uint8_t>& data, std::string& error)
{
    switch (type)
    {
    case RegistryValueType::String:
    case RegistryValueType::ExpandString:
        data = RegistryStringData(text);
        return true;
    case RegistryValueType::Dword:
    {
        std::uint64_t value = 0;
        if (!ParseUnsigned(text, 0xFFFFFFFFULL, value))
        {
            error = "the text is not a 32 bit number";
            return false;
        }
        data = RegistryDwordData(static_cast<std::uint32_t>(value));
        return true;
    }
    case RegistryValueType::Qword:
    {
        std::uint64_t value = 0;
        if (!ParseUnsigned(text, 0xFFFFFFFFFFFFFFFFULL, value))
        {
            error = "the text is not a 64 bit number";
            return false;
        }
        data = RegistryQwordData(value);
        return true;
    }
    case RegistryValueType::MultiString:
    {
        std::vector<std::wstring> parts;
        std::wstring current;
        for (const wchar_t character : text)
        {
            if (character == L'\n')
            {
                if (!current.empty())
                {
                    parts.push_back(current);
                }
                current.clear();
                continue;
            }
            if (character == L'\r')
            {
                continue;
            }
            current.push_back(character);
        }
        if (!current.empty())
        {
            parts.push_back(current);
        }
        data = RegistryMultiStringData(parts);
        return true;
    }
    case RegistryValueType::None:
    case RegistryValueType::Binary:
        return ParseRegistryHexText(text, data, error);
    }
    return false;
}

RegistryModel::RegistryModel()
{
    Reset();
}

void RegistryModel::Reset()
{
    root_ = RegistryKeyNode();
    root_.name = kRegistryContainerLabel;
    root_.removable = false;

    for (const auto& name : RegistryRootKeyNames())
    {
        RegistryKeyNode node;
        node.name = name;
        node.removable = false;
        root_.children.push_back(std::move(node));
    }
}

const RegistryKeyNode& RegistryModel::Root() const
{
    return root_;
}

RegistryKeyNode* RegistryModel::FindKey(const std::wstring& path)
{
    return const_cast<RegistryKeyNode*>(static_cast<const RegistryModel*>(this)->FindKey(path));
}

const RegistryKeyNode* RegistryModel::FindKey(const std::wstring& path) const
{
    const RegistryKeyNode* current = &root_;
    for (const auto& part : SplitRegistryPath(path))
    {
        const auto index = ChildIndex(*current, part);
        if (index < 0)
        {
            return nullptr;
        }
        current = &current->children[static_cast<std::size_t>(index)];
    }
    return current;
}

bool RegistryModel::EnsureKey(const std::wstring& path, std::string& error)
{
    const auto parts = SplitRegistryPath(path);
    if (parts.empty())
    {
        error = "the path does not name a key";
        return false;
    }

    bool is_root = false;
    for (const auto& name : RegistryRootKeyNames())
    {
        if (EqualsIgnoreCase(name, parts.front()))
        {
            is_root = true;
            break;
        }
    }
    if (!is_root)
    {
        error = "the path starts with an unknown root key: " + Quote(parts.front());
        return false;
    }

    RegistryKeyNode* current = &root_;
    for (const auto& part : parts)
    {
        auto index = ChildIndex(*current, part);
        if (index < 0)
        {
            RegistryKeyNode node;
            node.name = part;
            /* A key which is created now follows the key it is created below. */
            node.isolation = current->isolation;
            current->children.push_back(std::move(node));

            if (current == &root_)
            {
                index = static_cast<std::ptrdiff_t>(current->children.size() - 1);
            }
            else
            {
                /* The root keys keep their fixed order, every other key is sorted. */
                SortKey(*current);
                index = ChildIndex(*current, part);
            }
        }
        current = &current->children[static_cast<std::size_t>(index)];
    }
    return true;
}

std::vector<RegistryRow> RegistryModel::Rows(const std::wstring& path) const
{
    std::vector<RegistryRow> rows;

    const RegistryKeyNode* key = FindKey(path);
    if (key == nullptr)
    {
        return rows;
    }

    const auto key_path = NormalizeRegistryPath(path);
    rows.reserve(key->children.size() + key->values.size());

    for (const auto& child : key->children)
    {
        RegistryRow row;
        row.kind = RegistryRow::Kind::Key;
        row.key_path = key_path;
        row.name = child.name;
        row.isolation = child.isolation;
        rows.push_back(std::move(row));
    }

    for (const auto& value : key->values)
    {
        RegistryRow row;
        row.kind = RegistryRow::Kind::Value;
        row.key_path = key_path;
        row.name = value.name;
        row.isolation = value.isolation;
        row.type = value.type;
        row.data = value.data;
        rows.push_back(std::move(row));
    }

    return rows;
}

bool RegistryModel::AddKey(const std::wstring& parent, const std::wstring& name, std::string& error)
{
    RegistryKeyNode* node = FindKey(parent);
    if (node == nullptr)
    {
        error = "the parent key does not exist: " + Quote(parent);
        return false;
    }
    if (node == &root_)
    {
        error = "the registry view root holds the root keys only";
        return false;
    }
    if (!IsValidRegistryName(name, false))
    {
        error = "a key name must not be empty and must not contain a separator: " + Quote(name);
        return false;
    }
    if (ChildIndex(*node, name) >= 0)
    {
        error = "a key named " + Quote(name) + " already exists below " + Quote(node->name);
        return false;
    }

    RegistryKeyNode child;
    child.name = name;
    child.isolation = node->isolation;
    node->children.push_back(std::move(child));
    SortKey(*node);
    return true;
}

bool RegistryModel::RenameKey(const std::wstring& path, const std::wstring& new_name, std::string& error)
{
    RegistryKeyNode* node = FindKey(path);
    if (node == nullptr)
    {
        error = "the key does not exist: " + Quote(path);
        return false;
    }
    if (!node->removable)
    {
        error = "the root key " + Quote(node->name) + " cannot be renamed";
        return false;
    }
    if (!IsValidRegistryName(new_name, false))
    {
        error = "a key name must not be empty and must not contain a separator: " + Quote(new_name);
        return false;
    }

    RegistryKeyNode* parent = FindKey(RegistryParentPath(path));
    if (parent == nullptr)
    {
        error = "the parent key does not exist: " + Quote(RegistryParentPath(path));
        return false;
    }

    const auto index = ChildIndex(*parent, new_name);
    if (index >= 0 && &parent->children[static_cast<std::size_t>(index)] != node)
    {
        error = "a key named " + Quote(new_name) + " already exists below " + Quote(parent->name);
        return false;
    }

    node->name = new_name;
    SortKey(*parent);
    return true;
}

bool RegistryModel::RemoveKey(const std::wstring& path, std::string& error)
{
    RegistryKeyNode* node = FindKey(path);
    if (node == nullptr)
    {
        error = "the key does not exist: " + Quote(path);
        return false;
    }
    if (!node->removable)
    {
        error = "the root key " + Quote(node->name) + " cannot be removed";
        return false;
    }

    const auto parent_path = RegistryParentPath(path);
    RegistryKeyNode* parent = FindKey(parent_path);
    if (parent == nullptr)
    {
        error = "the parent key does not exist: " + Quote(parent_path);
        return false;
    }

    const auto index = ChildIndex(*parent, RegistryLeafName(path));
    if (index < 0)
    {
        error = "the key does not exist: " + Quote(path);
        return false;
    }

    parent->children.erase(parent->children.begin() + index);
    return true;
}

bool RegistryModel::AddValue(const std::wstring& parent, const std::wstring& name, RegistryValueType type,
                             const std::vector<std::uint8_t>& data, std::string& error)
{
    RegistryKeyNode* node = FindKey(parent);
    if (node == nullptr)
    {
        error = "the key does not exist: " + Quote(parent);
        return false;
    }
    if (node == &root_)
    {
        error = "the registry view root holds the root keys only";
        return false;
    }
    if (!IsValidRegistryName(name, true))
    {
        error = "a value name must not contain a separator: " + Quote(name);
        return false;
    }
    if (ValueIndex(*node, name) >= 0)
    {
        error = "a value named " + Quote(name) + " already exists below " + Quote(node->name);
        return false;
    }

    RegistryValueEntry value;
    value.name = name;
    value.type = type;
    value.data = data;
    /* A value which is created now follows the key it is created in. */
    value.isolation = node->isolation;
    node->values.push_back(std::move(value));
    SortKey(*node);
    return true;
}

bool RegistryModel::SetValue(const std::wstring& parent, const std::wstring& name, RegistryValueType type,
                             const std::vector<std::uint8_t>& data, std::string& error)
{
    RegistryKeyNode* node = FindKey(parent);
    if (node == nullptr)
    {
        error = "the key does not exist: " + Quote(parent);
        return false;
    }
    if (node == &root_)
    {
        error = "the registry view root holds the root keys only";
        return false;
    }
    if (!IsValidRegistryName(name, true))
    {
        error = "a value name must not contain a separator: " + Quote(name);
        return false;
    }

    const auto index = ValueIndex(*node, name);
    if (index < 0)
    {
        RegistryValueEntry value;
        value.name = name;
        value.type = type;
        value.data = data;
        /* A value which is created now follows the key it is created in. */
        value.isolation = node->isolation;
        node->values.push_back(std::move(value));
        SortKey(*node);
        return true;
    }

    /* An existing value keeps its isolation mode, an import must not drop it. */
    auto& value = node->values[static_cast<std::size_t>(index)];
    value.type = type;
    value.data = data;
    return true;
}

bool RegistryModel::UpdateValue(const std::wstring& parent, const std::wstring& old_name,
                                const std::wstring& new_name, RegistryValueType type,
                                const std::vector<std::uint8_t>& data, std::string& error)
{
    RegistryKeyNode* node = FindKey(parent);
    if (node == nullptr)
    {
        error = "the key does not exist: " + Quote(parent);
        return false;
    }
    if (node == &root_)
    {
        error = "the registry view root holds the root keys only";
        return false;
    }
    if (!IsValidRegistryName(new_name, true))
    {
        error = "a value name must not contain a separator: " + Quote(new_name);
        return false;
    }

    const auto index = ValueIndex(*node, old_name);
    if (index < 0)
    {
        error = "the value does not exist: " + Quote(old_name);
        return false;
    }

    const auto existing = ValueIndex(*node, new_name);
    if (existing >= 0 && existing != index)
    {
        error = "a value named " + Quote(new_name) + " already exists below " + Quote(node->name);
        return false;
    }

    auto& value = node->values[static_cast<std::size_t>(index)];
    value.name = new_name;
    value.type = type;
    value.data = data;
    SortKey(*node);
    return true;
}

bool RegistryModel::RemoveValue(const std::wstring& parent, const std::wstring& name)
{
    RegistryKeyNode* node = FindKey(parent);
    if (node == nullptr || node == &root_)
    {
        return false;
    }

    const auto index = ValueIndex(*node, name);
    if (index < 0)
    {
        return false;
    }

    node->values.erase(node->values.begin() + index);
    return true;
}

bool RegistryModel::SetKeyIsolation(const std::wstring& path, RegistryIsolation isolation)
{
    RegistryKeyNode* node = FindKey(path);
    if (node == nullptr)
    {
        return false;
    }

    /*
     * Only the key itself changes: its sub keys and its values keep their own
     * modes, so a mode never reaches below the key by accident. The subtree is
     * overwritten only through ApplyIsolationToSubtree().
     */
    node->isolation = isolation;
    return true;
}

bool RegistryModel::SetValueIsolation(const std::wstring& parent, const std::wstring& name,
                                      RegistryIsolation isolation)
{
    RegistryKeyNode* node = FindKey(parent);
    if (node == nullptr)
    {
        return false;
    }

    const auto index = ValueIndex(*node, name);
    if (index < 0)
    {
        return false;
    }

    auto& value = node->values[static_cast<std::size_t>(index)];
    value.isolation = isolation;
    return true;
}

bool RegistryModel::ApplyIsolationToSubtree(const std::wstring& path, RegistryIsolation isolation,
                                            bool include_values)
{
    RegistryKeyNode* node = FindKey(path);
    if (node == nullptr)
    {
        return false;
    }

    ApplyIsolation(*node, isolation, include_values);
    return true;
}

std::ptrdiff_t RegistryModel::ChildIndex(const RegistryKeyNode& parent, const std::wstring& name)
{
    for (std::size_t index = 0; index < parent.children.size(); ++index)
    {
        if (EqualsIgnoreCase(parent.children[index].name, name))
        {
            return static_cast<std::ptrdiff_t>(index);
        }
    }
    return -1;
}

std::ptrdiff_t RegistryModel::ValueIndex(const RegistryKeyNode& parent, const std::wstring& name)
{
    for (std::size_t index = 0; index < parent.values.size(); ++index)
    {
        if (EqualsIgnoreCase(parent.values[index].name, name))
        {
            return static_cast<std::ptrdiff_t>(index);
        }
    }
    return -1;
}

void RegistryModel::SortKey(RegistryKeyNode& key)
{
    std::sort(key.children.begin(), key.children.end(),
              [](const RegistryKeyNode& left, const RegistryKeyNode& right) {
                  return LessIgnoreCase(left.name, right.name);
              });
    std::sort(key.values.begin(), key.values.end(),
              [](const RegistryValueEntry& left, const RegistryValueEntry& right) {
                  return LessIgnoreCase(left.name, right.name);
              });
}

void RegistryModel::ApplyIsolation(RegistryKeyNode& key, RegistryIsolation isolation, bool include_values)
{
    /*
     * The mode is overwritten without an exception: a sub key or a value which
     * the user changed before is set to the new mode as well, which is what
     * the explicit recursion of the user interface asks for.
     */
    key.isolation = isolation;

    if (include_values)
    {
        for (auto& value : key.values)
        {
            value.isolation = isolation;
        }
    }

    for (auto& child : key.children)
    {
        ApplyIsolation(child, isolation, include_values);
    }
}

} // namespace appbox
