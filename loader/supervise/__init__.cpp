#include <wx/wx.h>
#include <wx/event.h>
#include <wx/filename.h>
#include <spdlog/spdlog.h>
#include <Windows.h>
#include <detours.h>
#include <zlib.h>
#include <list>
#include "utils/file.hpp"
#include "utils/macros.hpp"
#include "utils/winapi.hpp"
#include "utils/meta.hpp"
#include "utils/wstring.hpp"
#include "utils/layout.hpp"
#include "utils/zstream.hpp"
#include "utils/inject.hpp"
#include "VariableDecoder.hpp"
#include "App.hpp"
#include "Path.hpp"
#include "__init__.hpp"
#include "config.hpp"

struct ProcHandle : std::shared_ptr<void>
{
    ProcHandle() = default;

    ProcHandle(HANDLE hProcess) : std::shared_ptr<void>(hProcess, CloseHandle)
    {
    }
};

typedef std::list<ProcHandle> ProcHandleList;

struct SuperviseService : wxEvtHandler, wxThreadHelper
{
    SuperviseService();
    ~SuperviseService() override;
    wxThread::ExitCode Entry() override;

    VariableDecoder mVarDecoder; /* Environment decoder. */
    std::wstring    mSelfPath;   /* Path to self. */
    appbox::Meta    mMeta;       /* Metadata. */

    appbox::InjectConfig injectConfig;  /* Inject configuration. */
    std::wstring         tempDll32Path; /* Path to 32 bit dll. */
    std::wstring         tempDll64Path; /* Path to 64 bit dll. */
    ProcHandleList       procHandles;   /* Startup file process handle. */
};

SuperviseService::SuperviseService()
{
    mSelfPath = appbox::GetExePath();
    spdlog::info(L"Loader path: {}", mSelfPath);

    if (CreateThread(wxTHREAD_JOINABLE) != wxTHREAD_NO_ERROR)
    {
        spdlog::error("Supervise service create failed");
        wxGetApp().QueueEvent(new wxCommandEvent(APPBOX_EXIT_APPLICATION_IF_NO_GUI));
        return;
    }

    if (GetThread()->Run() != wxTHREAD_NO_ERROR)
    {
        spdlog::error("Supervise service run failed");
        wxGetApp().QueueEvent(new wxCommandEvent(APPBOX_EXIT_APPLICATION_IF_NO_GUI));
        return;
    }
}

SuperviseService::~SuperviseService()
{
    if (GetThread())
    {
        GetThread()->Delete();
        if (GetThread()->IsRunning())
        {
            GetThread()->Wait();
        }
    }
}

static uint64_t s_get_offset(HANDLE file)
{
    LARGE_INTEGER distanceToMove;
    distanceToMove.QuadPart = -20;
    if (!SetFilePointerEx(file, distanceToMove, nullptr, FILE_END))
    {
        throw std::runtime_error("SetFilePointerEx() failed");
    }

    uint8_t moc[20];
    appbox::ReadFileSized(file, moc, sizeof(moc));

    if (memcmp(moc, APPBOX_MAGIC, 8) != 0)
    {
        throw std::runtime_error("Invalid magic_2");
    }

    uint32_t crc = crc32(0, Z_NULL, 0);
    crc = crc32(crc, moc, 16);
    if (memcmp(&moc[16], &crc, 4) != 0)
    {
        throw std::runtime_error("Invalid crc");
    }

    /* Read offset */
    uint64_t offset = 0;
    memcpy(&offset, &moc[8], 8);

    return offset;
}

static void s_expand_necessary_variable(VariableDecoder& decoder, appbox::Meta& meta)
{
    std::wstring value = appbox::mbstowcs(meta.settings.sandboxLocation.c_str(), CP_UTF8);
    value = decoder.Decode(value);

    meta.settings.sandboxLocation = appbox::wcstombs(value.c_str(), CP_UTF8);
}

struct InflateProcessor
{
    appbox::LayoutSection* section;
    void (*process)(SuperviseService* service, appbox::ZInflateStream& is);
};

static void s_inflate_meta(SuperviseService* service, appbox::ZInflateStream& is)
{
    std::string metadata;
    while (1)
    {
        std::string tmp = is.inflate();
        if (tmp.empty())
        {
            break;
        }
        metadata.append(tmp);
    }
    service->mMeta = nlohmann::json::parse(metadata);

    s_expand_necessary_variable(service->mVarDecoder, service->mMeta);
}

static void s_wait_cache(std::string& cache, appbox::ZInflateStream& is, size_t size)
{
    while (cache.size() < size)
    {
        std::string tmp = is.inflate();
        if (tmp.empty())
        {
            throw std::runtime_error("Cache is not enough");
        }
        cache.append(tmp);
    }
}

static void s_inflate_filesystem(SuperviseService* service, appbox::ZInflateStream& is)
{
    wxString sandboxLocation = wxString::FromUTF8(service->mMeta.settings.sandboxLocation);
    spdlog::info(L"Sandbox location: {}", sandboxLocation.ToStdWstring());
    appbox::CreateNestedDirectory(sandboxLocation.ToStdWstring());

    std::string cache;
    while (true)
    {
        appbox::PayloadNode node;
        try
        {
            s_wait_cache(cache, is, sizeof(node));
        }
        catch (std::runtime_error&)
        {
            break;
        }
        memcpy(&node, cache.c_str(), sizeof(node));
        cache.erase(0, sizeof(node));
        s_wait_cache(cache, is, node.path_len);

        /* Get file path. */
        appbox::InjectFile injectFile;
        injectFile.attributes = node.attribute;
        injectFile.isolation = static_cast<appbox::IsolationMode>(node.isolation);
        injectFile.path = cache.substr(0, node.path_len);
        cache.erase(0, node.path_len);
        service->injectConfig.files.push_back(injectFile);

        std::string filePathSandbox =
            appbox::GetPathInSandbox(service->mMeta.settings.sandboxLocation, injectFile.path);
        std::wstring wFilePathSandbox = appbox::mbstowcs(filePathSandbox.c_str(), CP_UTF8);

        /* Get file payload. */
        s_wait_cache(cache, is, node.payload_len);
        std::string payload = cache.substr(0, node.payload_len);
        cache.erase(0, node.payload_len);

        spdlog::info("Inflate {}", injectFile.path);
        if (node.attribute & FILE_ATTRIBUTE_DIRECTORY)
        {
            appbox::CreateNestedDirectory(wFilePathSandbox);
        }
        else
        {
            appbox::WriteFileReplace(wFilePathSandbox, payload.data(), payload.size());
        }
    }
}

static void s_inflate_sandbox_dll(SuperviseService* service, appbox::ZInflateStream& is)
{
    std::string  dllDir = service->mMeta.settings.sandboxLocation + "\\%sandbox%";
    std::wstring wDllDir = appbox::mbstowcs(dllDir.c_str(), CP_UTF8);
    appbox::CreateNestedDirectory(wDllDir);

    std::string         cache;
    appbox::PayloadNode node;

    s_wait_cache(cache, is, sizeof(node));
    memcpy(&node, cache.c_str(), sizeof(node));
    cache.erase(0, sizeof(node));

    service->tempDll32Path = wDllDir + L"\\sandbox32.dll";
    appbox::FileHandle tempDll32(service->tempDll32Path.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                                 nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
    s_wait_cache(cache, is, node.payload_len);
    appbox::WriteFileSized(tempDll32.get(), cache.c_str(), node.payload_len);
    cache.erase(0, node.payload_len);

    s_wait_cache(cache, is, sizeof(node));
    memcpy(&node, cache.c_str(), sizeof(node));
    cache.erase(0, sizeof(node));

    service->tempDll64Path = wDllDir + L"\\sandbox64.dll";
    appbox::FileHandle tempDll64(service->tempDll64Path.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                                 nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
    s_wait_cache(cache, is, node.payload_len);
    appbox::WriteFileSized(tempDll64.get(), cache.c_str(), node.payload_len);
    cache.erase(0, node.payload_len);
}

static void s_process_payload(SuperviseService* service, HANDLE file)
{
    uint8_t magic[8];
    appbox::ReadFileSized(file, magic, sizeof(magic));

    if (memcmp(magic, APPBOX_MAGIC, 8) != 0)
    {
        throw std::runtime_error("Invalid magic_1");
    }

    appbox::Layout layout;
    appbox::ReadFileSized(file, &layout, sizeof(layout));

    InflateProcessor processor[] = {
        { &layout.meta,       s_inflate_meta        },
        { &layout.filesystem, s_inflate_filesystem  },
        { &layout.sandbox,    s_inflate_sandbox_dll },
    };

    for (size_t i = 0; i < std::size(processor); i++)
    {
        InflateProcessor* p = &processor[i];
        appbox::SetFilePosition(file, p->section->offset);
        appbox::ZInflateStream inflateStream(file, p->section->length);
        p->process(service, inflateStream);
    }
}

static void s_start_file(SuperviseService* service, const appbox::Meta& meta)
{
#if defined(_WIN64)
    const std::string& tempDllPath = appbox::wcstombs(service->tempDll64Path.c_str(), CP_UTF8);
#else
    const std::string& tempDllPath = appbox::wcstombs(service->tempDll32Path.c_str(), CP_UTF8);
#endif
    const nlohmann::json injectJson = service->injectConfig;
    const std::string    injectData = injectJson.dump();
    const GUID           guid = APPBOX_SANDBOX_GUID;

    for (auto it = meta.settings.startupFiles.begin(); it != meta.settings.startupFiles.end(); ++it)
    {
        const std::string& path = it->path;
        const std::string  filePath = appbox::GetPathInSandbox(meta.settings.sandboxLocation, path);
        const std::wstring wFilePath = appbox::mbstowcs(filePath.c_str(), CP_UTF8);
        spdlog::info("CreateProcess: {}", filePath);

        STARTUPINFOW startupInfo;
        memset(&startupInfo, 0, sizeof(startupInfo));
        startupInfo.cb = sizeof(startupInfo);

        PROCESS_INFORMATION processInfo;
        if (!DetourCreateProcessWithDllExW(wFilePath.c_str(), nullptr, nullptr, nullptr, FALSE,
                                           CREATE_SUSPENDED, nullptr, nullptr, &startupInfo,
                                           &processInfo, tempDllPath.c_str(), nullptr))
        {
            spdlog::info("CreateProcess() failed: {}", filePath);
            continue;
        }

        if (!DetourCopyPayloadToProcess(processInfo.hProcess, guid, injectData.c_str(),
                                        injectData.size()))
        {
            spdlog::info("DetourCopyPayloadToProcess() failed");
            CloseHandle(processInfo.hProcess);
            CloseHandle(processInfo.hThread);
            continue;
        }

        ResumeThread(processInfo.hThread);
        CloseHandle(processInfo.hThread);
        service->procHandles.push_back(processInfo.hProcess);
    }
}

static void s_generate_inject_data(SuperviseService* service)
{
    service->injectConfig.sandboxPath = service->mMeta.settings.sandboxLocation;
    service->injectConfig.dllPath32 = appbox::wcstombs(service->tempDll32Path.c_str(), CP_UTF8);
    service->injectConfig.dllPath64 = appbox::wcstombs(service->tempDll64Path.c_str(), CP_UTF8);
}

wxThread::ExitCode SuperviseService::Entry()
{
    try
    {
        appbox::FileHandle hFile(mSelfPath.c_str());
        uint64_t           offset = s_get_offset(hFile.get());
        spdlog::info("offset: {}", offset);
        appbox::SetFilePosition(hFile.get(), offset);
        s_process_payload(this, hFile.get());
        hFile.reset(); // close file.

        s_generate_inject_data(this);
        s_start_file(this, mMeta);
    }
    catch (const std::runtime_error& e)
    {
        spdlog::error("{}", e.what());
        goto FINISH;
    }

    while (!GetThread()->TestDestroy() && procHandles.size() > 0)
    {
        wxMilliSleep(1000);

        ProcHandleList::iterator it;
        for (it = procHandles.begin(); it != procHandles.end();)
        {
            HANDLE hProcess = (*it).get();
            DWORD  waitRet = WaitForSingleObject(hProcess, 0);
            switch (waitRet)
            {
            case WAIT_TIMEOUT:
                ++it;
                break;

            case WAIT_OBJECT_0:
                procHandles.erase(it++);
                break;

            default:
                spdlog::error("WaitForSingleObject({})={}", hProcess, waitRet);
                procHandles.erase(it++);
                break;
            }
        }
    }

FINISH:
    wxGetApp().QueueEvent(new wxCommandEvent(APPBOX_EXIT_APPLICATION_IF_NO_GUI));
    return (wxThread::ExitCode)0;
}

static SuperviseService* G = nullptr;

void appbox::supervise::Init()
{
    G = new SuperviseService;
}

void appbox::supervise::Exit()
{
    if (G->mMeta.settings.sandboxReset)
    {
        wxString   sandboxLocation = wxString::FromUTF8(G->mMeta.settings.sandboxLocation);
        wxFileName dir;
        dir.AssignDir(sandboxLocation);
        if (!dir.Rmdir(wxPATH_RMDIR_RECURSIVE))
        {
            spdlog::error("Failed to reset sandbox {}", G->mMeta.settings.sandboxLocation);
        }
    }

    delete G;
    G = nullptr;
}

VariableDecoder& appbox::supervise::GetVariableDecoder()
{
    return G->mVarDecoder;
}
