#include "hook/NtClose.hpp"
#include "hook/NtCreateFile.hpp"
#include "hook/NtOpenFile.hpp"
#include "hook/NtReadFile.hpp"
#include "hook/NtWriteFile.hpp"
#include "hook/RtlInitUnicodeString.hpp"
#include "CopyFileNt.hpp"

// 用 NT 接口创建（或打开）单层目录
static NTSTATUS NtCreateOneDirectory(const std::wstring& ntDir)
{
    UNICODE_STRING name;
    sys_RtlInitUnicodeString(&name, ntDir.c_str());

    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &name, OBJ_CASE_INSENSITIVE, nullptr, nullptr);

    HANDLE          hDir = nullptr;
    IO_STATUS_BLOCK iosb{};

    NTSTATUS st = sys_NtCreateFile(&hDir, FILE_LIST_DIRECTORY | SYNCHRONIZE, &oa, &iosb,
                                   /*AllocationSize*/ nullptr, FILE_ATTRIBUTE_NORMAL,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                   FILE_OPEN_IF, // 不存在则建，存在则打开
                                   FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT,
                                   /*EaBuffer*/ nullptr,
                                   /*EaLength*/ 0);

    if (NT_SUCCESS(st) && hDir)
    {
        sys_NtClose(hDir);
    }
    return st;
}

// 递归确保目录存在
static bool EnsureNtDirectory(const std::wstring& dir)
{
    if (dir.empty())
        return false;

    NTSTATUS st = NtCreateOneDirectory(dir);
    if (NT_SUCCESS(st))
        return true;

    // 父路径不存在 → 先递归创建父目录，再重试
    if (st == STATUS_OBJECT_PATH_NOT_FOUND)
    {
        size_t pos = dir.find_last_of(L'\\');
        if (pos == std::wstring::npos || pos == 0)
            return false;

        std::wstring parent = dir.substr(0, pos);
        if (parent.empty())
            return false;
        if (!EnsureNtDirectory(parent))
            return false;

        st = NtCreateOneDirectory(dir);
        return NT_SUCCESS(st);
    }
    return false;
}

// 给定文件路径，确保其父目录存在
static bool EnsureParentDirectoryOf(const std::wstring& filePath)
{
    size_t pos = filePath.find_last_of(L'\\');
    if (pos == std::wstring::npos)
        return true; // 没有父目录
    std::wstring parent = filePath.substr(0, pos);
    if (parent.empty())
        return true;
    return EnsureNtDirectory(parent);
}

/**
 * @brief Copy file.
 * @param[in] src  Source file path.
 * @param[in] dst  Destination file path.
 * @return True if success, otherwise false.
 */
bool appbox::CopyFileNt(const std::wstring& src, const std::wstring& dst)
{
    if (src.empty() || dst.empty())
        return false;

    // ---------- 1) 打开源文件 ----------
    UNICODE_STRING srcName;
    sys_RtlInitUnicodeString(&srcName, src.c_str());

    OBJECT_ATTRIBUTES srcOa;
    InitializeObjectAttributes(&srcOa, &srcName, OBJ_CASE_INSENSITIVE, nullptr, nullptr);

    HANDLE          hSrc = nullptr;
    IO_STATUS_BLOCK iosb{};
    NTSTATUS        st = sys_NtOpenFile(&hSrc, FILE_GENERIC_READ, &srcOa, &iosb, FILE_SHARE_READ,
                                        FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT);
    if (!NT_SUCCESS(st))
    {
        return false;
    }

    // ---------- 2) 确保 dst 的父目录存在 ----------
    if (!EnsureParentDirectoryOf(dst))
    {
        sys_NtClose(hSrc);
        return false;
    }

    // ---------- 3) 创建/覆盖目标文件 ----------
    UNICODE_STRING dstName;
    sys_RtlInitUnicodeString(&dstName, dst.c_str());

    OBJECT_ATTRIBUTES dstOa;
    InitializeObjectAttributes(&dstOa, &dstName, OBJ_CASE_INSENSITIVE, nullptr, nullptr);

    HANDLE hDst = nullptr;
    st = sys_NtCreateFile(&hDst, FILE_GENERIC_WRITE, &dstOa, &iosb,
                          /*AllocationSize*/ nullptr, FILE_ATTRIBUTE_NORMAL,
                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                          FILE_OVERWRITE_IF, // 不存在则建，存在则覆盖
                          FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT,
                          /*EaBuffer*/ nullptr,
                          /*EaLength*/ 0);
    if (!NT_SUCCESS(st))
    {
        sys_NtClose(hSrc);
        return false;
    }

    // ---------- 4) 循环读写 ----------
    constexpr ULONG   kBufSize = 64 * 1024;
    std::vector<BYTE> buffer(kBufSize);
    bool              ok = true;

    for (;;)
    {
        IO_STATUS_BLOCK rIosb{};
        NTSTATUS        rs = sys_NtReadFile(hSrc, nullptr, nullptr, nullptr, &rIosb, buffer.data(), kBufSize,
                                            /*ByteOffset*/ nullptr, nullptr);

        if (rs == STATUS_END_OF_FILE)
            break;
        if (!NT_SUCCESS(rs))
        {
            ok = false;
            break;
        }

        ULONG read = static_cast<ULONG>(rIosb.Information);
        if (read == 0)
            break;

        // NtWriteFile 也可能短写，循环写直到全部写完
        ULONG written = 0;
        while (written < read)
        {
            IO_STATUS_BLOCK wIosb{};
            NTSTATUS        ws =
                sys_NtWriteFile(hDst, nullptr, nullptr, nullptr, &wIosb, buffer.data() + written, read - written,
                                /*ByteOffset*/ nullptr, nullptr);
            if (!NT_SUCCESS(ws))
            {
                ok = false;
                break;
            }
            ULONG w = static_cast<ULONG>(wIosb.Information);
            if (w == 0)
            {
                ok = false;
                break;
            }
            written += w;
        }
        if (!ok)
            break;
    }

    sys_NtClose(hDst);
    sys_NtClose(hSrc);
    return ok;
}
