#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <Windows.h>
#include <stdexcept>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <zlib.h>
#include "macros.hpp"
#include "winapi.hpp"
#include "wstring.hpp"
#include "zstream.hpp"

enum FileIsolation
{
    ISOLATION_FULL = 0,
    ISOLATION_MERGE = 1,
    ISOLATION_WRITE_COPY = 2,
    ISOLATION_HIDE = 3,
};

enum FileAttribute
{
    ATTRIBUTE_FOLDER = 0x01,   /* This record is a folder. */
    ATTRIBUTE_STARTUP = 0x02,  /* The file is automatically startup. */
    ATTRIBUTE_READONLY = 0x04, /* Read only file or folder. */
};

struct StrIntPair
{
    const char* str;
    int         val;
};

/**
 * @brief File Record
 */
extern "C" struct FileRecord
{
    uint32_t path_len;    /* Path name length. */
    uint16_t isolation;   /* Isolation mode. */
    uint16_t attribute;   /* Bit-OR attribute. */
    uint64_t payload_len; /* Payload size. */
};

struct packer_ctx
{
    packer_ctx();
    ~packer_ctx();

    std::wstring template_path;   /* Template file path. */
    std::wstring target_path;     /* Target file path. */
    std::wstring executable_path; /* Where we are. */

    std::string    template_data; /* Content of the template file. Encoding: UTF-8. */
    nlohmann::json template_json; /* Json data of template file. */

    HANDLE         target_handle;     /* Target file handle. */
    uint64_t       offset_magic;      /* Packed content start offset. */
    uint64_t       offset_filesystem; /* Offset of filesystem. */
    nlohmann::json meta_json;         /* Meta data. */

    std::unique_ptr<appbox::ZDeflateStream> deflate_stream;
};

/* clang-format off */
static const wchar_t* s_help = L""
"Pack executable and resources into on file."
"Usage: packer [OPTIONS] target\n"
"  --template=\n"
"    Template file path.\n"
"  -h, --help\n"
"    Show this help and exit.\n";
/* clang-format on */

static packer_ctx* G = nullptr;

packer_ctx::packer_ctx()
{
    target_handle = INVALID_HANDLE_VALUE;
    offset_magic = 0;
    offset_filesystem = 0;
}

packer_ctx::~packer_ctx()
{
    if (target_handle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(target_handle);
        target_handle = INVALID_HANDLE_VALUE;
    }
}

static void s_parse_cmd(int argc, wchar_t* argv[])
{
    const wchar_t* opt;
    size_t         optlen;
    for (int i = 0; i < argc; i++)
    {
        if (wcscmp(argv[i], L"-h") == 0 || wcscmp(argv[i], L"--help") == 0)
        {
            wprintf(L"%s", s_help);
            exit(EXIT_SUCCESS);
        }

        opt = L"--template=";
        optlen = wcslen(opt);
        if (wcsncmp(argv[i], opt, optlen) == 0)
        {
            G->template_path = argv[i] + optlen;
            continue;
        }

        G->target_path = argv[i];
    }
}

static void s_at_exit(void)
{
    delete G;
    G = nullptr;
}

static std::string s_load_file(const std::wstring& path)
{
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        throw std::runtime_error("Failed to open file");
    }

    std::string content;
    char        buff[4096];
    DWORD       read_sz;
    while (ReadFile(hFile, buff, sizeof(buff), &read_sz, nullptr))
    {
        content.append(buff, read_sz);
    }
    CloseHandle(hFile);

    return content;
}

static size_t s_write_loader(void)
{
    std::wstring loader_path = G->executable_path + L"\\loader.exe";
    std::string  loader_data = s_load_file(loader_path);

    DWORD write_sz = 0;
    if (!WriteFile(G->target_handle, loader_data.c_str(), loader_data.size(), &write_sz, nullptr))
    {
        throw std::runtime_error("Failed to write loader");
    }

    return write_sz;
}

static void s_write_magic(void)
{
    if (!WriteFile(G->target_handle, APPBOX_MAGIC, 8, nullptr, nullptr))
    {
        throw std::runtime_error("Failed to write magic");
    }
}

static void s_write_metadata()
{
    std::string meta = G->meta_json.dump();
    uint64_t    meta_sz = meta.size();

    WriteFile(G->target_handle, &meta_sz, sizeof(meta_sz), nullptr, nullptr);
    WriteFile(G->target_handle, meta.c_str(), (DWORD)meta_sz, nullptr, nullptr);
}

static uint64_t s_get_offset(HANDLE hFile)
{
    LARGE_INTEGER liDistanceToMove, lpNewFilePointer;

    liDistanceToMove.QuadPart = 0;
    lpNewFilePointer.QuadPart = 0;

    SetFilePointerEx(hFile, liDistanceToMove, &lpNewFilePointer, FILE_CURRENT);
    return lpNewFilePointer.QuadPart;
}

static int s_get_compress_level()
{
    nlohmann::json v = G->template_json["settings"]["packer"]["compression"];
    return v.is_number() ? v.get<int>() : 0;
}

static uint16_t s_get_isolation(const std::string& isolation)
{
    static const StrIntPair s_kv[] = {
        { "full",       ISOLATION_FULL       },
        { "merge",      ISOLATION_MERGE      },
        { "write_copy", ISOLATION_WRITE_COPY },
        { "hide",       ISOLATION_HIDE       },
    };

    for (size_t i = 0; i < ARRAY_SIZE(s_kv); i++)
    {
        if (isolation == s_kv[i].str)
        {
            return static_cast<uint8_t>(s_kv[i].val);
        }
    }
    return ISOLATION_FULL;
}

static uint16_t s_get_attribute(const nlohmann::json& attr)
{
    static const StrIntPair s_kv[] = {
        { "file",     0                  },
        { "folder",   ATTRIBUTE_FOLDER   },
        { "startup",  ATTRIBUTE_STARTUP  },
        { "readonly", ATTRIBUTE_READONLY },
    };

    if (!attr.is_array())
    {
        return 0;
    }

    uint8_t ret = 0;
    for (nlohmann::json::const_iterator it = attr.begin(); it != attr.end(); it++)
    {
        std::string val = it->get<std::string>();
        for (size_t i = 0; i < ARRAY_SIZE(s_kv); i++)
        {
            if (val == s_kv[i].str)
            {
                ret |= s_kv[i].val;
            }
        }
    }
    return ret;
}

static std::string json_get_default(const nlohmann::json& obj, const std::string& dft)
{
    if (obj.is_string())
    {
        return obj.get<std::string>();
    }

    if (obj.is_null())
    {
        return dft;
    }

    throw std::runtime_error("Invalid JSON type");
}

static void s_write_filesystem()
{
    /* Backup length position. */
    G->offset_filesystem = s_get_offset(G->target_handle);

    /* Write empty length. */
    uint64_t filesystem_sz = 0;
    WriteFile(G->target_handle, &filesystem_sz, sizeof(filesystem_sz), nullptr, nullptr);

    G->deflate_stream =
        std::make_unique<appbox::ZDeflateStream>(G->target_handle, s_get_compress_level());

    nlohmann::json filesystem = G->template_json["filesystem"];
    if (!filesystem.is_array())
    {
        goto FINISH;
    }

    for (nlohmann::json::iterator it = filesystem.begin(); it != filesystem.end(); ++it)
    {
        nlohmann::json file = *it;
        std::string    path = file["path"].get<std::string>();
        std::string    isolation = json_get_default(file["isolation"], "full");
        std::string    type = json_get_default(file["type"], "file");
        std::string    copy = file["copy"].get<std::string>();
        std::wstring   copy_w = appbox::mbstowcs(copy.c_str(), CP_UTF8);

        FileRecord record;
        memset(&record, 0, sizeof(record));
        record.path_len = (uint32_t)path.size();
        record.isolation = s_get_isolation(isolation);
        record.attribute = s_get_attribute(file["attribute"]);

        HANDLE hFile = CreateFileW(copy_w.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile == INVALID_HANDLE_VALUE)
        {
            throw std::runtime_error("Failed to open file");
        }
        record.payload_len = appbox::GetFileSize(hFile);

        G->deflate_stream->deflate(&record, sizeof(record));
        G->deflate_stream->deflate(path.c_str(), path.size());
        G->deflate_stream->deflate(hFile);
        CloseHandle(hFile);
    }

    uint64_t      offset_end;
    LARGE_INTEGER liDistanceToMove;
FINISH:
    G->deflate_stream.reset();

    /* Get file cursor and calculate filesystem size. */
    offset_end = s_get_offset(G->target_handle);
    filesystem_sz = offset_end - G->offset_filesystem;

    /* Write filesystem size. */
    liDistanceToMove.QuadPart = static_cast<LONGLONG>(G->offset_filesystem);
    SetFilePointerEx(G->target_handle, liDistanceToMove, nullptr, FILE_BEGIN);
    WriteFile(G->target_handle, &filesystem_sz, sizeof(filesystem_sz), nullptr, nullptr);

    /* Restore file cursor. */
    liDistanceToMove.QuadPart = static_cast<LONGLONG>(offset_end);
    SetFilePointerEx(G->target_handle, liDistanceToMove, nullptr, FILE_BEGIN);
}

static void s_write_registry()
{
    uint64_t registry_sz = 0;
    WriteFile(G->target_handle, &registry_sz, sizeof(registry_sz), nullptr, nullptr);
}

static void s_write_offset()
{
    uint64_t offset = G->offset_magic;
    WriteFile(G->target_handle, &offset, sizeof(offset), nullptr, nullptr);
}

static void s_write_crc32()
{
    uint8_t buff[16];
    memcpy(&buff[0], APPBOX_MAGIC, strlen(APPBOX_MAGIC));
    memcpy(&buff[8], &G->offset_magic, sizeof(G->offset_magic));

    uint32_t crc = crc32(0, Z_NULL, 0);
    crc = crc32(crc, buff, sizeof(buff));
    WriteFile(G->target_handle, &crc, sizeof(crc), nullptr, nullptr);
}

int wmain(int argc, wchar_t* argv[])
{
    G = new packer_ctx;
    atexit(s_at_exit);

    s_parse_cmd(argc, argv);
    G->template_data = s_load_file(G->template_path);
    G->template_json = nlohmann::json::parse(G->template_data);
    G->executable_path = appbox::GetExePath();

    G->target_handle = CreateFileW(G->executable_path.c_str(), GENERIC_WRITE, 0, nullptr,
                                   CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (G->target_handle == INVALID_HANDLE_VALUE)
    {
        throw std::runtime_error("Failed to open file");
    }

    G->offset_magic = s_write_loader();
    s_write_magic();
    s_write_metadata();
    s_write_filesystem();
    s_write_registry();
    s_write_magic();
    s_write_offset();
    s_write_crc32();

    return 0;
}
