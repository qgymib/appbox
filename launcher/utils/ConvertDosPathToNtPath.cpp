#include "utils/WinCall.hpp"
#include "ConvertDosPathToNtPath.hpp"
#include <CLI/Encoding.hpp>
#include <mutex>

DWORD appbox::ConvertDosPathToNtPath(const std::wstring& dos_path, std::wstring& nt_path)
{
    if (sys_RtlDosPathNameToNtPathName_U_WithStatus == nullptr || sys_RtlFreeUnicodeString == nullptr)
    {
        return ERROR_PROC_NOT_FOUND;
    }

    UNICODE_STRING ntName;
    ZeroMemory(&ntName, sizeof(ntName));
    auto st = sys_RtlDosPathNameToNtPathName_U_WithStatus(dos_path.c_str(), &ntName, nullptr, nullptr);
    if (!NT_SUCCESS(st))
    {
        return sys_RtlNtStatusToDosError(st);
    }

    nt_path.assign(ntName.Buffer, ntName.Length / sizeof(WCHAR));
    sys_RtlFreeUnicodeString(&ntName);

    return 0;
}

DWORD appbox::ConvertDosPathToNtPath(const std::string& dos_path, std::string& nt_path)
{
    auto         dos_path_w = CLI::widen(dos_path);
    std::wstring nt_path_w;

    auto ret = ConvertDosPathToNtPath(dos_path_w, nt_path_w);
    if (ret != 0)
    {
        return ret;
    }

    nt_path = CLI::narrow(nt_path_w);
    return 0;
}
