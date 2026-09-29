# Registry Isolation

appbox isolates the registry of a sandboxed process by hooking the key open /
create / delete / save entry points of the NT registry API and redirecting them
onto a private **hive** file inside the overlay. The method is known as *hive
redirection*: instead of replaying every registry operation on a copy of the
real data, the sandbox mounts one real registry file and points every
redirected key at it. Most value level APIs (`NtSetValueKey`, ...) are not
hooked at all — they operate on whatever key handle the open / create returned,
which is already a handle into the hive. `NtQueryValueKey` and
`NtQueryMultipleValueKey` are the exceptions: they provide the read through,
and `NtDeleteValueKey` records the deletion, see below.

The hive holds **one sub key per root key of the view**, so the path of an
entry inside the hive equals its path in the registry workspace of the packer
and in the isolation file:

```
<root of the hive>
├── HKEY_CLASSES_ROOT
├── HKEY_CURRENT_USER
├── HKEY_LOCAL_MACHINE
├── HKEY_USERS
└── HKEY_CURRENT_CONFIG
```

The **isolation modes** of the entries travel next to the hive as a JSON
document (`isolation.json`). They decide which host entries stay visible and
where a modification lands: `Full` and `Hide` keep the host entry invisible and
`WriteCopy` keeps it visible. No mode ever writes to the host registry. All
three modes are enforced in full; the decision table behind them is the pure
module `sandbox/registry/IsolationPolicy.hpp` (see
[Isolation modes](#isolation-modes)). The limitations which remain are listed
in [Known gaps and limitations](#known-gaps-and-limitations).

A delete is a modification as well, and the host registry has no way to remove
an entry of the real registry on behalf of the sandbox. The isolation therefore
records the deletion inside the hive as a **whiteout**: the entry of the hive
is removed (when the hive holds one) and the entry of the host is marked as
deleted, so the merged view reports it as gone while the real registry keeps
its data. The markers live in a reserved key at the root of the hive, which is
not part of the view at all (see
[Deletion and whiteouts](#deletion-and-whiteouts)).

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

Reads and writes behave differently. The rows are written from the point of
view of a key which the hive holds; the behaviour of a key which only the host
holds is the one the isolation mode decides:

| Operation | Hive holds the key | Hive does not hold the key |
| --- | --- | --- |
| **open / read** (`NtOpenKey(Ex)`) | redirected into the hive | `WriteCopy` reads the host through, `Full` and `Hide` report that the key does not exist |
| **open / write** (`NtOpenKey(Ex)`) | redirected into the hive | `WriteCopy` copies the key up into the hive, `Full` and `Hide` report that the key does not exist |
| **create / write** (`NtCreateKey`) | redirected into the hive | the key is created inside the hive, intermediate keys included |
| **enumerate** (`NtEnumerateKey`, `NtEnumerateValueKey`) | merged view: hive entries first, then the non shadowed and visible real entries | visible real entries only |
| **value query** (`NtQueryValueKey`) | answered from the hive | falls back to the real registry (read through), unless the mode of the value hides the host |
| **value batch query** (`NtQueryMultipleValueKey`) | merged view: every entry is answered by the layer which holds it | visible real entries only |
| **delete key** (`NtDeleteKey`) | the key of the hive is removed and the visible host key is recorded as deleted | the visible host key is recorded as deleted (a whiteout) |
| **delete value** (`NtDeleteValueKey`) | the value of the hive is removed and the visible host value is recorded as deleted | the visible host value is recorded as deleted (a whiteout) |
| **save** (`NtSaveKey`, `NtSaveKeyEx`) | the merged view of the key is written into the caller file | the visible real entries are written into the caller file |

A sandboxed process therefore never modifies the real registry: every
modification lands in the hive, while values which only exist in the real
registry remain readable. A delete which runs on a handle of the host layer is
refused with `STATUS_ACCESS_DENIED`, because a handle which the caller opened
without the right to delete must not become the right to delete.

The registry workspace of the packer, which describes the registry the
packaged application will see, is documented in [README.md](../README.md).

## Isolation modes

The mode of a key or of a value describes how the sandboxed process sees the
entry:

| Mode | Visible for the sandbox | Writes |
| --- | --- | --- |
| `Full` | only the hive; the host entry is invisible even when the host holds it | redirected into the hive |
| `WriteCopy` | host and hive, the hive wins | redirected into the hive; a host key which the hive does not hold is copied up first |
| `Hide` | only the hive; the host entry is invisible | redirected into the hive |

`WriteCopy` is the default of every entry which the isolation file does not
mention, so a sandbox without an isolation file keeps the read through
behaviour. `WriteCopy` never hands out a real key handle for a write access
open: when the hive does not hold the key but the host does, the key is
**copied up** — a shadow key is created inside the hive and returned to the
caller, so the modification lands in the sandbox while the merged enumeration
and the read through keep the entries of the host key visible. A key which
neither layer holds is still reported as missing, because an open never
creates a key.

Every key and every value of the workspace carries an explicit mode, and the
isolation file lists all of them. An entry which the file does not mention — a
key the sandboxed process creates while it runs, or an entry of a hand written
isolation file — is resolved by walking the path upwards:

1. the entry itself when the file lists it,
2. otherwise the closest listed key above it,
3. otherwise `WriteCopy`.

That is what makes `Full` of a key hide the host entries below it as well,
including the ones the workspace of the packer does not even know about.

## On-disk layout

```
app\registry\user.hiv                    the hive of the virtual registry
app\registry\isolation.json              the isolation modes
data\registry\user.hiv                   the hive the sandbox mounts
```

The isolation file is read and written as the structure of its schema:
`common/RegistryIsolation.hpp` describes a key entry, a value entry and the
document, and both sides convert it with `to_json()` and `from_json()` instead
of reading or writing the members of a JSON object. An entry whose members are
incomplete, which are of another type, or which names an unknown mode is
therefore refused while the file is read, which is what keeps the packer and the
sandbox in step.

The hive holds the five root keys of the view and, below a reserved key which
is not part of the view, the **whiteout store** of the entries the sandbox
deleted:

```
<root of the hive>
├── HKEY_CLASSES_ROOT                  the five root keys of the view
├── HKEY_CURRENT_CONFIG
├── HKEY_CURRENT_USER
├── HKEY_LOCAL_MACHINE
├── HKEY_USERS
└── APPBOX_WHITEOUT                    the whiteout store, invisible for the view
    ├── K\HKEY_CURRENT_USER\...        the key of that path was deleted
    └── V\HKEY_CURRENT_USER\...        one value per deleted value name, the
                                       empty name for the default value
```

The hive of the resources is a read-only resource: mounting a hive writes to the
file, so the loader copies it into the state directory of the sandbox on the
first run and the sandbox mounts that copy. The mounted hive is a real registry
file: it grows as the sandboxed process writes keys and values and survives
process restarts, so a sandbox can be reused — a delete survives with it,
because the marker lives in the same file. Deleting the state directory (or
just these files) discards every registry modification the sandboxed process
ever made and brings back the registry of the archive.

The isolation file is UTF-8 JSON:

```json
{
  "version": 1,
  "keys":   [ { "path": "HKEY_CURRENT_USER\\Software\\Vendor", "isolation": "full" } ],
  "values": [ { "path": "HKEY_CURRENT_USER\\Software\\Vendor", "name": "Server",
                "isolation": "hide" } ]
}
```

* `path` is the key path from the root of the hive, which is the path of the
  entry in the workspace of the packer.
* `name` is the value name; an empty name addresses the default value of a key.
* `isolation` is one of `full`, `write_copy` and `hide`; the comparison
  ignores the case and accepts spaces or dashes instead of the underscore. The
  removed mode `merge` is **not** accepted anymore: a file which still carries
  it is rejected as a whole, so a sandbox built from an older archive falls
  back to `WriteCopy` for every entry until the archive is packed again.
* A file of another `version`, a malformed document or a missing file is
  reported in the log and ignored: the sandbox then treats every entry as
  `WriteCopy` instead of failing to start.

`Build` of the packer writes the two artifacts into the registry domain of the
resources of the archive, which is where the loader looks for them.

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
spell a reference. The whiteout store is skipped, because it holds the
bookkeeping of the sandbox and no value of the workspace. A value is written
back only when the expansion changed it, so the walk is idempotent and a hive
without a reference is left exactly as it is.

The syntax, the supported names and the rules of the expansion are documented in
[README.md](../README.md#variable-expansion); the expansion itself is
`appbox::ExpandRegistryValueData()` of `sandbox/utils/VariableExpansion.*`.

## Deletion and whiteouts

The markers live in the whiteout store of the hive (see
[On-disk layout](#on-disk-layout)), which the view never reaches: no path of
the view maps onto the reserved key, and no handle of it is ever handed out.
The store has two namespaces:

* `K\<hive relative path>` — the key of that path was deleted. The lookup walks
  the path upwards, so a deleted key hides its whole subtree: the delete of a
  key removes everything below it as well.
* `V\<hive relative path>` — one value per deleted value name of that key, the
  empty name for the default value. The name is stored as a value name and not
  as a path component, because a value name may contain a backslash.

The store is consulted wherever the merged view decides whether a host entry
stays visible — open, create, value query, merged enumeration and merged counts
share one predicate, so the enumeration and the counts can never disagree. A
failed marker write fails the whole delete call: reporting a success would let
the host entry reappear in the view of the sandbox.

## Hooked entry points

| Hook | Responsibility |
| --- | --- |
| `NtOpenKey` / `NtOpenKeyEx` | Open policy: hive first, read through or copy-up of the host key per the isolation mode. |
| `NtCreateKey` | Creates the key inside the hive, intermediate keys included; never reaches the real registry. |
| `NtDeleteKey` / `NtDeleteValueKey` | Removes the hive entry and records the visible host entry as deleted (a whiteout). |
| `NtEnumerateKey` / `NtEnumerateValueKey` | Merged two layer enumeration: hive entries first, then the visible real entries. |
| `NtQueryKey` | Key name translated back into the view; `SubKeys` / `Values` counts of the merged view. |
| `NtQueryValueKey` / `NtQueryMultipleValueKey` | Read through of a value / of a batch. |
| `NtSaveKey` / `NtSaveKeyEx` | Exports the merged view of a key. |
| `NtQueryObject` | Object names below the private hive mount are translated into the view path. |

Handles below the private hive mount run the merged logic; every other handle
(real fallback handles, handles of other roots) is forwarded unchanged. A
redirected handle is translated back into the view path wherever its name is
queried, so it behaves exactly like the key it shadows.

## Loader registry browser

The admin UI of the loader (`enable_admin_ui`) contains a read-only registry
browser which mirrors the layout of the Windows registry editor. It mounts
`data\registry\user.hiv` itself — the hive the loader seeded and the sandbox
mounts — and reads everything relative to the returned root handle — the host registry is never touched, and keys which only
exist in the real registry (the read through of the sandbox) are not part of
the view. The loader never writes to the hive, and the whiteout store stays
hidden. `Refresh` (F5) releases and remounts the file, picking up everything
the sandboxed process flushed to disk; when the sandbox has not created the
hive yet, the browser shows an empty tree with a hint instead of an error.

## Tests

The unit tests and the end-to-end cases of the registry isolation are
listed in [test/README.md](../test/README.md).

## Known gaps and limitations

1. **The remaining write side APIs are not hooked.** `NtRenameKey`,
   `NtReplaceKey`, `NtRestoreKey`, `NtLoadKey*` and `NtUnloadKey*` are not
   hooked. They act on a key handle, and a handle which permits a modification
   is a handle of the hive (an open which asks for a write right is copied up),
   so none of them can reach the real registry — but the isolation adds no
   policy of its own to them either: a rename of a shadow key renames the key
   inside the hive, for example, and leaves the host key of the old name in
   place, where the merged view then shows both.
2. **A delete is recorded, not replayed.** The host entry which the sandbox
   deleted stays in the real registry; only the view of the sandbox hides it.
   Another process which reads the real registry still sees the entry, and a
   second sandbox which mounts the same hive hides it as well, because the
   marker lives in the hive. Markers are never removed again, so an entry which
   was deleted once stays invisible for the sandbox even when the host creates
   it again — the merged view then shows the shadow of the sandbox only, which
   is what a delete followed by a create does in the real registry as well.
3. **A save exports a snapshot.** The file holds the merged view at the time of
   the call: a host value which the mode hides is not part of it, and a value
   which the sandbox writes after the call is not either. The save needs
   `SeBackupPrivilege`, which the caller has to enable like a direct save does.
4. **The private mount path is visible to other name queries.**
   `NtQueryObject` and `NtQueryKey` translate names of redirected handles back
   into the view path, but other name sources (for example
   `NtQueryKey(KeyFlagsInformation)` variants or handle duplication across
   processes) may still expose the mount.
5. **`HKEY_CLASSES_ROOT` is a root key of its own.** The kernel merges
   `HKCU\Software\Classes` into the classes root of the machine hive when the
   real path is used. The virtual registry keeps `HKEY_CLASSES_ROOT` as an
   independent sub tree of the hive, and entries below
   `HKEY_CURRENT_USER\Software\Classes` are reached through the current user
   root only.
6. **A sandbox which creates its own hive has no modes.** An archive without
   the two registry artifacts of the packer still redirects every write into
   the hive, but every entry keeps the default `WriteCopy`, so the host
   registry stays visible.
