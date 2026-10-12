#include "IsolationTable.hpp"
#include "utils/Log.hpp"
#include "WString.hpp"
#include <nlohmann/json.hpp>
#include <cwctype>
#include <exception>
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
    std::map<std::wstring, RegistryKeyIsolation, IsolationKeyLess>             keys;
    std::map<IsolationValueKey, RegistryValueIsolation, IsolationValueKeyLess> values;

    try
    {
        /*
         * The document is read as the structure of its schema
         * (`common/RegistryIsolation.hpp`) and never member by member, so a
         * member which is missing, which is of another type or which names an
         * unknown mode is refused while the file is read.
         */
        const auto document = nlohmann::json::parse(text).get<registry_isolation::Document>();

        if (document.version != registry_isolation::kVersion)
        {
            error = "unsupported isolation file version";
            return false;
        }

        /*
         * The two lists stay apart: a key entry covers the key and everything
         * below it, while a value entry only covers the value of that name,
         * which is the empty name for the default value of a key.
         */
        for (const auto& entry : document.keys)
        {
            keys[UTF8ToWide(entry.path)] = entry.isolation;
        }

        for (const auto& entry : document.values)
        {
            IsolationValueKey key;
            key.key_path = UTF8ToWide(entry.path);
            key.value_name = UTF8ToWide(entry.name);
            values[key] = entry.isolation;
        }
    }
    catch (const IsolationDocumentError& e)
    {
        error = e.what();
        return false;
    }
    catch (const std::exception& e)
    {
        error = std::string("the isolation file is not valid: ") + e.what();
        return false;
    }

    /*
     * The document is applied as the layer it describes: the modes it lists
     * replace the modes of the same entry which the layers below it set, while
     * the entries it does not list stay untouched. The document was collected
     * into local tables, so a document which cannot be used leaves the table
     * as it was.
     */
    for (auto& entry : keys)
    {
        keys_[entry.first] = entry.second;
    }

    for (auto& entry : values)
    {
        values_[entry.first] = entry.second;
    }

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

bool appbox::registry::IsolationTable::NamesRootKeys() const
{
    for (const wchar_t* root : registry_isolation::kRootKeyNames)
    {
        const auto it = keys_.find(root);
        if (it == keys_.end())
        {
            return false;
        }
    }

    return true;
}

appbox::RegistryKeyIsolation appbox::registry::IsolationTable::KeyMode(const std::wstring& key_path) const
{
    /*
     * Walk the path upwards: the closest listed ancestor covers its whole
     * subtree, which is what makes `Full` of a key hide the host entries below
     * it as well. `Hide` is the exception of the rule and covers the subtree
     * from any level: the content of a hidden subtree lives in the hidden store
     * of the hive, so a key below a hidden key has to be hidden as well,
     * whatever mode it names itself.
     */
    std::optional<RegistryKeyIsolation> closest;
    std::wstring                        probe = key_path;
    for (;;)
    {
        const auto it = keys_.find(probe);
        if (it != keys_.end())
        {
            if (it->second == RegistryKeyIsolation::Hide)
            {
                return RegistryKeyIsolation::Hide;
            }

            if (!closest.has_value())
            {
                closest = it->second;
            }
        }

        const auto separator = probe.rfind(L'\\');
        if (separator == std::wstring::npos)
        {
            break;
        }
        probe.erase(separator);
    }

    if (closest.has_value())
    {
        return *closest;
    }

    /*
     * The isolation file of the archive has to name the five root keys of the
     * view, so a path of the view which resolves to no listed entry describes a
     * table which was never validated. Reporting `Hide` keeps the host registry
     * out of the view, which is the answer a sandbox may never widen by
     * accident.
     */
    LOG_W("the isolation table names no mode for {}: the entry is treated as hidden", appbox::WideToUTF8(key_path));
    return RegistryKeyIsolation::Hide;
}

std::optional<appbox::RegistryValueIsolation> appbox::registry::IsolationTable::ListedValueMode(
    const std::wstring& key_path, const std::wstring& value_name) const
{
    const auto it = values_.find(IsolationValueKey{ key_path, value_name });
    if (it == values_.end())
    {
        return std::nullopt;
    }

    return it->second;
}
