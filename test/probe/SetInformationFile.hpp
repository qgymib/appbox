#ifndef APPBOX_TEST_PROBE_SET_INFORMATION_FILE_HPP
#define APPBOX_TEST_PROBE_SET_INFORMATION_FILE_HPP

#include "WinAPI.h" /* Must be first include file */
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
            /**
             * @brief Class of the call.
             *
             * - `Unknown` - the probe does not know the class and answers
             *   `STATUS_PROCEDURE_NOT_FOUND`. The reader falls back to it for a
             *   token it does not know and it is the default of the member, so a
             *   request without a class fails instead of moving the object in a
             *   way the case did not ask for.
             * - `Rename` - `FileRenameInformation`.
             * - `RenameEx` - `FileRenameInformationEx`.
             * - `Link` - `FileLinkInformation`.
             * - `LinkEx` - `FileLinkInformationEx`.
             */
            enum class Action
            {
                Unknown,  ///< A class the probe does not know.
                Rename,   ///< `FileRenameInformation`.
                RenameEx, ///< `FileRenameInformationEx`.
                Link,     ///< `FileLinkInformation`.
                LinkEx    ///< `FileLinkInformationEx`.
            };

            /**
             * @brief Access the source is opened with.
             *
             * - `Delete` - `DELETE | SYNCHRONIZE`, which a rename needs and
             *   which copies an entry of a read only layer into the overlay.
             * - `Read` - `GENERIC_READ | SYNCHRONIZE`, which a link is content
             *   with and which leaves an entry of a read only layer in place.
             */
            enum class Access
            {
                Delete, ///< `DELETE | SYNCHRONIZE`, the default.
                Read    ///< `GENERIC_READ | SYNCHRONIZE`.
            };

            /** Class of the call, see Action. */
            Action action = Action::Unknown;

            /** View path of the object which is moved or linked. */
            std::string source;

            /** Name the object is moved to or linked at. */
            std::string target;

            /** Directory the name is relative to, empty when it is a full path. */
            std::string rootDirectory;

            /** Access the source is opened with, see Access. */
            Access access = Access::Delete;

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
 * @brief The tokens of Action, which the protocol carries as text.
 *
 * The first entry carries no token: the reader falls back to it for a token it
 * does not know and the writer spells it as `null`, so a token which the probe
 * does not know is never taken for a class of the call.
 */
NLOHMANN_JSON_SERIALIZE_ENUM(ProtocolSetInformationFile::Req::Item::Action,
                             {
                                 { ProtocolSetInformationFile::Req::Item::Action::Unknown,  nullptr     },
                                 { ProtocolSetInformationFile::Req::Item::Action::Rename,   "rename"    },
                                 { ProtocolSetInformationFile::Req::Item::Action::RenameEx, "rename_ex" },
                                 { ProtocolSetInformationFile::Req::Item::Action::Link,     "link"      },
                                 { ProtocolSetInformationFile::Req::Item::Action::LinkEx,   "link_ex"   },
})

/**
 * @brief The tokens of Access, which the protocol carries as text.
 *
 * The first entry is the value the reader falls back to for a token it does not
 * know, so an unknown token opens the source with the access a rename needs.
 */
NLOHMANN_JSON_SERIALIZE_ENUM(ProtocolSetInformationFile::Req::Item::Access,
                             {
                                 { ProtocolSetInformationFile::Req::Item::Access::Delete, "delete" },
                                 { ProtocolSetInformationFile::Req::Item::Access::Read,   "read"   },
})

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
