#ifndef APPBOX_TEST_PROBE_SET_INFORMATION_FILE_HPP
#define APPBOX_TEST_PROBE_SET_INFORMATION_FILE_HPP

#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include "probe/__init__.hpp"
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace appbox::test
{

struct ProtocolSetInformationFile
{
    struct Req
    {
        struct Item
        {
            /** Class of the call: `rename`, `rename_ex`, `link` or `link_ex`. */
            std::string action;

            /** View path of the object which is moved or linked. */
            std::string source;

            /** Name the object is moved to or linked at. */
            std::string target;

            /** Directory the name is relative to, empty when it is a full path. */
            std::string rootDirectory;

            /**
             * Access the source is opened with: `delete`, which a rename needs
             * and which copies an entry of a read only layer into the overlay,
             * or `read`, which a link is content with.
             */
            std::string access = "delete";

            /** Whether an entry which exists is replaced. */
            bool replaceIfExists = false;

            NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Item, action, source, target, rootDirectory, access,
                                                        replaceIfExists)
        };

        std::vector<Item> items;

        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Req, items)
    };

    struct Item
    {
        /** NTSTATUS of the call. */
        long status = 0;

        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Item, status)
    };

    struct Rsp
    {
        /** One entry per request item, in order. */
        std::vector<Item> items;

        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Rsp, items)
    };
};

/**
 * @brief `NtSetInformationFile` probe.
 *
 * The probe opens the object of an item for deletion, which is the access a
 * rename and a link need, and calls the entry point with the class of the
 * item. Every item of one request is asked by the same process, which keeps
 * the number of probe calls of a case low.
 */
extern Probe ProbeSetInformationFile;

} // namespace appbox::test

#endif // APPBOX_TEST_PROBE_SET_INFORMATION_FILE_HPP
