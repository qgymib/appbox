#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>
#include <spdlog/spdlog.h>
#include "src/core/ZipWriter.hpp"
#include "SandboxLayout.hpp"
#include "WString.hpp"
#include "utils/ReadFileFull.hpp"
#include "utils/WriteFileFull.hpp"
#include "PatchBuilder.hpp"

namespace
{

/**
 * @brief Build the archive entry name of one resource of a package.
 *
 * A patch package roots the resource tree at the archive root, so the entry
 * name of a resource is its virtual path below a domain of the package with
 * the separators of the archive.
 *
 * @param[in] domain Name of the domain of the resource, for example
 *                   `filesystem`.
 * @param[in] virtual_path Path of the resource inside the domain.
 * @return The entry name of the archive.
 */
std::string EntryNameIn(const char* domain, const std::wstring& virtual_path)
{
    std::string entry(domain);
    entry.push_back('/');

    for (const auto character : appbox::WideToUTF8(virtual_path))
    {
        entry.push_back(character == '\\' ? '/' : character);
    }

    return entry;
}

/**
 * @brief Build the archive entry name of one file of the filesystem domain.
 * @param[in] virtual_path Path of the resource in the virtual filesystem.
 * @return The entry name of the archive.
 */
std::string EntryNameOf(const std::wstring& virtual_path)
{
    return EntryNameIn(appbox::layout::kFilesystemDirName, virtual_path);
}

/**
 * @brief Build a unique path for the archive of a package.
 * @return A path below the temporary directory of the machine.
 */
std::filesystem::path StagingPath()
{
    static unsigned counter = 0;
    const auto      ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           (L"appbox-patch-" + std::to_wstring(ticks) + L"-" + std::to_wstring(counter++) + L".zip");
}

/**
 * @brief Build a unique directory for the registry of a package.
 *
 * The hive of a package is written by the builder of the registry resources of
 * a case, which needs a directory of its own; it is removed again as soon as
 * the bytes of the hive were read.
 *
 * @return A path below the temporary directory of the machine.
 */
std::filesystem::path StagingDirectory()
{
    static unsigned counter = 0;
    const auto      ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           (L"appbox-patch-registry-" + std::to_wstring(ticks) + L"-" + std::to_wstring(counter++));
}

/**
 * @brief Build the bytes of the hive of the registry domain of a package.
 *
 * A case which describes keys or values gets a real hive, which the builder of
 * the registry resources of a case writes; a case which writes the bytes of a
 * broken hive itself gets them unchanged.
 *
 * @param[in] registry Registry domain of the package.
 * @param[out] hive The bytes of the hive, empty when the case describes none.
 * @param[out] error Error description on failure.
 * @return true on success.
 */
bool BuildRegistryHive(const appbox::test::PatchRegistry& registry, std::vector<std::uint8_t>& hive, std::string& error)
{
    hive.clear();

    if (!registry.raw_hive.empty())
    {
        hive.assign(registry.raw_hive.begin(), registry.raw_hive.end());
        return true;
    }

    if (registry.keys.empty() && registry.values.empty())
    {
        /* The package carries no hive at all, which keeps the hive below it. */
        return true;
    }

    const auto      root = StagingDirectory();
    std::error_code ec;
    std::filesystem::remove_all(root, ec);

    appbox::test::HiveBuilder builder(root);
    for (const auto& key : registry.keys)
    {
        builder.EnsureKey(key);
    }
    for (const auto& value : registry.values)
    {
        builder.SetValue(value.key_path, value.value_name, value.type, value.data);
    }

    bool written = builder.Write(error);
    if (written)
    {
        const auto hive_path = root / appbox::layout::kAppDirNameW / appbox::layout::kRegistryDirNameW /
                               appbox::layout::kRegistryHiveFileNameW;
        const auto read = appbox::test::ReadFileFull(hive_path.wstring(), hive);
        if (read != ERROR_SUCCESS)
        {
            error = "failed to read the hive of the package";
            written = false;
        }
    }

    std::filesystem::remove_all(root, ec);
    return written;
}

/**
 * @brief Write the archive of a package.
 * @param[in] path Path of the archive to write.
 * @param[in] files Files of the filesystem domain.
 * @param[in] isolation Isolation modes of the filesystem domain.
 * @param[in] registry Registry domain of the package.
 * @param[out] error Error description on failure.
 * @return true on success.
 */
bool BuildArchive(const std::filesystem::path& path, const std::vector<appbox::test::PatchFile>& files,
                  const std::vector<appbox::test::FsIsolationEntry>& isolation,
                  const appbox::test::PatchRegistry& registry, const appbox::test::PatchNetwork& network,
                  const appbox::test::PatchEnvironment& environment, std::string& error)
{
    try
    {
        appbox::ZipWriter writer(path.wstring());

        for (const auto& file : files)
        {
            const auto entry = EntryNameOf(file.path);
            if (!writer.AddFileBuffer(entry, file.content.data(), file.content.size(), error))
            {
                return false;
            }
        }

        if (!isolation.empty())
        {
            const auto text = appbox::test::BuildFsIsolationText(isolation);
            if (!writer.AddFileBuffer(EntryNameOf(appbox::layout::kIsolationFileNameW), text.data(), text.size(),
                                      error))
            {
                return false;
            }
        }

        /* The registry domain of the package, when the case describes it. */
        std::vector<std::uint8_t> hive;
        if (!BuildRegistryHive(registry, hive, error))
        {
            return false;
        }

        if (!hive.empty() &&
            !writer.AddFileBuffer(EntryNameIn(appbox::layout::kRegistryDirName, appbox::layout::kRegistryHiveFileNameW),
                                  hive.data(), hive.size(), error))
        {
            return false;
        }

        const std::string registry_isolation =
            !registry.raw_isolation.empty()
                ? registry.raw_isolation
                : (registry.isolation.empty() ? std::string()
                                              : appbox::test::BuildRegistryIsolationText(registry.isolation));

        if (!registry_isolation.empty() &&
            !writer.AddFileBuffer(EntryNameIn(appbox::layout::kRegistryDirName, appbox::layout::kIsolationFileNameW),
                                  registry_isolation.data(), registry_isolation.size(), error))
        {
            return false;
        }

        /* The network domain of the package, when the case describes it. */
        const std::string network_isolation =
            !network.raw_isolation.empty()
                ? network.raw_isolation
                : ((network.entries.empty() && !network.proxy.IsConfigured())
                       ? std::string()
                       : appbox::test::BuildNetworkIsolationText(network.entries, network.proxy));

        if (!network_isolation.empty() &&
            !writer.AddFileBuffer(EntryNameIn(appbox::layout::kNetworkDirName, appbox::layout::kIsolationFileNameW),
                                  network_isolation.data(), network_isolation.size(), error))
        {
            return false;
        }

        /* The environment domain of the package, when the case describes it. */
        const std::string environment_isolation =
            !environment.raw_isolation.empty()
                ? environment.raw_isolation
                : (environment.entries.empty() ? std::string()
                                               : appbox::test::BuildEnvironmentIsolationText(environment.entries));

        if (!environment_isolation.empty() &&
            !writer.AddFileBuffer(EntryNameIn(appbox::layout::kEnvironmentDirName, appbox::layout::kIsolationFileNameW),
                                  environment_isolation.data(), environment_isolation.size(), error))
        {
            return false;
        }

        return writer.Close(error);
    }
    catch (const std::exception& e)
    {
        error = e.what();
        return false;
    }
}

} // namespace

bool appbox::test::WritePatchPackage(const std::filesystem::path& zip_path, const std::vector<PatchFile>& files,
                                     const std::vector<FsIsolationEntry>& isolation, const PatchRegistry& registry,
                                     const PatchNetwork& network, const PatchEnvironment& environment)
{
    /*
     * The archive is built below the temporary directory of the machine and
     * copied into the destination afterwards, because libzip writes an archive
     * through a temporary file which it renames over the destination. A
     * process which watches the directory of a case (an editor which indexes
     * the folder of the run, a scanner) holds a destination which exists for a
     * while open long enough to make that rename fail, which would make a case
     * depend on the processes which look at its working directory.
     */
    const auto  staging = StagingPath();
    std::string error;
    if (!BuildArchive(staging, files, isolation, registry, network, environment, error))
    {
        std::error_code ec;
        std::filesystem::remove(staging, ec);
        SPDLOG_ERROR("failed to build the patch package '{}': {}", appbox::WideToUTF8(zip_path.wstring()), error);
        return false;
    }

    std::vector<uint8_t> bytes;
    const auto           read = ReadFileFull(staging.wstring(), bytes);

    std::error_code ec;
    std::filesystem::remove(staging, ec);

    if (read != ERROR_SUCCESS)
    {
        SPDLOG_ERROR("failed to read the built patch package '{}'", appbox::WideToUTF8(staging.wstring()));
        return false;
    }

    const auto written = WriteFileFull(zip_path.wstring(), bytes);
    if (written != ERROR_SUCCESS)
    {
        SPDLOG_ERROR("failed to write the patch package '{}': error {}", appbox::WideToUTF8(zip_path.wstring()),
                     written);
        return false;
    }

    return true;
}
