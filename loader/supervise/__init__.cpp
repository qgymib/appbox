#include <wx/wx.h>
#include <wx/event.h>
#include <spdlog/spdlog.h>
#include <Windows.h>
#include <zlib.h>
#include <nlohmann/json.hpp>
#include "utils/file.hpp"
#include "utils/macros.hpp"
#include "utils/winapi.hpp"
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
        throw wxString(L"SetFilePointerEx() failed");
    }

    uint8_t moc[20];
    if (!appbox::ReadFileRequiredSize(file, moc, sizeof(moc)))
    {
        throw wxString(L"ReadFileRequiredSize() failed");
    }

    if (memcmp(moc, APPBOX_MAGIC, 8) != 0)
    {
        throw wxString(L"Invalid magic");
    }

    uint32_t crc = crc32(0, Z_NULL, 0);
    crc = crc32(crc, moc, 16);
    if (memcmp(&moc[16], &crc, 4) != 0)
    {
        throw wxString(L"Invalid crc");
    }

    /* Read offset */
    uint64_t offset;
    memcpy(&offset, &moc[8], 8);

    return offset;
}

static std::string s_read_metadata(HANDLE file)
{
    uint64_t metadata_sz = 0;
    if (!appbox::ReadFileRequiredSize(file, &metadata_sz, sizeof(metadata_sz)))
    {
        throw wxString(L"Failed to read metadata size");
    }

    std::string metadata(metadata_sz, '\0');
    if (!appbox::ReadFileRequiredSize(file, &metadata[0], metadata_sz))
    {
        throw wxString(L"Failed to read metadata");
    }

    return metadata;
}

static void s_process_payload(HANDLE file, uint64_t offset)
{
    LARGE_INTEGER distanceToMove;
    distanceToMove.QuadPart = offset;
    if (!SetFilePointerEx(file, distanceToMove, nullptr, FILE_BEGIN))
    {
        throw wxString(L"Failed to set file position");
    }

    uint8_t magic[8];
    if (!appbox::ReadFileRequiredSize(file, magic, sizeof(magic)))
    {
        throw wxString(L"Failed to read magic");
    }

    if (memcmp(magic, APPBOX_MAGIC, 8) != 0)
    {
        throw wxString(L"Invalid magic");
    }

    std::string metadata_str = s_read_metadata(file);
    spdlog::info("metadata: {}", metadata_str);

    nlohmann::json metadata = nlohmann::json::parse(metadata_str);
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
    catch (const wxString& e)
    {
        spdlog::error(L"{}", e.ToStdWstring());
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
