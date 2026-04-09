#include <gtest/gtest.h>
#include <CLI/CLI.hpp>
#include "probe/__init__.hpp"
#include "Test.hpp"

int wmain(int argc, wchar_t* argv[])
{
    CLI::App app("AppBox unit tests");
    app.add_option("--loader", appbox::test::cmd_param.loader_path);
    appbox::test::ProbeInit(app);

    testing::InitGoogleTest(&argc, argv);
    CLI11_PARSE(app, argc, argv);

    return RUN_ALL_TESTS();
}
