#include "WinAPI.h" /* Must be first include file */
#include <cstring>
#include <utility>
#include "Table.hpp"

namespace
{

/**
 * @brief Find the character which separates the name of an entry from its value.
 *
 * An entry of an environment block is `name=value`, with one exception: the
 * drive relative current directory of a process is spelled `=C:=C:\...`, which
 * carries an empty name and therefore has to be split at its second separator.
 *
 * @param[in] entry One entry of the block, without its terminator.
 * @return The position of the separator, npos when the entry carries none.
 */
std::size_t SeparatorOf(const std::wstring& entry)
{
    if (entry.empty())
    {
        return std::wstring::npos;
    }

    const std::size_t begin = entry.front() == L'=' ? 1 : 0;
    return entry.find(L'=', begin);
}

} // namespace

appbox::environment::Table::Table() = default;

void appbox::environment::Table::Clear()
{
    std::lock_guard<std::mutex> guard(lock_);
    entries_.clear();
}

std::size_t appbox::environment::Table::Count() const
{
    std::lock_guard<std::mutex> guard(lock_);
    return entries_.size();
}

bool appbox::environment::Table::IsEmpty() const
{
    return Count() == 0;
}

std::size_t appbox::environment::Table::IndexOf(const std::wstring& name) const
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

bool appbox::environment::Table::Contains(const std::wstring& name) const
{
    if (name.empty())
    {
        return false;
    }

    std::lock_guard<std::mutex> guard(lock_);
    return IndexOf(name) != entries_.size();
}

bool appbox::environment::Table::Get(const std::wstring& name, std::wstring& value) const
{
    if (name.empty())
    {
        return false;
    }

    std::lock_guard<std::mutex> guard(lock_);

    const std::size_t index = IndexOf(name);
    if (index == entries_.size())
    {
        return false;
    }

    value = entries_[index].value;
    return true;
}

void appbox::environment::Table::Set(const std::wstring& name, const std::wstring& value)
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
        return;
    }

    entries_.push_back(Variable{ name, value });
}

bool appbox::environment::Table::Delete(const std::wstring& name)
{
    if (name.empty())
    {
        return false;
    }

    std::lock_guard<std::mutex> guard(lock_);

    const std::size_t index = IndexOf(name);
    if (index == entries_.size())
    {
        return false;
    }

    entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

std::vector<appbox::environment::Variable> appbox::environment::Table::Entries() const
{
    std::lock_guard<std::mutex> guard(lock_);
    return entries_;
}

void appbox::environment::Table::AssignBlock(const wchar_t* block, std::size_t size_in_bytes)
{
    std::vector<Variable> parsed;

    /* The length counts bytes, so the block is walked in characters. */
    const std::size_t available = size_in_bytes == 0 ? std::wstring::npos : size_in_bytes / sizeof(wchar_t);

    std::size_t offset = 0;
    while (block != nullptr)
    {
        if (available != std::wstring::npos && offset >= available)
        {
            break;
        }

        const wchar_t* entry_begin = block + offset;
        if (*entry_begin == L'\0')
        {
            break;
        }

        std::size_t length = 0;
        while ((available == std::wstring::npos || offset + length < available) && entry_begin[length] != L'\0')
        {
            ++length;
        }

        if (available != std::wstring::npos && offset + length >= available)
        {
            /* The block ends without a terminator, so the last entry is incomplete. */
            break;
        }

        const std::wstring entry(entry_begin, length);
        const std::size_t  separator = SeparatorOf(entry);
        if (separator != std::wstring::npos)
        {
            Variable variable;
            variable.name = entry.substr(0, separator);
            variable.value = entry.substr(separator + 1);
            parsed.push_back(std::move(variable));
        }

        offset += length + 1;
    }

    std::lock_guard<std::mutex> guard(lock_);
    entries_ = std::move(parsed);
}

wchar_t* appbox::environment::Table::CreateBlock() const
{
    std::lock_guard<std::mutex> guard(lock_);
    return BuildBlock();
}

wchar_t* appbox::environment::Table::BuildBlock() const
{
    /* One terminator for the whole block. */
    std::size_t length = 1;
    for (const auto& entry : entries_)
    {
        length += entry.name.size() + entry.value.size() + 2;
    }

    auto* block = static_cast<wchar_t*>(::HeapAlloc(::GetProcessHeap(), 0, length * sizeof(wchar_t)));
    if (block == nullptr)
    {
        return nullptr;
    }

    wchar_t* cursor = block;
    for (const auto& entry : entries_)
    {
        if (!entry.name.empty())
        {
            std::memcpy(cursor, entry.name.data(), entry.name.size() * sizeof(wchar_t));
            cursor += entry.name.size();
        }
        *cursor++ = L'=';

        if (!entry.value.empty())
        {
            std::memcpy(cursor, entry.value.data(), entry.value.size() * sizeof(wchar_t));
            cursor += entry.value.size();
        }
        *cursor++ = L'\0';
    }
    *cursor = L'\0';

    return block;
}

bool appbox::environment::NamesEqual(const std::wstring& left, const std::wstring& right)
{
    const int result = ::CompareStringOrdinal(left.data(), static_cast<int>(left.size()), right.data(),
                                              static_cast<int>(right.size()), TRUE);
    if (result == 0)
    {
        /* The ordinal comparison refuses text which is not well formed. */
        return left == right;
    }
    return result == CSTR_EQUAL;
}
