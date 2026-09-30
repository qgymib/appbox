#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <system_error>
#include <vector>
#include <spdlog/spdlog.h>
#include "src/core/ZipReader.hpp"
#include "SandboxLayout.hpp"
#include "WString.hpp"
#include "utils/Md5.hpp"
#include "PatchLayer.hpp"

namespace
{

/** Number of hexadecimal characters an MD5 digest has. */
constexpr std::size_t kDigestTextSize = 32;

/**
 * @brief Whether a character is a hexadecimal digit.
 * @param[in] character Character to test.
 * @return true when the character is a hexadecimal digit.
 */
bool IsHexDigit(char character)
{
    return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f') ||
           (character >= 'A' && character <= 'F');
}

/**
 * @brief Convert a character to its lower case ASCII form.
 * @param[in] character Character to convert.
 * @return The lower case form.
 */
char ToLowerAscii(char character)
{
    if (character >= 'A' && character <= 'Z')
    {
        return static_cast<char>(character - 'A' + 'a');
    }
    return character;
}

/**
 * @brief Read the digest a cache entry records for its package.
 *
 * The file holds the hexadecimal digest of the package the entry was
 * extracted from. Anything else, including a missing file, makes the cache
 * entry unusable, because the digest is the only marker of a complete
 * extraction.
 *
 * @param[in] path Path of the digest file of a cache entry.
 * @param[out] digest The recorded digest in lower case on success.
 * @return true when the file holds a digest.
 */
bool ReadRecordedDigest(const std::wstring& path, std::string& digest)
{
    digest.clear();

    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open())
    {
        return false;
    }

    std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());

    /* The digest is written with a trailing newline, which is not part of it. */
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' '))
    {
        text.pop_back();
    }

    if (text.size() != kDigestTextSize)
    {
        return false;
    }

    for (const auto character : text)
    {
        if (!IsHexDigit(character))
        {
            return false;
        }
    }

    for (const auto character : text)
    {
        digest.push_back(ToLowerAscii(character));
    }
    return true;
}

/**
 * @brief Extract a patch package into its cache entry.
 *
 * The package is extracted into a staging directory next to the cache entry
 * and the digest is written as the last file of it, so a run which is
 * interrupted half way cannot leave a directory which looks like a complete
 * extraction. The staging directory replaces the cache entry only when both
 * steps succeeded.
 *
 * @param[in] paths The resolved layout of the run.
 * @param[in] package_path Path of the package.
 * @param[in] name Name of the cache entry.
 * @param[in] digest Digest of the package.
 * @param[out] error Error description on failure.
 * @return true when the cache entry holds the extracted package.
 */
bool ExtractPackage(const appbox::SandboxPaths& paths, const std::wstring& package_path, const std::wstring& name,
                    const std::string& digest, std::string& error)
{
    const std::filesystem::path target(paths.PatchCacheDir(name));
    const std::filesystem::path staging(target.wstring() + L".tmp");

    std::error_code ec;
    std::filesystem::remove_all(staging, ec);
    ec.clear();

    error = appbox::ExtractArchive(package_path, staging.wstring());
    if (!error.empty())
    {
        std::filesystem::remove_all(staging, ec);
        return false;
    }

    {
        const auto    digest_path = staging / appbox::layout::kPatchDigestFileNameW;
        std::ofstream stream(digest_path, std::ios::binary | std::ios::trunc);
        if (!stream.is_open())
        {
            error = "failed to record the digest of the package";
            std::filesystem::remove_all(staging, ec);
            return false;
        }

        stream << digest << "\n";
        stream.close();
        if (!stream.good())
        {
            error = "failed to record the digest of the package";
            std::filesystem::remove_all(staging, ec);
            return false;
        }
    }

    std::filesystem::remove_all(target, ec);
    if (ec)
    {
        error = "failed to replace the cache entry: " + ec.message();
        std::filesystem::remove_all(staging, ec);
        return false;
    }

    std::filesystem::rename(staging, target, ec);
    if (ec)
    {
        error = "failed to replace the cache entry: " + ec.message();
        std::filesystem::remove_all(staging, ec);
        return false;
    }

    return true;
}

/**
 * @brief The path of a resource of a package, empty when it does not exist.
 *
 * A package overrides the resources it carries and not the resources of the
 * layers below it, so a resource which the package does not hold is reported
 * as missing and the launcher keeps the resource of the layer below it.
 *
 * @param[in] path Path of the resource inside the cache entry.
 * @return The path when a regular file lives there, an empty string otherwise.
 */
std::wstring ResourceFileOrEmpty(const std::wstring& path)
{
    std::error_code ec;
    if (std::filesystem::is_regular_file(path, ec))
    {
        return path;
    }

    return {};
}

/**
 * @brief Report the resources one extracted package carries.
 *
 * A package which carries no resource of a domain keeps the members of that
 * domain empty, because the layers below it stay the layers of the run for
 * that resource.
 *
 * @param[in] paths The resolved layout of the run.
 * @param[in] name Name of the cache entry of the package.
 * @return The layer the package contributes to the run.
 */
appbox::PatchLayer DescribeLayer(const appbox::SandboxPaths& paths, const std::wstring& name)
{
    std::error_code ec;

    appbox::PatchLayer layer;
    layer.name = name;

    const auto layer_root = paths.PatchLayerRoot(name);
    if (std::filesystem::is_directory(layer_root, ec))
    {
        layer.layer_root = layer_root;
    }

    layer.isolation_file = ResourceFileOrEmpty(paths.PatchIsolationFile(name));
    layer.registry_hive = ResourceFileOrEmpty(paths.PatchRegistryHive(name));
    layer.registry_isolation_file = ResourceFileOrEmpty(paths.PatchRegistryIsolationFile(name));
    layer.network_isolation_file = ResourceFileOrEmpty(paths.PatchNetworkIsolationFile(name));
    layer.environment_isolation_file = ResourceFileOrEmpty(paths.PatchEnvironmentIsolationFile(name));

    return layer;
}

} // namespace

std::vector<appbox::PatchLayer> appbox::LoadPatchLayers(const SandboxPaths& paths)
{
    std::vector<PatchLayer> layers;

    std::error_code ec;
    if (!std::filesystem::exists(paths.patch, ec))
    {
        SPDLOG_DEBUG("the run has no patch directory: {}", WideToUTF8(paths.patch));
        return layers;
    }

    if (!std::filesystem::is_directory(paths.patch, ec))
    {
        SPDLOG_WARN("the patch path '{}' is not a directory, no package is applied", WideToUTF8(paths.patch));
        return layers;
    }

    /* The packages of the directory, in the order they take effect in. */
    std::vector<std::wstring> packages;
    for (const auto& entry : std::filesystem::directory_iterator(paths.patch, ec))
    {
        std::error_code entry_ec;
        if (!entry.is_regular_file(entry_ec))
        {
            continue;
        }

        const auto file_name = entry.path().filename().wstring();
        if (SandboxPaths::PatchCacheName(file_name).empty())
        {
            /* A file without the extension of a package is not a package. */
            continue;
        }

        packages.push_back(file_name);
    }

    if (ec)
    {
        SPDLOG_ERROR("failed to enumerate the patch directory '{}': {}", WideToUTF8(paths.patch), ec.message());
        return layers;
    }

    if (packages.empty())
    {
        SPDLOG_DEBUG("the patch directory holds no package: {}", WideToUTF8(paths.patch));
        return layers;
    }

    std::sort(packages.begin(), packages.end());

    /*
     * The cache exists while the patch directory holds a package, so a user
     * who wants to reset the extraction deletes the directory instead of the
     * single entries.
     */
    std::filesystem::create_directories(paths.cache, ec);
    if (ec)
    {
        SPDLOG_ERROR("failed to create the patch cache '{}': {}", WideToUTF8(paths.cache), ec.message());
        return layers;
    }

    std::set<std::wstring> cache_names;
    for (const auto& file_name : packages)
    {
        const auto name = SandboxPaths::PatchCacheName(file_name);
        if (!cache_names.insert(name).second)
        {
            /*
             * Two packages can only share an entry when their names differ in
             * the case of the extension, because the extension is the only
             * part the name of the entry drops.
             */
            SPDLOG_WARN("the patch package '{}' shares its cache entry with an earlier package, it is skipped",
                        WideToUTF8(file_name));
            continue;
        }

        const auto  package_path = (std::filesystem::path(paths.patch) / file_name).wstring();
        std::string digest;
        std::string error;
        if (!Md5OfFile(package_path, digest, error))
        {
            SPDLOG_ERROR("the patch package '{}' is skipped: {}", WideToUTF8(file_name), error);
            continue;
        }

        std::string recorded;
        const bool  reused = ReadRecordedDigest(paths.PatchDigestFile(name), recorded) && recorded == digest;
        if (reused)
        {
            SPDLOG_DEBUG("the cache entry of the patch package '{}' is reused", WideToUTF8(file_name));
        }
        else if (!ExtractPackage(paths, package_path, name, digest, error))
        {
            SPDLOG_ERROR("the patch package '{}' is skipped: {}", WideToUTF8(file_name), error);
            continue;
        }

        layers.push_back(DescribeLayer(paths, name));
    }

    if (!layers.empty())
    {
        SPDLOG_INFO("the run applies {} patch package(s)", layers.size());
    }

    return layers;
}
