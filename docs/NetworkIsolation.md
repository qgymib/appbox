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

## Packer side

The `Network` workspace of `AppBox.exe` collects the network configuration of
the packaged application. Its tab strip carries the pages `Proxy`, `DNS` and
`IP Restrictions`; the `Proxy` and `DNS` pages are implemented, while the
`IP Restrictions` page shows the empty state of a reserved isolation domain.

### SOCKS5 proxy

The `Proxy` page holds the SOCKS5 proxy of the packaged application: the
protocol of the proxy, the two check boxes which pick the traffic it carries,
the address of the server, its port and the optional credentials. The password
is masked in the form and is stored as plain text, because the sandbox has to
send it to the server.

The two check boxes are the switch of the configuration: at least one of them
has to be set for the proxy to be in effect, and both of them being clear means
that the application connects directly. The server, the port and the
credentials stay in the model either way, so turning the proxy off does not
lose what was typed.

Every change of a control is handed to the model at once, and a hint line below
the form shows what the model did with it: the summary of the stored
configuration, or the description of the value it refused. A refused value is
not reported with a dialog and is not put back into the control, because the
user may be in the middle of typing it; the model keeps the last configuration
it accepted. The hint line never names the credentials.

The form is offered to the model as one configuration, so a change is either
accepted as a whole or refused as a whole:

- The server has to be free of whitespace characters while it carries a value.
- The port has to be empty or a decimal number between 1 and 65535 without a
  leading zero, so the text the user entered is never read as another port.
- A protocol may be enabled only while both the server and the port carry a
  value, because an enabled proxy without a server is a configuration the
  sandbox could not use.
- The user name and the password are free text: they may be empty and may carry
  whitespace characters.

The rules live in `src/core/NetworkModel.*` (the `ProxyConfig` structure and
`NetworkModel::SetProxy`), which holds no wxWidgets dependency and is unit
tested by `test/unit/NetworkModel.cpp`; the page itself is created by
`src/widget/NetworkPanel.*`.

The proxy is part of the project file and of the archive: `Build` writes it as
the optional `proxy` member of `data/network-isolation.json`, next to the
redirections, and the sandbox carries the traffic of the packaged application
through the server it names. The member is written only while the workspace
holds a proxy configuration, so a session without a proxy writes the document
of the previous schema and the schema version stays `1`.

### DNS redirections

The `DNS` page holds the DNS redirections of the packaged application: every
row pairs a `Hostname or IP Address` with the `Redirect` address the name has
to resolve to inside the sandbox. The rows are edited inside the table:

- `Add...` appends a row and opens its hostname cell. The table keeps at most
  one row which is still being filled in, and the row reaches the model once
  both of its cells carry a value.
- `Remove` drops the selected row, including a row which was not completed.
- A hostname has to be unique (the comparison ignores the case and a trailing
  dot, because the name resolution of the host does as well) and neither field
  may be empty or contain a whitespace character.
- The `Redirect` cell has to hold an IPv4 or an IPv6 address literal, because
  the sandbox answers the name with that address instead of asking the host. A
  value the model refuses is reported and the stored value is put back into the
  cell.

The rules live in `src/core/NetworkModel.*`, which holds no wxWidgets
dependency and is unit tested by `test/unit/NetworkModel.cpp`; the
workspace itself is `src/widget/NetworkPanel.*` with the tab strip in
`src/widget/NetworkTabBar.*`. The address literals and the hostname
normalization are shared with the sandbox through
`common/NetworkIsolation.hpp`, so the two sides cannot drift apart.

### Project file

The redirections and the proxy travel with the project file of
`File -> Export Configuration...` and are restored by
`File -> Import Configuration...`: the document of
`src/core/ProjectDocument.*` holds the redirections as the `network` array and
the proxy as the optional `proxy` object, and `src/core/ProjectFile.*` maps
both to and from the model of the workspace. The two members of the network
workspace look like this (the other members of the document are left out):

```json
{
  "network": [ { "hostname": "update.example.com", "redirect": "127.0.0.1" } ],
  "proxy": { "type": "socks5", "tcp": true, "udp": false,
             "server": "127.0.0.1", "port": "1080",
             "username": "user", "password": "secret" }
}
```

The `proxy` member is written only while a proxy is configured - a protocol is
enabled or a field carries a value - and a document which does not hold it
reads as a session without a proxy, which also clears the proxy of the session
it is imported into. The schema version therefore stays `1`: the member is
optional, so a project file which was written before it existed is still
accepted.

## Schema of the isolation file

The packer writes the network configuration of the workspace into the overlay of
the archive as `data/network-isolation.json`:

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

The loader derives the path of the file inside the extracted overlay and hands
it to the sandbox as `SandboxConfig.network_isolation_dos_path`. A missing file,
a missing configuration or a malformed document is not an error: the sandbox
then behaves like one without an isolation file, so every name keeps the
resolution of the host and every connection keeps the path of the host.

The `proxy` member is optional: it is written while the workspace holds a proxy
configuration, which is a protocol that is switched on or a field which carries
a value. A document which does not carry it describes a session without a proxy,
and so does a document whose proxy names another protocol, whose server is
missing or whose port is not a port. The schema version therefore stays `1`, so
an archive which was written before the member existed is still accepted.

The document is written by `src/core/NetworkIsolationFile.*` (packer) and read
by `sandbox/network/DnsTable.*` (the redirections) and
`sandbox/network/ProxyConfig.*` (the proxy) in the sandbox; the tokens, the
address literals and the rules of a port are shared through
`common/NetworkIsolation.hpp`.

## Sandbox side

The sandbox loads the file into the DNS table of its instance
(`sandbox/network/Isolation.*`, registered before the hooks are attached, so the
file is read through the original entry points of the process) and the name
resolution hooks answer a question from that table:

| Step | Behaviour |
| --- | --- |
| Lookup | The hostname is compared ignoring the case and a trailing dot; the table matches a whole name and never a suffix or a pattern. |
| Address family | An IPv4 redirect answers an `AF_INET` question and an IPv6 redirect answers an `AF_INET6` question; a question for a specific family is not answered by an entry of the other family, and `AF_UNSPEC` accepts both. |
| Hit | The resolution is answered with the redirect address and no query leaves the process. |
| Miss | The question is forwarded to the original entry point of the process, which resolves it the way the host does. |

### Hook layer

Every hooked entry point has its own file (`sandbox/hook/GetAddrInfoW.*`,
`sandbox/hook/getaddrinfo.*`, `sandbox/hook/GetAddrInfoExW.*`,
`sandbox/hook/gethostbyname.*`, `sandbox/hook/DnsQuery_A.*`,
`sandbox/hook/DnsQuery_W.*` and `sandbox/hook/DnsQuery_UTF8.*`), which cover the
name resolution of winsock and the one of the DNS client. The helpers they share
— the name conversion, the lookup in the DNS table, the family mapping and the
loading of the two modules — live in `sandbox/utils/NameResolution.*`:

| Hook | Notes |
| --- | --- |
| `ws2_32!GetAddrInfoW` | The resolution of the modern winsock API, which is what a browser, `ping` and `curl` call. |
| `ws2_32!getaddrinfo` | The ANSI variant of the same API. |
| `ws2_32!GetAddrInfoExW` | Only the plain synchronous question is redirected; a question of another name space, an asynchronous question and one which continues in an overlapped operation keep the resolution of the host. |
| `ws2_32!gethostbyname` | The legacy resolution, which answers IPv4 addresses only. |
| `dnsapi!DnsQuery_A`, `DnsQuery_W`, `DnsQuery_UTF8` | The DNS client, which is what `nslookup` and the applications which ask for a record type call. Only a question for `A`, `AAAA` or `ANY` is redirected, because the entry holds an address. |

A hit is served by handing the redirect address to the original entry point as
the name to resolve, together with the flags which forbid a name resolution
(`AI_NUMERICHOST` for winsock, `DNS_QUERY_NO_WIRE_QUERY` for the DNS client):
the operating system then answers the question from the literal itself, so the
result has the shape the caller expects, it is freed by the caller the usual
way, and no query can reach the network even if the redirect were not a literal.
The record name of an answer is the redirect address instead of the queried
name, which is the one visible difference to a resolution of the host.

`ws2_32.dll` and `dnsapi.dll` are loaded on demand, so the sandbox loads both
before it resolves the entry points of their hooks: a hook which cannot be
resolved is fatal in isolation mode, and the sandbox would otherwise silently
stop redirecting the name resolution.

### Proxy layer

The proxy of the workspace is applied by the same module, from the `proxy`
member of the isolation file: the engine (`sandbox/network/Proxy.*`) holds the
configuration, the address of the server and the state of the sockets of the
application, and the hooks of the socket API call it. The protocol itself is
implemented by `sandbox/network/Socks5.*`, which holds no Windows dependency and
is unit tested on its own.

| Hook | Behaviour |
| --- | --- |
| `ws2_32!connect`, `ws2_32!WSAConnect` | A stream socket is connected to the server of the proxy and the handshake is performed before the call returns, so the socket is connected to the target when the application gets the control back. |
| `ws2_32!sendto`, `ws2_32!WSASendTo` | A datagram of a datagram socket is wrapped by the protocol and sent to the relay of the association of the socket; the caller is answered with the number of the payload bytes it handed over. |
| `ws2_32!recvfrom`, `ws2_32!WSARecvFrom` | A datagram which came from the relay is unwrapped and reported with the address the payload was sent to; every other datagram is handed over the way the host delivers it. |
| `ws2_32!closesocket` | The state of the socket is released with the socket, including the control connection of an association. |

The engine reaches its own server through the entry points the hooks saved, so
the traffic of the proxy never passes the hooks again. The entry points are
installed once the hooks are attached, which is the moment a saved pointer
carries the trampoline to the original code.

**TCP.** A connection is established by the handshake of the protocol: the
greeting, the credentials of the configuration while they are set, and a
`CONNECT` request which names the target. The application never sees the server
of the proxy: the socket is connected to the target when the call returns, or
the call fails.

**UDP.** A datagram socket gets one association, which is opened by its first
datagram and reused by every datagram which follows, so a socket which talks to
several targets carries them all through the one relay the server granted it.
The association is a control connection of its own and its relay is the address
the server reported; a server which reports the unspecified address asks the
client to keep the address it reached the server with, which is what the engine
does.

**The mode of a socket is never changed.** The handshake waits for a socket
which is not ready yet with `select` and a deadline, so an application which
works with non-blocking sockets keeps working the same way and the sandbox never
has to restore a mode it cannot read back. A handshake which does not finish
within its deadline fails the call with `WSAETIMEDOUT`.

**A failure is a failure.** A server which cannot be reached, a handshake which
is refused and a reply which reports an error fail the call of the application
with the error of the protocol, and the connection is never carried directly: an
application can never believe that it is proxied while it is not.

**Relation to the redirections.** The target of a request is the address the
application asked for, so a connection to a name reaches the sandbox as the
address the name resolution answered with. A name the `DNS` page redirects is
therefore resolved to the redirect address before the proxy sees it, and the
proxy connects to that address through the server. The name of the server of the
proxy itself is resolved the way the application resolves a name, so a
redirection of the workspace applies to it as well.

### Why the hooks are not on an `Nt` function

The isolation of the filesystem and of the registry hooks the `Nt` functions of
the process, because that is where their choke point is. The name resolution has
no such point, which was measured instead of assumed: a program which resolves a
name inside the sandbox opens `\??\Nsi` and sends only parameter queries to it
(`IOCTL_NSI_*`, whose request carries pointers to caller structures and never a
hostname), while the DNS question itself leaves the process as a UDP datagram
over `\Device\Afd`. Neither `ws2_32.dll` nor `dnsapi.dll` carries the device name
of the NSI device, so there is no kernel level entry point which carries the
queried name; the user mode entry points of the two libraries are the closest
layer which does.

### Known gaps

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
