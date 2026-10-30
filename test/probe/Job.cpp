#include "WinAPI.h" /* Must be first include file */
#include <cstdint>
#include "Job.hpp"

/**
 * @brief Report the job object this process is a member of.
 *
 * The query of the job of the calling process is named by a null handle, which
 * is the job the process itself is a member of: the launcher creates every
 * process of a run as a member of a job, so the report describes that job.
 *
 * @param[in] request Request of the case, which the report does not read.
 * @return The report of the job.
 */
appbox::test::ProtocolJob::Rsp appbox::test::ProbeJob_Entry(const appbox::test::ProtocolJob::Req& /*request*/)
{
    appbox::test::ProtocolJob::Rsp rsp;

    BOOL in_job = FALSE;
    if (IsProcessInJob(GetCurrentProcess(), nullptr, &in_job))
    {
        rsp.in_job = in_job != FALSE;
    }

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits;
    ZeroMemory(&limits, sizeof(limits));
    if (QueryInformationJobObject(nullptr, JobObjectExtendedLimitInformation, &limits, sizeof(limits), nullptr))
    {
        rsp.limit_flags = limits.BasicLimitInformation.LimitFlags;
    }

    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting;
    ZeroMemory(&accounting, sizeof(accounting));
    if (QueryInformationJobObject(nullptr, JobObjectBasicAccountingInformation, &accounting, sizeof(accounting),
                                  nullptr))
    {
        rsp.active_processes = accounting.ActiveProcesses;
    }

    return rsp;
}
