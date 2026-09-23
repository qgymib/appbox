#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include "WString.hpp"
#include "RegistryIsolation.hpp"
#include "HiveReader.hpp"

namespace
{

/**
 * @brief Trim leading and trailing backslashes of a relative key path.
 * @param[in] path The path to clean.
 * @return The path without surrounding separators.
 */
std::wstring CleanRelativePath(const std::wstring& path)
{
    size_t begin = 0;
    size_t end   = path.size();

    while (begin < end && path[begin] == L'\\')
    {
        ++begin;
    }
    while (end > begin && path[end - 1] == L'\\')
    {
        --end;
    }

    return path.substr(begin, end - begin);
}

/**
 * @brief Format a byte as two hexadecimal digits.
 * @param[in] byte The byte to format.
 * @return The two digit hexadecimal representation.
 */
std::wstring HexByte(BYTE byte)
{
    static const wchar_t* digits = L"0123456789abcdef";

    std::wstring out;
    out += digits[byte >> 4];
    out += digits[byte & 0x0f];
    return out;
}

/**
 * @brief RAII owner of a key handle used by one enumeration call.
 *
 * The root handle of the hive mount is borrowed (never closed here) while
 * handles of sub keys are owned and closed on destruction. RegOpenKeyExW with
 * an empty sub key name may alias the passed handle, so the root is used
 * directly instead of being opened again.
 */
class ScopedKey
{
public:
    ScopedKey() : key_(nullptr), owned_(false)
    {
    }

    ~ScopedKey()
    {
        if (owned_ && key_ != nullptr)
        {
            RegCloseKey(key_);
        }
    }

    ScopedKey(const ScopedKey&) = delete;
    ScopedKey& operator=(const ScopedKey&) = delete;
    ScopedKey(ScopedKey&&) = delete;
    ScopedKey& operator=(ScopedKey&&) = delete;

    /**
     * @brief The held handle, null when nothing was opened.
     */
    HKEY get() const
    {
        return key_;
    }

    /**
     * @brief Take a handle.
     * @param[in] key The handle to hold.
     * @param[in] owned true when the handle must be closed on destruction.
     */
    void set(HKEY key, bool owned)
    {
        key_   = key;
        owned_ = owned;
    }

private:
    HKEY key_;   /* The held key handle. */
    bool owned_; /* Whether the handle is closed on destruction. */
};

/**
 * @brief Open a key below the hive root.
 *
 * @param[in] root The root handle of the hive mount.
 * @param[in] relative_path The key path relative to the hive root, empty for
 *                         the root itself.
 * @param[out] key The key handle, borrowed for the root and owned otherwise.
 * @return true on success.
 */
bool OpenKeyBelow(HKEY root, const std::wstring& relative_path, ScopedKey& key)
{
    if (root == nullptr)
    {
        return false;
    }

    const auto path = CleanRelativePath(relative_path);
    if (path.empty())
    {
        key.set(root, false);
        return true;
    }

    HKEY handle = nullptr;
    LONG ret    = RegOpenKeyExW(root, path.c_str(), 0, KEY_READ, &handle);
    if (ret != ERROR_SUCCESS)
    {
        SPDLOG_ERROR("failed to open the hive key {}: {} ({})", appbox::WideToUTF8(path), ret, GetLastError());
        return false;
    }

    key.set(handle, true);
    return true;
}

} // namespace

namespace appbox
{

HiveReader::HiveReader()
{
}

HiveReader::~HiveReader()
{
    Close();
}

bool HiveReader::Open(const std::wstring& hive_file)
{
    Close();

    file_    = hive_file;
    missing_ = false;

    /*
     * RegLoadAppKeyW creates the file when it does not exist, but the hive is
     * owned by the sandbox: an empty overlay has no hive yet and the loader
     * must not be the one who creates it.
     */
    std::error_code ec;
    if (!std::filesystem::exists(hive_file, ec))
    {
        SPDLOG_INFO("sandbox registry hive does not exist yet: {}", appbox::WideToUTF8(hive_file));
        missing_ = true;
        return false;
    }

    HKEY root = nullptr;
    /* dwFlags is documented as reserved and must be zero. */
    LONG ret = RegLoadAppKeyW(hive_file.c_str(), &root, KEY_READ, 0, 0);
    if (ret != ERROR_SUCCESS)
    {
        SPDLOG_ERROR("failed to mount the sandbox registry hive {}: {} ({})", appbox::WideToUTF8(hive_file), ret,
                     GetLastError());
        return false;
    }

    root_ = root;
    return true;
}

void HiveReader::Close()
{
    if (root_ != nullptr)
    {
        RegCloseKey(root_);
        root_ = nullptr;
    }
}

bool HiveReader::Refresh()
{
    if (file_.empty())
    {
        return false;
    }
    return Open(file_);
}

bool HiveReader::IsOpen() const
{
    return root_ != nullptr;
}

bool HiveReader::IsMissing() const
{
    return missing_;
}

bool HiveReader::EnumSubKeys(const std::wstring& relative_path, std::vector<std::wstring>& names)
{
    names.clear();

    ScopedKey key;
    if (!OpenKeyBelow(root_, relative_path, key))
    {
        return false;
    }

    DWORD      sub_keys        = 0;
    DWORD      max_subkey_len  = 0;
    const LONG info            = RegQueryInfoKeyW(key.get(), nullptr, nullptr, nullptr, &sub_keys, &max_subkey_len,
                                                  nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
    if (info != ERROR_SUCCESS)
    {
        SPDLOG_ERROR("RegQueryInfoKeyW failed: {} ({})", info, GetLastError());
        return false;
    }

    if (sub_keys == 0)
    {
        return true;
    }

    std::vector<wchar_t> name(max_subkey_len + 1);
    for (DWORD i = 0; i < sub_keys; ++i)
    {
        DWORD name_len = static_cast<DWORD>(name.size());
        name[0]        = L'\0';
        const LONG ret = RegEnumKeyExW(key.get(), i, name.data(), &name_len, nullptr, nullptr, nullptr, nullptr);
        if (ret == ERROR_NO_MORE_ITEMS)
        {
            break;
        }
        if (ret == ERROR_MORE_DATA)
        {
            /* A key was renamed or created concurrently: retry with a larger buffer. */
            name.resize(name_len + 1);
            --i;
            continue;
        }
        if (ret != ERROR_SUCCESS)
        {
            SPDLOG_ERROR("RegEnumKeyExW failed at index {}: {} ({})", i, ret, GetLastError());
            return false;
        }

        names.emplace_back(name.data(), name_len);
    }

    /*
     * The root of the hive carries the whiteout store of the sandbox next to
     * the five root keys of the view. The store is an implementation detail of
     * the isolation and not part of the view the browser shows, so it is
     * dropped here; the key itself keeps working for the sandbox.
     */
    if (CleanRelativePath(relative_path).empty())
    {
        names.erase(std::remove_if(names.begin(), names.end(),
                                   [](const std::wstring& name) {
                                       return _wcsicmp(name.c_str(), appbox::registry_whiteout::kStoreKey) == 0;
                                   }),
                    names.end());
    }

    return true;
}

bool HiveReader::EnumValues(const std::wstring& relative_path, std::vector<RegistryValue>& values)
{
    values.clear();

    ScopedKey key;
    if (!OpenKeyBelow(root_, relative_path, key))
    {
        return false;
    }

    DWORD values_count       = 0;
    DWORD max_value_name_len = 0;
    DWORD max_value_len      = 0;
    const LONG info          = RegQueryInfoKeyW(key.get(), nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                                                &values_count, &max_value_name_len, &max_value_len, nullptr, nullptr);
    if (info != ERROR_SUCCESS)
    {
        SPDLOG_ERROR("RegQueryInfoKeyW failed: {} ({})", info, GetLastError());
        return false;
    }

    if (values_count == 0)
    {
        return true;
    }

    std::vector<wchar_t> name(max_value_name_len + 1);
    /* The data buffer of a value without data may be zero sized, keep one byte. */
    std::vector<BYTE> data(max_value_len + 1);

    for (DWORD i = 0; i < values_count; ++i)
    {
        RegistryValue value;
        DWORD         type     = 0;
        for (;;)
        {
            DWORD name_len = static_cast<DWORD>(name.size());
            DWORD data_len = static_cast<DWORD>(data.size());
            name[0]        = L'\0';

            const LONG ret = RegEnumValueW(key.get(), i, name.data(), &name_len, nullptr, &type, data.data(), &data_len);
            if (ret == ERROR_SUCCESS)
            {
                value.name.assign(name.data(), name_len);
                value.type = type;
                value.data.assign(data.data(), data.data() + data_len);
                break;
            }
            if (ret == ERROR_MORE_DATA)
            {
                /* Grow the buffer which was too small and retry the index. */
                name.resize(name_len + 1);
                data.resize(data_len + 1);
                continue;
            }
            if (ret == ERROR_NO_MORE_ITEMS)
            {
                return true;
            }

            SPDLOG_ERROR("RegEnumValueW failed at index {}: {} ({})", i, ret, GetLastError());
            return false;
        }

        values.push_back(std::move(value));
    }

    return true;
}

bool HiveReader::HasSubKeys(const std::wstring& relative_path)
{
    /*
     * The root asks the enumeration, because the raw sub key count of the hive
     * root includes the whiteout store of the sandbox, which is not part of the
     * view (see EnumSubKeys).
     */
    if (CleanRelativePath(relative_path).empty())
    {
        std::vector<std::wstring> names;
        return EnumSubKeys(relative_path, names) && !names.empty();
    }

    ScopedKey key;
    if (!OpenKeyBelow(root_, relative_path, key))
    {
        return false;
    }

    DWORD sub_keys = 0;
    /* Only the sub key count is needed, every other parameter stays null. */
    const LONG ret = RegQueryInfoKeyW(key.get(), nullptr, nullptr, nullptr, &sub_keys, nullptr, nullptr, nullptr,
                                      nullptr, nullptr, nullptr, nullptr);

    if (ret != ERROR_SUCCESS)
    {
        SPDLOG_ERROR("RegQueryInfoKeyW failed: {} ({})", ret, GetLastError());
        return false;
    }

    return sub_keys > 0;
}

std::wstring FormatValueTypeName(DWORD type)
{
    switch (type)
    {
    case REG_NONE:
        return L"REG_NONE";
    case REG_SZ:
        return L"REG_SZ";
    case REG_EXPAND_SZ:
        return L"REG_EXPAND_SZ";
    case REG_BINARY:
        return L"REG_BINARY";
    case REG_DWORD:
        return L"REG_DWORD";
    case REG_DWORD_BIG_ENDIAN:
        return L"REG_DWORD_BIG_ENDIAN";
    case REG_LINK:
        return L"REG_LINK";
    case REG_MULTI_SZ:
        return L"REG_MULTI_SZ";
    case REG_RESOURCE_LIST:
        return L"REG_RESOURCE_LIST";
    case REG_FULL_RESOURCE_DESCRIPTOR:
        return L"REG_FULL_RESOURCE_DESCRIPTOR";
    case REG_RESOURCE_REQUIREMENTS_LIST:
        return L"REG_RESOURCE_REQUIREMENTS_LIST";
    case REG_QWORD:
        return L"REG_QWORD";
    default:
    {
        std::wostringstream oss;
        oss << L"REG_0x" << std::hex << type;
        return oss.str();
    }
    }
}

std::wstring ValueDataAsString(const RegistryValue& value)
{
    std::wstring text(reinterpret_cast<const wchar_t*>(value.data.data()), value.data.size() / sizeof(wchar_t));

    /* The trailing NUL of a terminated string is not part of the content. */
    if (!text.empty() && text.back() == L'\0')
    {
        text.pop_back();
    }
    return text;
}

std::wstring FormatValueData(const RegistryValue& value, size_t max_chars)
{
    std::wstring text;

    switch (value.type)
    {
    case REG_SZ:
    case REG_EXPAND_SZ:
    {
        /* The registry editor shows the raw, unexpanded content. */
        text = ValueDataAsString(value);
        if (text.empty())
        {
            text = L"(value not set)";
        }
        break;
    }
    case REG_MULTI_SZ:
    {
        /* regedit joins the entries of a multi string with a space. */
        std::wstring       rest = ValueDataAsString(value);
        std::wostringstream oss;
        bool               first = true;
        for (;;)
        {
            const size_t pos = rest.find(L'\0');
            const auto   part = rest.substr(0, pos);
            if (!part.empty())
            {
                if (!first)
                {
                    oss << L" ";
                }
                oss << part;
                first = false;
            }
            if (pos == std::wstring::npos)
            {
                break;
            }
            rest = rest.substr(pos + 1);
        }
        text = oss.str();
        break;
    }
    case REG_DWORD:
    {
        if (value.data.size() < sizeof(DWORD))
        {
            break;
        }
        DWORD number = 0;
        memcpy(&number, value.data.data(), sizeof(number));
        std::wostringstream oss;
        oss << L"0x" << std::hex << std::setw(8) << std::setfill(L'0') << number << L" (" << std::dec << number << L")";
        text = oss.str();
        break;
    }
    case REG_QWORD:
    {
        if (value.data.size() < sizeof(ULONGLONG))
        {
            break;
        }
        ULONGLONG number = 0;
        memcpy(&number, value.data.data(), sizeof(number));
        std::wostringstream oss;
        oss << L"0x" << std::hex << std::setw(16) << std::setfill(L'0') << number << L" (" << std::dec << number << L")";
        text = oss.str();
        break;
    }
    default:
    {
        /* Binary and every other type become a byte hex dump. */
        std::wostringstream oss;
        for (size_t i = 0; i < value.data.size(); ++i)
        {
            if (i > 0)
            {
                oss << L" ";
            }
            oss << HexByte(value.data[i]);
        }
        text = oss.str();
        if (text.empty())
        {
            text = L"(zero-length binary value)";
        }
        break;
    }
    }

    if (max_chars > 0 && text.size() > max_chars)
    {
        const size_t keep = max_chars > 3 ? max_chars - 3 : max_chars;
        text              = text.substr(0, keep) + L"...";
    }
    return text;
}

std::wstring FormatHexDump(const std::vector<BYTE>& data)
{
    static constexpr size_t kBytesPerLine = 16;

    std::wostringstream oss;
    for (size_t i = 0; i < data.size(); i += kBytesPerLine)
    {
        if (i > 0)
        {
            oss << L"\r\n";
        }

        oss << std::hex << std::setw(8) << std::setfill(L'0') << i << L"  ";

        const size_t line = (data.size() - i < kBytesPerLine) ? data.size() - i : kBytesPerLine;
        for (size_t j = 0; j < line; ++j)
        {
            if (j > 0)
            {
                oss << L" ";
            }
            oss << HexByte(data[i + j]);
        }
    }
    return oss.str();
}

} // namespace appbox
