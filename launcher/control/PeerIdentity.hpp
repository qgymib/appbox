#ifndef APPBOX_LAUNCHER_CONTROL_PEERIDENTITY_HPP
#define APPBOX_LAUNCHER_CONTROL_PEERIDENTITY_HPP

#include "WinAPI.h" /* Must be first include file: <ntstatus.h> has to precede <windows.h>. */
#include <sddl.h>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
#include "utils/HostPath.hpp"

namespace appbox::control
{

/**
 * @brief Outcome of the authentication of a client of the control pipe.
 *
 * The verdict is what the master instance answers with when it refuses a start
 * request of a client it does not trust, so the two kinds of refusal stay
 * distinguishable: an image or a user which is not the one of the master
 * instance is `Unauthorized`, while a client of the same program and the same
 * user which runs at another integrity level is `IntegrityMismatch`.
 */
enum class PeerVerdict
{
    Authorized,        /* The client may send a start request. */
    Unauthorized,      /* The image or the user of the client is not the one of the master instance. */
    IntegrityMismatch, /* The client runs at another integrity level than the master instance. */
};

/**
 * @brief Identity of one process, as the control pipe authenticates it.
 *
 * The three members are the facts a master instance compares with its own
 * identity: the program the process runs, the user whose token it carries and
 * the integrity level of that token. The level is the last sub authority of the
 * integrity SID, which is the number the mandatory label policies are written
 * with (`0x1000` for low, `0x2000` for medium, `0x3000` for high).
 */
struct ProcessIdentity
{
    std::wstring  image_path;        /*!< Host path of the image, normalized, empty when unreadable. */
    std::wstring  user_sid;          /*!< String SID of the user of the token, empty when unreadable. */
    std::uint32_t integrity_rid = 0; /*!< Last sub authority of the integrity SID of the token. */
    bool          valid = false;     /*!< True when every member above was read. */
};

namespace detail
{

/**
 * @brief Read the name of the image a process runs.
 *
 * @param[in] process Handle of the process, opened with
 *                    `PROCESS_QUERY_LIMITED_INFORMATION`.
 * @param[out] path Full path of the image, in the spelling the system reports.
 * @return true when the name was read.
 */
inline bool PeerIdentityReadImagePath(HANDLE process, std::wstring& path)
{
    std::wstring buffer(MAX_PATH, L'\0');

    for (;;)
    {
        DWORD size = static_cast<DWORD>(buffer.size());
        if (QueryFullProcessImageNameW(process, 0, buffer.data(), &size) != FALSE)
        {
            buffer.resize(size);
            path = std::move(buffer);
            return !path.empty();
        }

        /*
         * The length of a path of the system is bounded, so a name which does
         * not fit is reported as unreadable instead of growing the buffer
         * without an end.
         */
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || buffer.size() >= 0x8000u)
        {
            return false;
        }

        buffer.resize(buffer.size() * 2);
    }
}

/**
 * @brief Read the string SID of the user of a token.
 * @param[in] token Token opened with `TOKEN_QUERY`.
 * @param[out] sid Text of the SID, for example `S-1-5-21-...`.
 * @return true when the SID was read.
 */
inline bool PeerIdentityReadUserSid(HANDLE token, std::wstring& sid)
{
    std::vector<std::uint8_t> buffer(256, 0);

    for (;;)
    {
        DWORD size = 0;
        if (GetTokenInformation(token, TokenUser, buffer.data(), static_cast<DWORD>(buffer.size()), &size) != FALSE)
        {
            const auto* user = reinterpret_cast<const TOKEN_USER*>(buffer.data());

            LPWSTR text = nullptr;
            if (ConvertSidToStringSidW(user->User.Sid, &text) == FALSE)
            {
                return false;
            }

            sid = text;
            LocalFree(text);
            return true;
        }

        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || size <= buffer.size())
        {
            return false;
        }

        buffer.resize(size);
    }
}

/**
 * @brief Read the integrity level of a token.
 * @param[in] token Token opened with `TOKEN_QUERY`.
 * @param[out] rid Last sub authority of the integrity SID of the token.
 * @return true when the level was read.
 */
inline bool PeerIdentityReadIntegrity(HANDLE token, std::uint32_t& rid)
{
    std::vector<std::uint8_t> buffer(256, 0);

    for (;;)
    {
        DWORD size = 0;
        if (GetTokenInformation(token, TokenIntegrityLevel, buffer.data(), static_cast<DWORD>(buffer.size()), &size) !=
            FALSE)
        {
            const auto* label = reinterpret_cast<const TOKEN_MANDATORY_LABEL*>(buffer.data());
            if (label->Label.Sid == nullptr)
            {
                return false;
            }

            const auto count = *GetSidSubAuthorityCount(label->Label.Sid);
            if (count == 0)
            {
                return false;
            }

            rid = *GetSidSubAuthority(label->Label.Sid, static_cast<DWORD>(count - 1));
            return true;
        }

        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || size <= buffer.size())
        {
            return false;
        }

        buffer.resize(size);
    }
}

} // namespace detail

/**
 * @brief Read the identity of one process.
 *
 * The call reads what a master instance compares with its own identity: the
 * program the process runs, the user of its token and the integrity level of
 * that token. A process which cannot be opened, whose token cannot be read or
 * whose level is missing is reported as unreadable, which the caller treats as
 * a refused client.
 *
 * @param[in] pid Process id of the process to read.
 * @param[out] identity The identity to fill, invalid when the call fails.
 * @return true when every member of the identity was read.
 */
inline bool QueryProcessIdentity(std::uint32_t pid, ProcessIdentity& identity)
{
    identity = ProcessIdentity{};

    if (pid == 0)
    {
        return false;
    }

    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (process == nullptr)
    {
        return false;
    }

    std::wstring  image;
    std::wstring  user;
    std::uint32_t rid = 0;
    HANDLE        token = nullptr;

    const bool readable = detail::PeerIdentityReadImagePath(process, image) &&
                          OpenProcessToken(process, TOKEN_QUERY, &token) != FALSE &&
                          detail::PeerIdentityReadUserSid(token, user) && detail::PeerIdentityReadIntegrity(token, rid);

    if (token != nullptr)
    {
        CloseHandle(token);
    }

    CloseHandle(process);

    if (!readable)
    {
        return false;
    }

    identity.image_path = NormalizeHostPath(image);
    identity.user_sid = std::move(user);
    identity.integrity_rid = rid;
    identity.valid = !identity.image_path.empty();
    return identity.valid;
}

/**
 * @brief Read the identity of this process.
 * @param[out] identity The identity to fill, invalid when the call fails.
 * @return true when every member of the identity was read.
 */
inline bool QueryOwnIdentity(ProcessIdentity& identity)
{
    return QueryProcessIdentity(GetCurrentProcessId(), identity);
}

/**
 * @brief Compare two image paths of the host.
 *
 * Both paths are normalized first, so the comparison ignores the case, a
 * trailing separator and the difference between a relative and an expanded
 * spelling.
 *
 * @param[in] left Left operand.
 * @param[in] right Right operand.
 * @return true when both paths name the same program.
 */
inline bool IsSameImagePath(const std::wstring& left, const std::wstring& right)
{
    if (left.empty() || right.empty())
    {
        return false;
    }

    return SameHostPath(left, right);
}

/**
 * @brief Decide whether the server of a control pipe is the master of a sandbox.
 *
 * A second instance verifies the process at the other end of the pipe before it
 * sends its request, so a process which occupied the name of the control pipe of
 * a sandbox receives nothing. The server is trusted when it runs the image of
 * this run under the user and the integrity level of this run.
 *
 * The image is the only fact of the program which is compared: the injection
 * module of the sandbox leaves every launcher alone, so a launcher runs the file
 * it was started from and never a copy of a layer, and the kernel reports the
 * same host path to both ends of the pipe.
 *
 * @param[in] server Identity of the server of the pipe.
 * @param[in] own Identity of this run.
 * @param[out] error_message Human readable description of a refusal, empty when
 *                           the server is trusted.
 * @return The verdict.
 */
inline PeerVerdict AuthorizeServer(const ProcessIdentity& server, const ProcessIdentity& own,
                                   std::string& error_message)
{
    error_message.clear();

    if (!server.valid || !own.valid)
    {
        error_message = "the identity of the server of the control pipe cannot be read";
        return PeerVerdict::Unauthorized;
    }

    if (!IsSameImagePath(server.image_path, own.image_path))
    {
        error_message = "the server of the control pipe runs another program";
        return PeerVerdict::Unauthorized;
    }

    if (server.user_sid != own.user_sid)
    {
        error_message = "the server of the control pipe runs as another user";
        return PeerVerdict::Unauthorized;
    }

    if (server.integrity_rid != own.integrity_rid)
    {
        error_message = "the server of the control pipe runs at another integrity level";
        return PeerVerdict::IntegrityMismatch;
    }

    return PeerVerdict::Authorized;
}

/**
 * @brief Decide whether a client of the control pipe may send a start request.
 *
 * The identity of the client is the only boundary of the feature: the
 * identifier and the root of a sandbox are readable by every process the
 * sandbox runs, so the master instance trusts a client which runs its own
 * program under its own user and at its own integrity level only.
 *
 * @param[in] peer Identity of the client.
 * @param[in] own Identity of the master instance.
 * @param[out] error_message Human readable description of a refusal, empty when
 *                           the client is authorized.
 * @return The verdict.
 */
inline PeerVerdict AuthorizePeer(const ProcessIdentity& peer, const ProcessIdentity& own, std::string& error_message)
{
    error_message.clear();

    if (!peer.valid || !own.valid)
    {
        error_message = "the identity of the client of the control pipe cannot be read";
        return PeerVerdict::Unauthorized;
    }

    if (!IsSameImagePath(peer.image_path, own.image_path))
    {
        error_message = "the client does not run the launcher of this sandbox";
        return PeerVerdict::Unauthorized;
    }

    if (peer.user_sid != own.user_sid)
    {
        error_message = "the client does not run as the user of the sandbox";
        return PeerVerdict::Unauthorized;
    }

    if (peer.integrity_rid != own.integrity_rid)
    {
        error_message = "the client runs at another integrity level than the sandbox";
        return PeerVerdict::IntegrityMismatch;
    }

    return PeerVerdict::Authorized;
}

} // namespace appbox::control

#endif // APPBOX_LAUNCHER_CONTROL_PEERIDENTITY_HPP
