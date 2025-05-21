#include <wx/wx.h>
#include <wx/event.h>
#include <spdlog/spdlog.h>
#include <Windows.h>
#include <zlib.h>
#include <nlohmann/json.hpp>
#include "utils/file.hpp"
#include "utils/macros.hpp"
#include "utils/winapi.hpp"
#include "utils/meta.hpp"
#include "utils/zstream.hpp"
#include "utils/wstring.hpp"
#include "App.hpp"
#include "__init__.hpp"

class SuperviseService : public wxEvtHandler, public wxThreadHelper
{
public:
    SuperviseService();
    ~SuperviseService() override;

protected:
    wxThread::ExitCode Entry() override;

private:
    std::wstring m_self_path; /* Path to self. */
};

SuperviseService::SuperviseService()
{
    m_self_path = appbox::GetExePath();
    spdlog::info(L"Loader path: {}", m_self_path);

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

struct PayloadDecompressor
{
    PayloadDecompressor(HANDLE file, size_t size);
    bool WaitForCache(size_t size);
    void Process();
    void ProcessMeta();
    void ProcessFilesystem();

    appbox::ZInflateStream mStream;  /* Inflate stream. */
    appbox::PayloadNode    mCurrent; /* Payload type current processing. */
    std::string            mData;    /* Payload data. */
};

PayloadDecompressor::PayloadDecompressor(HANDLE file, size_t size) : mStream(file, size)
{
}

bool PayloadDecompressor::WaitForCache(size_t size)
{
    while (mData.size() < size)
    {
        std::string cache = mStream.inflate();
        if (cache.empty())
        {
            return false;
        }
        mData.append(cache);
    }
    return true;
}

void PayloadDecompressor::Process()
{
    while (1)
    {
        switch (mCurrent.type)
        {
        case appbox::PAYLOAD_TYPE_NONE:
            if (!WaitForCache(sizeof(mCurrent)))
            {
                wxASSERT(mData.empty());
                return;
            }
            memcpy(&mCurrent, &mData[0], sizeof(mCurrent));
            mData.erase(0, sizeof(appbox::PayloadNode));
            spdlog::info("node: type={} path_sz={} payload_sz={}", mCurrent.type, mCurrent.path_len,
                         mCurrent.payload_len);
            break;

        case appbox::PAYLOAD_TYPE_METADATA:
            spdlog::info("Metadata");
            ProcessMeta();
            mCurrent.type = appbox::PAYLOAD_TYPE_NONE;
            break;

        case appbox::PAYLOAD_TYPE_FILESYSTEM:
            spdlog::info("Filesystem");
            ProcessFilesystem();
            mCurrent.type = appbox::PAYLOAD_TYPE_NONE;
            break;

        default: {
            std::wstring msg =
                wxString::Format("Invalid payload type %d", mCurrent.type).ToStdWstring();
            throw std::runtime_error(appbox::wcstombs(msg.c_str()));
        }
        }
    }
}

void PayloadDecompressor::ProcessFilesystem()
{
    if (!WaitForCache(mCurrent.path_len))
    {
        throw std::runtime_error("Corrupted data");
    }

    std::string pathU8 = mData.substr(0, mCurrent.path_len);
    mData.erase(0, mCurrent.path_len);
    spdlog::info("Path: {}.", pathU8);

    if (!WaitForCache(mCurrent.payload_len))
    {
        throw std::runtime_error("Corrupted data");
    }
    std::string payload = mData.substr(0, mCurrent.payload_len);
    mData.erase(0, mCurrent.payload_len);

    // TODO: write to filesystem.
}

void PayloadDecompressor::ProcessMeta()
{
    wxASSERT(mCurrent.path_len == 0);
    if (!WaitForCache(mCurrent.payload_len))
    {
        throw std::runtime_error("Corrupted data");
    }

    std::string metaStr = mData.substr(0, mCurrent.payload_len);
    mData.erase(0, mCurrent.payload_len);

    spdlog::info("Meta: {}", metaStr);
}

static void s_process_payload(HANDLE file, uint64_t offset)
{
    appbox::SetFilePosition(file, offset);

    uint8_t magic[8];
    appbox::ReadFileSized(file, magic, sizeof(magic));

    if (memcmp(magic, APPBOX_MAGIC, 8) != 0)
    {
        throw std::runtime_error("Invalid magic_1");
    }

    uint64_t payload_sz = 0;
    appbox::ReadFileSized(file, &payload_sz, sizeof(payload_sz));

    PayloadDecompressor pd(file, payload_sz);
    pd.Process();
}

wxThread::ExitCode SuperviseService::Entry()
{
    std::shared_ptr<void> hFile(CreateFileW(m_self_path.c_str(), GENERIC_READ, FILE_SHARE_READ,
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
        s_process_payload(hFile.get(), offset);
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
