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
