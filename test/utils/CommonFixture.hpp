#ifndef APPBOX_TEST_UTILS_COMMON_FIXTURE_HPP
#define APPBOX_TEST_UTILS_COMMON_FIXTURE_HPP

#include <gtest/gtest.h>
#include <filesystem>

namespace appbox::test
{

/**
 * @brief Common fixture for tests.
 * @note Every test case should inherit this class.
 */
class CommonFixture : public ::testing::Test
{
public:
    CommonFixture();
    ~CommonFixture() override;

    /**
     * @brief Skip the case when the run cannot start the sandbox.
     *
     * Every case starts the real loader, which injects the sandbox injection
     * modules of the resource root of the case: a run which was started without
     * them cannot run a case, so the cases skip themselves instead of failing.
     *
     * @see appbox::test::SandboxModulesAvailable()
     */
    void SetUp() override;

    /**
     * @brief Get the path of the CWD directory.
     * @return The path of the CWD directory.
     */
    std::filesystem::path GetCWD() const;

    /**
     * @brief Get the path of the CWD directory as a string.
     * @return The path of the CWD directory as a string.
     */
    std::wstring GetCWDString() const;

private:
    struct Data;
    Data* data_;
};

} // namespace appbox::test

#endif
