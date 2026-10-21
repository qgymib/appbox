#include "GetPEB.hpp"

#ifdef _M_ARM64
#define NtCurrentPeb() (*((ULONG_PTR*)(__getReg(18) + 0x60)))
#elif _WIN64
#define NtCurrentPeb() ((ULONG_PTR)__readgsqword(0x60))
#else // _M_X86
#define NtCurrentPeb() ((ULONG_PTR)__readfsdword(0x30))
#endif

// Pointer to 64-bit PEB_LDR_DATA is at offset 0x0018 of 64-bit PEB
// Pointer to 32-bit PEB_LDR_DATA is at offset 0x000C of 32-bit PEB
#define GET_ADDR_OF_PEB NtCurrentPeb()

// Offset of the environment pointer inside the process parameters. The
// structure is not part of the headers of the SDK, so the offset is spelled out;
// it is the one `RtlSetEnvironmentVar` of `ntdll` reads and writes (0x80 in a 64
// bit process, 0x48 in a 32 bit one).
#ifdef _WIN64
#define PROCESS_PARAMETERS_ENVIRONMENT_OFFSET 0x80
#else
#define PROCESS_PARAMETERS_ENVIRONMENT_OFFSET 0x48
#endif

// Offset of the current directory inside the process parameters. The member is
// the `CURDIR` record of the structure, whose `DosPath` string comes first, so
// the offset is the one of the record (0x38 in a 64 bit process, 0x24 in a 32
// bit one).
#ifdef _WIN64
#define PROCESS_PARAMETERS_CURRENT_DIRECTORY_OFFSET 0x38
#else
#define PROCESS_PARAMETERS_CURRENT_DIRECTORY_OFFSET 0x24
#endif

#ifdef _WIN64

#define GET_PEB_LDR_DATA (*(PEB_LDR_DATA**)(GET_ADDR_OF_PEB + 0x18))
#define GET_PEB_IMAGE_BASE (*(ULONG_PTR*)(GET_ADDR_OF_PEB + 0x10))
#define GET_PEB_MAJOR_VERSION (*(USHORT*)(GET_ADDR_OF_PEB + 0x118))
#define GET_PEB_MINOR_VERSION (*(USHORT*)(GET_ADDR_OF_PEB + 0x11c))
#define GET_PEB_IMAGE_BUILD (*(USHORT*)(GET_ADDR_OF_PEB + 0x120))

#else

#define GET_PEB_LDR_DATA (*(PEB_LDR_DATA**)(GET_ADDR_OF_PEB + 0x0C))
#define GET_PEB_IMAGE_BASE (*(ULONG_PTR*)(GET_ADDR_OF_PEB + 0x08))
#define GET_PEB_MAJOR_VERSION (*(USHORT*)(GET_ADDR_OF_PEB + 0xa4))
#define GET_PEB_MINOR_VERSION (*(USHORT*)(GET_ADDR_OF_PEB + 0xa8))
#define GET_PEB_IMAGE_BUILD (*(USHORT*)(GET_ADDR_OF_PEB + 0xac))

#endif

appbox::PEB appbox::GetPEB()
{
    PEB p;
    ZeroMemory(&p, sizeof(PEB));

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
#endif
    p.ImageBuild = GET_PEB_IMAGE_BUILD;
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

    return p;
}

PWSTR* appbox::GetProcessEnvironmentSlot()
{
    const auto peb = reinterpret_cast<PPEB>(GET_ADDR_OF_PEB);
    if (peb == nullptr || peb->ProcessParameters == nullptr)
    {
        return nullptr;
    }

    auto* parameters = reinterpret_cast<BYTE*>(peb->ProcessParameters);
    return reinterpret_cast<PWSTR*>(parameters + PROCESS_PARAMETERS_ENVIRONMENT_OFFSET);
}

std::wstring appbox::GetImagePathFromPeb()
{
    const auto peb = reinterpret_cast<PPEB>(GET_ADDR_OF_PEB);
    if (peb == nullptr || peb->ProcessParameters == nullptr)
    {
        return {};
    }

    const UNICODE_STRING& name = peb->ProcessParameters->ImagePathName;
    if (name.Buffer == nullptr || name.Length == 0)
    {
        return {};
    }

    /* The buffer is counted, so it is read by its length and never as a text. */
    return std::wstring(name.Buffer, static_cast<std::size_t>(name.Length) / sizeof(wchar_t));
}

std::wstring appbox::GetCurrentDirectoryFromPeb()
{
    const auto peb = reinterpret_cast<PPEB>(GET_ADDR_OF_PEB);
    if (peb == nullptr || peb->ProcessParameters == nullptr)
    {
        return {};
    }

    const auto* parameters = reinterpret_cast<const BYTE*>(peb->ProcessParameters);
    const auto* directory =
        reinterpret_cast<const UNICODE_STRING*>(parameters + PROCESS_PARAMETERS_CURRENT_DIRECTORY_OFFSET);
    if (directory->Buffer == nullptr || directory->Length == 0)
    {
        return {};
    }

    /* The buffer is counted, so it is read by its length and never as a text. */
    return std::wstring(directory->Buffer, static_cast<std::size_t>(directory->Length) / sizeof(wchar_t));
}
