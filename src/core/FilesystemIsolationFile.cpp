#include "FilesystemIsolationFile.hpp"
#include "WString.hpp"
#include <nlohmann/json.hpp>
#include <exception>
#include <utility>

bool appbox::BuildFilesystemIsolationFile(const FilesystemIsolationModel& model, std::string& text,
                                          std::string& error)
{
    error.clear();
    text.clear();

    try
    {
        nlohmann::json document;
        document[filesystem_isolation::kVersionKey] = filesystem_isolation::kVersion;
        document[filesystem_isolation::kEntriesKey] = nlohmann::json::array();

        for (const auto& entry : model.Entries())
        {
            nlohmann::json item;
            item[filesystem_isolation::kPathKey] = WideToUTF8(entry.path);
            item[filesystem_isolation::kKindKey] = filesystem_isolation::EntryKindToken(entry.kind);
            item[filesystem_isolation::kIsolationKey] = filesystem_isolation::IsolationToken(entry.isolation);
            document[filesystem_isolation::kEntriesKey].push_back(std::move(item));
        }

        text = document.dump(2);
        return true;
    }
    catch (const std::exception& e)
    {
        error = std::string("failed to build the filesystem isolation file: ") + e.what();
        return false;
    }
}
