#ifndef APPBOX_TEST_UTILS_NAMERESOLUTIONPROBE_HPP
#define APPBOX_TEST_UTILS_NAMERESOLUTIONPROBE_HPP

namespace appbox::test
{

/** Option which turns a process into the name resolution probe of the tracer. */
constexpr const wchar_t* kNameResolutionProbeOption = L"--appbox-name-resolution-probe";

/**
 * @brief Run the name resolution probe when the command line of the current
 *        process asks for it.
 *
 * The probe is the target of the integration test of the tracer. It loads the
 * socket library and the DNS client on demand and calls their name resolution
 * entry points through `GetProcAddress`, so a tracer run has to arm a module
 * which the loader maps after the initial break of the process: the DNS client
 * is not part of the import table of this executable.
 *
 * The probe is a process of its own and never drives a test run; the caller has
 * to return as soon as the function reports that the probe ran, because the
 * command line then holds the options of the probe instead of the options of a
 * test run.
 *
 * @return true when the current process is the probe.
 */
bool RunNameResolutionProbeIfRequested();

} // namespace appbox::test

#endif // APPBOX_TEST_UTILS_NAMERESOLUTIONPROBE_HPP
