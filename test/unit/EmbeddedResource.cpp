#include "sandbox/utils/WinAPI.h" /* Must be included before any other headers. */
#include <gtest/gtest.h>
#include <algorithm>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>
#include "EmbeddedResource.hpp"
#include "src/core/EmbeddedResourceIds.h"
#include "launcher/EmbeddedResourceIds.h"
#include "utils/LauncherPath.hpp"
#include "utils/ReadFileFull.hpp"
#include "utils/SandboxDll.hpp"
#include "Test.hpp"
#include "WString.hpp"

namespace
{

/** Identifier of a resource which no executable of the build carries. */
constexpr int kUnknownResourceId = 0x7FFF;

/**
 * @brief RAII helper which opens an executable as a resource data file.
 *
 * The payloads of the packer are resources of its own executable, so a test
 * which checks them opens the packer without running it: a module which was
 * opened as a data file is never initialized, it only carries its resources.
 */
class DataFileModule
{
public:
    /**
     * @brief Open an executable as a data file.
     * @param[in] path Path of the executable, empty to open nothing.
     */
    explicit DataFileModule(const std::wstring& path)
    {
        if (!path.empty())
        {
            module_ = ::LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE);
        }
    }

    ~DataFileModule()
    {
        if (module_ != nullptr)
        {
            ::FreeLibrary(module_);
        }
    }

    DataFileModule(const DataFileModule&) = delete;
    DataFileModule& operator=(const DataFileModule&) = delete;
    DataFileModule(DataFileModule&&) = delete;
    DataFileModule& operator=(DataFileModule&&) = delete;

    /**
     * @brief Get the handle of the opened module.
     * @return The handle, nullptr when the module could not be opened.
     */
    void* get() const
    {
        return module_;
    }

private:
    HMODULE module_ = nullptr;
};

/**
 * @brief Read a file of the build tree.
 * @param[in] path Path of the file.
 * @return The content of the file, empty when it cannot be read.
 */
std::string ReadFile(const std::wstring& path)
{
    std::string data;
    if (appbox::test::ReadFileFull(path, data) != ERROR_SUCCESS)
    {
        return {};
    }
    return data;
}

/**
 * @brief Collect the numeric identifiers of the RCDATA resources of a module.
 *
 * Every executable numbers the resources it carries on its own, so an
 * identifier only means something in the module which declares it. The set of
 * identifiers is what tells the resources of one executable from those of
 * another: a payload of the packer is read with an identifier of the packer, a
 * payload of the launcher with one of the launcher, and neither identifier is
 * looked up in the other executable.
 *
 * @param[in] module Module which carries the resources.
 * @return The identifiers in ascending order, empty when the module cannot be
 *         enumerated.
 */
std::vector<int> RcdataResourceIds(void* module)
{
    std::vector<int> ids;

    const auto collect = [](HMODULE, LPCWSTR, LPWSTR name, LONG_PTR parameter) -> BOOL {
        if (IS_INTRESOURCE(name))
        {
            auto* collected = reinterpret_cast<std::vector<int>*>(parameter);
            collected->push_back(static_cast<int>(reinterpret_cast<ULONG_PTR>(name)));
        }
        return TRUE;
    };

    const auto instance = static_cast<HMODULE>(module);
    const auto type = reinterpret_cast<LPCWSTR>(RT_RCDATA);
    if (!EnumResourceNamesW(instance, type, collect, reinterpret_cast<LONG_PTR>(&ids)))
    {
        return {};
    }

    std::sort(ids.begin(), ids.end());
    return ids;
}

/**
 * @brief Compare one payload of the packer with the file it was built from.
 *
 * The sizes are compared before the content, because a failure which prints two
 * payloads of several megabytes would bury the report of the run.
 *
 * @param[in] payload The payload read from the executable.
 * @param[in] path Path of the file the payload was built from.
 */
void ExpectPayloadMatchesFile(const std::string_view& payload, const std::wstring& path)
{
    const auto file = ReadFile(path);
    ASSERT_FALSE(file.empty()) << appbox::WideToUTF8(path);
    EXPECT_EQ(payload.size(), file.size()) << appbox::WideToUTF8(path);
    EXPECT_TRUE(payload == std::string_view(file)) << appbox::WideToUTF8(path);
}

} // namespace

/**
 * Condition:
 * 1. Every executable of the build numbers the RCDATA resources it carries on
 *    its own, starting at 101.
 * 2. The packer of the build carries the launcher program as its only RCDATA
 *    resource.
 * 3. The launcher of the build carries the two sandbox injection modules as its
 *    only RCDATA resources.
 * 4. The resources are read with the module opened as a data file, which is the
 *    lookup the packer and the launcher perform at run time.
 *
 * Expected:
 * 1. The RCDATA identifier set of the packer is exactly the launcher payload,
 *    and that payload is byte for byte the launcher file of the build tree.
 * 2. The RCDATA identifier set of the launcher is exactly the two modules, and
 *    every module is byte for byte the module file of the build tree.
 */
TEST(Unit_EmbeddedResource, PayloadsMatchTheBuildTree)
{
    if (!appbox::test::SandboxModulesAvailable())
    {
        GTEST_SKIP() << "the sandbox injection modules are unavailable: pass --sandbox32= and --sandbox64=";
    }

    const auto& packer = appbox::test::config.packer_path;
    if (packer.empty())
    {
        GTEST_SKIP() << "the packer is unavailable: pass --packer=";
    }

    std::string      error;
    std::string_view payload;

    {
        DataFileModule module(packer);
        ASSERT_NE(module.get(), nullptr) << appbox::WideToUTF8(packer);

        EXPECT_EQ(RcdataResourceIds(module.get()), (std::vector<int>{ IDR_APPBOX_LAUNCHER }))
            << "the packer has an unexpected RCDATA resource";

        /* The packer carries the launcher program of this build. */
        ASSERT_TRUE(appbox::ReadEmbeddedResource(module.get(), IDR_APPBOX_LAUNCHER, payload, error)) << error;
        ExpectPayloadMatchesFile(payload, appbox::test::LauncherPath());
    }

    {
        const auto&    launcher = appbox::test::LauncherPath();
        DataFileModule module(launcher);
        ASSERT_NE(module.get(), nullptr) << appbox::WideToUTF8(launcher);

        EXPECT_EQ(RcdataResourceIds(module.get()), (std::vector<int>{ IDR_APPBOX_SANDBOX32, IDR_APPBOX_SANDBOX64 }))
            << "the launcher has an unexpected RCDATA resource";

        ASSERT_TRUE(appbox::ReadEmbeddedResource(module.get(), IDR_APPBOX_SANDBOX32, payload, error)) << error;
        ExpectPayloadMatchesFile(payload, appbox::test::Sandbox32DllPath());

        ASSERT_TRUE(appbox::ReadEmbeddedResource(module.get(), IDR_APPBOX_SANDBOX64, payload, error)) << error;
        ExpectPayloadMatchesFile(payload, appbox::test::Sandbox64DllPath());
    }
}

/**
 * Condition:
 * 1. A resource identifier is read from an executable which carries no such
 *    resource.
 *
 * Expected:
 * 1. The reader fails and reports the identifier instead of handing out an
 *    empty payload, which the packer would turn into an empty archive entry.
 */
TEST(Unit_EmbeddedResource, MissingResourceIsReported)
{
    /*
     * The running executable carries none of the payloads of the packer, which
     * makes it the executable of the case.
     */
    std::string      error;
    std::string_view payload;

    EXPECT_FALSE(appbox::ReadEmbeddedResource(nullptr, kUnknownResourceId, payload, error));
    EXPECT_FALSE(error.empty());
    EXPECT_TRUE(payload.empty());
}
