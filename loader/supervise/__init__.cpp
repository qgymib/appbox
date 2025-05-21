#include <wx/wx.h>
#include <wx/event.h>
#include <spdlog/spdlog.h>
#include <Windows.h>
#include <zlib.h>
#include "utils/file.hpp"
#include "utils/macros.hpp"
#include "utils/winapi.hpp"
#include "PayloadDecompressor.hpp"
#include "VariableDecoder.hpp"
#include "App.hpp"
#include "__init__.hpp"

struct SuperviseService : wxEvtHandler, wxThreadHelper
{
    SuperviseService();
    ~SuperviseService() override;
    wxThread::ExitCode Entry() override;

    VariableDecoder mVarDecoder; /* Environment decoder. */
    std::wstring    mSelfPath;   /* Path to self. */
    nlohmann::json  mMeta;       /* Metadata. */
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

static nlohmann::json s_get_meta(HANDLE file)
{
    uint64_t metadata_sz = 0;
    appbox::ReadFileSized(file, &metadata_sz, sizeof(metadata_sz));
    spdlog::info("metadata_sz: {}", metadata_sz);

    std::string metadata;
    auto        inflateStream = std::make_shared<appbox::ZInflateStream>(file, metadata_sz);
    while (1)
    {
        std::string tmp = inflateStream->inflate();
        if (tmp.empty())
        {
            break;
        }
        metadata.append(tmp);
    }

    spdlog::info("metadata: {}", metadata);
    return nlohmann::json::parse(metadata);
}

static void s_inflate_payload(SuperviseService* service, HANDLE file)
{
    (void)service;
    uint64_t payload_sz = 0;
    appbox::ReadFileSized(file, &payload_sz, sizeof(payload_sz));
    spdlog::info("payload_sz: {}", payload_sz);

    PayloadDecompressor pd(file, payload_sz);
    pd.Process();
    spdlog::info("decompress finished");
}

static void s_process_payload(SuperviseService* service, HANDLE file)
{
    uint8_t magic[8];
    appbox::ReadFileSized(file, magic, sizeof(magic));

    if (memcmp(magic, APPBOX_MAGIC, 8) != 0)
    {
        throw std::runtime_error("Invalid magic_1");
    }

    service->mMeta = s_get_meta(file);
    s_inflate_payload(service, file);
}

wxThread::ExitCode SuperviseService::Entry()
{
    std::shared_ptr<void> hFile(CreateFileW(mSelfPath.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                            nullptr, OPEN_EXISTING, 0, nullptr),
                                CloseHandle);
    if (hFile.get() == INVALID_HANDLE_VALUE)
    {
        spdlog::error("Open self failed.");
        goto EXIT_APPLICATION;
    }

    try
    {
        uint64_t offset = s_get_offset(hFile.get());
        spdlog::info("offset: {}", offset);
        appbox::SetFilePosition(hFile.get(), offset);
        s_process_payload(this, hFile.get());
    }
    catch (const std::runtime_error& e)
    {
        spdlog::error("{}", e.what());
        goto EXIT_APPLICATION;
    }

    while (!GetThread()->TestDestroy())
    {
        wxMilliSleep(100);
    }
    goto FINISH;

EXIT_APPLICATION:
    wxGetApp().QueueEvent(new wxCommandEvent(APPBOX_EXIT_APPLICATION_IF_NO_GUI));
FINISH:
    return (wxThread::ExitCode)0;
}

static SuperviseService* G = nullptr;

void appbox::supervise::Init()
{
    G = new SuperviseService;
}

void appbox::supervise::Exit()
{
    delete G;
    G = nullptr;
}
