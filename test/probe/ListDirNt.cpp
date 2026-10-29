#include "WinAPI.h" /* Must be first include file */
#include "WString.hpp"
#include "utils/NtDirectoryQuery.hpp"
#include "ListDirNt.hpp"

using namespace appbox::test;

/**
 * @brief Enumerate a directory through the NT entry points.
 *
 * The probe is the thin adapter between the request of a case and the shared
 * enumeration of `utils/NtDirectoryQuery.hpp`: the probe runs inside the
 * sandbox, so the enumeration it asks for reaches the merged view of the
 * layers. A case which needs the answer of the file system itself calls the
 * shared function in its own process instead.
 */
static nlohmann::json ProbeListDirNt_Entry(const nlohmann::json& data)
{
    const auto req = data.get<ProtocolListDirNt::Req>();

    NtDirectoryQuery query;
    query.path = appbox::UTF8ToWide(req.path);
    query.info_class = req.info_class;
    query.extended = req.extended;
    query.create_file = req.create_file;
    query.duplicate = req.duplicate;
    query.remove_before_query = req.remove_before_query;

    const auto result = QueryNtDirectory(query);

    ProtocolListDirNt::Rsp rsp;
    rsp.status = result.status;
    rsp.names = result.names;
    return rsp;
}

appbox::test::Probe appbox::test::ProbeListDirNt("ListDirNt", ProbeListDirNt_Entry);
