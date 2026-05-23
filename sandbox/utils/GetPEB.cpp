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
