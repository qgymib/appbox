#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include "RegReadValues.hpp"
#include "utils/RegistryRootKey.hpp"
#include "WString.hpp"
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace
{

/**
 * @brief Read the data of a value as wide text.
 *
 * The registry stores a string value as UTF-16 without a guaranteed trailing
 * null character, so the length is derived from the byte count. A blob which
 * is not made of whole wide characters is reported as no text at all.
 *
 * @param[in] data Raw data of the value.
 * @return The text of the data, empty when it is not wide text.
 */
std::wstring WideText(const std::vector<BYTE>& data)
{
    if ((data.size() % sizeof(wchar_t)) != 0)
    {
        return std::wstring();
    }

    std::wstring text(data.size() / sizeof(wchar_t), L'\0');
    if (!text.empty())
    {
        memcpy(text.data(), data.data(), data.size());
    }

    return text;
}

/**
 * @brief Split wide text into the parts which null characters separate.
 *
 * The terminator of a `REG_MULTI_SZ` list and a trailing empty item are not
 * part of the result, so the items of the list are reported as they are.
 *
 * @param[in] text Text to split.
 * @return The parts which hold at least one character.
 */
std::vector<std::string> Items(const std::wstring& text)
{
    std::vector<std::string> items;

    std::size_t start = 0;
    while (start < text.size())
    {
        const std::size_t end = text.find(L'\0', start);
        if (end == std::wstring::npos)
        {
            items.push_back(appbox::WideToUTF8(text.substr(start)));
            break;
        }

        if (end != start)
        {
            items.push_back(appbox::WideToUTF8(text.substr(start, end - start)));
        }

        start = end + 1;
    }

    return items;
}

/**
 * @brief Format the data of a value as a hexadecimal text.
 * @param[in] data Raw data of the value.
 * @return The data as a lower case hexadecimal text, without separators.
 */
std::string Hex(const std::vector<BYTE>& data)
{
    static const char kDigits[] = "0123456789abcdef";

    std::string text;
    text.reserve(data.size() * 2);
    for (const BYTE byte : data)
    {
        text.push_back(kDigits[byte >> 4]);
        text.push_back(kDigits[byte & 0x0F]);
    }

    return text;
}

/**
 * @brief Read one value of an open key into an answer.
 * @param[in] key The open key.
 * @param[in] name Name of the value, empty for the default value.
 * @param[out] answer The answer to fill.
 */
void ReadValue(HKEY key, const std::wstring& name, appbox::test::ProtocolRegReadValues::Answer& answer)
{
    DWORD type = 0;
    DWORD size = 0;

    answer.query_code = RegQueryValueExW(key, name.c_str(), nullptr, &type, nullptr, &size);
    if (answer.query_code != ERROR_SUCCESS)
    {
        return;
    }

    std::vector<BYTE> data(size);
    DWORD             read = size;
    answer.query_code =
        RegQueryValueExW(key, name.c_str(), nullptr, &type, data.empty() ? nullptr : data.data(), &read);
    if (answer.query_code != ERROR_SUCCESS)
    {
        return;
    }
    data.resize(read);

    answer.type = type;
    answer.bytes = Hex(data);

    /* Only the string types of the registry read their data as text. */
    if (type != REG_SZ && type != REG_EXPAND_SZ && type != REG_MULTI_SZ)
    {
        return;
    }

    const std::wstring text = WideText(data);
    const std::size_t  end = text.find(L'\0');
    answer.text = appbox::WideToUTF8(end == std::wstring::npos ? text : text.substr(0, end));
    answer.items = Items(text);
}

} // namespace

static nlohmann::json ProbeRegReadValues_Entry(const nlohmann::json& data)
{
    const auto req = data.get<appbox::test::ProtocolRegReadValues::Req>();

    appbox::test::ProtocolRegReadValues::Rsp rsp;

    HKEY  key = nullptr;
    DWORD open_code = ERROR_INVALID_PARAMETER;

    const auto root = appbox::test::RegistryRootHandle(req.Root);
    if (root != nullptr)
    {
        open_code = RegOpenKeyExW(root, appbox::UTF8ToWide(req.Key).c_str(), 0, KEY_QUERY_VALUE, &key);
    }

    for (const auto& name : req.Values)
    {
        appbox::test::ProtocolRegReadValues::Answer answer;
        answer.open_code = open_code;

        if (open_code == ERROR_SUCCESS)
        {
            ReadValue(key, appbox::UTF8ToWide(name), answer);
        }

        rsp.values.push_back(std::move(answer));
    }

    if (key != nullptr)
    {
        RegCloseKey(key);
    }

    return rsp;
}

appbox::test::Probe appbox::test::ProbeRegReadValues("RegReadValues", ProbeRegReadValues_Entry);
