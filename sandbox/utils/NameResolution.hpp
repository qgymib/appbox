#ifndef APPBOX_SANDBOX_UTILS_NAME_RESOLUTION_HPP
#define APPBOX_SANDBOX_UTILS_NAME_RESOLUTION_HPP

#include "utils/WinAPI.h" /* Must be first include file */
#include "network/DnsTable.hpp"
#include <nlohmann/json.hpp>
#include <string>

namespace appbox
{
namespace network
{

/**
 * @brief Convert a counted wide name of the caller to UTF-8.
 *
 * The name comes from the application, so the conversion must not throw and
 * must not read past the end of the buffer: the counted conversion helper of
 * the log module reads the text by length and reports an empty text when the
 * length of the string does not fit its buffer.
 *
 * @param[in] name Name of the caller, may be null.
 * @return The UTF-8 name, empty when it cannot be read.
 */
std::string WideNameToUTF8(PCWSTR name);

/**
 * @brief Convert an ANSI name of the caller to UTF-8.
 *
 * The name of the application is text of its ANSI code page. A hostname, which
 * is what a redirection is written for, is ASCII, so it is the same in both
 * encodings; a name which cannot be converted is not redirected.
 *
 * @param[in] name Name of the caller, may be null.
 * @return The UTF-8 name, empty when it cannot be read.
 */
std::string AnsiNameToUTF8(PCSTR name);

/**
 * @brief Convert a redirect address to the wide text of a resolution.
 * @param[in] redirect Address literal of the isolation file.
 * @return The wide text, empty when it cannot be converted.
 */
std::wstring RedirectToWide(const std::string& redirect);

/**
 * @brief Translate an address family of a resolution to the family of a query.
 * @param[in] family Family the application asked for.
 * @return The requested family.
 */
RequestedFamily FamilyOf(int family);

/**
 * @brief Translate the type of a DNS query to the family of a query.
 * @param[in] type Type of the query.
 * @param[out] family Family the type asks for.
 * @return true when the type asks for an address, which a redirection answers.
 */
bool FamilyOfQueryType(WORD type, RequestedFamily& family);

/**
 * @brief Look up the address a hostname has to resolve to.
 *
 * @param[in] hostname Hostname the application asked for, UTF-8.
 * @param[in] family Family the application asked for.
 * @return The redirect address, empty when the name is not redirected.
 */
std::string FindRedirect(const std::string& hostname, RequestedFamily family);

/**
 * @brief Build the parameters of a log message of a DNS client query.
 *
 * The three entry points of the DNS client share the parameters of their
 * question, so they share the parser of the log message as well.
 *
 * @param[in] name Name of the query, UTF-8.
 * @param[in] type Type of the query.
 * @param[in] options Options of the query.
 * @param[in] redirect Address the name is redirected to, empty when it is not
 *                     redirected.
 * @return The parameters of the log message.
 */
nlohmann::json DnsQueryLogParam(const std::string& name, WORD type, DWORD options, const std::string& redirect);

/**
 * @brief Load the modules which carry the name resolution of an application.
 *
 * The name resolution lives in `ws2_32.dll` and `dnsapi.dll`, which a process
 * loads on demand. Both are loaded here so the entry points of their hooks can
 * be resolved, because a hook which cannot be resolved is fatal in isolation
 * mode: the sandbox would otherwise silently stop redirecting the name
 * resolution of the application. The modules are system libraries, so loading
 * them changes nothing but the set of modules the process has mapped.
 *
 * @return true when both modules are available.
 */
bool LoadNameResolutionModules();

} // namespace network
} // namespace appbox

#endif // APPBOX_SANDBOX_UTILS_NAME_RESOLUTION_HPP
