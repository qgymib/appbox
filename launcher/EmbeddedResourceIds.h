#ifndef APPBOX_LAUNCHER_EMBEDDED_RESOURCE_IDS_H
#define APPBOX_LAUNCHER_EMBEDDED_RESOURCE_IDS_H

/*
 * Identifiers of the payloads the launcher carries as RCDATA resources of its
 * own executable: the two sandbox injection modules it releases into the cache
 * directory of the sandbox at every start (see `common/SandboxLayout.hpp`).
 *
 * Every executable numbers the resources it carries on its own, starting at 101:
 * a resource id is only meaningful for the module which declares it, so the
 * packer declares the launcher program it carries in
 * `src/core/EmbeddedResourceIds.h` and may use a number this header uses for a
 * different payload.
 *
 * The header is included by the resource script which declares the payloads and
 * by the code which reads them back (see `common/EmbeddedResource.hpp` and
 * `launcher/utils/SandboxModules.cpp`), so both sides always agree on the
 * identifiers. They are macros instead of constants because the resource
 * compiler is not a C++ compiler.
 *
 * The two sandbox injection modules are named by `cmake/LauncherResources.rc.in`,
 * which CMake turns into a resource script of the target (see
 * `launcher/resource.rc`), and declared by that script.
 *
 * The run configuration and the startup image are different: the packer writes
 * them into the launcher payload at pack time (see
 * `src/core/LauncherResources.cpp`), so the resource script of the launcher does
 * not declare them. Their identifiers are nevertheless declared here, so the
 * packer which writes the payload and the launcher which reads it back agree on
 * the numbers without either of them spelling them out.
 *
 * A payload which is added to the launcher later has to use the next free
 * number.
 */

/** @brief The 32 bit sandbox injection module the launcher releases. */
#define IDR_APPBOX_SANDBOX32 101

/** @brief The 64 bit sandbox injection module the launcher releases. */
#define IDR_APPBOX_SANDBOX64 102

/** @brief The run configuration the packer writes into the launcher payload. */
#define IDR_APPBOX_LAUNCHER_CONFIG 103

/** @brief The startup image of the session, embedded by the packer. */
#define IDR_APPBOX_STARTUP_IMAGE 104

#endif // APPBOX_LAUNCHER_EMBEDDED_RESOURCE_IDS_H
