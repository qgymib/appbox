#ifndef APPBOX_PACKER_CORE_NETWORK_MODEL_HPP
#define APPBOX_PACKER_CORE_NETWORK_MODEL_HPP

#include <cstddef>
#include <string>
#include <vector>

namespace appbox
{

/**
 * @brief One DNS redirection of the Network workspace.
 *
 * The entry names a hostname or an IP address the sandboxed application asks
 * for and the address that name is redirected to, which is the pair of the
 * `Hostname or IP Address` and `Redirect` columns of the workspace.
 */
struct DnsRedirectEntry
{
    /**
     * @brief Hostname or IP address which is redirected.
     */
    std::wstring hostname;

    /**
     * @brief Address the name is redirected to.
     */
    std::wstring redirect;
};

/**
 * @brief Editable content of the Network workspace of the packer.
 *
 * The model holds the DNS redirections the user entered in the workspace. It
 * holds no wxWidgets dependency and never touches the network or the host
 * filesystem, so its validation rules are unit testable.
 *
 * The entries keep the order they were added in, so the table of the
 * workspace shows them the way the user entered them.
 *
 * Every operation which can fail validates its input first and reports an
 * English error description without changing the model.
 */
class NetworkModel
{
public:
    /**
     * @brief Drop every DNS redirection.
     *
     * The model is left in the state of a fresh session.
     */
    void Reset();

    /**
     * @brief Whether the model holds no DNS redirection.
     * @return true when no entry was added.
     */
    bool IsEmpty() const;

    /**
     * @brief Get every DNS redirection the model holds.
     *
     * The entries are ordered the way they were added, so the rows of the
     * workspace table follow the order the user entered them in.
     *
     * @return The entries in insertion order.
     */
    const std::vector<DnsRedirectEntry>& DnsEntries() const;

    /**
     * @brief Append one DNS redirection.
     *
     * The call fails when the hostname or the redirect target is empty, when
     * either of them contains a whitespace character, or when the hostname is
     * already listed. The comparison of the hostnames ignores the case,
     * because the name resolution of the host does as well.
     *
     * @param[in] hostname Hostname or IP address to redirect.
     * @param[in] redirect Address the name is redirected to.
     * @param[out] error Error description on failure.
     * @return true on success.
     */
    bool AddDnsEntry(const std::wstring& hostname, const std::wstring& redirect, std::string& error);

    /**
     * @brief Replace one DNS redirection.
     *
     * The entry keeps its position, so editing a row of the workspace table
     * does not reorder the table. An entry may be renamed to its own hostname
     * with a different case; a hostname which is listed by another entry is
     * refused.
     *
     * The call fails when the index does not name an entry, when the hostname
     * or the redirect target is empty, when either of them contains a
     * whitespace character, or when the hostname is listed by another entry.
     *
     * @param[in] index Position of the entry to replace.
     * @param[in] hostname Hostname or IP address to redirect.
     * @param[in] redirect Address the name is redirected to.
     * @param[out] error Error description on failure.
     * @return true on success.
     */
    bool SetDnsEntry(std::size_t index, const std::wstring& hostname, const std::wstring& redirect, std::string& error);

    /**
     * @brief Drop one DNS redirection.
     * @param[in] index Position of the entry to drop.
     * @return true when the index named an entry and it was removed.
     */
    bool RemoveDnsEntry(std::size_t index);

    /**
     * @brief Find the entry of a hostname.
     * @param[in] hostname Hostname to look for, compared ignoring the case.
     * @return The position of the entry, -1 when the hostname is not listed.
     */
    std::ptrdiff_t IndexOfHostname(const std::wstring& hostname) const;

private:
    /**
     * @brief The DNS redirections in insertion order.
     */
    std::vector<DnsRedirectEntry> dns_entries_;
};

} // namespace appbox

#endif // APPBOX_PACKER_CORE_NETWORK_MODEL_HPP
