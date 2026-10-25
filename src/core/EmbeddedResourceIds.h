#ifndef APPBOX_PACKER_CORE_EMBEDDED_RESOURCE_IDS_H
#define APPBOX_PACKER_CORE_EMBEDDED_RESOURCE_IDS_H

/*
 * Identifiers of the payloads the packer carries as RCDATA resources of its own
 * executable: the launcher program it writes into a standalone archive.
 *
 * Every executable numbers the resources it carries on its own, starting at 101:
 * a resource id is only meaningful for the module which declares it, so the
 * launcher declares its two sandbox injection modules in
 * `launcher/EmbeddedResourceIds.h` and may use a number this header uses for a
 * different payload.
 *
 * The header is included by the resource script which declares the payload and
 * by the code which reads it back (see `common/EmbeddedResource.hpp`), so both
 * sides always agree on the identifier. It is a macro instead of a constant
 * because the resource compiler is not a C++ compiler.
 *
 * The payload itself is named by `cmake/EmbeddedResources.rc.in`, which CMake
 * turns into a resource script of the target (see `src/resource.rc`).
 */

/** @brief The launcher program the packer writes into a standalone archive. */
#define IDR_APPBOX_LAUNCHER 101

#endif // APPBOX_PACKER_CORE_EMBEDDED_RESOURCE_IDS_H
