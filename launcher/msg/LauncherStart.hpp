#ifndef APPBOX_LAUNCHER_MSG_LAUNCHERSTART_HPP
#define APPBOX_LAUNCHER_MSG_LAUNCHERSTART_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace appbox
{

/**
 * @brief Protocol version of the start request of a second launcher instance.
 *
 * The version is carried by every request, so a master instance which does not
 * know the version of a request refuses it with `version_unsupported` instead of
 * reading the fields of a newer layout as if they were its own.
 */
constexpr int kLauncherStartVersion = 1;

/**
 * @brief Limits of the start request of a second launcher instance.
 *
 * The limits are the ones of the `Limits and timeouts` section of
 * `docs/Launcher.md`. They bound what a client may make the master instance
 * allocate before the request was validated, so every field and the whole
 * request carry a limit of their own.
 */
namespace launcher_start_limits
{
constexpr std::size_t kMaxRequestSize = 1024u * 1024u;    /* 1 MiB, the encoded request. */
constexpr std::size_t kMaxSelectionSize = 256u;           /* The selection of the startup files. */
constexpr std::size_t kMaxSandboxRootSize = 32u * 1024u;  /* The host root of the sandbox. */
constexpr std::size_t kMaxCwdSize = 32u * 1024u;          /* The current directory of the client. */
constexpr std::size_t kMaxArgsSize = 64u * 1024u;         /* The arguments of the request together. */
constexpr std::size_t kMaxEnvironmentSize = 512u * 1024u; /* The environment of the request together. */
} // namespace launcher_start_limits

/**
 * @brief Stable identifiers of the answers of the master instance.
 *
 * The identifiers are part of the wire format: a second instance maps them to
 * its exit code, so they are spelled out here instead of being built at the
 * place which reports them.
 */
namespace launcher_start_status
{
constexpr const char* kStarted = "started";
constexpr const char* kRefused = "refused";
constexpr const char* kShuttingDown = "shutting_down";
} // namespace launcher_start_status

/**
 * @brief Stable error identifiers of a refused start request.
 *
 * The identifiers are part of the wire format: a second instance maps them to
 * its exit code, so every reason the master instance reports has one of its own.
 */
namespace launcher_start_error
{
constexpr const char* kSandboxMismatch = "sandbox_mismatch";
constexpr const char* kVersionUnsupported = "version_unsupported";
constexpr const char* kBusy = "busy";
constexpr const char* kMalformedRequest = "malformed_request";
constexpr const char* kPayloadTooLarge = "payload_too_large";
constexpr const char* kNoTarget = "no_target";
constexpr const char* kTriggerNotFound = "trigger_not_found";
constexpr const char* kShellNotFound = "shell_not_found";
constexpr const char* kImageNotFound = "image_not_found";
constexpr const char* kCwdNotFound = "cwd_not_found";
constexpr const char* kStartFailed = "start_failed";
constexpr const char* kShuttingDown = "shutting_down";
constexpr const char* kUnauthorized = "unauthorized";
constexpr const char* kIntegrityMismatch = "integrity_mismatch";
constexpr const char* kServerMismatch = "server_mismatch";
} // namespace launcher_start_error

/**
 * @brief Message which starts a program of a running sandbox.
 *
 * The message is the handshake of the multi instance feature: a launcher which
 * lost the race for the control pipe of a sandbox sends the request of its own
 * command line to the launcher which won the race, and the master instance
 * starts the programs the request selects inside the sandbox it prepared.
 *
 * The message carries no path of an executable: the master instance starts the
 * programs of its own configuration, and the only exception is the selection of
 * the shell, whose command line is the `args` of the request. The control pipe
 * is therefore the only boundary of the feature, which is why the access to it
 * is restricted to the user of the master instance.
 */
struct MsgLauncherStart
{
    static constexpr const char* Method = "LauncherStart";

    /**
     * @brief One variable of the environment of the client.
     */
    struct EnvironmentEntry
    {
        std::string key;   /*!< Name of the variable, in UTF-8. */
        std::string value; /*!< Value of the variable, in UTF-8. */

        NLOHMANN_DEFINE_TYPE_INTRUSIVE(EnvironmentEntry, key, value)
    };

    /**
     * @brief Request of a second launcher instance.
     */
    struct Req
    {
        /**
         * @brief Version of the protocol the request was built with.
         *
         * A master instance which does not know the version refuses the
         * request, so a later layout may add fields without being read as if it
         * were the current one.
         */
        int version = 0;

        /**
         * @brief Identifier of the sandbox of the client, in canonical form.
         *
         * The master instance compares it with its own: a value which differs
         * names another sandbox, which is never joined.
         */
        std::string sandbox_id;

        /**
         * @brief Host directory which carries the sandbox of the client.
         *
         * Two copies of one archive carry the same identifier and different
         * roots, so the root tells the master instance whether the client is a
         * copy of its own sandbox or another one.
         */
        std::string sandbox_root;

        /**
         * @brief Programs the request selects, in UTF-8.
         *
         * `auto` starts every auto start file of the configuration of the
         * master instance, `trigger:<name>` starts the file of that trigger and
         * `shell` starts the shell of the host with `args` as its command.
         */
        std::string selection = "auto";

        /**
         * @brief Arguments the started programs receive, in UTF-8.
         *
         * The arguments are appended to the arguments of the startup file the
         * selection names; a shell runs them as its command instead.
         */
        std::vector<std::string> args;

        /**
         * @brief Environment of the client, as key and value pairs.
         *
         * The block is handed to the created process, so a program of a second
         * instance sees the environment of the instance which asked for it.
         * The entries which start with `=` are not sent, because they describe
         * the current directory of a drive and are no variables.
         */
        std::vector<EnvironmentEntry> environment;

        /**
         * @brief Whether the environment of the request is composed already.
         *
         * A client which ran inside a sandbox saw the environment of that
         * sandbox, which was composed once, and the master instance told the
         * sandbox of the created process not to compose it a second time. The
         * injection module of the sandbox leaves every launcher alone now, so a
         * client always runs on the host and the member is always false; it is
         * kept because the sandbox protocol of the injection data carries it.
         */
        bool environment_is_composed = false;

        /**
         * @brief Current directory the client saw, in UTF-8.
         *
         * The value is a path of the view inside the sandbox and a path of the
         * host outside of it. An empty value asks the created process to inherit
         * the current directory of the master instance.
         */
        std::string cwd;

        NLOHMANN_DEFINE_TYPE_INTRUSIVE(Req, version, sandbox_id, sandbox_root, selection, args, environment,
                                       environment_is_composed, cwd)
    };

    /**
     * @brief Answer of the master instance.
     */
    struct Rsp
    {
        /**
         * @brief Outcome of the request: `started`, `refused` or `shutting_down`.
         */
        std::string status = launcher_start_status::kRefused;

        /**
         * @brief Identifier of the sandbox of the master instance.
         *
         * The client compares it with its own, so an answer of a foreign server
         * which happens to listen on the same pipe name is recognized.
         */
        std::string sandbox_id;

        /**
         * @brief Process ids of the programs the request created.
         *
         * The list covers every process of the request and is only valid while
         * the status is `started`.
         */
        std::vector<std::uint32_t> pids;

        /**
         * @brief Stable identifier of the reason of a refusal, see
         *        `launcher_start_error`.
         */
        std::string error_code;

        /**
         * @brief Human readable description of the reason of a refusal.
         */
        std::string error_message;

        /**
         * @brief Win32 code of a creation which failed.
         *
         * A request which is refused with `start_failed` carries the code the
         * process creation reported, because the client reports it as its own
         * exit code. Every other refusal carries zero.
         */
        int win32_error = 0;

        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Rsp, status, sandbox_id, pids, error_code, error_message,
                                                    win32_error)
    };
};

/**
 * @brief Kind of the programs a start request selects.
 */
enum class LauncherStartSelectionKind
{
    Auto,    /*!< Every auto start file of the configuration. */
    Trigger, /*!< The startup file which carries one trigger. */
    Shell,   /*!< The shell of the host, with the arguments of the request. */
};

/**
 * @brief Selection of the startup files of a start request.
 */
struct LauncherStartSelection
{
    LauncherStartSelectionKind kind = LauncherStartSelectionKind::Auto; /*!< Kind of the selection. */
    std::string                trigger; /*!< Trigger, empty unless the kind is Trigger. */
};

/**
 * @brief Build the selection of a request from the options of a run.
 *
 * The selection of a second instance is the selection of its own command line:
 * a run which asks for a trigger hands that trigger over, a run of the shell
 * asks for the shell and every other run asks for the auto start files of the
 * master instance.
 *
 * @param[in] shell Whether the run asks for the shell of the host.
 * @param[in] has_trigger Whether the run names a trigger.
 * @param[in] trigger Trigger the run names, ignored unless \p has_trigger is
 *                    set.
 * @return The selection, in the form `auto`, `trigger:<name>` or `shell`.
 */
inline std::string MakeLauncherStartSelection(bool shell, bool has_trigger, const std::string& trigger)
{
    if (shell)
    {
        return "shell";
    }

    if (has_trigger)
    {
        return "trigger:" + trigger;
    }

    return "auto";
}

/**
 * @brief Read the selection of a request.
 *
 * @param[in] text Selection of the request, in UTF-8.
 * @param[out] selection The selection to fill, only set when the call succeeds.
 * @return true when the text names a selection, false when it does not.
 */
inline bool ParseLauncherStartSelection(const std::string& text, LauncherStartSelection& selection)
{
    if (text == "auto")
    {
        selection.kind = LauncherStartSelectionKind::Auto;
        selection.trigger.clear();
        return true;
    }

    if (text == "shell")
    {
        selection.kind = LauncherStartSelectionKind::Shell;
        selection.trigger.clear();
        return true;
    }

    const std::string prefix = "trigger:";
    if (text.size() > prefix.size() && text.compare(0, prefix.size(), prefix) == 0)
    {
        selection.kind = LauncherStartSelectionKind::Trigger;
        selection.trigger = text.substr(prefix.size());
        return true;
    }

    return false;
}

/**
 * @brief Validate the fields of a start request.
 *
 * The call checks what a master instance may check before it queues a request:
 * the version of the protocol and the limits of every field. It is a pure
 * function, so a unit test covers the validation without a pipe, a process or a
 * sandbox.
 *
 * The limits are checked before the content, so a request which carries a field
 * of an unusable size is reported as too large instead of being reported as
 * malformed because the oversized value happens to carry no selection.
 *
 * @param[in] request Request to validate.
 * @param[out] error_code Stable identifier of the reason, see
 *                        `launcher_start_error`; empty on success.
 * @param[out] error_message Human readable description, empty on success.
 * @return true when the request may be queued.
 */
inline bool ValidateLauncherStartRequest(const MsgLauncherStart::Req& request, std::string& error_code,
                                         std::string& error_message)
{
    error_code.clear();
    error_message.clear();

    if (request.version != kLauncherStartVersion)
    {
        error_code = launcher_start_error::kVersionUnsupported;
        error_message = "the version " + std::to_string(request.version) + " of the request is not supported";
        return false;
    }

    std::size_t args_size = 0;
    for (const auto& arg : request.args)
    {
        args_size += arg.size();
    }

    std::size_t environment_size = 0;
    for (const auto& entry : request.environment)
    {
        environment_size += entry.key.size() + entry.value.size();
    }

    if (request.selection.size() > launcher_start_limits::kMaxSelectionSize)
    {
        error_code = launcher_start_error::kPayloadTooLarge;
        error_message = "the selection of the request is too large";
        return false;
    }

    if (request.sandbox_root.size() > launcher_start_limits::kMaxSandboxRootSize)
    {
        error_code = launcher_start_error::kPayloadTooLarge;
        error_message = "the sandbox root of the request is too large";
        return false;
    }

    if (request.cwd.size() > launcher_start_limits::kMaxCwdSize)
    {
        error_code = launcher_start_error::kPayloadTooLarge;
        error_message = "the current directory of the request is too large";
        return false;
    }

    if (args_size > launcher_start_limits::kMaxArgsSize)
    {
        error_code = launcher_start_error::kPayloadTooLarge;
        error_message = "the arguments of the request are too large";
        return false;
    }

    if (environment_size > launcher_start_limits::kMaxEnvironmentSize)
    {
        error_code = launcher_start_error::kPayloadTooLarge;
        error_message = "the environment of the request is too large";
        return false;
    }

    const std::string encoded = nlohmann::json(request).dump();
    if (encoded.size() > launcher_start_limits::kMaxRequestSize)
    {
        error_code = launcher_start_error::kPayloadTooLarge;
        error_message = "the request is too large";
        return false;
    }

    if (request.sandbox_id.empty())
    {
        error_code = launcher_start_error::kMalformedRequest;
        error_message = "the request carries no sandbox identifier";
        return false;
    }

    if (request.sandbox_root.empty())
    {
        error_code = launcher_start_error::kMalformedRequest;
        error_message = "the request carries no sandbox root";
        return false;
    }

    LauncherStartSelection selection;
    if (!ParseLauncherStartSelection(request.selection, selection))
    {
        error_code = launcher_start_error::kMalformedRequest;
        error_message = "the selection '" + request.selection + "' of the request is not known";
        return false;
    }

    return true;
}

} // namespace appbox

#endif // APPBOX_LAUNCHER_MSG_LAUNCHERSTART_HPP
