#include "utils/WinAPI.h" /* Must be first include file */
#include <algorithm>
#include <cstring>
#include <utility>
#include "Table.hpp"

namespace
{

/**
 * @brief Append the ANSI text of a wide string.
 *
 * A character the code page cannot express is written as its default character,
 * which is what the ANSI entry points of the operating system do.
 *
 * @param[in] text The text to convert.
 * @param[in,out] out The text which receives the converted characters.
 * @return true on success.
 */
bool AppendAnsi(const std::wstring& text, std::string& out)
{
    if (text.empty())
    {
        return true;
    }

    const int size =
        ::WideCharToMultiByte(CP_ACP, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0)
    {
        return false;
    }

    const std::size_t offset = out.size();
    out.resize(offset + static_cast<std::size_t>(size));

    return ::WideCharToMultiByte(CP_ACP, 0, text.data(), static_cast<int>(text.size()), out.data() + offset, size,
                                 nullptr, nullptr) == size;
}

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

appbox::environment::Table::~Table()
{
    /*
     * A block which the application did not release is freed here: the table
     * owns every block it handed out, and the end of the process is the last
     * moment they can be released.
     */
    for (auto* block : blocks_)
    {
        ::HeapFree(::GetProcessHeap(), 0, block);
    }
    blocks_.clear();

    for (auto* block : ansi_blocks_)
    {
        ::HeapFree(::GetProcessHeap(), 0, block);
    }
    ansi_blocks_.clear();
}

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

void appbox::environment::Table::AssignBlock(const wchar_t* block)
{
    std::vector<Variable> parsed;

    if (block != nullptr)
    {
        const wchar_t* cursor = block;
        while (*cursor != L'\0')
        {
            const std::wstring entry(cursor);
            const std::size_t  separator = SeparatorOf(entry);
            if (separator != std::wstring::npos)
            {
                Variable variable;
                variable.name = entry.substr(0, separator);
                variable.value = entry.substr(separator + 1);
                parsed.push_back(std::move(variable));
            }

            cursor += entry.size() + 1;
        }
    }

    std::lock_guard<std::mutex> guard(lock_);
    entries_ = std::move(parsed);
}

wchar_t* appbox::environment::Table::CreateBlock()
{
    std::lock_guard<std::mutex> guard(lock_);

    wchar_t* block = BuildBlock();
    if (block == nullptr)
    {
        return nullptr;
    }

    blocks_.push_back(block);
    return block;
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

wchar_t* appbox::environment::Table::CreateOwnedBlock() const
{
    std::lock_guard<std::mutex> guard(lock_);
    return BuildBlock();
}

bool appbox::environment::Table::ReleaseBlock(wchar_t* block)
{
    if (block == nullptr)
    {
        return false;
    }

    std::lock_guard<std::mutex> guard(lock_);

    const auto it = std::find(blocks_.begin(), blocks_.end(), block);
    if (it == blocks_.end())
    {
        return false;
    }

    blocks_.erase(it);
    ::HeapFree(::GetProcessHeap(), 0, block);
    return true;
}

char* appbox::environment::Table::CreateAnsiBlock()
{
    std::lock_guard<std::mutex> guard(lock_);

    std::string text;
    for (const auto& entry : entries_)
    {
        if (!AppendAnsi(entry.name, text))
        {
            return nullptr;
        }
        text.push_back('=');
        if (!AppendAnsi(entry.value, text))
        {
            return nullptr;
        }
        text.push_back('\0');
    }
    text.push_back('\0');

    auto* block = static_cast<char*>(::HeapAlloc(::GetProcessHeap(), 0, text.size()));
    if (block == nullptr)
    {
        return nullptr;
    }

    std::memcpy(block, text.data(), text.size());
    ansi_blocks_.push_back(block);
    return block;
}

bool appbox::environment::Table::ReleaseAnsiBlock(char* block)
{
    if (block == nullptr)
    {
        return false;
    }

    std::lock_guard<std::mutex> guard(lock_);

    const auto it = std::find(ansi_blocks_.begin(), ansi_blocks_.end(), block);
    if (it == ansi_blocks_.end())
    {
        return false;
    }

    ansi_blocks_.erase(it);
    ::HeapFree(::GetProcessHeap(), 0, block);
    return true;
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
