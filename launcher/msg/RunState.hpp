#ifndef APPBOX_LAUNCHER_MSG_RUNSTATE_HPP
#define APPBOX_LAUNCHER_MSG_RUNSTATE_HPP

#include <cstdint>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace appbox
{

/**
 * @brief Protocol version of the state query of a running sandbox.
 *
 * The version is carried by every request, so a master instance which does not
 * know the version of a query refuses it instead of reading the fields of a
 * newer layout as if they were its own.
 */
constexpr int kRunStateVersion = 1;

/**
 * @brief Stable identifiers of the reasons a state query is refused.
 *
 * The identifiers are part of the wire format, so a client tells a query which
 * was refused because of its own version from a query which was refused
 * because the client is not trusted.
 */
namespace run_state_error
{
constexpr const char* kVersionUnsupported = "version_unsupported";
constexpr const char* kUnauthorized = "unauthorized";
constexpr const char* kIntegrityMismatch = "integrity_mismatch";
} // namespace run_state_error

/**
 * @brief Message which reports the state of a run of the launcher.
 *
 * The message is a read-only query of the master instance of a sandbox: it
 * describes what the run prepared, what it started and what it did with the
 * requests of the other launchers, so a client observes the run itself instead
 * of the log files it happens to write. The log of a run is a diagnostic
 * artifact and never part of this answer.
 *
 * The answer carries the facts a caller can otherwise only infer from a log:
 * the stage of the run, whether the startup screen of the session was created
 * and closed, the size of the image the screen decoded, the folder a
 * self-extracting program extracted its payload into, the requests the run
 * queued and handled, the processes a rollback terminated, and every program of
 * the run with its origin, its request and its exit code.
 *
 * A refused query answers with an `error_code` and no state, like the start
 * request of a second instance answers a refusal with a status: a caller which
 * reads the state of a refusal would take the zero of every member for a fact
 * of the run.
 */
struct MsgRunState
{
    static constexpr const char* Method = "RunState";

    /**
     * @brief Request of a state query.
     */
    struct Req
    {
        /**
         * @brief Version of the protocol the query was built with.
         *
         * A master instance which does not know the version refuses the query,
         * so a later layout may add fields without being read as if it were
         * the current one.
         */
        int version = 0;

        NLOHMANN_DEFINE_TYPE_INTRUSIVE(Req, version)
    };

    /**
     * @brief One process the run created.
     *
     * The entry describes a process the control layer accounts for and not a
     * line of a log: the process id, the origin of the job, the request which
     * asked for it, whether it exited and with which code, and whether the
     * rollback of its request terminated it.
     */
    struct Program
    {
        /** Process id of the created process, zero while no process was created. */
        std::uint32_t pid = 0;

        /** Whether the program is one of the run's own configuration. */
        bool initial = false;

        /** Request the program belongs to, zero for a program of the run itself. */
        std::uint64_t request_id = 0;

        /** Whether the job reported that its processes exited. */
        bool exited = false;

        /** Whether the rollback of its request terminated the program. */
        bool terminated = false;

        /** Exit code of the program, meaningful only while `exited` is set. */
        std::uint32_t exit_code = 0;

        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Program, pid, initial, request_id, exited, terminated, exit_code)
    };

    /**
     * @brief Answer of the master instance.
     *
     * Every member describes the run at the moment of the query. A member which
     * reports a stage of the past (the screen of the session was shown and
     * closed again) is a fact the run recorded and not a state which may still
     * change, so a caller needs no second query and no wait to read it.
     */
    struct Rsp
    {
        /** Stable identifier of the reason the query is refused, empty for a state. */
        std::string error_code;

        /** Human readable description of the reason of a refusal. */
        std::string error_message;

        /** Identifier of the sandbox of the master instance. */
        std::string sandbox_id;

        /** Name of the last stage the run reported, see `StartupStageName()`. */
        std::string stage;

        /** Whether the runtime of the sandbox exists, which the requests need. */
        bool prepared = false;

        /** Whether a startup screen was created for the session. */
        bool screen_shown = false;

        /** Whether the startup screen of the run was closed again. */
        bool screen_closed = false;

        /** Width of the startup image the screen decoded, zero while none was decoded. */
        std::uint32_t image_width = 0;

        /** Height of the startup image the screen decoded, zero while none was decoded. */
        std::uint32_t image_height = 0;

        /** Reason the startup image could not be decoded, empty on success. */
        std::string image_error;

        /** Host folder a self-extracting run extracted its payload into, empty otherwise. */
        std::string extraction_root;

        /** Number of requests which waited for the control thread. */
        std::uint64_t queued_requests = 0;

        /** Number of requests which arrived before the sandbox was prepared. */
        std::uint64_t queued_before_prepared = 0;

        /** Number of requests whose programs the run created. */
        std::uint64_t handled_requests = 0;

        /** Number of processes a rollback of a request terminated. */
        std::uint64_t rolled_back_processes = 0;

        /** Every program of the run, in the order the run created them. */
        std::vector<Program> programs;

        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Rsp, error_code, error_message, sandbox_id, stage, prepared,
                                                    screen_shown, screen_closed, image_width, image_height, image_error,
                                                    extraction_root, queued_requests, queued_before_prepared,
                                                    handled_requests, rolled_back_processes, programs)
    };
};

} // namespace appbox

#endif // APPBOX_LAUNCHER_MSG_RUNSTATE_HPP
