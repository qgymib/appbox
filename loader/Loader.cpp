#include <wx/wx.h>
#include <spdlog/spdlog.h>
#include <cmrc/cmrc.hpp>
#include <sstream>
#include <chrono>
#include "sandbox/utils/WinAPI.h"
#include "Random.hpp"
#include "rpc/__init__.hpp"
#include "utils/GetExecutableDir.hpp"
#include "WString.hpp"
#include "Loader.hpp"

CMRC_DECLARE(sandbox_resource);
wxDEFINE_EVENT(APPBOX_EXIT_APPLICATION_IF_NO_GUI, wxCommandEvent);

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

static bool ConvertDosToNt(const std::string& dos_path, std::string& nt_path)
{
    auto         dos_path_w = appbox::UTF8ToWide(dos_path);
    std::wstring nt_path_w;
    if (!ConvertDosToNt(dos_path_w, nt_path_w))
    {
        return false;
    }

    nt_path = appbox::WideToUTF8(nt_path_w);
    return true;
}

AppBoxLoaderRuntime::AppBoxLoaderRuntime()
{
    std::time_t timestamp = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    auto        random_str = appbox::RandomString(16);
    auto        unique_path = fmt::format("appbox-{}-{}", timestamp, random_str);

    this->inject_data.pipe_path = fmt::format(R"(\\.\pipe\{})", unique_path);
    ConvertDosToNt(wxGetApp().loader_config.base_fs, inject_data.base_nt_path);
    ConvertDosToNt(wxGetApp().loader_config.overlay_fs, inject_data.overlay_nt_path);

    {
        auto                  w_overlay_path = appbox::UTF8ToWide(wxGetApp().loader_config.overlay_fs);
        std::filesystem::path overlay_path(w_overlay_path);
        this->inject_data.sandbox32_dos_path = appbox::WideToUTF8((overlay_path / "sandbox32.dll").wstring());
        this->inject_data.sandbox64_dos_path = appbox::WideToUTF8((overlay_path / "sandbox64.dll").wstring());

        std::filesystem::create_directory(overlay_path);
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
