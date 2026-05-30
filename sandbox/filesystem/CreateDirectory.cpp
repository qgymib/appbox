#include "filesystem/Sequence.hpp"
#include "hook/NtClose.hpp"
#include "hook/NtCreateFile.hpp"
#include "hook/RtlInitUnicodeString.hpp"
#include "CreateDirectory.hpp"

// Create one directory at `nt_path`. Succeeds if it already exists as a directory;
// fails (returns STATUS_OBJECT_NAME_COLLISION-ish status) if a file already occupies it.
static NTSTATUS CreateOneDirectory(const std::wstring& nt_path)
{
    UNICODE_STRING us;
    sys_RtlInitUnicodeString(&us, const_cast<PWSTR>(nt_path.c_str()));

    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &us, OBJ_CASE_INSENSITIVE, nullptr, nullptr);

    HANDLE          h = nullptr;
    IO_STATUS_BLOCK iosb = {};
    NTSTATUS status = sys_NtCreateFile(&h, FILE_LIST_DIRECTORY | SYNCHRONIZE, &oa, &iosb, nullptr,
                                       FILE_ATTRIBUTE_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                       FILE_OPEN_IF, // open or create
                                       FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT, nullptr, 0);

    if (NT_SUCCESS(status) && h)
    {
        sys_NtClose(h);
    }
    return status;
}

bool appbox::filesystem::CreateDirectories(const std::wstring& path, size_t offset)
{
    auto seq = appbox::filesystem::Sequence(path, offset, L"\\", true);
    for (auto& dir : seq)
    {
        auto nt = CreateOneDirectory(dir);
        if (!NT_SUCCESS(nt))
        {
            return false;
        }
    }
    return true;
}
