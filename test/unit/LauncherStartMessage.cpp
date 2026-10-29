#include <gtest/gtest.h>
#include <cstdint>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "msg/LauncherStart.hpp"

namespace
{

/**
 * @brief Build a request which passes the validation of the master instance.
 *
 * The request carries a value in every field, so a case which changes one field
 * describes the difference it checks and nothing else.
 *
 * @return The request.
 */
appbox::MsgLauncherStart::Req ValidRequest()
{
    appbox::MsgLauncherStart::Req request;
    request.version = appbox::kLauncherStartVersion;
    request.sandbox_id = "2b1f7c4e-9a3d-4f6b-8c2e-5d7a1b3c9e0f";
    request.sandbox_root = "C:\\AppBox\\Sandbox";
    request.selection = "trigger:app";
    request.args = { "--startup_marker", "app" };
    request.environment = {
        appbox::MsgLauncherStart::EnvironmentEntry{ "TEMP", "C:\\Temp" },
        appbox::MsgLauncherStart::EnvironmentEntry{ "NAME", u8"值"     }
    };
    request.environment_is_composed = true;
    request.cwd = "C:\\AppBox\\Sandbox\\data\\filesystem\\#USERPROFILE#";
    return request;
}

/**
 * @brief Report whether two environment blocks carry the same entries.
 * @param[in] left Left operand.
 * @param[in] right Right operand.
 * @return true when both blocks carry the same entries in the same order.
 */
bool SameEnvironment(const std::vector<appbox::MsgLauncherStart::EnvironmentEntry>& left,
                     const std::vector<appbox::MsgLauncherStart::EnvironmentEntry>& right)
{
    if (left.size() != right.size())
    {
        return false;
    }

    for (std::size_t index = 0; index < left.size(); ++index)
    {
        if (left[index].key != right[index].key || left[index].value != right[index].value)
        {
            return false;
        }
    }

    return true;
}

} // namespace

/**
 * @brief A request and an answer keep every field over the wire, including the
 *        non-ASCII text of an environment value and the Win32 code of a failed
 *        creation.
 */
TEST(Unit_LauncherStartMessage, RoundTrip)
{
    const auto request = ValidRequest();

    const nlohmann::json document = request;
    const auto           restored = document.get<appbox::MsgLauncherStart::Req>();

    EXPECT_EQ(restored.version, request.version);
    EXPECT_EQ(restored.sandbox_id, request.sandbox_id);
    EXPECT_EQ(restored.sandbox_root, request.sandbox_root);
    EXPECT_EQ(restored.selection, request.selection);
    EXPECT_EQ(restored.args, request.args);
    EXPECT_TRUE(SameEnvironment(restored.environment, request.environment)) << document.dump();
    EXPECT_TRUE(restored.environment_is_composed);
    EXPECT_EQ(restored.cwd, request.cwd);

    appbox::MsgLauncherStart::Rsp response;
    response.status = appbox::launcher_start_status::kRefused;
    response.sandbox_id = request.sandbox_id;
    response.pids = { 1234u, 5678u };
    response.error_code = appbox::launcher_start_error::kStartFailed;
    response.error_message = "the process cannot be created";
    response.win32_error = 5;

    const nlohmann::json answer = response;
    const auto           restored_answer = answer.get<appbox::MsgLauncherStart::Rsp>();

    EXPECT_EQ(restored_answer.status, response.status);
    EXPECT_EQ(restored_answer.sandbox_id, response.sandbox_id);
    EXPECT_EQ(restored_answer.pids, response.pids);
    EXPECT_EQ(restored_answer.error_code, response.error_code);
    EXPECT_EQ(restored_answer.error_message, response.error_message);
    EXPECT_EQ(restored_answer.win32_error, 5);

    /* An answer of a master instance which knows no Win32 code carries zero. */
    nlohmann::json started = answer;
    started.erase("win32_error");
    EXPECT_EQ(started.get<appbox::MsgLauncherStart::Rsp>().win32_error, 0);
}

/**
 * @brief A request of another protocol version is refused, so a later layout
 *        may add fields without being read as if it were the current one.
 */
TEST(Unit_LauncherStartMessage, RejectsUnknownVersion)
{
    auto request = ValidRequest();

    std::string error_code;
    std::string error_message;
    EXPECT_TRUE(appbox::ValidateLauncherStartRequest(request, error_code, error_message)) << error_message;
    EXPECT_TRUE(error_code.empty());
    EXPECT_TRUE(error_message.empty());

    request.version = appbox::kLauncherStartVersion + 1;
    EXPECT_FALSE(appbox::ValidateLauncherStartRequest(request, error_code, error_message));
    EXPECT_EQ(error_code, appbox::launcher_start_error::kVersionUnsupported);
    EXPECT_FALSE(error_message.empty());

    /* A request which carries no version at all is refused as well. */
    request.version = 0;
    EXPECT_FALSE(appbox::ValidateLauncherStartRequest(request, error_code, error_message));
    EXPECT_EQ(error_code, appbox::launcher_start_error::kVersionUnsupported);
}

/**
 * @brief A field which exceeds its limit refuses the request before the master
 *        instance allocates anything for it, and the whole request carries a
 *        limit of its own.
 */
TEST(Unit_LauncherStartMessage, RejectsOversizedField)
{
    std::string error_code;
    std::string error_message;

    {
        auto request = ValidRequest();
        request.selection = std::string(appbox::launcher_start_limits::kMaxSelectionSize + 1, 'a');
        EXPECT_FALSE(appbox::ValidateLauncherStartRequest(request, error_code, error_message));
        EXPECT_EQ(error_code, appbox::launcher_start_error::kPayloadTooLarge);
    }

    {
        auto request = ValidRequest();
        request.sandbox_root = std::string(appbox::launcher_start_limits::kMaxSandboxRootSize + 1, 'a');
        EXPECT_FALSE(appbox::ValidateLauncherStartRequest(request, error_code, error_message));
        EXPECT_EQ(error_code, appbox::launcher_start_error::kPayloadTooLarge);
    }

    {
        auto request = ValidRequest();
        request.cwd = std::string(appbox::launcher_start_limits::kMaxCwdSize + 1, 'a');
        EXPECT_FALSE(appbox::ValidateLauncherStartRequest(request, error_code, error_message));
        EXPECT_EQ(error_code, appbox::launcher_start_error::kPayloadTooLarge);
    }

    {
        auto request = ValidRequest();
        request.args = { std::string(appbox::launcher_start_limits::kMaxArgsSize + 1, 'a') };
        EXPECT_FALSE(appbox::ValidateLauncherStartRequest(request, error_code, error_message));
        EXPECT_EQ(error_code, appbox::launcher_start_error::kPayloadTooLarge);
    }

    {
        auto request = ValidRequest();
        request.environment = {
            appbox::MsgLauncherStart::EnvironmentEntry{
                                                       "BIG", std::string(appbox::launcher_start_limits::kMaxEnvironmentSize + 1, 'a') }
        };
        EXPECT_FALSE(appbox::ValidateLauncherStartRequest(request, error_code, error_message));
        EXPECT_EQ(error_code, appbox::launcher_start_error::kPayloadTooLarge);
    }

    {
        /*
         * Every field stays inside its own limit while the encoded request
         * exceeds the limit of the whole request: a control character is
         * escaped with six bytes, so a value of the size limit of the
         * environment carries more bytes over the wire than it holds.
         */
        auto request = ValidRequest();
        request.environment = {
            appbox::MsgLauncherStart::EnvironmentEntry{
                                                       "ESCAPED", std::string(appbox::launcher_start_limits::kMaxEnvironmentSize / 2, '\x01') }
        };
        EXPECT_FALSE(appbox::ValidateLauncherStartRequest(request, error_code, error_message));
        EXPECT_EQ(error_code, appbox::launcher_start_error::kPayloadTooLarge);
    }
}

/**
 * @brief The selection of a request is the selection of the command line of the
 *        second instance, and a text which names no selection is refused.
 */
TEST(Unit_LauncherStartMessage, SelectionMapping)
{
    EXPECT_EQ(appbox::MakeLauncherStartSelection(false, false, std::string()), "auto");
    EXPECT_EQ(appbox::MakeLauncherStartSelection(false, true, "app"), "trigger:app");
    EXPECT_EQ(appbox::MakeLauncherStartSelection(true, false, std::string()), "shell");

    /* A run of the shell ignores a trigger, like the run itself does. */
    EXPECT_EQ(appbox::MakeLauncherStartSelection(true, true, "app"), "shell");

    appbox::LauncherStartSelection selection;

    ASSERT_TRUE(appbox::ParseLauncherStartSelection("auto", selection));
    EXPECT_EQ(selection.kind, appbox::LauncherStartSelectionKind::Auto);
    EXPECT_TRUE(selection.trigger.empty());

    ASSERT_TRUE(appbox::ParseLauncherStartSelection("shell", selection));
    EXPECT_EQ(selection.kind, appbox::LauncherStartSelectionKind::Shell);

    ASSERT_TRUE(appbox::ParseLauncherStartSelection("trigger:app", selection));
    EXPECT_EQ(selection.kind, appbox::LauncherStartSelectionKind::Trigger);
    EXPECT_EQ(selection.trigger, "app");

    /* A trigger which carries the separator of the selection keeps it. */
    ASSERT_TRUE(appbox::ParseLauncherStartSelection("trigger:a:b", selection));
    EXPECT_EQ(selection.trigger, "a:b");

    EXPECT_FALSE(appbox::ParseLauncherStartSelection("", selection));
    EXPECT_FALSE(appbox::ParseLauncherStartSelection("AUTO", selection));
    EXPECT_FALSE(appbox::ParseLauncherStartSelection("trigger:", selection));
    EXPECT_FALSE(appbox::ParseLauncherStartSelection("trigger", selection));
    EXPECT_FALSE(appbox::ParseLauncherStartSelection("other", selection));

    /* Every selection the builder makes is read back by the parser. */
    for (const bool shell : { false, true })
    {
        for (const bool has_trigger : { false, true })
        {
            const auto text = appbox::MakeLauncherStartSelection(shell, has_trigger, "app");
            EXPECT_TRUE(appbox::ParseLauncherStartSelection(text, selection)) << text;
            EXPECT_EQ(selection.kind, shell ? appbox::LauncherStartSelectionKind::Shell
                                            : (has_trigger ? appbox::LauncherStartSelectionKind::Trigger
                                                           : appbox::LauncherStartSelectionKind::Auto));
        }
    }
}

/**
 * @brief A request which carries no identifier, no root or no known selection
 *        is malformed instead of being repaired by the master instance.
 */
TEST(Unit_LauncherStartMessage, RejectsMalformedRequest)
{
    std::string error_code;
    std::string error_message;

    {
        auto request = ValidRequest();
        request.sandbox_id.clear();
        EXPECT_FALSE(appbox::ValidateLauncherStartRequest(request, error_code, error_message));
        EXPECT_EQ(error_code, appbox::launcher_start_error::kMalformedRequest);
    }

    {
        auto request = ValidRequest();
        request.sandbox_root.clear();
        EXPECT_FALSE(appbox::ValidateLauncherStartRequest(request, error_code, error_message));
        EXPECT_EQ(error_code, appbox::launcher_start_error::kMalformedRequest);
    }

    {
        auto request = ValidRequest();
        request.selection = "appbox";
        EXPECT_FALSE(appbox::ValidateLauncherStartRequest(request, error_code, error_message));
        EXPECT_EQ(error_code, appbox::launcher_start_error::kMalformedRequest);
    }
}
