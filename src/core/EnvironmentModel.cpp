#include "EnvironmentModel.hpp"
#include "WString.hpp"
#include <cwctype>

namespace
{

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
 * @brief Whether two variable names name the same variable.
 *
 * The environment of a process is case insensitive on Windows, so `Path` names
 * the same variable as `PATH` does and the two must not be listed twice.
 *
 * @param[in] left Left name.
 * @param[in] right Right name.
 * @return true when both names are equal ignoring the case.
 */
bool NamesEqual(const std::wstring& left, const std::wstring& right)
{
    if (left.size() != right.size())
    {
        return false;
    }

    for (std::size_t index = 0; index < left.size(); ++index)
    {
        if (std::towlower(static_cast<std::wint_t>(left[index])) !=
            std::towlower(static_cast<std::wint_t>(right[index])))
        {
            return false;
        }
    }
    return true;
}

/**
 * @brief Validate the name of an environment variable.
 *
 * @param[in] name The name to check.
 * @param[out] error Error description on failure.
 * @return true when the name may be stored.
 */
bool ValidateName(const std::wstring& name, std::string& error)
{
    if (name.empty())
    {
        error = "the name must not be empty";
        return false;
    }
    if (name.find(L'=') != std::wstring::npos)
    {
        /* The equals sign separates the name from the value in the block. */
        error = "the name " + Quote(name) + " contains an equals sign";
        return false;
    }
    return true;
}

} // namespace

namespace appbox
{

const std::vector<std::wstring>& EnvironmentIsolationNames()
{
    static const std::vector<std::wstring> names = { L"Full", L"Write Copy" };
    return names;
}

const std::vector<std::wstring>& EnvironmentMergeModeNames()
{
    static const std::vector<std::wstring> names = { L"Replace", L"Host", L"Prepend", L"Append" };
    return names;
}

std::wstring EnvironmentIsolationName(EnvironmentIsolation isolation)
{
    const auto& names = EnvironmentIsolationNames();
    const auto  index = static_cast<std::size_t>(isolation);
    if (index >= names.size())
    {
        return names.front();
    }
    return names[index];
}

std::wstring EnvironmentMergeModeName(EnvironmentMergeMode merge)
{
    const auto& names = EnvironmentMergeModeNames();
    const auto  index = static_cast<std::size_t>(merge);
    if (index >= names.size())
    {
        return names.front();
    }
    return names[index];
}

std::wstring EnvironmentIsolationDescription(EnvironmentIsolation isolation)
{
    switch (isolation)
    {
    case EnvironmentIsolation::Full:
        return L"Isolation mode 'Full': the application does not see the value of the host; it sees the stored value "
               L"of the variable.";
    case EnvironmentIsolation::WriteCopy:
        return L"Isolation mode 'Write Copy': the application sees the value of the host merged with the stored value "
               L"of the variable, and the merge mode decides how the two are joined. This is the default mode.";
    }
    return L"";
}

std::wstring EnvironmentMergeModeDescription(EnvironmentMergeMode merge)
{
    switch (merge)
    {
    case EnvironmentMergeMode::Replace:
        return L"Merge mode 'Replace': the application sees the stored value of the variable and not the value of the "
               L"host.\nExample: a host value of 'foo' and a stored value of 'bar' report 'bar'.";
    case EnvironmentMergeMode::Host:
        return L"Merge mode 'Host': the application sees the value of the host; the stored value is ignored.";
    case EnvironmentMergeMode::Prepend:
        return L"Merge mode 'Prepend': the stored value is put in front of the value of the host, joined with the "
               L"merge "
               L"string.\nExample: a stored value of 'bar' with ';' in front of a host value of 'foo' reports "
               L"'bar;foo'.";
    case EnvironmentMergeMode::Append:
        return L"Merge mode 'Append': the stored value is put behind the value of the host, joined with the merge "
               L"string.\nExample: a stored value of 'bar' with ';' behind a host value of 'foo' reports 'foo;bar'.";
    }
    return L"";
}

void ApplyPathVariableDefaults(EnvironmentEntry& entry)
{
    if (!environment_isolation::IsPathVariableName(entry.name))
    {
        return;
    }

    entry.merge = environment_isolation::kPathVariableMergeMode;
    entry.merge_string = environment_isolation::kPathVariableMergeString;
}

void EnvironmentModel::Reset()
{
    entries_.clear();
}

bool EnvironmentModel::IsEmpty() const
{
    return entries_.empty();
}

const std::vector<EnvironmentEntry>& EnvironmentModel::Entries() const
{
    return entries_;
}

bool EnvironmentModel::AddEntry(const EnvironmentEntry& entry, std::string& error)
{
    if (!ValidateName(entry.name, error))
    {
        return false;
    }
    if (IndexOfName(entry.name) >= 0)
    {
        error = "the name " + Quote(entry.name) + " is listed twice";
        return false;
    }

    entries_.push_back(entry);
    return true;
}

bool EnvironmentModel::SetEntry(std::size_t index, const EnvironmentEntry& entry, std::string& error)
{
    if (index >= entries_.size())
    {
        error = "the index " + std::to_string(index) + " does not name an environment variable";
        return false;
    }
    if (!ValidateName(entry.name, error))
    {
        return false;
    }

    const auto other = IndexOfName(entry.name);
    if (other >= 0 && static_cast<std::size_t>(other) != index)
    {
        error = "the name " + Quote(entry.name) + " is listed twice";
        return false;
    }

    entries_[index] = entry;
    return true;
}

bool EnvironmentModel::RemoveEntry(std::size_t index)
{
    if (index >= entries_.size())
    {
        return false;
    }

    entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

std::ptrdiff_t EnvironmentModel::IndexOfName(const std::wstring& name) const
{
    for (std::size_t index = 0; index < entries_.size(); ++index)
    {
        if (NamesEqual(entries_[index].name, name))
        {
            return static_cast<std::ptrdiff_t>(index);
        }
    }
    return -1;
}

} // namespace appbox
