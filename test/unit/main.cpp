#include <gtest/gtest.h>
#include "unit/LoaderPath.hpp"
#include "WString.hpp"
#include <cstring>
#include <string>
#include <vector>

namespace
{

/** Command line prefix of the loader path. */
constexpr const char* kLoaderPrefix = "--loader=";

/**
 * @brief Take the loader path out of the command line.
 *
 * The argument is removed before the GoogleTest flags are parsed, so the
 * runner does not report it as an unknown flag.
 *
 * @param[in,out] arguments Command line arguments of the process.
 */
void TakeLoaderPath(std::vector<char*>& arguments)
{
    std::vector<char*> remaining;
    remaining.reserve(arguments.size());
    for (auto* argument : arguments)
    {
        const std::string text = argument != nullptr ? argument : "";
        if (text.rfind(kLoaderPrefix, 0) == 0)
        {
            appbox::test::SetLoaderPath(
                appbox::UTF8ToWide(text.substr(std::strlen(kLoaderPrefix))));
            continue;
        }

        remaining.push_back(argument);
    }

    arguments = std::move(remaining);
}

} // namespace

/**
 * @brief Entry point of the AppBox unit test executable.
 *
 * Unlike AppBoxTests, this executable runs pure in-process unit tests and does
 * not start the loader or inject the sandbox DLL, so it stays independent from
 * the end-to-end probe chain. The optional `--loader=<path>` argument names
 * the loader executable for the tests which work on the real loader payload.
 *
 * @param[in] argc Number of command line arguments.
 * @param[in] argv Command line arguments.
 * @return The result of the test run.
 */
int main(int argc, char** argv)
{
    std::vector<char*> arguments(argv, argv + argc);
    TakeLoaderPath(arguments);

    auto count = static_cast<int>(arguments.size());
    testing::InitGoogleTest(&count, arguments.data());
    return RUN_ALL_TESTS();
}
