#include "RebasePath.hpp"
#include "WString.hpp"

NTSTATUS appbox::filesystem::RebasePath(const std::wstring& path, const std::wstring& fs, std::wstring& out)
{
    out = fs;

    auto comp = appbox::Split(path, L"\\");
    if (comp.size() < 2)
    {
        return STATUS_INVALID_PARAMETER;
    }

    /* Remove the first two components: empty and namespace. */
    comp.erase(comp.begin(), comp.begin() + 2);

    auto comp_sz = comp.size();
    for (size_t i = 0; i < comp_sz; i++)
    {
        out += L"\\" + comp[i];
        if (i == 0 && out.back() == L':')
        {
            out.pop_back();
        }
    }

    return 0;
}
