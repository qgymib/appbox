#include "sandbox/utils/WinAPI.h" /* Must be included before any other headers. */
#include <gtest/gtest.h>
#include <filesystem>
#include <string>
#include <string_view>
#include "src/core/EmbeddedResource.hpp"
#include "src/core/EmbeddedResourceIds.h"
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
 * 1. The packer of the build carries the launcher program and the two sandbox
 *    injection modules as RCDATA resources.
 * 2. The resources are read with the module opened as a data file, which is the
 *    lookup the packer performs at run time.
 *
 * Expected:
 * 1. Every payload is byte for byte the file of the build tree it was embedded
 *    from, so the archive of a pack run carries the modules of this build.
 */
TEST(Unit_EmbeddedResource, PayloadsMatchTheBuildTree)
{
    const auto& packer = appbox::test::config.packer_path;
    if (packer.empty())
    {
        GTEST_SKIP() << "the packer is unavailable: pass --packer=";
    }

    DataFileModule module(packer);
    ASSERT_NE(module.get(), nullptr) << appbox::WideToUTF8(packer);

    std::string      error;
    std::string_view payload;

    ASSERT_TRUE(appbox::ReadEmbeddedResource(module.get(), IDR_APPBOX_SANDBOX32, payload, error)) << error;
    ExpectPayloadMatchesFile(payload, appbox::test::Sandbox32DllPath());

    ASSERT_TRUE(appbox::ReadEmbeddedResource(module.get(), IDR_APPBOX_SANDBOX64, payload, error)) << error;
    ExpectPayloadMatchesFile(payload, appbox::test::Sandbox64DllPath());

    /*
     * The embedded launcher is the launcher of this build, which is the program the
     * end-to-end cases start.
     */
    ASSERT_TRUE(appbox::ReadEmbeddedResource(module.get(), IDR_APPBOX_LAUNCHER, payload, error)) << error;
    ExpectPayloadMatchesFile(payload, appbox::test::LauncherPath());
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
