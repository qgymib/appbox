#ifndef APPBOX_TEST_UTILS_PROBE_PATH_HPP
#define APPBOX_TEST_UTILS_PROBE_PATH_HPP

#include <string>
#include "ProbeCall.hpp"
#include "Test.hpp"

namespace appbox::test
{

/**
 * @brief Get the path of the probe executable of a bit size.
 *
 * The probe is the program a case runs inside the sandbox. It is a program of
 * its own, built for 32 bit and 64 bit, so a case chooses the bit size of the
 * process its operations run in. The paths are the ones the run was started
 * with: `ctest` passes `--probe32=` and `--probe64=`, and the environment
 * variables `APPBOX_TEST_PROBE32` and `APPBOX_TEST_PROBE64` are the fallback of
 * a run which is started by hand.
 *
 * A run which was started without them is refused before the first case, see
 * `appbox::test::ValidateProbePaths()`: the probe is the program a case runs
 * inside the sandbox, so a case which cannot start it cannot be verified.
 *
 * @param[in] bitness The bit size of the probe.
 * @return The path of the executable, empty when it was not provided.
 */
inline const std::wstring& ProbePath(ProbeBitness bitness)
{
    return bitness == ProbeBitness::Bit32 ? config.probe32_path : config.probe64_path;
}

} // namespace appbox::test

#endif // APPBOX_TEST_UTILS_PROBE_PATH_HPP
