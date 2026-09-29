#include "RegistryIsolationFile.hpp"
#include "WString.hpp"
#include <nlohmann/json.hpp>
#include <exception>
#include <utility>

namespace
{

/**
 * @brief Append the isolation modes of one key, of its values and of its subtree.
 *
 * @param[in] key The key to visit.
 * @param[in] path Path of the key inside the virtual registry.
 * @param[in,out] document The document which collects the entries.
 */
void CollectEntries(const appbox::RegistryKeyNode& key, const std::wstring& path,
                    appbox::registry_isolation::Document& document)
{
    appbox::registry_isolation::KeyEntry key_entry;
    key_entry.path = appbox::WideToUTF8(path);
    key_entry.isolation = key.isolation;
    document.keys.push_back(std::move(key_entry));

    for (const auto& value : key.values)
    {
        appbox::registry_isolation::ValueEntry value_entry;
        value_entry.path = appbox::WideToUTF8(path);
        value_entry.name = appbox::WideToUTF8(value.name);
        value_entry.isolation = value.isolation;
        document.values.push_back(std::move(value_entry));
    }

    for (const auto& child : key.children)
    {
        CollectEntries(child, appbox::JoinRegistryPath(path, child.name), document);
    }
}

} // namespace

bool appbox::BuildRegistryIsolationFile(const RegistryModel& model, std::string& text, std::string& error)
{
    error.clear();
    text.clear();

    try
    {
        /*
         * The document is built as the structure of the schema
         * (`common/RegistryIsolation.hpp`) and not as a JSON object, so the text
         * the packer writes and the text the sandbox reads are described by one
         * definition.
         */
        registry_isolation::Document document;

        /*
         * The container itself carries no path, so the walk starts at the root
         * keys: their names are the first component of every entry path.
         */
        for (const auto& root : model.Root().children)
        {
            CollectEntries(root, root.name, document);
        }

        text = nlohmann::json(document).dump(2);
        return true;
    }
    catch (const std::exception& e)
    {
        error = std::string("failed to build the registry isolation file: ") + e.what();
        return false;
    }
}
