#include <spdlog/spdlog.h>
#include "builder/FsBuilder.hpp"
#include "probe/RegistryReadKey.hpp"
#include "utils/CommonFixture.hpp"
#include "WString.hpp"

typedef appbox::test::CommonFixture Reg;
using namespace appbox::test;

/**
 * @brief Test reading a registry value inside the sandbox.
 *
 * Condition:
 * 1. Sandbox is active.
 * 2. Read HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\ProductName.
 *
 * Expected:
 * 1. The registry value can be read successfully.
 * 2. The returned value is a non-empty string.
 */
TEST_F(Reg, ReadKey_SingleLower_ExistsInLower)
{
    /* clang-format off */
    auto tree = FsRoot::Make(GetCWD(), {
        FsDir::Make(L"Upper", {}),
        FsDir::Make(L"Lower", {
            FsFile::Make(L"registry.reg",
R"REGISTRY(
Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion]
"ProductName"="AppBox Test"
)REGISTRY")
        })
    });
    /* clang-format on*/

    auto config = tree->Build();

    ProtocolRegistryReadKey::Req req;
    req.path = "HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
    req.key = "ProductName";
    auto rsp = ProbeRegistryReadKey.Call(req, GetCWDString(), config).get<ProtocolRegistryReadKey::Rsp>();

    ASSERT_EQ(rsp.code, ERROR_SUCCESS);
    ASSERT_EQ(rsp.value, "AppBox Test");
}
