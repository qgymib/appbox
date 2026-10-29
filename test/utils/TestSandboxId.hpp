#ifndef APPBOX_TEST_UTILS_TEST_SANDBOX_ID_HPP
#define APPBOX_TEST_UTILS_TEST_SANDBOX_ID_HPP

#include <string>

namespace appbox::test
{

/**
 * @brief Derive the identifier of the sandbox a case describes.
 *
 * The identifier is a required member of the run configuration, and the
 * launcher derives the name of its control pipe from it, so the cases need an
 * identifier which is stable and unique: stable because one case may run the
 * launcher several times and every run of it belongs to the same sandbox, and
 * unique because two cases which share one identifier would compete for the
 * very same pipe name, which turns into a random failure of the case which
 * starts second.
 *
 * The working directory of a case carries both properties - it is created once
 * per case and it is never shared - so the identifier is a hash of that path
 * expanded into the `8-4-4-4-12` layout of `common/SandboxId.hpp`. The hash is
 * not a security boundary, it only has to spread the paths of the cases.
 *
 * The header is named `TestSandboxId.hpp` and not `SandboxId.hpp` on purpose:
 * a header of `test/utils` with the name of a header of `common` would shadow
 * it in every translation unit of the helpers.
 *
 * @param[in] path Path of the case, normally its working directory.
 * @return The canonical identifier of the sandbox of the case.
 */
std::string SandboxIdForPath(const std::wstring& path);

} // namespace appbox::test

#endif // APPBOX_TEST_UTILS_TEST_SANDBOX_ID_HPP
