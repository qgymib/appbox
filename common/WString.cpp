#include <stdexcept>
#include "WinAPI.hpp"
#include "WString.hpp"

std::wstring appbox::UTF8ToWide(const char* s)
{
    int size_need = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    if (size_need < 0)
    {
        throw std::runtime_error("MultiByteToWideChar() failed");
    }
    wchar_t* new_str = new wchar_t[size_need];
    if (MultiByteToWideChar(CP_UTF8, 0, s, -1, new_str, size_need) != size_need)
    {
        throw std::runtime_error("MultiByteToWideChar() failed");
    }
    std::wstring dst(new_str);
    delete[] new_str;
    return dst;
}

std::string appbox::WideToUTF8(const wchar_t* s)
{
    int   size_need = WideCharToMultiByte(CP_UTF8, 0, s, -1, nullptr, 0, nullptr, nullptr);
    char* new_str = new char[size_need];
    WideCharToMultiByte(CP_UTF8, 0, s, -1, new_str, size_need, nullptr, nullptr);
    std::string dst(new_str);
    delete[] new_str;
    return dst;
}
