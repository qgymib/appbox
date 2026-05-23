#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include "probe/__init__.hpp"
#include "WString.hpp"
#include "__init__.hpp"
#include <gtest/gtest.h>

struct ReadFile : testing::Test
{
    ReadFile()
    {
        cwd_ = appbox::test::CWD::Create();
    }
    ~ReadFile() override
    {
        cwd_->NoCleanup(HasFailure());
    }

    appbox::test::CWD::Ptr cwd_;
};

static DWORD ReadFileW(const std::wstring& FileName, std::string& data)
{
    auto hFile = CreateFileW(FileName.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        return GetLastError();
    }

    LARGE_INTEGER fileSize;
    if (!GetFileSizeEx(hFile, &fileSize))
    {
        return GetLastError();
    }

    data.clear();
    data.resize(fileSize.QuadPart);
    auto rd = ReadFile(hFile, data.data(), static_cast<DWORD>(fileSize.QuadPart), nullptr, nullptr);
    CloseHandle(hFile);

    return rd ? 0 : GetLastError();
}

static DWORD WriteFileW(const std::wstring& FileName, const std::string& data)
{
    auto hFile = CreateFileW(FileName.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        return GetLastError();
    }

    auto wr = WriteFile(hFile, data.data(), static_cast<DWORD>(data.size()), nullptr, nullptr);
    CloseHandle(hFile);

    return wr ? 0 : GetLastError();
}

struct ProbeReadFile
{
    static constexpr char* Method = "ReadFile";

    struct Req
    {
        std::string FileName; /* File name encoding in UTF-8. */
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Req, FileName)
    };

    struct Rsp
    {
        DWORD       code = 0; /* Error code. */
        std::string data;     /* Binary data. */
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Rsp, code, data)
    };

    static nlohmann::json Entry(const nlohmann::json& data)
    {
        auto req = data.get<Req>();

        Rsp rsp;
        rsp.code = ReadFileW(appbox::UTF8ToWide(req.FileName), rsp.data);

        return rsp;
    }
};

static appbox::test::Probe probe(ProbeReadFile::Method, ProbeReadFile::Entry);

TEST_F(ReadFile, HostFS)
{
    auto        file_path = cwd_->cwd_ / L"ReadFile_HostFS";
    std::string file_data = "Hello, world!";

    /* Write to test file. */
    ASSERT_EQ(WriteFileW(file_path.wstring(), file_data), 0);

    /* Check file content in sandbox */
    {
        ProbeReadFile::Req req;
        req.FileName = appbox::WideToUTF8(file_path.wstring());
        auto rsp = probe.Call(req, cwd_->WString()).get<ProbeReadFile::Rsp>();
        ASSERT_EQ(rsp.code, 0);
        ASSERT_EQ(rsp.data, file_data);
    }
}
