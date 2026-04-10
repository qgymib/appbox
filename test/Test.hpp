#ifndef APPBOX_TEST_HPP
#define APPBOX_TEST_HPP

#include <string>

namespace appbox::test
{

struct CmdParam
{
    std::wstring loader_path; /* Path to loader */
    std::wstring log_level = L"info";
};

extern CmdParam cmd_param;

} // namespace appbox::test

#endif // APPBOX_TEST_HPP
