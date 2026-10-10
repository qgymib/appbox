#include "probe/RegTransacted.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "Random.hpp"
#include "WString.hpp"
#include <filesystem>
#include <system_error>

typedef appbox::test::CommonFixture E2E_Reg;
using namespace appbox::test;

namespace
{

/**
 * @brief RAII helper which owns an application hive file of a case.
 *
 * The hive is mounted and closed again while the helper is created, so the file
 * exists on disk and the probe of the case can mount it as well. The file and
 * its transaction logs are removed when the helper goes out of scope.
 */
class ScratchHive
{
public:
    /**
     * @brief Create the hive file.
     * @param[in] path Path of the file.
     */
    explicit ScratchHive(const std::filesystem::path& path) : path_(path)
    {
        HKEY root = nullptr;
        if (RegLoadAppKeyW(path_.wstring().c_str(), &root, KEY_ALL_ACCESS, 0, 0) == ERROR_SUCCESS)
        {
            RegCloseKey(root);
            created_ = true;
        }
    }

    ~ScratchHive()
    {
        const auto      text = path_.wstring();
        std::error_code ec;
        std::filesystem::remove(text, ec);
        std::filesystem::remove(text + L".LOG1", ec);
        std::filesystem::remove(text + L".LOG2", ec);
    }

    ScratchHive(const ScratchHive&) = delete;
    ScratchHive& operator=(const ScratchHive&) = delete;

    /**
     * @brief Whether the hive file was created.
     * @return true when the file exists.
     */
    bool created() const
    {
        return created_;
    }

    /**
     * @brief Path of the hive file.
     * @return The path.
     */
    const std::filesystem::path& path() const
    {
        return path_;
    }

private:
    std::filesystem::path path_;
    bool                  created_ = false;
};

} // namespace

/**
 * Condition:
 * 1. The probe mounts an application hive of its own. A mount of
 *    `RegLoadAppKeyW` lives below `\REGISTRY\A` and is therefore outside the
 *    root keys of the view, so the isolation must not redirect the call.
 * 2. Create a key inside that hive with `NtCreateKeyTransacted` and write a
 *    value through the handle.
 *
 * Expected:
 * 1. The call is forwarded unchanged and the kernel answers it: the status is
 *    the one the same call reports without the sandbox
 *    (`STATUS_RM_NOT_ACTIVE`, the answer of an application hive for a
 *    transacted call), so the arguments of the caller reach the kernel in their
 *    own order and the call is not turned into an isolated one.
 * 2. The hive of the probe does not hold the key afterwards: the sandbox
 *    neither created it in the hive of the probe nor in its own hive.
 */
TEST_F(E2E_Reg, Transacted_ForeignHiveIsForwarded)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data")
    });
    /* clang-format on */

    auto config = tree.Build();

    ScratchHive hive(GetCWD() / (L"transacted-" + appbox::UTF8ToWide(appbox::RandomString(8)) + L".hiv"));
    ASSERT_TRUE(hive.created()) << "the hive of the case cannot be created";

    ProtocolRegTransacted::Req req;
    req.Hive = appbox::WideToUTF8(hive.path().wstring());
    req.Key = "AppBoxTransacted";
    req.Api = "create";
    req.Access = "write";
    req.Value = "TestValue";
    req.Data = "forwarded";
    req.End = "commit";

    const auto rsp = ProbeRegTransacted.Call(req, GetCWD(), config).get<ProtocolRegTransacted::Rsp>();
    ASSERT_EQ(rsp.mount_code, static_cast<DWORD>(ERROR_SUCCESS));
    ASSERT_EQ(rsp.tx_code, static_cast<DWORD>(ERROR_SUCCESS));
    EXPECT_EQ(rsp.call_code, static_cast<DWORD>(STATUS_RM_NOT_ACTIVE));
    EXPECT_EQ(rsp.after_open_code, static_cast<DWORD>(ERROR_FILE_NOT_FOUND));
}
