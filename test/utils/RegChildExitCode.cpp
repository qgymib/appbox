#include "RegChildExitCode.hpp"
#include <spdlog/fmt/fmt.h>

std::string appbox::test::DescribeRegChildExitCode(unsigned long code)
{
    if ((code & ~static_cast<unsigned long>(kRegChildExitCodeMask)) != 0)
    {
        return fmt::format("exit code {:#x} is not one of the worker (0x0-0xf): the process did not reach its own code",
                           code);
    }

    std::string text;
    const auto  append = [&text](bool set, const char* yes, const char* no) {
        if (!text.empty())
        {
            text += " | ";
        }
        text += set ? yes : no;
    };
    append((code & RegChildOpenedKey) != 0, "key opened", "key not opened");
    append((code & RegChildReadValue) != 0, "value read", "value not read");
    append((code & RegChildValueMatched) != 0, "value matched", "value did not match");
    append((code & RegChildWroteValues) != 0, "values written", "values not written");
    return fmt::format("{:#x} ({})", code, text);
}
