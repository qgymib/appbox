#include "IsolationTable.hpp"
#include "WString.hpp"
#include <nlohmann/json.hpp>
#include <cwctype>
#include <exception>
#include <string>
#include <utility>
#include <vector>

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
 * @brief Read a string member of a json object.
 *
 * @param[in] entry The json object to read.
 * @param[in] member The member name.
 * @param[in] required Whether a missing member is an error.
 * @param[out] out The member value.
 * @param[out] error Error description on failure.
 * @return true when the member was read.
 */
bool ReadString(const nlohmann::json& entry, const char* member, bool required, std::string& out, std::string& error)
{
    const auto it = entry.find(member);
    if (it == entry.end())
    {
        if (!required)
        {
            return true;
        }
        error = std::string("an isolation file entry has no '") + member + "' member";
        return false;
    }

    if (!it->is_string())
    {
        error = std::string("the '") + member + "' member of an isolation file entry is not a string";
        return false;
    }

    out = it->get<std::string>();
    return true;
}

/**
 * @brief Read one entry of a mode list of the isolation file.
 *
 * @param[in] entry The json object of the entry.
 * @param[in] with_name Whether the entry carries the name of a value.
 * @param[out] path The key path of the entry.
 * @param[out] name The value name of the entry, empty for a key entry.
 * @param[out] mode The isolation mode of the entry.
 * @param[out] error Error description on failure.
 * @return true when the entry was read.
 */
bool ReadEntry(const nlohmann::json& entry, bool with_name, std::string& path, std::string& name,
               appbox::RegistryIsolation& mode, std::string& error)
{
    if (!entry.is_object())
    {
        error = "an isolation file entry is not an object";
        return false;
    }

    std::string token;
    if (!ReadString(entry, appbox::registry_isolation::kPathKey, true, path, error) ||
        !ReadString(entry, appbox::registry_isolation::kNameKey, with_name, name, error) ||
        !ReadString(entry, appbox::registry_isolation::kIsolationKey, true, token, error))
    {
        return false;
    }

    if (path.empty())
    {
        error = "an isolation file entry has an empty key path";
        return false;
    }

    if (!appbox::registry_isolation::ParseIsolationToken(token, mode))
    {
        error = "unknown isolation mode '" + token + "' in the isolation file";
        return false;
    }

    return true;
}

/**
 * @brief Read a mode list of the isolation file.
 *
 * @param[in] document The whole document.
 * @param[in] member The member name of the list.
 * @param[in] with_name Whether the entries carry a value name.
 * @param[out] entries The parsed entries in file order.
 * @param[out] error Error description on failure.
 * @return true when the list was read, also when the document does not hold it.
 */
bool ReadEntryList(const nlohmann::json& document, const char* member, bool with_name,
                   std::vector<std::pair<appbox::registry::IsolationValueKey, appbox::RegistryIsolation>>& entries,
                   std::string& error)
{
    const auto list = document.find(member);
    if (list == document.end())
    {
        return true;
    }

    if (!list->is_array())
    {
        error = std::string("the '") + member + "' member of the isolation file is not a list";
        return false;
    }

    for (const auto& entry : *list)
    {
        std::string path;
        std::string name;
        appbox::RegistryIsolation mode = appbox::RegistryIsolation::WriteCopy;
        if (!ReadEntry(entry, with_name, path, name, mode, error))
        {
            return false;
        }

        appbox::registry::IsolationValueKey key;
        key.key_path = appbox::UTF8ToWide(path);
        key.value_name = appbox::UTF8ToWide(name);
        entries.emplace_back(key, mode);
    }

    return true;
}

} // namespace

bool appbox::registry::IsolationKeyLess::operator()(const std::wstring& left, const std::wstring& right) const
{
    return LessIgnoreCase(left, right);
}

bool appbox::registry::IsolationValueKeyLess::operator()(const IsolationValueKey& left,
                                                         const IsolationValueKey& right) const
{
    if (!EqualsIgnoreCase(left.key_path, right.key_path))
    {
        return LessIgnoreCase(left.key_path, right.key_path);
    }
    return LessIgnoreCase(left.value_name, right.value_name);
}

bool appbox::registry::IsolationTable::Parse(const std::string& text, std::string& error)
{
    std::vector<std::pair<IsolationValueKey, RegistryIsolation>> key_entries;
    std::vector<std::pair<IsolationValueKey, RegistryIsolation>> value_entries;

    try
    {
        const auto document = nlohmann::json::parse(text);
        if (!document.is_object())
        {
            error = "the isolation file is not a JSON object";
            return false;
        }

        if (document.value(registry_isolation::kVersionKey, 0) != registry_isolation::kVersion)
        {
            error = "unsupported isolation file version";
            return false;
        }

        if (!ReadEntryList(document, registry_isolation::kKeysKey, false, key_entries, error) ||
            !ReadEntryList(document, registry_isolation::kValuesKey, true, value_entries, error))
        {
            return false;
        }
    }
    catch (const std::exception& e)
    {
        error = std::string("the isolation file is not valid: ") + e.what();
        return false;
    }

    /*
     * The two lists stay apart: a key entry covers the key and everything
     * below it, while a value entry only covers the value of that name, which
     * is the empty name for the default value of a key.
     */
    std::map<std::wstring, RegistryIsolation, IsolationKeyLess> keys;
    for (const auto& entry : key_entries)
    {
        keys[entry.first.key_path] = entry.second;
    }

    std::map<IsolationValueKey, RegistryIsolation, IsolationValueKeyLess> values;
    for (const auto& entry : value_entries)
    {
        values[entry.first] = entry.second;
    }

    keys_ = std::move(keys);
    values_ = std::move(values);
    return true;
}

bool appbox::registry::IsolationTable::Empty() const
{
    return keys_.empty() && values_.empty();
}

std::size_t appbox::registry::IsolationTable::KeyCount() const
{
    return keys_.size();
}

std::size_t appbox::registry::IsolationTable::ValueCount() const
{
    return values_.size();
}

appbox::RegistryIsolation appbox::registry::IsolationTable::KeyMode(const std::wstring& key_path) const
{
    /*
     * Walk the path upwards: the closest listed ancestor covers its whole
     * subtree, which is what makes `Full` of a key hide the host entries
     * below it as well.
     */
    std::wstring probe = key_path;
    for (;;)
    {
        const auto it = keys_.find(probe);
        if (it != keys_.end())
        {
            return it->second;
        }

        const auto separator = probe.rfind(L'\\');
        if (separator == std::wstring::npos)
        {
            return RegistryIsolation::WriteCopy;
        }
        probe.erase(separator);
    }
}

appbox::RegistryIsolation appbox::registry::IsolationTable::ValueMode(const std::wstring& key_path,
                                                                     const std::wstring& value_name) const
{
    const auto it = values_.find(IsolationValueKey{ key_path, value_name });
    if (it != values_.end())
    {
        return it->second;
    }

    /* A value which is not listed follows the key which holds it. */
    return KeyMode(key_path);
}

bool appbox::registry::IsolationTable::HidesHost(RegistryIsolation mode)
{
    return mode == RegistryIsolation::Full || mode == RegistryIsolation::Hide;
}
