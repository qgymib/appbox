#ifndef APPBOX_TEST_PROBE_QUERY_INFORMATION_FILE_HPP
#define APPBOX_TEST_PROBE_QUERY_INFORMATION_FILE_HPP

#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include "probe/__init__.hpp"
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace appbox::test
{

struct ProtocolQueryInformationFile
{
    struct Req
    {
        struct Item
        {
            /** View path of the file to open. */
            std::string path;

            /** Access of the open: `read` or `write`. */
            std::string mode;

            /**
             * Size of the buffer the name classes are asked with, zero asks
             * with the default buffer of the probe.
             */
            unsigned long length = 0;

            NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Item, path, mode, length)
        };

        std::vector<Item> items;

        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Req, items)
    };

    struct Item
    {
        /** NTSTATUS of the open of the file. */
        long status = 0;

        /** NTSTATUS of `FileNameInformation`. */
        long nameStatus = 0;

        /** NTSTATUS of `FileNormalizedNameInformation`. */
        long normalizedStatus = 0;

        /** NTSTATUS of `FileAllInformation`. */
        long allStatus = 0;

        /** Name `FileNameInformation` reported, in UTF-8. */
        std::string name;

        /** Name `FileNormalizedNameInformation` reported, in UTF-8. */
        std::string normalized;

        /** Name `FileAllInformation` reported, in UTF-8. */
        std::string all;

        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Item, status, nameStatus, normalizedStatus, allStatus, name,
                                                    normalized, all)
    };

    struct Rsp
    {
        /** One entry per request item, in order. */
        std::vector<Item> items;

        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Rsp, items)
    };
};

/**
 * @brief `NtQueryInformationFile` probe.
 *
 * The probe opens the file of an item with the access of the item and asks the
 * three information classes which report a name, so a case pins what the
 * sandbox reports for a handle of every layer of the view.
 */
extern Probe ProbeQueryInformationFile;

} // namespace appbox::test

#endif // APPBOX_TEST_PROBE_QUERY_INFORMATION_FILE_HPP
