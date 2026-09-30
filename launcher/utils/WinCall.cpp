#include "WinCall.hpp"

T_RtlDosPathNameToNtPathName_U_WithStatus sys_RtlDosPathNameToNtPathName_U_WithStatus = nullptr;
T_RtlFreeUnicodeString                    sys_RtlFreeUnicodeString = nullptr;
T_RtlNtStatusToDosError                   sys_RtlNtStatusToDosError = nullptr;

DWORD appbox::WinCallInit()
{
    auto dll = GetModuleHandleW(L"ntdll.dll");
    sys_RtlDosPathNameToNtPathName_U_WithStatus = reinterpret_cast<T_RtlDosPathNameToNtPathName_U_WithStatus>(
        GetProcAddress(dll, "RtlDosPathNameToNtPathName_U_WithStatus"));
    sys_RtlFreeUnicodeString = reinterpret_cast<T_RtlFreeUnicodeString>(GetProcAddress(dll, "RtlFreeUnicodeString"));
    sys_RtlNtStatusToDosError = reinterpret_cast<T_RtlNtStatusToDosError>(GetProcAddress(dll, "RtlNtStatusToDosError"));
    return 0;
}
