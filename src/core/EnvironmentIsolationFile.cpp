#include "EnvironmentIsolationFile.hpp"
#include "EnvironmentIsolation.hpp"
#include "WString.hpp"
#include <nlohmann/json.hpp>
#include <exception>
#include <utility>

bool appbox::BuildEnvironmentIsolationFile(const EnvironmentModel& model, std::string& text, std::string& error)
{
    error.clear();
    text.clear();

    try
    {
        /*
         * The document is built as the structure of the schema
         * (`common/EnvironmentIsolation.hpp`) and not as a JSON object, so the
         * text the packer writes and the text the sandbox reads are described by
         * one definition.
         */
        environment_isolation::Document document;

        for (const auto& entry : model.Entries())
        {
            environment_isolation::Entry item;
            item.name = WideToUTF8(entry.name);
            item.value = WideToUTF8(entry.value);
            item.isolation = entry.isolation;
            item.merge = entry.merge;
            item.merge_string = WideToUTF8(entry.merge_string);
            document.entries.push_back(std::move(item));
        }

        text = nlohmann::json(document).dump(2);
        return true;
    }
    catch (const std::exception& e)
    {
        error = std::string("failed to build the environment isolation file: ") + e.what();
        return false;
    }
}
