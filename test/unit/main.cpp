#include <gtest/gtest.h>

/**
 * @brief Entry point of the AppBox unit test executable.
 *
 * Unlike AppBoxTests, this executable runs pure in-process unit tests and does
 * not start the loader or inject the sandbox DLL, so it stays independent from
 * the end-to-end probe chain.
 *
 * @param[in] argc Number of command line arguments.
 * @param[in] argv Command line arguments.
 * @return The result of the test run.
 */
int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
