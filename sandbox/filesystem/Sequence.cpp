#include "filesystem/Sequence.hpp"

std::vector<std::wstring> appbox::filesystem::Sequence(const std::wstring& path, size_t offset,
                                                       const std::wstring& delimiter, bool bIncludeLast)
{
    std::vector<std::wstring> result;

    std::wstring::size_type pos;
    while ((pos = path.find(delimiter, offset)) != std::wstring::npos)
    {
        result.push_back(path.substr(0, pos));
        offset = pos + 1;
    }
    if (bIncludeLast)
    {
        result.push_back(path);
    }

    return result;
}
