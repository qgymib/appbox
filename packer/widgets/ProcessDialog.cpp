#include <wx/wx.h>
#include <stdexcept>
#include <zlib.h>
#include <spdlog/spdlog.h>
#include <spdlog/fmt/bin_to_hex.h>
#include "utils/file.hpp"
#include "utils/zstream.hpp"
#include "utils/wstring.hpp"
#include "ProcessDialog.hpp"
#include "FileDataView.hpp"

struct ProcessDialog::Data : wxThreadHelper
{
    Data(ProcessDialog* owner, const appbox::Meta& meta, const Config& config);
    ~Data() override;
    wxThread::ExitCode Entry() override;
    void               Log(const wxString& msg);
    void               OnProcessLog(const wxThreadEvent&);

    ProcessDialog* m_owner; /* Object own this data. */
    wxTextCtrl*    logTextCtrl;

    appbox::Meta m_meta;   /* Meta data. */
    Config       m_config; /* Pack configuration. */
};

wxDEFINE_EVENT(APPBOX_PROCESSDIALOG_LOG, wxThreadEvent);

ProcessDialog::Config::Config()
{
    compress = 0;
}

ProcessDialog::Data::Data(ProcessDialog* owner, const appbox::Meta& meta, const Config& config)
{
    m_owner = owner;
    m_meta = meta;
    m_config = config;

    {
        wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
        logTextCtrl = new wxTextCtrl(m_owner, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                     wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY);
        sizer->Add(logTextCtrl, 1, wxEXPAND);
        sizer->Add(m_owner->CreateButtonSizer(wxCLOSE));
        m_owner->SetSizer(sizer);
    }
    owner->SetSize(wxSize(600, 400));

    /* Bind events before create thread. */
    m_owner->Bind(APPBOX_PROCESSDIALOG_LOG, &Data::OnProcessLog, this);

    if (CreateThread(wxTHREAD_JOINABLE) != wxTHREAD_NO_ERROR)
    {
        throw std::runtime_error("Failed to create thread.");
    }
    if (GetThread()->Run() != wxTHREAD_NO_ERROR)
    {
        throw std::runtime_error("Failed to start thread.");
    }
}

ProcessDialog::Data::~Data()
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

void ProcessDialog::Data::Log(const wxString& msg)
{
    wxThreadEvent* e = new wxThreadEvent(APPBOX_PROCESSDIALOG_LOG);
    e->SetString(msg);
    m_owner->QueueEvent(e);
}

void ProcessDialog::Data::OnProcessLog(const wxThreadEvent& e)
{
    logTextCtrl->AppendText(e.GetString());
    logTextCtrl->AppendText(L"\n");
}

static void s_write_loader(HANDLE file, const wxString& path)
{
    std::string loader = appbox::ReadFileAll(path.ToStdWstring());
    appbox::WriteFileSized(file, loader.c_str(), loader.size());
}

static void s_write_meta(ProcessDialog::Data* iner, appbox::ZDeflateStream& ds,
                         const appbox::Meta& meta)
{
    nlohmann::json metaJson = meta;
    std::string    metaStr = metaJson.dump();
    iner->Log(L"Writing metadata");
    ds.deflate(metaStr.c_str(), metaStr.size());
}

static void s_write_filesystem_entry(ProcessDialog::Data* iner, appbox::ZDeflateStream& ds,
                                     const FileDataView::Filesystem& entry)
{
    appbox::FileHandle  hFile;
    appbox::PayloadNode node;
    const wxString      wSandboxPath = wxString::FromUTF8(entry.sandboxPath);
    const wxString      wSourcePath = wxString::FromUTF8(entry.sourcePath);

    node.type = appbox::PAYLOAD_TYPE_FILESYSTEM;
    node.isolation = entry.isolation;
    node.attribute = entry.dwFileAttributes;
    node.path_len = entry.sandboxPath.size();

    if (!(entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
    {
        hFile = appbox::FileHandle(wSourcePath.ToStdWstring().c_str());
        node.payload_len = appbox::GetFileSize(hFile.get());
    }

    iner->Log(wxString::Format(L"Processing %s --> %s", entry.sourcePath.c_str(),
                               entry.sandboxPath.c_str()));
    ds.deflate(&node, sizeof(node));
    ds.deflate(entry.sandboxPath.c_str(), entry.sandboxPath.size());

    if (hFile)
    {
        ds.deflate(hFile.get());
    }

    hFile.reset();

    for (auto it = entry.children.begin(); it != entry.children.end(); ++it)
    {
        s_write_filesystem_entry(iner, ds, *it);
    }
}

static void s_write_filesystem(ProcessDialog::Data* iner, appbox::ZDeflateStream& ds,
                               const ProcessDialog::Config& config)
{
    for (auto it = config.filesystem.begin(); it != config.filesystem.end(); ++it)
    {
        s_write_filesystem_entry(iner, ds, *it);
    }
}

wxThread::ExitCode ProcessDialog::Data::Entry()
{
    try
    {
        appbox::FileHandle outputFile(m_config.outputPath.ToStdWstring().c_str(), GENERIC_WRITE, 0,
                                      nullptr, CREATE_ALWAYS);

        Log(L"Writing loader");
        s_write_loader(outputFile.get(), m_config.loaderPath);

        Log(L"Writing magic");
        uint64_t payload_offset = appbox::GetFilePosition(outputFile.get());
        appbox::WriteFileSized(outputFile.get(), APPBOX_MAGIC, 8);

        {
            uint64_t length = 0;
            uint64_t off_begin = appbox::GetFilePosition(outputFile.get());
            appbox::WriteFileSized(outputFile.get(), &length, sizeof(length));
            appbox::ZDeflateStream deflateStream(outputFile.get(), m_config.compress);
            s_write_meta(this, deflateStream, m_meta);
            deflateStream.finish();
            length = appbox::GetFilePosition(outputFile.get()) - off_begin - sizeof(off_begin);
            appbox::WriteFilePositionSized(outputFile.get(), &length, sizeof(length), off_begin);
        }

        {
            uint64_t length = 0;
            uint64_t off_begin = appbox::GetFilePosition(outputFile.get());
            appbox::WriteFileSized(outputFile.get(), &length, sizeof(length));
            appbox::ZDeflateStream deflateStream(outputFile.get(), m_config.compress);
            s_write_filesystem(this, deflateStream, m_config);
            deflateStream.finish();
            length = appbox::GetFilePosition(outputFile.get()) - off_begin - sizeof(off_begin);
            appbox::WriteFilePositionSized(outputFile.get(), &length, sizeof(length), off_begin);
        }

        std::array<char, 20> buff;
        memcpy(&buff[0], APPBOX_MAGIC, 8);
        memcpy(&buff[8], &payload_offset, sizeof(payload_offset));

        uint32_t crc = crc32(0, Z_NULL, 0);
        crc = crc32(crc, (Bytef*)&buff[0], 16);
        memcpy(&buff[16], &crc, sizeof(crc));
        appbox::WriteFileSized(outputFile.get(), &buff[0], buff.size());

        Log(L"Build finish");
    }
    catch (const std::runtime_error& e)
    {
        Log(wxString::FromUTF8(e.what()).ToStdWstring());
    }

    return 0;
}

ProcessDialog::ProcessDialog(wxWindow* parent, const appbox::Meta& meta, const Config& config)
    : wxDialog(parent, wxID_ANY, _("Process"))
{
    m_data = new Data(this, meta, config);
}

ProcessDialog::~ProcessDialog()
{
    delete m_data;
}
