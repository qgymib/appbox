#include "WinAPI.h" /* Must be first include file */
#include "utils/TestText.hpp"
#include <string>
#include "WString.hpp"

std::string appbox::test::ToUTF8OrEmpty(const std::wstring& text)
{
    return appbox::WideToUTF8(text).value_or(std::string());
}

std::string appbox::test::WideToAcp(const std::wstring& text)
{
    if (text.empty())
    {
        return std::string();
    }

    const int size = static_cast<int>(text.size());
    const int ansi_size = ::WideCharToMultiByte(CP_ACP, 0, text.data(), size, nullptr, 0, nullptr, nullptr);
    if (ansi_size <= 0)
    {
        return std::string();
    }

    std::string ansi(static_cast<std::size_t>(ansi_size), '\0');
    if (::WideCharToMultiByte(CP_ACP, 0, text.data(), size, ansi.data(), ansi_size, nullptr, nullptr) != ansi_size)
    {
        return std::string();
    }

    return ansi;
}

std::string appbox::test::AcpToUTF8(const std::string& text)
{
    if (text.empty())
    {
        return std::string();
    }

    const int size = static_cast<int>(text.size());
    const int wide_size = ::MultiByteToWideChar(CP_ACP, 0, text.data(), size, nullptr, 0);
    if (wide_size <= 0)
    {
        return std::string();
    }

    std::wstring wide(static_cast<std::size_t>(wide_size), L'\0');
    if (::MultiByteToWideChar(CP_ACP, 0, text.data(), size, wide.data(), wide_size) != wide_size)
    {
        return std::string();
    }

    return appbox::WideToUTF8(wide).value_or(std::string());
}
