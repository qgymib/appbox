#include "FilesystemIsolationFile.hpp"
#include "WString.hpp"
#include <nlohmann/json.hpp>
#include <exception>
#include <utility>

bool appbox::BuildFilesystemIsolationFile(const FilesystemIsolationModel& model, std::string& text, std::string& error)
{
    error.clear();
    text.clear();

    try
    {
        /*
         * The document is built as the structure of the schema
         * (`common/FilesystemIsolation.hpp`) and not as a JSON object, so the
         * text the packer writes and the text the sandbox reads are described
         * by one definition.
         */
        filesystem_isolation::Document document;

        for (const auto& entry : model.Entries())
        {
            filesystem_isolation::Entry item;
            item.path = WideToUTF8(entry.path);
            item.kind = entry.kind;
            item.isolation = entry.isolation;
            document.entries.push_back(std::move(item));
        }

        text = nlohmann::json(document).dump(2);
        return true;
    }
    catch (const std::exception& e)
    {
        error = std::string("failed to build the filesystem isolation file: ") + e.what();
        return false;
    }
}
