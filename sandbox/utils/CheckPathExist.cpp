#include "hook/NtQueryAttributesFile.hpp"
#include "hook/RtlInitUnicodeString.hpp"
#include "CheckPathExist.hpp"

NTSTATUS appbox::CheckPathExist(const std::wstring& path, ULONG Attributes, PFILE_BASIC_INFORMATION info)
{
    FILE_BASIC_INFORMATION tmpInfo;
    if (info == nullptr)
    {
        info = &tmpInfo;
    }

    UNICODE_STRING usPath;
    sys_RtlInitUnicodeString(&usPath, path.c_str());

    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &usPath, Attributes, nullptr, nullptr);

    return sys_NtQueryAttributesFile(&oa, info);
}
