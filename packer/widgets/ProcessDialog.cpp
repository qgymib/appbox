#include <wx/wx.h>
#include <stdexcept>
#include <zlib.h>
#include <spdlog/spdlog.h>
#include <spdlog/fmt/bin_to_hex.h>
#include "utils/layout.hpp"
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

struct DeflateProcessor
{
    appbox::LayoutSection* section;
    void (*process_fn)(ProcessDialog::Data* iner, appbox::ZDeflateStream& ds);
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

static void s_process_meta(ProcessDialog::Data* iner, appbox::ZDeflateStream& ds)
{
    nlohmann::json metaJson = iner->m_meta;
    std::string    metaStr = metaJson.dump();
    iner->Log(L"Writing metadata");
    ds.deflate(metaStr.c_str(), metaStr.size());
}

static void s_process_filesystem(ProcessDialog::Data* iner, appbox::ZDeflateStream& ds)
{
    s_write_filesystem(iner, ds, iner->m_config);
}

static void s_process_sandbox_dll(ProcessDialog::Data* iner, appbox::ZDeflateStream& ds)
{
    appbox::PayloadNode node;

    node.type = appbox::PAYLOAD_TYPE_SANDBOX_32;
    appbox::FileHandle fDll32(L"sandbox32.dll");
    node.payload_len = appbox::GetFileSize(fDll32.get());
    iner->Log(L"Writing sandbox32.dll");
    ds.deflate(&node, sizeof(node));
    ds.deflate(fDll32.get());

    node.type = appbox::PAYLOAD_TYPE_SANDBOX_64;
    appbox::FileHandle fDll64(L"sandbox64.dll");
    node.payload_len = appbox::GetFileSize(fDll64.get());
    iner->Log(L"Writing sandbox64.dll");
    ds.deflate(&node, sizeof(node));
    ds.deflate(fDll64.get());
}

wxThread::ExitCode ProcessDialog::Data::Entry()
{
    appbox::Layout   layout;
    DeflateProcessor processor[] = {
        { &layout.meta,       s_process_meta        },
        { &layout.filesystem, s_process_filesystem  },
        { &layout.sandbox,    s_process_sandbox_dll },
    };
    memset(&layout, 0, sizeof(layout));

    try
    {
        appbox::FileHandle outputFile(m_config.outputPath.ToStdWstring().c_str(), GENERIC_WRITE, 0,
                                      nullptr, CREATE_ALWAYS);

        Log(L"Writing loader");
        s_write_loader(outputFile.get(), m_config.loaderPath);

        Log(L"Writing magic");
        uint64_t payload_offset = appbox::GetFilePosition(outputFile.get());
        appbox::WriteFileSized(outputFile.get(), APPBOX_MAGIC, 8);
        appbox::WriteFileSized(outputFile.get(), &layout, sizeof(layout));

        for (size_t i = 0; i < std::size(processor); ++i)
        {
            const DeflateProcessor* p = &processor[i];
            p->section->offset = appbox::GetFilePosition(outputFile.get());
            appbox::ZDeflateStream deflateStream(outputFile.get(), m_config.compress);
            p->process_fn(this, deflateStream);
            deflateStream.finish();
            p->section->length = appbox::GetFilePosition(outputFile.get()) - p->section->offset;
        }
        appbox::WriteFilePositionSized(outputFile.get(), &layout, sizeof(layout),
                                       payload_offset + 8);

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
