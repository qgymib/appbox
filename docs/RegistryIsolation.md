# Registry Isolation

appbox isolates the registry of a sandboxed process by hooking the key open /
create / delete / save entry points of the NT registry API and redirecting them
onto a private **hive** file inside the overlay. The method is known as *hive
redirection*: instead of replaying every registry operation on a copy of the
real data, the sandbox mounts one real registry file and points every redirected
key at it.

Most value level APIs operate on whatever key handle the open or the create
returned, which is already a handle into the hive. Two of them are hooked on top
of that, because the isolation mode of an entry decides the layer a call acts on
and not the handle: `NtQueryValueKey` and `NtQueryMultipleValueKey` provide the
read through, and `NtSetValueKey` and `NtDeleteValueKey` route a modification to
the layer the mode of the value names — see
[Isolation modes](#isolation-modes).

The hive holds **one sub key per root key of the view**, so the path of an entry
inside the hive equals its path in the registry workspace of the packer and in
the isolation file:

```
<root of the hive>
├── HKEY_CLASSES_ROOT
├── HKEY_CURRENT_USER
├── HKEY_LOCAL_MACHINE
├── HKEY_USERS
├── HKEY_CURRENT_CONFIG
├── APPBOX_HIDDEN                      the content of the hidden entries
└── APPBOX_WHITEOUT                    the bookkeeping of the deleted entries
```

The two reserved keys are siblings of the root keys of the view and are never
part of it: no path of the view maps onto them and no handle of them is handed
out to the sandboxed process. The first one carries the packed content of the
entries whose mode is `Hide`, the second one carries the deletions the sandbox
recorded (see [Deletion and whiteouts](#deletion-and-whiteouts)).

The **isolation modes** of the entries travel next to the hive as a JSON
document (`isolation.json`). A **key** and a **value** carry a mode of their own,
because the two answer different questions: the mode of a key decides which host
entries below it stay visible and where the modification of a sub key or of a
value lands, while the mode of a value decides how the value itself is seen. The
modes are enforced in full; the decision table behind them is the pure module
`sandbox/registry/IsolationPolicy.hpp` (see [Isolation modes](#isolation-modes)).
The limitations which remain are listed in
[Known gaps and limitations](#known-gaps-and-limitations).

`Merge` is the **only** mode which modifies the real registry: a value or a sub
key which the sandbox does not hold is written, deleted or created in the host,
while every other mode keeps the host registry untouched and redirects the
modification into the sandbox.

The isolation file is the description of the run and not an option: the sandbox
refuses to start without a usable document, because it cannot tell which host
entries stay visible without one and there is no default mode to fall back to
(see [On-disk layout](#on-disk-layout)).

A delete which runs against the sandbox records the entry as deleted inside the
hive as a **whiteout**: the entry of the hive is removed (when the hive holds
one) and the entry of the host is marked as deleted, so the merged view reports
it as gone while the real registry keeps its data. The markers live in the
reserved key at the root of the hive, see
[Deletion and whiteouts](#deletion-and-whiteouts).

A rename is the delete of the old name followed by a create of the new one: the
key of the hive is renamed inside the hive and the visible host key of the old
name is recorded as deleted the same way, so the merged view reports the new name
alone instead of both of them. A rename which the isolation cannot express in the
view is refused instead of being forwarded to the real registry, see
[Hooked entry points](#hooked-entry-points).

The same rule covers the transacted open and the transacted create: the hive
layer answers them with the transaction of the caller, and a call which the hive
layer cannot answer is refused instead of being read through the real registry,
see [Hooked entry points](#hooked-entry-points).

## Scope

All five root keys of the view are redirected:

| Root key | NT path |
| --- | --- |
| `HKEY_LOCAL_MACHINE` | `\REGISTRY\MACHINE` |
| `HKEY_CLASSES_ROOT` | `\REGISTRY\MACHINE\SOFTWARE\CLASSES` |
| `HKEY_CURRENT_CONFIG` | `\REGISTRY\MACHINE\SYSTEM\CURRENTCONTROLSET\HARDWARE PROFILES\CURRENT` |
| `HKEY_CURRENT_USER` | `\REGISTRY\USER\<SID>` of the sandboxed user |
| `HKEY_USERS` | `\REGISTRY\USER` |

The prefixes are matched longest first, so the classes root and the hardware
profile are not answered by the machine root they live below, and the current
user root wins over the `HKEY_USERS` prefix. Every other path (for example the
private application hive mounts below `\REGISTRY\A`) is forwarded unchanged.

The view of a sandboxed process is the hive layer merged with the entries of the
real registry which the isolation mode of the key keeps visible. The rows below
are written from the point of view of a key which the hive holds; the behaviour
of a key which only the host holds is the one its mode decides:

| Operation | Mode | The hive holds the key | The hive does not hold the key |
| --- | --- | --- | --- |
| **open / read** (`NtOpenKey(Ex)`) | `Full`, `Hide` | redirected into the hive | the key does not exist |
| | `Merge`, `Write Copy` | redirected into the hive | the key of the host is handed out (read through) |
| **open / write** (`NtOpenKey(Ex)`) | `Full`, `Hide` | redirected into the hive | the key does not exist |
| | `Merge` | redirected into the hive | the key of the host is handed out, because a modification of an entry the sandbox does not hold lands in the host |
| | `Write Copy` | redirected into the hive | the key is copied up into the hive (a shadow key is created and handed out) |
| **create** (`NtCreateKey`) | `Full`, `Hide`, `Write Copy` | redirected into the hive | the key is created inside the hive, intermediate keys included |
| | `Merge` | redirected into the hive | the key is created in the real registry, the keys above it included |
| **enumerate** (`NtEnumerateKey`, `NtEnumerateValueKey`) | every mode | merged view: hive entries first, then the non shadowed and visible real entries | visible real entries only |
| **value query** (`NtQueryValueKey`) | every mode | answered from the hive | falls back to the real registry (read through), unless the mode of the value hides the host |
| **value batch query** (`NtQueryMultipleValueKey`) | every mode | merged view: every entry is answered by the layer which holds it | visible real entries only |
| **value write** (`NtSetValueKey`) | `Full`, `Hide`, `Write Copy` | into the hive | into the hive (the key is created when it is missing) |
| | `Merge` | into the hive, because the sandbox holds the value | into the **real registry** |
| **delete key** (`NtDeleteKey`) | `Full`, `Hide`, `Write Copy` | the key of the hive is removed and the visible host key is recorded as deleted | the visible host key is recorded as deleted (a whiteout) |
| | `Merge` | the key of the hive is removed and the visible host key is recorded as deleted | the key of the **real registry** is deleted |
| **delete value** (`NtDeleteValueKey`) | `Full`, `Hide`, `Write Copy` | the value of the hive is removed and the visible host value is recorded as deleted | the visible host value is recorded as deleted (a whiteout) |
| | `Merge` | the value of the hive is removed and the visible host value is recorded as deleted | the value of the **real registry** is deleted |
| **rename** (`NtRenameKey`) | `Full`, `Hide`, `Write Copy` | the key of the hive is renamed and the visible host key of the old name is recorded as deleted | the rename is refused with `STATUS_ACCESS_DENIED`: a handle of the host layer never renames a key of the real registry |
| | `Merge` | the key of the hive is renamed and the visible host key of the old name is recorded as deleted | the key of the **real registry** is renamed |
| **save** (`NtSaveKey`, `NtSaveKeyEx`) | every mode | the merged view of the key is written into the caller file | the visible real entries are written into the caller file |

`Merge` is the row which differs from the others: it is the write rule of the
mode, see [Isolation modes](#isolation-modes). Every other mode keeps the real
registry untouched, while values which only exist in the real registry remain
readable. A modification which runs on a handle of the host layer is refused with
`STATUS_ACCESS_DENIED` for every mode but `Merge`, because a handle which the
caller opened without the right to modify an entry must not become that right
through the isolation. The entry points which the isolation does not hook at all
are listed in [Known gaps and limitations](#known-gaps-and-limitations).

The transacted variants of the open (`NtOpenKeyTransacted`,
`NtOpenKeyTransactedEx`) and of the create (`NtCreateKeyTransacted`) run the same
table with one exception: a key which the hive does not hold is never read
through for them, see [Hooked entry points](#hooked-entry-points).

The registry workspace of the packer, which describes the registry the packaged
application will see, is documented in [README.md](../README.md).

## Isolation modes

### Key modes

The mode of a key decides which host entries below the key stay visible and where
a modification of a sub key or of a value lands:

| Mode | Visible for the sandbox | Writes |
| --- | --- | --- |
| `Full` | only the hive; the host entry is invisible even when the host holds it | redirected into the hive |
| `Merge` | host and hive, the hive wins | a value the sandbox does not hold is modified in the host, a value it holds in the hive; a sub key the sandbox does not hold is created in the host, a sub key it holds follows its own mode |
| `Write Copy` | host and hive, the hive wins | redirected into the hive; a host key which the hive does not hold is copied up first |
| `Hide` | only the hive, and the packed content of the entry is invisible as well: the entry is reported as missing until the sandbox writes it | redirected into the hive |

`Merge` is the mode which lets a modification reach the real registry, and it
does so by the rule of the mode: the sandbox is the layer which the sandboxed
process owns, so a value the sandbox holds is modified in the hive while a value
it does not hold is modified in the host. A value the sandbox **deleted** counts
as held: the deletion is recorded in the whiteout store, so a later write of that
value lands in the sandbox again and the value of the host stays hidden — without
that rule the value of the host would reappear with the data the sandbox deleted.
A sub key the sandbox does not hold is created in the host, and the keys above it
are created in the host as well, because a registry key needs its parent.

`Write Copy` never hands out a real key handle for a write access open: when the
hive does not hold the key but the host does, the key is **copied up** — a shadow
key is created inside the hive and returned to the caller, so the modification
lands in the sandbox while the merged enumeration and the read through keep the
entries of the host key visible. A key which neither layer holds is still
reported as missing, because an open never creates a key.

`Hide` hides the entry completely: the mode keeps the host entry invisible **and**
the packed content of the entry is placed in the hidden store of the hive while
the packer runs, so the path of the view carries nothing either. The entry is
therefore reported as missing until the sandboxed process writes it, and the
write lands in the view of the sandbox, which is what makes the entry visible from
that point on — the same behaviour the `Whiteout` mode of the filesystem
isolation has for a folder. The content of the hidden store is not lost: a layer
which gives the entry another mode makes it visible again, see
[Patch layers](#patch-layers).

### Value modes

The mode of a value decides how the value itself is seen:

| Mode | Visible for the sandbox | Writes |
| --- | --- | --- |
| `Full` | the value of the hive; the value of the host is invisible | redirected into the hive |
| `Hide` | neither the value of the hive nor the value of the host: the value is reported as missing | redirected into the hive; the value is visible from that point on |

A value has no mode of its own when the isolation file does not list it: it
follows the mode of the key which holds it, so a value below a `Merge` key reads
the value of the host through, a value below a `Write Copy` key is copied up
with its key, and a value below a `Full` or `Hide` key is answered by the hive
alone.

### Resolving the mode of an entry

The mode of an entry which the isolation file lists is that mode. The mode of
every other entry — a key the sandboxed process creates while it runs, or an
entry of a hand written file — is resolved by walking the path upwards:

1. the entry itself when the file lists it,
2. otherwise the closest listed key above it.

`Hide` is the exception of the rule and covers its subtree from any level: a key
below a hidden key is hidden as well, whatever mode it names itself, because the
whole subtree lives in the hidden store of the hive.

There is no default mode. The isolation file of the archive has to name the five
root keys of the view, so the mode of every path of the view resolves; a file
which misses one of them fails the start of the sandbox instead of showing the
host registry. That is what makes `Full` of a key hide the host entries below it
as well, including the ones the workspace of the packer does not even know
about.

## On-disk layout

```
app\registry\user.hiv                    the hive of the virtual registry
app\registry\isolation.json              the isolation modes
data\registry\user.hiv                   the hive the sandbox mounts
```

The isolation file is read and written as the structure of its schema:
`common/RegistryIsolation.hpp` describes a key entry, a value entry and the
document, and both sides convert it with `to_json()` and `from_json()` instead of
reading or writing the members of a JSON object. An entry whose members are
incomplete, which are of another type, or which names an unknown mode is
therefore refused while the file is read, which is what keeps the packer and the
sandbox in step.

The hive holds the five root keys of the view, the hidden store of the entries
whose mode is `Hide` and, below a reserved key which is not part of the view, the
**whiteout store** of the entries the sandbox deleted:

```
<root of the hive>
├── HKEY_CLASSES_ROOT                  the five root keys of the view
├── HKEY_CURRENT_CONFIG
├── HKEY_CURRENT_USER
├── HKEY_LOCAL_MACHINE
├── HKEY_USERS
├── APPBOX_HIDDEN                      the content of the hidden entries, it
│   └── HKEY_CURRENT_USER\...          mirrors the structure of the view
└── APPBOX_WHITEOUT                    the whiteout store, invisible for the view
    ├── K\HKEY_CURRENT_USER\...        the key of that path was deleted
    └── V\HKEY_CURRENT_USER\...        one value per deleted value name, the
                                       empty name for the default value
```

The hidden store is created by the packer and holds the content of the entries
whose mode is `Hide`, which is what makes the entry invisible while its content
is kept: the view of the hive does not carry it, so a read is answered by the
mode and a write lands in the view, and the packed content is still there for a
layer which gives the entry another mode. The store is only created when the
model holds a hidden entry, so an archive without one carries no store at all.

The hive of the resources is a read-only resource: mounting a hive writes to the
file, so the launcher copies it into the state directory of the sandbox on the
first run and the sandbox mounts that copy. A patch package carries a hive of its
own, which the launcher merges into that copy before the sandbox mounts it (see
[Patch layers](#patch-layers)). The mounted hive is a real registry file: it
grows as the sandboxed process writes keys and values and survives process
restarts, so a sandbox can be reused — a delete survives with it, because the
marker lives in the same file. Deleting the state directory (or just these
files) discards every registry modification the sandboxed process ever made and
brings back the registry of the archive.

Every process of the sandbox mounts the file itself, and a file which another
process already mounted is served by the hive of that mount: the second process
reports the mount name of the first one and addresses the same hive. The
processes of one sandbox therefore share their registry state — a key or a value
one of them writes is visible to the others while they run, a process which
starts a helper process reads the registry of the helper while both of them run,
and the state of the whole run reaches the file when the last mount is closed, so
no process overwrites the state of another one. The mount is a call of the
process: a mount which fails — the token of the process may not load a hive, an
AppContainer for example — fails the sandbox of that process, because the
registry isolation would otherwise stop silently.

The isolation file is UTF-8 JSON:

```json
{
  "version": 1,
  "keys":   [ { "path": "HKEY_CURRENT_USER\\Software\\Vendor", "isolation": "merge" } ],
  "values": [ { "path": "HKEY_CURRENT_USER\\Software\\Vendor", "name": "Server",
                "isolation": "hide" } ]
}
```

* `path` is the key path from the root of the hive, which is the path of the
  entry in the workspace of the packer.
* `name` is the value name; an empty name addresses the default value of a key.
* `isolation` of a **key** entry is one of `full`, `merge`, `write_copy` and
  `hide`; the comparison ignores the case and accepts spaces or dashes instead
  of the underscore.
* `isolation` of a **value** entry is one of `full` and `hide`. A value is never
  copied up and a write of a value never reaches the host registry, so the two
  modes of a key are refused for a value entry: a file which names one is
  rejected as a whole.
* A file of another `version`, a malformed document, a missing file or a
  document which names no root key of the view fails the start of the sandbox
  with the reason in the log. A file of a patch package which cannot be used
  fails the start as well, while a package which carries no isolation file
  changes nothing (see [Patch layers](#patch-layers)).

`Build` of the packer writes the two artifacts into the registry domain of the
resources of the archive, which is where the launcher looks for them.

## Patch layers

The `patch` directory next to the launcher of a standalone archive carries patch
packages, and the registry domain of a package takes effect at every start (see
[PatchLayer.md](PatchLayer.md)):

* The hive of a package (`registry/user.hiv`) is **merged into the hive the
  sandbox mounts**: the keys and the values of the package replace the entries of
  the same name of the layers below it, and an entry no package names keeps the
  content of the archive and of the earlier runs. The merge is per key and per
  value, so a package which lists a single value keeps every other entry of the
  layers below it. The hidden store of the package is merged like every other
  sub key of the hive, so the content a package hides lands in the store of the
  run.
* The isolation modes of a package (`registry/isolation.json`) are applied on top
  of the modes of the archive: the sandbox reads the isolation files of the run
  in layer order, so the mode of the last file which names a key or a value is
  the mode the sandboxed process observes, while an entry no later file names
  keeps the mode of the layers below it.
* A package may name a mode which differs from the mode the layer below it packed
  the content with. The launcher **materializes** that difference right after it
  merged the hive of the package: an entry the package marks `Hide` is copied
  into the hidden store and removed from the view, and an entry the package gives
  another mode is copied out of the store into the view. The state of the sandbox
  wins over the packed content in the second direction, because a write of the
  run is the highest layer of the view.
* A package which carries no resource of the registry domain changes nothing
  here, and a hive which cannot be mounted is skipped with the registry of that
  package: a broken package never fails the run. An isolation file which cannot
  be used fails the start of the sandbox instead, because the modes of the run
  cannot be resolved without it.

The merge writes into the hive of the state directory, so a modification or a
deletion the sandboxed application made to an entry a package names is reset by
the next start, while an entry no package names keeps the state of the sandbox.

## Variable references

A value of the workspace may reference a known folder of the machine which runs
the sandbox with `%APPBOX:<NAME>%` instead of spelling its path out, so an
archive stays correct on a machine whose folders are somewhere else. The
references are replaced while the hive is mounted — which happens before the
hooks are attached — so every read path of the registry reports the expanded
value without a hook which would have to resize a caller buffer: the value
query, the enumeration, the batch query and the export of a key all see it.

The walk covers the string types of the registry only: `REG_SZ`, `REG_EXPAND_SZ`
and `REG_MULTI_SZ`, whose every item is expanded on its own. `REG_DWORD`,
`REG_BINARY` and every other type keep their bytes, even when they happen to
spell a reference. The hidden store is part of the walk, because it carries
values of the workspace which are visible again as soon as a layer gives the
entry another mode. The whiteout store is skipped, because it holds the
bookkeeping of the sandbox and no value of the workspace. A value is written back
only when the expansion changed it, so the walk is idempotent and a hive without
a reference is left exactly as it is.

The syntax, the supported names and the rules of the expansion are documented in
[README.md](../README.md#variable-expansion); the expansion itself is
`appbox::ExpandRegistryValueData()` of `sandbox/utils/VariableExpansion.*`.

## Deletion and whiteouts

The markers live in the whiteout store of the hive (see
[On-disk layout](#on-disk-layout)), which the view never reaches: no path of the
view maps onto the reserved key, and no handle of it is ever handed out. The
store has two namespaces:

* `K\<hive relative path>` — the key of that path was deleted. The lookup walks
  the path upwards, so a deleted key hides its whole subtree: the delete of a key
  removes everything below it as well.
* `V\<hive relative path>` — one value per deleted value name of that key, the
  empty name for the default value. The name is stored as a value name and not as
  a path component, because a value name may contain a backslash.

The store is consulted wherever the merged view decides whether a host entry
stays visible — open, create, value query, merged enumeration and merged counts
share one predicate, so the enumeration and the counts can never disagree. A
failed marker write fails the whole delete call: reporting a success would let
the host entry reappear in the view of the sandbox. A rename records the `K\`
marker of the name it leaves behind, so the old name stays invisible for the same
reason and the merged view reports the new name alone.

`Merge` is the exception of the rule: the delete of an entry the sandbox does not
hold lands in the real registry and records no marker, because the entry is gone
from the host as well. The marker of an entry the sandbox holds is recorded like
it is for every other mode, which is what keeps the entry of the host invisible
after the delete and what makes a later write of the entry land in the sandbox
again.

## Hooked entry points

| Hook | Responsibility |
| --- | --- |
| `NtOpenKey` / `NtOpenKeyEx` | Open policy: hive first, then the host per the isolation mode — read through, the host handle for a write of a merged key, or copy-up for `Write Copy`. |
| `NtOpenKeyTransacted` / `NtOpenKeyTransactedEx` | The same open policy with the transaction of the caller: the hive layer is opened with it, the host layer is never used. |
| `NtCreateKey` | Creates the key inside the hive, intermediate keys included; a merged key the sandbox does not hold is created in the real registry, the keys above it included. |
| `NtCreateKeyTransacted` | Creates the key and its intermediate keys inside the hive with the transaction of the caller; never reaches the real registry. |
| `NtSetValueKey` | Routes the write to the layer the mode of the value names: the sandbox for every mode but `Merge`, the real registry for a merged value the sandbox does not hold. |
| `NtDeleteKey` / `NtDeleteValueKey` | Removes the hive entry and records the visible host entry as deleted (a whiteout), or deletes the entry of the real registry for `Merge` when the sandbox does not hold it. |
| `NtRenameKey` | Renames the key of the hive and records the visible host key of the old name as deleted (a whiteout), or renames the key of the real registry for `Merge` when the sandbox does not hold it; a root key of the view and a destination which the merged view already holds are refused. |
| `NtEnumerateKey` / `NtEnumerateValueKey` | Merged two layer enumeration: hive entries first, then the visible real entries. |
| `NtQueryKey` | Key name translated back into the view; `SubKeys` / `Values` counts of the merged view. |
| `NtQueryValueKey` / `NtQueryMultipleValueKey` | Read through of a value / of a batch. |
| `NtSaveKey` / `NtSaveKeyEx` | Exports the merged view of a key. |
| `NtSetInformationKey` / `NtFlushKey` / `NtCompressKey` / `NtLockRegistryKey` | A call which acts on the key or on the hive file of its handle: a handle of the host layer is refused with `STATUS_ACCESS_DENIED`, a handle of the hive is forwarded. |
| `NtQueryObject` | Object names below the private hive mount are translated into the view path. |

Handles below the private hive mount run the merged logic; a handle of the host
layer is refused where the call would act on the key of the host or on the hive
file which holds it (see the table), and every other handle (handles of other
roots) is forwarded unchanged. A redirected handle is translated back into the
view path wherever its name is queried, so it behaves exactly like the key it
shadows.

The value entry points are the ones which decide their layer themselves: a write
or a delete of a value the sandbox does not hold below a merged key runs against
the real registry, and the isolation opens the key of that layer (and creates it
when the host does not hold it) instead of acting on the handle of the caller.
The granted access of the handle of the caller is tested before the re-route, so
a caller which opened the key without the right to write does not gain it through
the isolation.

A rename which the isolation answers is a modification of the view alone, so the
isolation adds three refusals of its own to it. A rename of a **root key of the
view** is refused with `STATUS_ACCESS_DENIED`: the name of a root key is the
first component of every path of the hive, so a rename would make the whole
subtree unreachable for the view, and the kernel refuses the rename of the root
of a hive the same way. A rename onto a name which the **merged view already
holds** is refused with `STATUS_CANNOT_DELETE`, which is the status the kernel
reports for a destination key which exists: the renamed key would otherwise be
merged with a visible key of the host layer. A new name which is empty, which
carries a separator or which cannot be read is refused with
`STATUS_INVALID_PARAMETER`, which is what the kernel reports for a name which
does not name a key of the same parent; a forwarded rename would leave the host
key of the old name in the view, which is why the isolation never forwards one.

The three transacted entry points run the open policy and the create policy of
the isolation with one difference: the transaction of the caller is forwarded to
the hive layer, so a handle the caller receives from them is bound to that
transaction and the changes the caller makes through it are committed or rolled
back with it, inside the sandbox. The host layer is never opened with the
transaction: a handle of the host layer would enlist the real hive into the
transaction of the sandboxed process, which writes the transaction log of the
real hive and lets the transaction of the caller collide with a transaction
another process holds on that hive. A transacted call is never answered with a
plain (non transacted) operation either, because the rollback of the caller would
silently stop working. The rows of the transacted policy are therefore:

| Call | The hive layer answers | The hive layer does not answer |
| --- | --- | --- |
| open, read access | the key of the hive, bound to the transaction | `STATUS_NOT_SUPPORTED`: the answer would be a handle of the host layer |
| open, write access | the key of the hive, bound to the transaction | the key is copied up into the hive with the transaction for `Write Copy`, `STATUS_NOT_SUPPORTED` for `Merge`, or the failure of the hive layer is reported |
| create | the key of the hive, its intermediate keys included, bound to the transaction | a transacted create always lands in the hive |

The hive of the sandbox is an application hive (`RegLoadAppKey`), and an
application hive does not support transactions: the kernel refuses every
transacted operation on one with `STATUS_RM_NOT_ACTIVE`. A transacted call of a
key of the view therefore fails — with `STATUS_RM_NOT_ACTIVE` for the create and
for the write access open, which is the failure of the hive layer reported as it
is, and with `STATUS_NOT_SUPPORTED` for the read access open, which the isolation
refuses instead of reading it through. The call fails closed instead of being
forwarded to the real registry, which is the layer a sandboxed process would
reach without the isolation.

The table is the whole surface of the isolation: an entry point which is not
named in it is forwarded to the real registry with the path or the handle of the
caller, and the answer of such a call is the answer of the layer the object
belongs to. [Known gaps and limitations](#known-gaps-and-limitations) lists the
points which matter.

## Tests

The unit tests and the end-to-end cases of the registry isolation are listed in
[test/README.md](../test/README.md).

## Known gaps and limitations

The points below are visible in the current code and have to be kept in mind when
the isolation is extended or tested. They fall into two groups. The first group
is about the semantics the view cannot express: the hook answers, but its answer
is the one of a single layer rather than the one of the composed view. The second
group is about the entry points the isolation does not hook at all: a call which
reaches the registry through them acts on the layer of the object of the call.

### The semantics the view cannot express

1. **A delete of the sandbox is recorded, not replayed.** An entry the sandbox
   deletes below a mode other than `Merge` stays in the real registry; only the
   view of the sandbox hides it. Another process which reads the real registry
   still sees the entry, and a second sandbox which mounts the same hive hides it
   as well, because the marker lives in the hive. The marker is never removed
   again, so the entry stays invisible for the whole life of the hive: a delete
   the sandbox runs and the create which follows it inside the sandbox are
   consistent, because the hive holds the key that create made, while an entry
   which the host creates again after the delete is never reported, which the
   real registry would report. `Merge` is the mode which deletes the entry of the
   host itself when the sandbox does not hold it, so it has neither the marker
   nor the gap.
2. **A save exports a snapshot.** The file holds the merged view at the time of
   the call: a host value which the mode hides is not part of it, and a value
   which the sandbox writes after the call is not either. The save needs
   `SeBackupPrivilege`, which the caller has to enable like a direct save does.
3. **The private mount path is visible to the other name queries.**
   `NtQueryObject` and `NtQueryKey` translate names of redirected handles back
   into the view path, but other name sources (for example
   `NtQueryKey(KeyFlagsInformation)` variants, handle duplication across
   processes, or a query entry point the isolation does not hook, see the second
   group below) may still expose the mount.
4. **`HKEY_CLASSES_ROOT` is a root key of its own.** The kernel merges
   `HKCU\Software\Classes` into the classes root of the machine hive when the
   real path is used. The virtual registry keeps `HKEY_CLASSES_ROOT` as an
   independent sub tree of the hive, and entries below
   `HKEY_CURRENT_USER\Software\Classes` are reached through the current user root
   only.
5. **A value mode needs a key of the hive.** The value modes of an isolation file
   are applied by the entry points which decide their layer themselves, and by
   the merged view of a key the hive holds. A key which only the host holds is
   answered by a handle of the host layer, which the read entry points forward
   unchanged, so the read of a `Full` or `Hide` value of such a key still reports
   the value of the host and its enumeration still lists the name. The write of
   that value is redirected into the sandbox, because `NtSetValueKey` resolves
   the layer itself. A key the archive holds — the normal case of a workspace
   which lists modes for the values of its keys — is opened inside the hive, which
   is what makes the modes apply to the reads as well.
6. **A hidden entry needs a packer which places its content.** The content of a
   `Hide` entry lives in the hidden store of the hive, which the packer fills
   while it writes the artifacts of the workspace. An entry which the workspace
   marks `Hide` and which the packer did not place there — a hand written hive,
   for example — is reported as missing like every hidden entry, and a layer which
   gives it another mode makes nothing visible, because there is no content to
   reveal.
7. **The registry view of a 32 bit process is not applied to the hive.** The
   kernel separates the views of the registry by rewriting the path of a key for
   `KEY_WOW64_32KEY`, for `KEY_WOW64_64KEY` and for the default view of a 32 bit
   process, and the hive lives below `\REGISTRY\A`, which is none of the paths it
   rewrites. Both views therefore address one key of the hive: a value a 32 bit
   process writes to `HKEY_LOCAL_MACHINE\SOFTWARE` is the value a 64 bit process
   of the same sandbox reads there, while the real registry keeps the two apart.
   The host layer behaves the other way round: the read through passes the access
   mask of the caller to the open of the real key, so the kernel rewrites that
   open, while the merged enumeration and the merged counts open the real key with
   a fixed mask (`KEY_QUERY_VALUE` and `KEY_ENUMERATE_SUB_KEYS`) and therefore
   with the view of the process. An enumeration and a read of one key can
   therefore describe two different views of the real registry, and the entries of
   the hive are described by neither of them.
8. **A key handle which enters the sandbox from outside is used unchanged.** The
   isolation decides at the open, at the create and at the value entry points, and
   it never hands out a handle which permits a modification of a key of the host
   layer (an open which asks for a write right is copied up into the hive). A
   handle which the sandbox receives from another process — a duplicated handle,
   an inherited handle, or a handle a host process passes over an IPC channel —
   was not opened by the hooks, so it addresses the key of the layer it was opened
   in, and a value entry point which acts on it re-routes the call by the mode of
   the value, while a key level call still acts on that layer. The isolation
   covers the keys a sandboxed process opens itself.

### The entry points the isolation does not hook at all

The hooks of the isolation are the ones `sandbox/hook/` installs, so an entry
point which is not named in [Hooked entry points](#hooked-entry-points) reaches
the registry with the path or the handle of the caller, and the answer of such a
call is the answer of the layer the object belongs to. The points below are the
ones which matter for the view.

1. **The load and the unload of a hive.** `NtLoadKey`, `NtLoadKey2`, `NtLoadKey3`
   and `NtLoadKeyEx` name the key a hive is loaded into with an
   `OBJECT_ATTRIBUTES` and not with a key handle, and `NtUnloadKey`,
   `NtUnloadKey2` and `NtUnloadKeyEx` name the key they unload the same way, so
   the rule of the handle does not cover them: a sandboxed process which holds the
   right to load a hive (a process which runs elevated, for example) can load one
   at a path of the view and unload a hive of the host, and both modify the real
   registry.
2. **`NtReplaceKey` and `NtRestoreKey`.** The two are not hooked, but they act on
   a key handle, and a handle which permits a modification is a handle of the
   hive: a replace or a restore of a key of the view lands in the hive of the
   sandbox and never in the real registry. A key of the host layer is only reached
   for `Merge`, which is the mode whose handle of the host layer permits a
   modification.
3. **`NtSaveMergedKeys`.** The call is not hooked, so the layers it writes into
   the file are the ones of the two key handles the caller passes: a read through
   handle of the host layer contributes the entries of the real key, including the
   ones an isolation mode or a whiteout hides, and the merged view `NtSaveKey`
   exports is not assembled here.
4. **The change notification.** `NtNotifyChangeKey` and
   `NtNotifyChangeMultipleKeys` are not hooked. A watch a sandboxed process
   registers on a key it opened for reading is a watch of the real key, so the
   process is notified about a change another process makes to the real registry
   and it is not notified about the change the sandbox itself makes, because that
   one lands in the hive. A watch on a handle of the hive observes the hive alone,
   so neither kind of watch describes the merged view.
5. **The remaining key APIs.** `NtQueryOpenSubKeys`, `NtQueryOpenSubKeysEx` and
   `NtInitializeRegistry` are forwarded unchanged. The first two name the key they
   act on with an `OBJECT_ATTRIBUTES` and not with a key handle, and the layout of
   their arguments is not documented, so the isolation cannot tell which layer a
   call names; the kernel answers them with `STATUS_PRIVILEGE_NOT_HELD` for a
   caller whose token does not hold the privilege the call asks for, which is what
   an ordinary application receives, while a sandboxed process which holds the
   privilege (a process which runs elevated, for example) reaches the real
   registry through them. `NtInitializeRegistry` names no key at all — it takes
   the boot condition of the system — so there is no layer to choose and no path
   to redirect. The key APIs which act on the object of a key handle
   (`NtSetInformationKey`, `NtFlushKey`, `NtCompressKey` and `NtLockRegistryKey`)
   are hooked and refuse a handle of the host layer, see
   [Hooked entry points](#hooked-entry-points). None of the three is part of a
   path an ordinary application uses; they are listed so that a call which reaches
   one of them is not read as a call the isolation answered.
