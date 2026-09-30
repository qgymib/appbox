#ifndef APPBOX_TEST_PROBE_FONTS_HPP
#define APPBOX_TEST_PROBE_FONTS_HPP

#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include "probe/__init__.hpp"
#include <cstddef>
#include <nlohmann/json.hpp>
#include <string>

namespace appbox::test
{

struct ProtocolFonts
{
    struct Req
    {
        std::string family;        /* Family name the case expects to be usable. Encoding in UTF-8. */
        std::string absent_family; /* Family name the case expects not to be carried, empty when it has none. */
        std::string view_path;     /* View path of a font file of the case. Encoding in UTF-8. */

        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Req, family, absent_family, view_path)
    };

    struct Rsp
    {
        bool        enumerated = false;        /* Whether the font table of this process carries the family. */
        bool        absent_enumerated = false; /* Whether it carries the family which must be absent. */
        std::string face;                      /* Face name a font which is created for the family reports. */
        bool        created = false;           /* Whether the created font is the face of the family. */
        int         add_result = 0;            /* Return value of AddFontResourceExW(view_path, FR_PRIVATE, 0). */
        bool        file_read = false;         /* Whether the file of the view could be read. */
        std::size_t file_size = 0;             /* Size of the file of the view. */

        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Rsp, enumerated, absent_enumerated, face, created, add_result,
                                                    file_read, file_size)
    };
};

/**
 * @brief Font probe: the font table of the sandboxed process and the font file of the view.
 */
extern Probe ProbeFonts;

} // namespace appbox::test

#endif // APPBOX_TEST_PROBE_FONTS_HPP
