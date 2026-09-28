#ifndef APPBOX_SANDBOX_UTILS_WINSOCK_HPP
#define APPBOX_SANDBOX_UTILS_WINSOCK_HPP

/*
 * The socket API of the sandbox is the winsock 2 one, and its headers are
 * included before the header of the project: that one includes <windows.h>,
 * which pulls in the winsock 1.1 header, and the two socket headers cannot be
 * included next to each other. A translation unit which uses the socket API
 * therefore includes this header as its first include file.
 */
#include <ntstatus.h>
#define WIN32_NO_STATUS

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif

/* winsock2.h includes <windows.h> itself, which then skips the winsock 1.1 header. */
#include <winsock2.h>
#include <ws2tcpip.h>

#include "utils/WinAPI.h"

#endif // APPBOX_SANDBOX_UTILS_WINSOCK_HPP
