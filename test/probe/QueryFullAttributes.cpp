#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include <string>
#include <vector>
#include "WString.hpp"
#include "QueryFullAttributes.hpp"

/* clang-format off */
typedef NTSTATUS (*T_NtQueryFullAttributesFileProbe)(
    /* [IN] */  POBJECT_ATTRIBUTES              ObjectAttributes,
    /* [OUT] */ PFILE_NETWORK_OPEN_INFORMATION  FileInformation
);
/* clang-format on */

/**
 * @brief Initialize a UNICODE_STRING which refers to a wide string.
 * @param[out] us The string to initialize.
 * @param[in] text The text to refer to, which has to outlive \p us.
 */
static void InitString(UNICODE_STRING& us, const std::wstring& text)
{
    us.Length = static_cast<USHORT>(text.size() * sizeof(wchar_t));
    us.MaximumLength = static_cast<USHORT>(us.Length + sizeof(wchar_t));
    us.Buffer = const_cast<PWSTR>(text.c_str());
}

/**
 * @brief Convert a Win32 path to the NT path the NT entry points take.
 * @param[in] path Win32 path.
 * @return The NT path of the same object.
 */
static std::wstring AsNtPath(const std::wstring& path)
{
    std::wstring nt_path = path;
    if (nt_path.size() >= 2 && nt_path[1] == L':')
    {
        nt_path.insert(0, L"\\??\\");
    }
    return nt_path;
}

static nlohmann::json ProbeQueryFullAttributes_Entry(const nlohmann::json& data)
{
    const auto req = data.get<appbox::test::ProtocolQueryFullAttributes::Req>();

    appbox::test::ProtocolQueryFullAttributes::Rsp rsp;

    auto ntdll = GetModuleHandleW(L"ntdll.dll");
    auto fn_query =
        ntdll == nullptr
            ? nullptr
            : reinterpret_cast<T_NtQueryFullAttributesFileProbe>(GetProcAddress(ntdll, "NtQueryFullAttributesFile"));

    for (const auto& path : req.paths)
    {
        appbox::test::ProtocolQueryFullAttributes::Item item;

        if (fn_query == nullptr)
        {
            item.status = static_cast<long>(STATUS_PROCEDURE_NOT_FOUND);
            rsp.items.push_back(item);
            continue;
        }

        const std::wstring nt_path = AsNtPath(appbox::UTF8ToWide(path));

        UNICODE_STRING us_path;
        InitString(us_path, nt_path);

        OBJECT_ATTRIBUTES oa;
        InitializeObjectAttributes(&oa, &us_path, OBJ_CASE_INSENSITIVE, nullptr, nullptr);

        FILE_NETWORK_OPEN_INFORMATION info = {};
        item.status = static_cast<long>(fn_query(&oa, &info));
        if (NT_SUCCESS(item.status))
        {
            item.attributes = info.FileAttributes;
            item.size = static_cast<long long>(info.EndOfFile.QuadPart);
        }

        rsp.items.push_back(item);
    }

    return rsp;
}

appbox::test::Probe appbox::test::ProbeQueryFullAttributes("QueryFullAttributes", ProbeQueryFullAttributes_Entry);
