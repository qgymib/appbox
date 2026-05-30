#include <cassert>
#include "DirName.hpp"
#include "WString.hpp"

std::wstring appbox::filesystem::DirName(const std::wstring& path)
{
    assert(path[0] == L'\\');

    /* Remote trailing slash */
    auto cp_path = path;
    while (cp_path.back() == L'\\')
    {
        cp_path.pop_back();
    }

    auto vec = appbox::Split(cp_path, L"\\");
    auto vec_sz = vec.size();
    if (vec_sz > 3)
    {
        vec.pop_back();
        vec_sz--;
    }

    std::wstring ret;
    for (size_t i = 1; i < vec_sz; ++i)
    {
        ret += L"\\" + vec[i];
    }

    return ret;
}
