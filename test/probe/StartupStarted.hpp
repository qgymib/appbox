#ifndef APPBOX_TEST_PROBE_STARTUP_STARTED_HPP
#define APPBOX_TEST_PROBE_STARTUP_STARTED_HPP

#include "WinAPI.h" /* Must be first include file */
#include "probe/__init__.hpp"
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace appbox::test
{

struct ProtocolStartupStarted
{
    /**
     * @brief One operation the process runs with a name of its own directory.
     */
    struct Operation
    {
        /**
         * @brief Kind of the operation: `read`, `write` or `delete`.
         */
        std::string op;

        /** @brief Name of the file, relative to the current directory of the process. */
        std::string name;

        /** @brief Content of a write, unused by the other kinds. */
        std::string data;

        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Operation, op, name, data)
    };

    /**
     * @brief Task data of the probe.
     *
     * The data is empty for a report of the process alone. A case which checks
     * the directory of the process names the operations it runs with a name of
     * that directory: every operation is resolved against the current directory
     * of the process, which is what the view has to answer.
     */
    struct Req
    {
        /** @brief Operations to run with a relative name, in the order of the list. */
        std::vector<Operation> relative_ops;

        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Req, relative_ops)
    };

    /**
     * @brief Outcome of one operation of the task data.
     */
    struct OperationResult
    {
        /** @brief Error code of the operation, zero on success. */
        DWORD code = 0;

        /** @brief Content the operation read, empty for the other kinds. */
        std::string data;

        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(OperationResult, code, data)
    };

    struct Rsp
    {
        std::string marker; /* Marker of the startup file which started this process. */

        /**
         * @brief Process id of the process the launcher created.
         *
         * The id lets a case tell the processes of one run apart when the
         * launcher starts several startup files.
         */
        std::uint32_t pid = 0;

        /**
         * @brief Process id of the process which created this one.
         *
         * The launcher creates the startup file itself, so the id is the one of
         * the launcher of the run; a run which starts a process of its own in
         * between reports the id of that process instead.
         */
        std::uint32_t parent_pid = 0;

        /**
         * @brief Path of the image this process runs, in UTF-8.
         *
         * The sandbox starts a program from the host layer which holds it, so
         * the path names the layer and not the path of the view the
         * configuration spells.
         */
        std::string module_path;

        /**
         * @brief Command line of this process, in UTF-8.
         *
         * A case which checks that the arguments of one request do not travel
         * into the programs of another request compares the command line of
         * every process with the request it belongs to.
         */
        std::string command_line;

        /**
         * @brief Entries of the environment of this process, in UTF-8.
         *
         * Every entry is a `name=value` text, in the order of the block of the
         * process. The entries are what a case reads to pin the environment a
         * request passed on: an entry the sandbox composed twice carries the
         * value it joined twice.
         */
        std::vector<std::string> environment;

        /**
         * @brief Current directory this process works in, in UTF-8.
         *
         * The sandbox rewrites the directory of a process which was created in
         * a directory of a layer, so the report names the path of the view the
         * application knows instead of the directory of the layer.
         */
        std::string cwd;

        /**
         * @brief Outcomes of the operations of the task data, in the order of
         *        the list.
         */
        std::vector<OperationResult> relative_ops;

        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Rsp, marker, pid, parent_pid, module_path, command_line,
                                                    environment, cwd, relative_ops)
    };
};

/**
 * @brief Probe which reports the startup file which started this process.
 *
 * The end-to-end cases of the launcher put a marker into the arguments of every
 * startup file, so the probe tells which of the startup files of a
 * configuration the launcher really started. The report carries the process
 * chain, the image and the current directory of the process as well, which is
 * what pins that the launcher created the process itself, that it created it
 * from the layer which holds the image and that the process works in the
 * directory of the view.
 */
extern Probe ProbeStartupStarted;

} // namespace appbox::test

#endif // APPBOX_TEST_PROBE_STARTUP_STARTED_HPP
