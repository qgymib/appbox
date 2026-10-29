#ifndef APPBOX_TEST_PROBE_JOB_HPP
#define APPBOX_TEST_PROBE_JOB_HPP

#include "WinAPI.h" /* Must be first include file */
#include "probe/__init__.hpp"
#include <cstdint>
#include <nlohmann/json.hpp>

namespace appbox::test
{

/**
 * @brief Report of the job object this process is a member of.
 */
struct ProtocolJob
{
    struct Rsp
    {
        /** True when this process is a member of a job object. */
        bool in_job = false;

        /**
         * @brief Flags of the limits of the job.
         *
         * The flags tell whether the job kills its processes when its last
         * handle closes and whether a process of the job may break away from
         * it, which is what a case pins.
         */
        std::uint32_t limit_flags = 0;

        /** Number of processes the job holds. */
        std::uint32_t active_processes = 0;

        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Rsp, in_job, limit_flags, active_processes)
    };
};

/**
 * @brief Probe which reports the job object of this process.
 *
 * The launcher creates every process of a run as a member of a job object, so
 * the process which runs this probe reports the limits of the job the launcher
 * created and the number of processes it holds.
 */
extern Probe ProbeJob;

} // namespace appbox::test

#endif // APPBOX_TEST_PROBE_JOB_HPP
