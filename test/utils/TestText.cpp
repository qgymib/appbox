#include "utils/TestText.hpp"
#include <string>
#include "WString.hpp"

std::string appbox::test::ToUTF8OrEmpty(const std::wstring& text)
{
    return appbox::WideToUTF8(text).value_or(std::string());
}
