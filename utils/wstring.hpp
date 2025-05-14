#ifndef APPBOX_UTILS_WSTRING_HPP
#define APPBOX_UTILS_WSTRING_HPP

#include <string>

namespace appbox
{

std::wstring mbstowcs(const char* s);
std::wstring mbstowcs(const char* s, size_t n);

}

#endif
