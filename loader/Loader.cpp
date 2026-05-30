#include <wx/wx.h>
#include <spdlog/spdlog.h>
#include <cmrc/cmrc.hpp>
#include <sstream>
#include <chrono>
#include <Shlobj.h>
#include <algorithm>
#include "sandbox/utils/WinAPI.h"
#include "Random.hpp"
#include "rpc/__init__.hpp"
#include "utils/GetExecutableDir.hpp"
#include "WString.hpp"
#include "Loader.hpp"

CMRC_DECLARE(sandbox_resource);
wxDEFINE_EVENT(APPBOX_EXIT_APPLICATION_IF_NO_GUI, wxCommandEvent);

struct FolderMapping
{
    const wchar_t* name;
    const GUID     guid;
};

static const FolderMapping s_known_folders[] = {
    /* Known FolderID */
    { L"%Fonts%",                  FOLDERID_Fonts                  }, /* %windir%\Fonts */
    { L"%LocalAppData%",           FOLDERID_LocalAppData           }, /* %LOCALAPPDATA% (%USERPROFILE%\AppData\Local) */
    { L"%LocalAppDataLow%",        FOLDERID_LocalAppDataLow        }, /* %USERPROFILE%\AppData\LocalLow */
    { L"%Profile%",                FOLDERID_Profile                }, /* %USERPROFILE% (%SystemDrive%\Users\%USERNAME%) */
    { L"%ProgramData%",            FOLDERID_ProgramData            }, /* %ALLUSERSPROFILE% (%ProgramData%, %SystemDrive%\ProgramData) */
    { L"%ProgramFiles%",           FOLDERID_ProgramFiles           }, /* %ProgramFiles% (%SystemDrive%\Program Files) */
    { L"%ProgramFilesX64%",        FOLDERID_ProgramFilesX64        }, /* %ProgramFiles% (%SystemDrive%\Program Files) */
    { L"%ProgramFilesX86%",        FOLDERID_ProgramFilesX86        }, /* %ProgramFiles% (%SystemDrive%\Program Files) */
    { L"%ProgramFilesCommon%",     FOLDERID_ProgramFilesCommon     }, /* %ProgramFiles%\Common Files */
    { L"%ProgramFilesCommonX64%",  FOLDERID_ProgramFilesCommonX64  }, /* %ProgramFiles%\Common Files */
    { L"%ProgramFilesCommonX86%",  FOLDERID_ProgramFilesCommonX86  }, /* %ProgramFiles%\Common Files */
    { L"%RoamingAppData%",         FOLDERID_RoamingAppData         }, /* %APPDATA% (%USERPROFILE%\AppData\Roaming) */
    { L"%UserProfiles%",           FOLDERID_UserProfiles           }, /* %SystemDrive%\Users */
    { L"%UserProgramFiles%",       FOLDERID_UserProgramFiles       }, /* %LOCALAPPDATA%\Programs */
    { L"%UserProgramFilesCommon%", FOLDERID_UserProgramFilesCommon }, /* %LOCALAPPDATA%\Programs\Common */
    { L"%Windows%",                FOLDERID_Windows                }, /* %windir% */
    /* For compatibility */
    { L"%ALLUSERSPROFILE%",        FOLDERID_ProgramData            }, /* %ALLUSERSPROFILE% (%ProgramData%, %SystemDrive%\ProgramData) */
    { L"%APPDATA%",                FOLDERID_RoamingAppData         }, /* %APPDATA% (%USERPROFILE%\AppData\Roaming) */
    { L"%USERPROFILE%",            FOLDERID_Profile                }, /* %USERPROFILE% (%SystemDrive%\Users\%USERNAME%) */
    { L"%windir%",                 FOLDERID_Windows                }, /* %windir% */
};

static void ExtractSandboxDll(const std::wstring& dll32_path, const std::wstring& dll64_path)
{
    auto fs = cmrc::sandbox_resource::get_filesystem();
    {
        auto dll = fs.open("lib/AppBoxSandbox32.dll");

        std::ofstream ofs(dll32_path, std::ios::binary | std::ios::trunc);
        ofs.write(dll.begin(), dll.size());
    }
    {
        auto          dll = fs.open("lib/AppBoxSandbox64.dll");
        std::ofstream ofs(dll64_path, std::ios::binary | std::ios::trunc);
        ofs.write(dll.begin(), dll.size());
    }
}

static void ExtractSandboxDll(const std::string& dll32_path, const std::string& dll64_path)
{
    auto dll32_path_w = appbox::UTF8ToWide(dll32_path);
    auto dll64_path_w = appbox::UTF8ToWide(dll64_path);
    ExtractSandboxDll(dll32_path_w, dll64_path_w);
}

/**
 * @brief Convert DOS path to NT path.
 * @param[in] dos_path DOS path.
 * @param[out] nt_path NT path.
 * @return true if success, otherwise false.
 */
static bool ConvertDosToNt(const std::wstring& dos_path, std::wstring& nt_path)
{
    static T_RtlDosPathNameToNtPathName_U_WithStatus sys_RtlDosPathNameToNtPathName_U_WithStatus = nullptr;
    static T_RtlFreeUnicodeString                    sys_RtlFreeUnicodeString = nullptr;
    static std::once_flag                            once;

    std::call_once(once, []() {
        auto dll = GetModuleHandleW(L"ntdll.dll");
        sys_RtlDosPathNameToNtPathName_U_WithStatus = reinterpret_cast<T_RtlDosPathNameToNtPathName_U_WithStatus>(
            GetProcAddress(dll, "RtlDosPathNameToNtPathName_U_WithStatus"));
        sys_RtlFreeUnicodeString =
            reinterpret_cast<T_RtlFreeUnicodeString>(GetProcAddress(dll, "RtlFreeUnicodeString"));
    });

    if (sys_RtlDosPathNameToNtPathName_U_WithStatus == nullptr || sys_RtlFreeUnicodeString == nullptr)
    {
        return false;
    }

    UNICODE_STRING ntName;
    ZeroMemory(&ntName, sizeof(ntName));
    auto st = sys_RtlDosPathNameToNtPathName_U_WithStatus(dos_path.c_str(), &ntName, nullptr, nullptr);
    if (!NT_SUCCESS(st) || ntName.Buffer == nullptr)
    {
        return false;
    }

    nt_path.assign(ntName.Buffer, ntName.Length / sizeof(WCHAR));
    sys_RtlFreeUnicodeString(&ntName);

    return true;
}

static bool ConvertDosToNt(const std::wstring& dos_path, std::string& nt_path)
{
    std::wstring nt_path_w;
    if (!ConvertDosToNt(dos_path, nt_path_w))
    {
        return false;
    }
    nt_path = appbox::WideToUTF8(nt_path_w);
    return true;
}

static bool MapOverlayFS(const std::string& fs, std::string& mapped_fs)
{
    auto dos_path_w = appbox::UTF8ToWide(fs);
    /* Remove trailing slash */
    while (dos_path_w.back() == L'\\')
    {
        dos_path_w.pop_back();
    }
    dos_path_w += L"\\filesystem";

    std::wstring nt_path_w;
    if (!ConvertDosToNt(dos_path_w, nt_path_w))
    {
        return false;
    }

    mapped_fs = appbox::WideToUTF8(nt_path_w);

    /* Create directory */
    return std::filesystem::create_directories(dos_path_w);
}

/**
 * @brief Check if directory exists.
 * @param[in] path Directory path.
 * @return true if directory exists, otherwise false.
 */
static bool IsDirExist(const std::wstring& path)
{
    if (path.empty())
    {
        return false;
    }

    const DWORD attributes = ::GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES)
    {
        /* Path not exist or cannot be accessed */
        return false;
    }

    return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

static bool GenerateLowerFS(appbox::SandboxLowerFS& fs, const std::wstring& host_path, const GUID& guid)
{
    wchar_t* path = nullptr;
    if (SHGetKnownFolderPath(guid, 0, nullptr, &path) != S_OK)
    {
        SPDLOG_ERROR("failed to get known folder path");
        return false;
    }

    auto ret = ConvertDosToNt(path, fs.mapped_nt_path);
    CoTaskMemFree(path);
    if (!ret)
    {
        return false;
    }

    return ConvertDosToNt(host_path, fs.host_nt_path);
}

static bool MapBaseFS(const std::string& fs, std::vector<appbox::SandboxLowerFS>& mapped_fs)
{
    auto dos_path_w = appbox::UTF8ToWide(fs);
    /* Remove trailing slash */
    while (dos_path_w.back() == '\\')
    {
        dos_path_w.pop_back();
    }
    dos_path_w += L"\\filesystem";

    std::vector<appbox::SandboxLowerFS> tmp_fs;
    for (const auto& name : s_known_folders)
    {
        auto host_dos_path = dos_path_w + L"\\" + name.name;
        if (IsDirExist(host_dos_path))
        {
            appbox::SandboxLowerFS ret;
            if (!GenerateLowerFS(ret, host_dos_path, name.guid))
            {
                return false;
            }
            tmp_fs.push_back(ret);
        }
    }

    /* Sort by mapped NT path length */
    std::sort(tmp_fs.begin(), tmp_fs.end(), [](const appbox::SandboxLowerFS& a, const appbox::SandboxLowerFS& b) {
        return a.mapped_nt_path.size() > b.mapped_nt_path.size();
    });

    /* Merge lower fs paths */
    mapped_fs.insert(mapped_fs.end(), tmp_fs.begin(), tmp_fs.end());

    return true;
}

AppBoxLoaderRuntime::AppBoxLoaderRuntime()
{
    std::time_t timestamp = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    auto        random_str = appbox::RandomString(16);
    auto        unique_path = fmt::format("appbox-{}-{}", timestamp, random_str);

    this->inject_data.pipe_path = fmt::format(R"(\\.\pipe\{})", unique_path);
    for (const auto& f : wxGetApp().loader_config.base_fs)
    {
        MapBaseFS(f, inject_data.fs_lower);
    }
    MapOverlayFS(wxGetApp().loader_config.overlay_fs, inject_data.fs_upper);

    {
        auto                  w_overlay_path = appbox::UTF8ToWide(wxGetApp().loader_config.overlay_fs);
        std::filesystem::path overlay_path(w_overlay_path);
        this->inject_data.sandbox32_dos_path = appbox::WideToUTF8((overlay_path / "sandbox32.dll").wstring());
        this->inject_data.sandbox64_dos_path = appbox::WideToUTF8((overlay_path / "sandbox64.dll").wstring());
    }

    ExtractSandboxDll(inject_data.sandbox32_dos_path, inject_data.sandbox64_dos_path);

    this->pipe_server = appbox::RemoteServer::Create(this->inject_data.pipe_path);
    SPDLOG_DEBUG("pipe listen on {}", this->inject_data.pipe_path);

    /* Methods must register before rpc server start. */
    appbox::RpcInit(this->pipe_server);
    this->pipe_server->Start();
}

AppBoxLoaderRuntime::~AppBoxLoaderRuntime()
{
    this->pipe_server.reset();
}
