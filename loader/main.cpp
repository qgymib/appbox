#if 0
#include <Windows.h>
#include <cstdlib>
#include <vector>
#include <stdexcept>
#include <zlib.h>
#include <nlohmann/json.hpp>
#include "winapi.hpp"
#include "macros.hpp"

union UnionData4 {
    uint8_t  bin[4];
    uint32_t val;
};

union UnionData8 {
    uint8_t  bin[8];
    uint64_t val;
};

struct LoaderCtx
{
    LoaderCtx();
    ~LoaderCtx();

    int flag_dump_meta; /* Flag: Dump metadata. */

    std::wstring   exe_path;     /* Path to self executable. */
    HANDLE         hFileSelf;    /* Handle to self. */
    uint64_t       offset_start; /* Offset to magic. */
    nlohmann::json meta;         /* Metadata. */
};

/* clang-format off */
static const wchar_t* s_help = L"\n"
"AppBox loader\n"
"Usage: %s [OPTIONS] program_options\n"
"  --X-AppBox-DumpMeta\n"
"    Dump metadata and exit.\n"
"  --X-AppBox-Help\n"
"    Show this help and exit.\n"
;
/* clang-format on */


static LoaderCtx* G = nullptr;

LoaderCtx::LoaderCtx()
{
    flag_dump_meta = 0;
    hFileSelf = INVALID_HANDLE_VALUE;
    offset_start = 0;
}

LoaderCtx::~LoaderCtx()
{
    if (hFileSelf != INVALID_HANDLE_VALUE)
    {
        CloseHandle(hFileSelf);
        hFileSelf = INVALID_HANDLE_VALUE;
    }
}


static void s_at_exit(void)
{
    delete G;
    G = nullptr;

    if (GetConsoleWindow())
    {
        FreeConsole();
    }
}

static void fReOpen(const char* path, const char* mode, FILE* stream)
{
    FILE* s = stream;
    freopen_s(&s, path, mode, stream);
}

void AttachConsoleIfNeeded()
{
    // Check if the program is already attached to a console
    if (AttachConsole(ATTACH_PARENT_PROCESS))
    {
        // Successfully attached to a parent console
        fReOpen("CONOUT$", "w", stdout); // Redirect stdout
        fReOpen("CONOUT$", "w", stderr); // Redirect stderr
        fReOpen("CONIN$", "r", stdin);   // Redirect stdin
    }
    else
    {
        // No parent console, create a new one
        if (AllocConsole())
        {
            fReOpen("CONOUT$", "w", stdout); // Redirect stdout
            fReOpen("CONOUT$", "w", stderr); // Redirect stderr
            fReOpen("CONIN$", "r", stdin);   // Redirect stdin

            // Optionally set the console title
            SetConsoleTitleW(L"My GUI Program Console");
        }
    }
}

static void s_parser_cmdline(PWSTR pCmdLine)
{
    int     argc = 0;
    LPWSTR* argv = CommandLineToArgvW(pCmdLine, &argc);

    for (int i = 0; i < argc; i++)
    {
        if (wcscmp(argv[i], L"--X-AppBox-DumpMeta") == 0)
        {
            G->flag_dump_meta = 1;
            continue;
        }
        if (wcscmp(argv[i], L"--X-AppBox-Help") == 0)
        {
            AttachConsoleIfNeeded();
            wprintf(s_help, argv[0]);
            exit(EXIT_SUCCESS);
        }
    }
}

static void s_parse_magic_offset()
{
    uint8_t buff[20];

    LARGE_INTEGER liDistanceToMove;
    liDistanceToMove.QuadPart = -20;
    if (!SetFilePointerEx(G->hFileSelf, liDistanceToMove, nullptr, FILE_END))
    {
        throw std::runtime_error("SetFilePointerEx()");
    }

    DWORD read_sz = 0;
    if (!ReadFile(G->hFileSelf, buff, sizeof(buff), &read_sz, nullptr) || read_sz != sizeof(buff))
    {
        throw std::runtime_error("ReadFile()");
    }
    if (memcmp(buff, APPBOX_MAGIC, 8))
    {
        throw std::runtime_error("Invalid magic");
    }

    UnionData4 crc;
    crc.val = crc32(0, Z_NULL, 0);
    crc.val = crc32(crc.val, buff, 16);
    if (memcmp(crc.bin, &buff[16], 4) != 0)
    {
        throw std::runtime_error("Invalid CRC32");
    }

    UnionData8 offset;
    memcpy(offset.bin, &buff[8], 8);
    G->offset_start = offset.val;

    liDistanceToMove.QuadPart = G->offset_start;
    SetFilePointerEx(G->hFileSelf, liDistanceToMove, nullptr, FILE_BEGIN);
    if (!ReadFile(G->hFileSelf, buff, 8, &read_sz, nullptr) || read_sz != 8)
    {
        throw std::runtime_error("ReadFile()");
    }
    if (memcmp(buff, APPBOX_MAGIC, 8) != 0)
    {
        throw std::runtime_error("Invalid magic");
    }
    G->offset_start += 8;
}

static size_t ReadFile(HANDLE hFile, void* buff, size_t size)
{
    DWORD read_sz = 0;
    if (!ReadFile(hFile, buff, (DWORD)size, &read_sz, nullptr))
    {
        throw std::runtime_error("ReadFile()");
    }
    return read_sz;
}

static void ReadFileSize(HANDLE hFile, std::vector<uint8_t>& buff, size_t size)
{
    buff.resize(size);
    size_t read_sz = ReadFile(hFile, buff.data(), size);
    if (read_sz != size)
    {
        throw std::runtime_error("ReadFile()");
    }
}

static void s_load_meta()
{
    uint64_t meta_sz = 0;
    if (ReadFile(G->hFileSelf, &meta_sz, sizeof(meta_sz)) != sizeof(meta_sz))
    {
        throw std::runtime_error("ReadFile()");
    }

    std::vector<uint8_t> meta;
    ReadFileSize(G->hFileSelf, meta, (size_t)meta_sz);
    meta.push_back('\n');

    G->meta = nlohmann::json::parse(meta.data());

    if (G->flag_dump_meta)
    {
        AttachConsoleIfNeeded();
        std::string data = G->meta.dump(4);
        printf("%s", data.c_str());
        exit(EXIT_SUCCESS);
    }
}

static void s_load()
{
    LARGE_INTEGER liDistanceToMove;
    liDistanceToMove.QuadPart = G->offset_start;
    if (!SetFilePointerEx(G->hFileSelf, liDistanceToMove, nullptr, FILE_BEGIN))
    {
        throw std::runtime_error("SetFilePointerEx()");
    }

    s_load_meta();
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow)
{
    (void)hInstance;
    (void)hPrevInstance;
    (void)nCmdShow;

    G = new LoaderCtx;
    atexit(s_at_exit);

    s_parser_cmdline(pCmdLine);
    G->exe_path = appbox::GetExePath();
    G->hFileSelf = CreateFileW(G->exe_path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, 0, nullptr);

    s_parse_magic_offset();
    s_load();

    return 0;
}

#else

#include <wx/wx.h>
#include <wx/cmdline.h>
#include "supervise/__init__.hpp"
#include "widgets/MainFrame.hpp"

class LoaderApp : public wxApp
{
public:
    LoaderApp();
    virtual bool OnInit();
    virtual int  OnExit();
    virtual void OnInitCmdLine(wxCmdLineParser& parser);
    virtual bool OnCmdLineParsed(wxCmdLineParser& parser);

private:
    bool option_admin;
};

LoaderApp::LoaderApp()
{
    option_admin = false;
}

bool LoaderApp::OnInit()
{
    /* The command line arguments will be parsered here. */
    if (!wxApp::OnInit())
    {
        return false;
    }

    MainFrame* main_frame = new MainFrame;
    main_frame->Show(option_admin);

    appbox::supervise::Init();
    return true;
}

int LoaderApp::OnExit()
{
    appbox::supervise::Exit();
    return 0;
}

void LoaderApp::OnInitCmdLine(wxCmdLineParser& parser)
{
    /* clang-format off */
    static const wxCmdLineEntryDesc options[] = {
        {
            wxCMD_LINE_SWITCH,
            nullptr,
            "X-AppBox-Admin",
            "Enable management interface",
            wxCMD_LINE_VAL_NONE,
            wxCMD_LINE_PARAM_OPTIONAL,
        },
        wxCMD_LINE_DESC_END,
    };
    /* clang-format on */
    parser.SetDesc(options);
    parser.SetSwitchChars("-");

    wxApp::OnInitCmdLine(parser);
}

bool LoaderApp::OnCmdLineParsed(wxCmdLineParser& parser)
{
    this->option_admin = parser.Found("X-AppBox-Admin");
    return wxApp::OnCmdLineParsed(parser);
}

wxIMPLEMENT_APP(LoaderApp); // NOLINT

#endif
