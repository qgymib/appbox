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

The launcher derives the path of the file inside the extracted resources and hands
it to the sandbox, together with the file of every patch package of the run (see
[Patch layers](#patch-layers)). A missing file, a missing configuration or a
malformed document is not an error: the sandbox then behaves like one without an
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
`sandbox/network/Socks5.*`, the functions which have no import library are
implemented by `sandbox/network/Extensions.*`, and the completion of an
operation the sandbox performs itself is `sandbox/network/Completion.*`. The
hooks of the socket API call them:

| Hook | Behaviour |
| --- | --- |
| `ws2_32!connect`, `ws2_32!WSAConnect` | A stream socket is connected to the server of the proxy and the handshake is performed before the call returns, so the socket is connected to the target when the application gets the control back. A datagram socket is not connected at all: the target is recorded and the socket keeps its own stack, because the protocol has no connection for a datagram. |
| `ws2_32!send`, `ws2_32!WSASend` | A datagram of a connected datagram socket is wrapped by the protocol and sent to the relay of the association. A stream socket keeps the path of the host, because its bytes already reach the target through the connection the proxy made. |
| `ws2_32!recv`, `ws2_32!WSARecv` | A datagram which came from the relay is unwrapped and handed over as the payload of the connected socket. |
| `ws2_32!sendto`, `ws2_32!WSASendTo` | A datagram of a datagram socket is wrapped by the protocol and sent to the relay of the association of the socket. |
| `ws2_32!recvfrom`, `ws2_32!WSARecvFrom` | A datagram which came from the relay is unwrapped and reported with the address the payload was sent to. |
| `ws2_32!WSAIoctl` | The answer to `SIO_GET_EXTENSION_FUNCTION_POINTER` for `WSAID_CONNECTEX`, `WSAID_WSASENDMSG` and `WSAID_WSARECVMSG` is replaced by the function of the sandbox. |
| `ws2_32!getpeername` | A socket the proxy connected reports the address the application asked for, and not the address of the server of the proxy. |
| `ws2_32!closesocket` | The state of the socket is released with the socket. |

**TCP** is carried by the handshake of the protocol; **UDP** is carried by one
association per datagram socket, which is opened by its first datagram and
reused by every datagram which follows, so a socket which talks to several
targets carries them all through the one relay the server granted it. A
datagram socket which is connected is carried as well, because the address it
was connected to is the target of every datagram it sends.

### The functions which have no import library

`ConnectEx`, `WSASendMsg` and `WSARecvMsg` are not exported by a library an
application links against: it asks `WSAIoctl` for their addresses with the
identifier of the extension. A hook of the export therefore cannot reach them,
so the sandbox answers the question instead and hands out its own
implementation. The call reaches the system first, which keeps the answer of
the host for every other extension, and only the three the proxy carries are
replaced.

### An operation the sandbox performs itself

The payload of a datagram of the protocol is not the payload of the
application, so an operation which the application makes as an overlapped one
cannot be handed to the stack: the stack would read the message of the protocol
into the buffers of the application, or would need a buffer which outlives the
call. The sandbox therefore performs the operation and reports its result the
way the stack would have reported it:

| Kind | Behaviour |
| --- | --- |
| Overlapped send | The datagram is sent before the call returns, and the result is written into the `OVERLAPPED` of the application. |
| Overlapped receive | The operation is queued, and the worker of the sandbox waits for the datagram of the relay and fills the buffers of the application when it arrives. |
| Completion | `Internal` and `InternalHigh` are written, the event of the operation is signalled, and a completion packet is queued to the completion port of the socket when it has one. |
| Return | The call reports `WSA_IO_PENDING`, which is the shape a caller of an overlapped operation expects: it waits for the completion, and the completion is always delivered. |

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
* **A socket the proxy carries never reaches the host directly.** Every entry
  point of a datagram socket of a carried kind is answered by the sandbox, so a
  datagram cannot leave the process on the path of the host while the proxy is
  enabled.
* **Relation to the redirections.** The target of a request is the address the
  application asked for, so a name the `DNS` page redirects is resolved to the
  redirect address before the proxy sees it, and the proxy connects to that
  address through the server. The name of the server of the proxy itself is
  resolved the way the application resolves a name, so a redirection of the
  workspace applies to it as well.

The engine reaches its own server through the entry points the hooks saved, so
the traffic of the proxy never passes the hooks again.

## Patch layers

A run composes its network configuration from the layers of the run: the file of
the resources of the archive comes first and the file of every patch package of
the `patch` directory follows in ascending name order. The sandbox applies the
files in that order and merges them, so a package overrides the configuration it
names and keeps the configuration it does not name:

| Member | Merge |
| --- | --- |
| `entries` | Per hostname. A file which lists a hostname overrides the entry of the same hostname of the files below it, while a hostname no later file lists keeps the entry below it. A file which lists no redirection therefore keeps every redirection below it. |
| `proxy` | Per file. The proxy of the last file which names a proxy that can be used is the proxy of the run, and a file which names none — or whose proxy names another protocol, whose server is missing or whose port is not a port — keeps the proxy below it. |

A file which is not a network isolation file of the supported version is
skipped with everything it carries: neither its redirections nor its proxy take
part in the run, and the files below and above it are applied as if it were not
there. The merge of the redirections is atomic per file, so a malformed document
never changes a redirection of the layers below it.

The proxy of a run is therefore the proxy of the **last layer which configures
one**, and not the proxy of the last layer: a patch package whose `Network`
workspace holds no proxy leaves the proxy of the archive in place.

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

The proxy is on the same layer for a reason of its own: the address a request
carries is the address the application asked for, and a program spells that
address at the entry points of `ws2_32` and not at the device. A socket control
request on `\Device\Afd` carries the address the stack built, after the
redirections of the workspace were applied and after the address family was
chosen, and it does not carry the call the application made: a connection and a
datagram look the same at that layer, and a socket which the proxy has to leave
alone cannot be told from one it has to carry. The same boundary is where the
product of the same kind draws its line: the runtime of Turbo Studio hooks the
exports of `ws2_32` and not the device, and the only `NtDeviceIoControlFile`
hook it carries serves a device of its own (see
[TurboStudioAnalysis.md](TurboStudioAnalysis.md)).

## Tests

The unit tests and the end-to-end cases of the network isolation are listed in
[test/README.md](../test/README.md).

## Known gaps and limitations

- An application which sends a DNS question itself, without the name resolution
  of winsock or of the DNS client, is not redirected.
- A question which asks for a record type other than an address keeps the
  resolution of the host.
- `GetAddrInfoExW` is redirected for its synchronous form only.
- A request never carries a name: the sandbox sends the address the application
  asked for, so a name is resolved inside the process and not at the server of
  the proxy.
- A datagram of an association which is fragmented (`FRAG` other than zero) is
  not reassembled; the application is answered with an empty datagram.
- A socket which the application creates before the sandbox is injected cannot
  be carried, because the state of a socket is built by the calls the sandbox
  sees.
- `getsockname` keeps the answer of the host, so the local address of a socket
  the proxy carries is the one of its connection to the server.
- The control data of a message is not synthesized: a message which is received
  through `WSARecvMsg` reports no control data.
- An overlapped operation which the sandbox completes reports a failure as
  `STATUS_UNSUCCESSFUL`, because the error of the operation is not translated
  into the status the stack would have reported.
- The worker of the sandbox serves a bounded number of receives at once, and a
  receive beyond that number fails with `WSAENOBUFS`.
- A synchronous receive waits at most thirty seconds, or the receive timeout of
  the socket when it is set.
- The completion port of a socket is read for every completion, which is a
  system call per overlapped operation.
