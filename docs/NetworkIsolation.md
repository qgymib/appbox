# Network Isolation

The network isolation of appbox redirects the name resolution of a packaged
application: a name the `Network` workspace of the packer lists is answered with
the address the workspace stores for it, and every other name keeps the
resolution of the host. The redirection is served from the isolation file of the
archive and never asks a server, so a redirected name is resolved the same way
on every machine, online or offline.

## Packer side

The `Network` workspace of `AppBox.exe` collects the network configuration of
the packaged application. Its tab strip carries the pages `Proxy`, `DNS` and
`IP Restrictions`; only the `DNS` page is implemented, the other two show the
empty state of a reserved isolation domain.

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

The redirections travel with the project file of `File -> Export Configuration...`
and are restored by `File -> Import Configuration...`: the document of
`src/core/ProjectDocument.*` holds them as the `network` array, and
`src/core/ProjectFile.*` maps them to and from the model of the workspace.

## Schema of the isolation file

The packer writes the redirections into the overlay of the archive as
`data/network-isolation.json`:

```json
{
  "version": 1,
  "entries": [
    { "hostname": "update.example.com", "redirect": "127.0.0.1" }
  ]
}
```

The loader derives the path of the file inside the extracted overlay and hands
it to the sandbox as `SandboxConfig.network_isolation_dos_path`. A missing file,
a missing configuration or a malformed document is not an error: the sandbox
then behaves like one without an isolation file and every name keeps the
resolution of the host.

The document is written and read by `src/core/NetworkIsolationFile.*` (packer)
and by `sandbox/network/DnsTable.*` (sandbox); the tokens and the address
literals are shared through `common/NetworkIsolation.hpp`.

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
