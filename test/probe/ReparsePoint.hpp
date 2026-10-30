#ifndef APPBOX_TEST_PROBE_REPARSE_POINT_HPP
#define APPBOX_TEST_PROBE_REPARSE_POINT_HPP

#include "WinAPI.h" /* Must be first include file */
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace appbox::test
{

struct ProtocolReparsePoint
{
    static constexpr const char* Method = "ReparsePoint";

    struct Req
    {
        struct Item
        {
            /**
             * @brief Action of the item.
             *
             * - `Unknown` - the probe does not know the action and answers
             *   `STATUS_INVALID_PARAMETER`. The reader falls back to it for a
             *   token it does not know and it is the default of the member, so a
             *   request without an action fails instead of acting on the path in
             *   a way the case did not ask for.
             * - `Junction` - create a junction with `FSCTL_SET_REPARSE_POINT`.
             * - `Symlink` - create a symbolic link with `CreateSymbolicLinkW`.
             * - `SetTag` - create a link whose tag the item selects.
             * - `Remove` - remove the data of a link with
             *   `FSCTL_DELETE_REPARSE_POINT`.
             * - `Read` - read the data of a link with `FSCTL_GET_REPARSE_POINT`.
             * - `Attributes` - read the attributes with `GetFileAttributesW`.
             * - `FullAttributes` - read the attributes together with the
             *   reparse point with `GetFileAttributesExW`.
             * - `ReadText` - read the content of the file a link names.
             * - `RemoveDir` - remove a directory with `RemoveDirectoryW`.
             */
            enum class Action
            {
                Unknown,        ///< An action the probe does not know.
                Junction,       ///< Create a junction.
                Symlink,        ///< Create a symbolic link.
                SetTag,         ///< Create a link whose tag the item selects.
                Remove,         ///< Remove the data of a link.
                Read,           ///< Read the data of a link.
                Attributes,     ///< Read the attributes of the entry.
                FullAttributes, ///< Read the attributes and the reparse point of the entry.
                ReadText,       ///< Read the content of the file a link names.
                RemoveDir       ///< Remove a directory.
            };

            /** Action of the item, see Action. */
            Action action = Action::Unknown;

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
 * @brief The tokens of Action, which the protocol carries as text.
 *
 * The first entry carries no token: the reader falls back to it for a token it
 * does not know and the writer spells it as `null`, so a token which the probe
 * does not know is never taken for an action of the path.
 */
NLOHMANN_JSON_SERIALIZE_ENUM(ProtocolReparsePoint::Req::Item::Action,
                             {
                                 { ProtocolReparsePoint::Req::Item::Action::Unknown,        nullptr           },
                                 { ProtocolReparsePoint::Req::Item::Action::Junction,       "junction"        },
                                 { ProtocolReparsePoint::Req::Item::Action::Symlink,        "symlink"         },
                                 { ProtocolReparsePoint::Req::Item::Action::SetTag,         "set_tag"         },
                                 { ProtocolReparsePoint::Req::Item::Action::Remove,         "remove"          },
                                 { ProtocolReparsePoint::Req::Item::Action::Read,           "read"            },
                                 { ProtocolReparsePoint::Req::Item::Action::Attributes,     "attributes"      },
                                 { ProtocolReparsePoint::Req::Item::Action::FullAttributes, "full_attributes" },
                                 { ProtocolReparsePoint::Req::Item::Action::ReadText,       "read_text"       },
                                 { ProtocolReparsePoint::Req::Item::Action::RemoveDir,      "remove_dir"      },
})

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
 *
 * @param[in] request Request of the probe.
 * @return The answer of the probe.
 */
ProtocolReparsePoint::Rsp ProbeReparsePoint_Entry(const ProtocolReparsePoint::Req& request);

} // namespace appbox::test

#endif // APPBOX_TEST_PROBE_REPARSE_POINT_HPP
