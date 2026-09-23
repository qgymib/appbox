#include "RegistryIsolationFile.hpp"
#include "WString.hpp"
#include <nlohmann/json.hpp>
#include <exception>

namespace
{

/**
 * @brief Append the isolation modes of one key, of its values and of its subtree.
 *
 * @param[in] key The key to visit.
 * @param[in] path Path of the key inside the virtual registry.
 * @param[in,out] keys The list of the key entries.
 * @param[in,out] values The list of the value entries.
 */
void CollectEntries(const appbox::RegistryKeyNode& key, const std::wstring& path, nlohmann::json& keys,
                    nlohmann::json& values)
{
    nlohmann::json key_entry;
    key_entry[appbox::registry_isolation::kPathKey] = appbox::WideToUTF8(path);
    key_entry[appbox::registry_isolation::kIsolationKey] =
        appbox::registry_isolation::IsolationToken(key.isolation);
    keys.push_back(std::move(key_entry));

    for (const auto& value : key.values)
    {
        nlohmann::json value_entry;
        value_entry[appbox::registry_isolation::kPathKey] = appbox::WideToUTF8(path);
        value_entry[appbox::registry_isolation::kNameKey] = appbox::WideToUTF8(value.name);
        value_entry[appbox::registry_isolation::kIsolationKey] =
            appbox::registry_isolation::IsolationToken(value.isolation);
        values.push_back(std::move(value_entry));
    }

    for (const auto& child : key.children)
    {
        CollectEntries(child, appbox::JoinRegistryPath(path, child.name), keys, values);
    }
}

} // namespace

bool appbox::BuildRegistryIsolationFile(const RegistryModel& model, std::string& text, std::string& error)
{
    error.clear();
    text.clear();

    try
    {
        nlohmann::json document;
        document[registry_isolation::kVersionKey] = registry_isolation::kVersion;
        document[registry_isolation::kKeysKey] = nlohmann::json::array();
        document[registry_isolation::kValuesKey] = nlohmann::json::array();

        /*
         * The container itself carries no path, so the walk starts at the root
         * keys: their names are the first component of every entry path.
         */
        for (const auto& root : model.Root().children)
        {
            CollectEntries(root, root.name, document[registry_isolation::kKeysKey],
                           document[registry_isolation::kValuesKey]);
        }

        text = document.dump(2);
        return true;
    }
    catch (const std::exception& e)
    {
        error = std::string("failed to build the registry isolation file: ") + e.what();
        return false;
    }
}
