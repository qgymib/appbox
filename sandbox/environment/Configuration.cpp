#include "utils/WinAPI.h" /* Must be first include file */
#include <exception>
#include <utility>
#include "Table.hpp"
#include "WString.hpp"
#include <nlohmann/json.hpp>
#include "Configuration.hpp"

bool appbox::environment::ParseIsolationDocument(const std::string& text, std::vector<ConfiguredVariable>& out,
                                                 std::string& error)
{
    out.clear();
    error.clear();

    const std::string description = "the environment isolation file";

    environment_isolation::Document document;
    try
    {
        /*
         * The document is read as the structure of its schema
         * (`common/EnvironmentIsolation.hpp`) and never member by member, so a
         * member which is missing, which is of another type or which names an
         * unknown mode is refused while the file is read. The error text of an
         * entry names its position in the list.
         */
        document = nlohmann::json::parse(text).get<environment_isolation::Document>();
    }
    catch (const IsolationDocumentError& e)
    {
        error = e.what();
        return false;
    }
    catch (const std::exception& e)
    {
        error = description + " is not valid JSON: " + e.what();
        return false;
    }

    if (document.version != environment_isolation::kVersion)
    {
        error = description + " carries version " + std::to_string(document.version) + ", which is not supported";
        return false;
    }

    /*
     * The entries are collected apart from the result: a document which is
     * refused has to leave the caller without a single entry, so a caller never
     * applies half of a broken configuration.
     */
    std::vector<ConfiguredVariable> parsed;

    try
    {
        for (std::size_t index = 0; index < document.entries.size(); ++index)
        {
            const environment_isolation::Entry& item = document.entries[index];

            ConfiguredVariable variable;
            variable.name = UTF8ToWide(item.name);
            variable.value = UTF8ToWide(item.value);
            variable.isolation = item.isolation;
            variable.merge = item.merge;
            variable.merge_string = UTF8ToWide(item.merge_string);

            /*
             * A variable which is listed twice is refused by the reader and not
             * by the structure: the comparison of two names is the case
             * insensitive ordinal comparison of the operating system, which is
             * applied to the wide text of the names.
             */
            for (const auto& listed : parsed)
            {
                if (NamesEqual(listed.name, variable.name))
                {
                    error = environment_isolation::EntryPrefix(index) + "the variable is listed twice";
                    return false;
                }
            }

            parsed.push_back(std::move(variable));
        }
    }
    catch (const std::exception& e)
    {
        error = description + " is not valid: " + e.what();
        return false;
    }

    out = std::move(parsed);
    return true;
}

appbox::environment::State::State() = default;

void appbox::environment::State::Clear()
{
    std::lock_guard<std::mutex> guard(lock_);
    entries_.clear();
}

std::size_t appbox::environment::State::Count() const
{
    std::lock_guard<std::mutex> guard(lock_);
    return entries_.size();
}

std::size_t appbox::environment::State::IndexOf(const std::wstring& name) const
{
    for (std::size_t index = 0; index < entries_.size(); ++index)
    {
        if (NamesEqual(entries_[index].name, name))
        {
            return index;
        }
    }
    return entries_.size();
}

void appbox::environment::State::Record(const std::wstring& name, const std::wstring& value)
{
    if (name.empty())
    {
        return;
    }

    std::lock_guard<std::mutex> guard(lock_);

    const std::size_t index = IndexOf(name);
    if (index != entries_.size())
    {
        entries_[index].value = value;
        entries_[index].deleted = false;
        return;
    }

    entries_.push_back(Modification{ name, value, false });
}

void appbox::environment::State::RecordDeletion(const std::wstring& name)
{
    if (name.empty())
    {
        return;
    }

    std::lock_guard<std::mutex> guard(lock_);

    const std::size_t index = IndexOf(name);
    if (index != entries_.size())
    {
        entries_[index].value.clear();
        entries_[index].deleted = true;
        return;
    }

    entries_.push_back(Modification{ name, std::wstring(), true });
}

std::vector<appbox::environment::Modification> appbox::environment::State::Entries() const
{
    std::lock_guard<std::mutex> guard(lock_);
    return entries_;
}

bool appbox::environment::State::Build(std::string& text, std::string& error) const
{
    text.clear();
    error.clear();

    const std::vector<Modification> entries = Entries();

    try
    {
        /*
         * The document is built as the structure of its schema
         * (`common/EnvironmentIsolation.hpp`) and not as a JSON object, so the
         * text the sandbox writes and the text it reads back are described by
         * one definition.
         */
        environment_isolation::StateDocument document;

        for (const auto& entry : entries)
        {
            environment_isolation::StateEntry item;
            item.name = WideToUTF8(entry.name);
            item.value = WideToUTF8(entry.value);
            item.deleted = entry.deleted;
            document.entries.push_back(std::move(item));
        }

        /*
         * A name or a value the application stored may not be well formed
         * UTF-16, so the text is written with the replacement character instead
         * of failing the document.
         */
        text = nlohmann::json(document).dump(2, ' ', false, nlohmann::json::error_handler_t::replace);
        return true;
    }
    catch (const std::exception& e)
    {
        error = std::string("failed to build the environment state: ") + e.what();
        return false;
    }
}

bool appbox::environment::State::Parse(const std::string& text, std::string& error)
{
    error.clear();

    const std::string description = "the environment state file";

    environment_isolation::StateDocument document;
    try
    {
        /*
         * The document is read as the structure of its schema
         * (`common/EnvironmentIsolation.hpp`) and never member by member, so a
         * member which is missing or which is of another type is refused while
         * the file is read.
         */
        document = nlohmann::json::parse(text).get<environment_isolation::StateDocument>();
    }
    catch (const IsolationDocumentError& e)
    {
        error = e.what();
        return false;
    }
    catch (const std::exception& e)
    {
        error = description + " is not valid JSON: " + e.what();
        return false;
    }

    if (document.version != environment_isolation::kStateVersion)
    {
        error = description + " carries version " + std::to_string(document.version) + ", which is not supported";
        return false;
    }

    std::vector<Modification> parsed;

    try
    {
        for (const auto& item : document.entries)
        {
            Modification modification;
            modification.name = UTF8ToWide(item.name);
            modification.value = UTF8ToWide(item.value);
            modification.deleted = item.deleted;

            /* A variable which is listed twice ends up with the last entry. */
            std::size_t position = parsed.size();
            for (std::size_t listed = 0; listed < parsed.size(); ++listed)
            {
                if (NamesEqual(parsed[listed].name, modification.name))
                {
                    position = listed;
                    break;
                }
            }

            if (position == parsed.size())
            {
                parsed.push_back(std::move(modification));
            }
            else
            {
                parsed[position] = std::move(modification);
            }
        }
    }
    catch (const std::exception& e)
    {
        error = description + " is not valid: " + e.what();
        return false;
    }

    std::lock_guard<std::mutex> guard(lock_);
    entries_ = std::move(parsed);
    return true;
}
