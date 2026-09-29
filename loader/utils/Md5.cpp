#include <windows.h>
#include <bcrypt.h>
#include <cstdio>
#include <string>
#include <vector>
#include <spdlog/spdlog.h>
#include "WString.hpp"
#include "Md5.hpp"

namespace
{

/** Size of the buffer the file is read with. */
constexpr std::size_t kReadBufferSize = 64 * 1024;

/** Length of an MD5 digest in bytes. */
constexpr std::size_t kDigestSize = 16;

/** Number of hexadecimal characters one digest byte needs. */
constexpr std::size_t kHexDigitsPerByte = 2;

/**
 * @brief Format a status code of the CNG API.
 * @param[in] context Description of the failed operation.
 * @param[in] status Status code the operation reported.
 * @return The composed error message.
 */
std::string FormatError(const char* context, NTSTATUS status)
{
    return fmt::format("{} (0x{:08X})", context, static_cast<unsigned long>(status));
}

/**
 * @brief RAII wrapper closing an algorithm provider of the CNG API.
 */
struct AlgorithmGuard
{
    /**
     * @brief Close the provider.
     */
    ~AlgorithmGuard()
    {
        if (handle != nullptr)
        {
            BCryptCloseAlgorithmProvider(handle, 0);
        }
    }

    BCRYPT_ALG_HANDLE handle = nullptr;
};

/**
 * @brief RAII wrapper destroying a hash of the CNG API.
 */
struct HashGuard
{
    /**
     * @brief Destroy the hash.
     */
    ~HashGuard()
    {
        if (handle != nullptr)
        {
            BCryptDestroyHash(handle);
        }
    }

    BCRYPT_HASH_HANDLE handle = nullptr;
};

/**
 * @brief RAII wrapper closing a stream.
 */
struct FileGuard
{
    /**
     * @brief Close the stream.
     */
    ~FileGuard()
    {
        if (file != nullptr)
        {
            std::fclose(file);
        }
    }

    FILE* file = nullptr;
};

/**
 * @brief Hash the content of an open stream into a hash handle.
 * @param[in] hash Handle of the hash.
 * @param[in] file Stream to read.
 * @param[out] error Error description on failure.
 * @return true when the whole stream was hashed.
 */
bool HashStream(BCRYPT_HASH_HANDLE hash, FILE* file, std::string& error)
{
    std::vector<unsigned char> buffer(kReadBufferSize);
    for (;;)
    {
        const auto read = std::fread(buffer.data(), 1, buffer.size(), file);
        if (read == 0)
        {
            break;
        }

        const auto status = BCryptHashData(hash, buffer.data(), static_cast<ULONG>(read), 0);
        if (!BCRYPT_SUCCESS(status))
        {
            error = FormatError("failed to hash the content of the file", status);
            return false;
        }

        if (read < buffer.size())
        {
            /* A short read ends the content of a regular file. */
            break;
        }
    }

    if (std::ferror(file) != 0)
    {
        error = "failed to read the content of the file";
        return false;
    }

    return true;
}

/**
 * @brief Format a digest as lower case hexadecimal text.
 * @param[in] raw Digest bytes.
 * @return The hexadecimal text.
 */
std::string FormatDigest(const unsigned char* raw)
{
    static const char kHexDigits[] = "0123456789abcdef";

    std::string digest;
    digest.reserve(kDigestSize * kHexDigitsPerByte);
    for (std::size_t index = 0; index < kDigestSize; ++index)
    {
        digest.push_back(kHexDigits[raw[index] >> 4]);
        digest.push_back(kHexDigits[raw[index] & 0x0F]);
    }
    return digest;
}

} // namespace

bool appbox::Md5OfFile(const std::wstring& path, std::string& digest, std::string& error)
{
    digest.clear();
    error.clear();

    AlgorithmGuard algorithm;
    auto           status = BCryptOpenAlgorithmProvider(&algorithm.handle, BCRYPT_MD5_ALGORITHM, nullptr, 0);
    if (!BCRYPT_SUCCESS(status))
    {
        error = FormatError("failed to open the MD5 provider", status);
        return false;
    }

    DWORD object_size = 0;
    DWORD written = 0;
    status = BCryptGetProperty(algorithm.handle, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&object_size),
                               sizeof(object_size), &written, 0);
    if (!BCRYPT_SUCCESS(status) || object_size == 0)
    {
        error = FormatError("failed to read the size of the MD5 state", status);
        return false;
    }

    std::vector<unsigned char> object(object_size);
    HashGuard                  hash;
    status = BCryptCreateHash(algorithm.handle, &hash.handle, object.data(), object_size, nullptr, 0, 0);
    if (!BCRYPT_SUCCESS(status))
    {
        error = FormatError("failed to create the MD5 state", status);
        return false;
    }

    FileGuard file;
    if (_wfopen_s(&file.file, path.c_str(), L"rb") != 0 || file.file == nullptr)
    {
        error = "failed to open '" + appbox::WideToUTF8(path) + "'";
        return false;
    }

    if (!HashStream(hash.handle, file.file, error))
    {
        error += " of '" + appbox::WideToUTF8(path) + "'";
        return false;
    }

    unsigned char raw[kDigestSize] = {};
    status = BCryptFinishHash(hash.handle, raw, static_cast<ULONG>(kDigestSize), 0);
    if (!BCRYPT_SUCCESS(status))
    {
        error = FormatError("failed to finish the digest", status);
        return false;
    }

    digest = FormatDigest(raw);
    return true;
}
