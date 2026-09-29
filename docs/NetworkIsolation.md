# Network Isolation

The network isolation of appbox redirects the name resolution of a packaged
application: a name the `Network` workspace of the packer lists is answered with
the address the workspace stores for it, and every other name keeps the
resolution of the host. The redirection is served from the isolation file of the
archive and never asks a server, so a redirected name is resolved the same way
on every machine, online or offline.

Next to the redirections the same workspace collects the SOCKS5 proxy of the
packaged application. The proxy travels with the project file and with the
archive, and the sandbox carries the traffic of the packaged application through
the server it names: the interception lives at the winsock layer, which is the
layer the name resolution already uses.

The `Network` workspace of `AppBox.exe` — the `Proxy` and `DNS` pages and their
editing rules — is documented in [README.md](../README.md).

## Schema of the isolation file

The packer writes the network configuration of the workspace into the network
domain of the resources of the archive as `app/network/isolation.json`:

```json
{
  "version": 1,
  "entries": [
    { "hostname": "update.example.com", "redirect": "127.0.0.1" }
  ],
  "proxy": { "type": "socks5", "tcp": true, "udp": false,
             "server": "127.0.0.1", "port": "1080",
             "username": "user", "password": "secret" }
}
```

The `proxy` member is optional: it is written while the workspace holds a proxy
configuration, which is a protocol that is switched on or a field which carries
a value. A document which does not carry it describes a session without a proxy,
and so does a document whose proxy names another protocol, whose server is
missing or whose port is not a port. The schema version therefore stays `1`, so
an archive which was written before the member existed is still accepted.

The loader derives the path of the file inside the extracted resources and hands
it to the sandbox. A missing file, a missing configuration or a malformed
document is not an error: the sandbox then behaves like one without an
isolation file, so every name keeps the resolution of the host and every
connection keeps the path of the host.

The tokens, the address literals and the rules of a port are shared between
the packer and the sandbox through `common/NetworkIsolation.hpp`, so the two
sides cannot drift apart. The document itself is read and written as the
structure of the schema as well: the header describes an entry, the proxy and
the document, and both sides convert them with `to_json()` and `from_json()`
instead of reading or writing the members of a JSON object. An entry whose
members are incomplete, which are of another type, or which carries an empty
hostname is therefore refused while the file is read. The proxy is the one
exception: its members are read leniently, so a proxy which cannot be used
leaves a session without a proxy instead of failing the whole document.

## Sandbox side: DNS redirections

The sandbox loads the file into its DNS table before the hooks are attached,
and the name resolution hooks answer a question from that table:

| Step | Behaviour |
| --- | --- |
| Lookup | The hostname is compared ignoring the case and a trailing dot; the table matches a whole name and never a suffix or a pattern. |
| Address family | An IPv4 redirect answers an `AF_INET` question and an IPv6 redirect answers an `AF_INET6` question; a question for a specific family is not answered by an entry of the other family, and `AF_UNSPEC` accepts both. |
| Hit | The resolution is answered with the redirect address and no query leaves the process. |
| Miss | The question is forwarded to the original entry point of the process, which resolves it the way the host does. |

A hit is served by handing the redirect address to the original entry point as
the name to resolve, together with the flags which forbid a name resolution
(`AI_NUMERICHOST` for winsock, `DNS_QUERY_NO_WIRE_QUERY` for the DNS client),
so no query can reach the network even if the redirect were not a literal. The
record name of an answer is the redirect address instead of the queried name,
which is the one visible difference to a resolution of the host.

### Name resolution hooks

| Hook | Notes |
| --- | --- |
| `ws2_32!GetAddrInfoW` | The resolution of the modern winsock API, which is what a browser, `ping` and `curl` call. |
| `ws2_32!getaddrinfo` | The ANSI variant of the same API. |
| `ws2_32!GetAddrInfoExW` | Only the plain synchronous question is redirected. |
| `ws2_32!gethostbyname` | The legacy resolution, which answers IPv4 addresses only. |
| `dnsapi!DnsQuery_A`, `DnsQuery_W`, `DnsQuery_UTF8` | The DNS client, which is what `nslookup` calls. Only a question for `A`, `AAAA` or `ANY` is redirected, because the entry holds an address. |

`ws2_32.dll` and `dnsapi.dll` are loaded on demand, so the sandbox loads both
before it resolves the entry points of their hooks: a hook which cannot be
resolved is fatal in isolation mode, and the sandbox would otherwise silently
stop redirecting the name resolution.

## Sandbox side: SOCKS5 proxy

The proxy of the workspace is applied by the same module, from the `proxy`
member of the isolation file. The engine (`sandbox/network/Proxy.*`) holds the
configuration and the per-socket state, the protocol itself is implemented by
`sandbox/network/Socks5.*`, and the hooks of the socket API call it:

| Hook | Behaviour |
| --- | --- |
| `ws2_32!connect`, `ws2_32!WSAConnect` | A stream socket is connected to the server of the proxy and the handshake is performed before the call returns, so the socket is connected to the target when the application gets the control back. |
| `ws2_32!sendto`, `ws2_32!WSASendTo` | A datagram of a datagram socket is wrapped by the protocol and sent to the relay of the association of the socket. |
| `ws2_32!recvfrom`, `ws2_32!WSARecvFrom` | A datagram which came from the relay is unwrapped and reported with the address the payload was sent to. |
| `ws2_32!closesocket` | The state of the socket is released with the socket. |

**TCP** is carried by the handshake of the protocol; **UDP** is carried by one
association per datagram socket, which is opened by its first datagram and
reused by every datagram which follows, so a socket which talks to several
targets carries them all through the one relay the server granted it.

Key constraints of the design:

* **The mode of a socket is never changed.** The handshake waits for a socket
  which is not ready yet with `select` and a deadline, so an application which
  works with non-blocking sockets keeps working the same way and the sandbox
  never has to restore a mode it cannot read back. A handshake which does not
  finish within its deadline fails the call with `WSAETIMEDOUT`.
* **A failure is a failure.** A server which cannot be reached, a handshake
  which is refused and a reply which reports an error fail the call of the
  application with the error of the protocol, and the connection is never
  carried directly: an application can never believe that it is proxied while it
  is not.
* **Relation to the redirections.** The target of a request is the address the
  application asked for, so a name the `DNS` page redirects is resolved to the
  redirect address before the proxy sees it, and the proxy connects to that
  address through the server. The name of the server of the proxy itself is
  resolved the way the application resolves a name, so a redirection of the
  workspace applies to it as well.

The engine reaches its own server through the entry points the hooks saved, so
the traffic of the proxy never passes the hooks again.

## Why the hooks are not on an `Nt` function

The isolation of the filesystem and of the registry hooks the `Nt` functions of
the process, because that is where their choke point is. The name resolution has
no such point, which was measured instead of assumed: a program which resolves a
name inside the sandbox opens `\??\Nsi` and sends only parameter queries to it,
while the DNS question itself leaves the process as a UDP datagram over
`\Device\Afd`. Neither `ws2_32.dll` nor `dnsapi.dll` carries the device name of
the NSI device, so there is no kernel level entry point which carries the
queried name; the user mode entry points of the two libraries are the closest
layer which does.

## Known gaps

- An application which sends a DNS question itself, without the name resolution
  of winsock or of the DNS client, is not redirected.
- A question which asks for a record type other than an address keeps the
  resolution of the host.
- `GetAddrInfoExW` is redirected for its synchronous form only.
- A datagram socket which is connected (`connect` on a datagram socket, then
  `send`) keeps the path of the host: the sandbox carries the datagram entry
  points (`sendto`, `WSASendTo`) and not `send`.
- A datagram which is sent or received as an overlapped operation keeps the path
  of the host: the message of the protocol is built in the buffer of the sandbox,
  which the stack reads after the call returned.
- `WSASendMsg` and `WSARecvMsg` are not carried by the proxy.
- A request never carries a name: the sandbox sends the address the application
  asked for, so a name is resolved inside the process and not at the server of
  the proxy.
- A datagram of an association which is fragmented (`FRAG` other than zero) is
  not reassembled; the application is answered with an empty datagram.
- A socket which the application creates before the sandbox is injected cannot
  be carried, because the state of a socket is built by the calls the sandbox
  sees.
