#ifndef APPBOX_TEST_PROBE_REPARSE_POINT_HPP
#define APPBOX_TEST_PROBE_REPARSE_POINT_HPP

#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include "probe/__init__.hpp"
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace appbox::test
{

struct ProtocolReparsePoint
{
    struct Req
    {
        struct Item
        {
            /**
             * Action of the item: `junction`, `symlink`, `set_tag`, `remove`,
             * `read`, `attributes`, `full_attributes`, `read_text` or
             * `remove_dir`.
             */
            std::string action;

            /** View path the action acts on. */
            std::string path;

            /** Target of the link a `junction` or a `symlink` creates. */
            std::string target;

            /** Tag a `set_tag` writes. */
            ULONG tag = 0;

            /**
             * Whether the target of a symbolic link is relative to the
             * directory of the link.
             */
            bool relative = false;

            /** Whether a `junction` or a `symlink` links a folder. */
            bool directory = true;

            NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Item, action, path, target, tag, relative, directory)
        };

        std::vector<Item> items;

        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Req, items)
    };

    struct Item
    {
        /** Status of the entry point the action called. */
        long status = 0;

        /** Error of the last call which failed, zero when it succeeded. */
        DWORD code = 0;

        /** Attributes of the entry, `INVALID_FILE_ATTRIBUTES` when unknown. */
        DWORD attributes = INVALID_FILE_ATTRIBUTES;

        /** Tag of the reparse point. */
        ULONG tag = 0;

        /** Name the file system follows. */
        std::string substitute;

        /** Name the shell shows. */
        std::string print;

        /** Content a `read_text` read. */
        std::string text;

        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Item, status, code, attributes, tag, substitute, print, text)
    };

    struct Rsp
    {
        /** One entry per request item, in order. */
        std::vector<Item> items;

        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Rsp, items)
    };
};

/**
 * @brief Reparse point probe.
 *
 * The probe acts on a path of the view with the entry points the sandbox hooks:
 * it creates a junction with `FSCTL_SET_REPARSE_POINT`, creates a symbolic link
 * with `CreateSymbolicLinkW`, removes the data of a link with
 * `FSCTL_DELETE_REPARSE_POINT` and reads it back with
 * `FSCTL_GET_REPARSE_POINT`. It reports the status of the entry point, the
 * attributes of the entry and the two names the data of a link carries, so a
 * case can pin what the view stored and what it reports.
 */
extern Probe ProbeReparsePoint;

} // namespace appbox::test

#endif // APPBOX_TEST_PROBE_REPARSE_POINT_HPP
