#ifndef APPBOX_PACKER_CORE_EMBEDDED_RESOURCE_IDS_H
#define APPBOX_PACKER_CORE_EMBEDDED_RESOURCE_IDS_H

/*
 * Identifiers of the payloads the packer carries as RCDATA resources of its own
 * executable: the launcher program it writes into a standalone archive and the
 * two sandbox injection modules it writes into the resource root of that
 * archive (see `common/SandboxLayout.hpp`).
 *
 * The header is included by the resource script which declares the payloads and
 * by the code which reads them back (see `src/core/EmbeddedResource.hpp`), so
 * both sides always agree on the identifiers. They are macros instead of
 * constants because the resource compiler is not a C++ compiler.
 *
 * The payloads themselves are named by `cmake/EmbeddedResources.rc.in`, which
 * CMake turns into the resource script of the executable.
 */

/** @brief The launcher program, the payload of a standalone archive. */
#define IDR_APPBOX_LAUNCHER 101

/** @brief The 32 bit sandbox injection module below `app`. */
#define IDR_APPBOX_SANDBOX32 102

/** @brief The 64 bit sandbox injection module below `app`. */
#define IDR_APPBOX_SANDBOX64 103

#endif // APPBOX_PACKER_CORE_EMBEDDED_RESOURCE_IDS_H
