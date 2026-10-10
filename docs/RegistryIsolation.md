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

A sandboxed process therefore never modifies the real registry through an entry
point the isolation hooks: every modification lands in the hive, while values
which only exist in the real registry remain readable. A delete which runs on a
handle of the host layer is refused with `STATUS_ACCESS_DENIED`, because a
handle which the caller opened without the right to delete must not become the
right to delete. The entry points which the isolation does not hook at all are
listed in [Known gaps and limitations](#known-gaps-and-limitations).

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
file, so the launcher copies it into the state directory of the sandbox on the
first run and the sandbox mounts that copy. A patch package carries a hive of
its own, which the launcher merges into that copy before the sandbox mounts it
(see [Patch layers](#patch-layers)). The mounted hive is a real registry file:
it grows as the sandboxed process writes keys and values and survives process
restarts, so a sandbox can be reused — a delete survives with it, because the
marker lives in the same file. Deleting the state directory (or just these
files) discards every registry modification the sandboxed process ever made and
brings back the registry of the archive.

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
resources of the archive, which is where the launcher looks for them.

## Patch layers

The `patch` directory next to the launcher of a standalone archive carries patch
packages, and the registry domain of a package takes effect at every start (see
[PatchLayer.md](PatchLayer.md)):

* The hive of a package (`registry/user.hiv`) is **merged into the hive the
  sandbox mounts**: the keys and the values of the package replace the entries
  of the same name of the layers below it, and an entry no package names keeps
  the content of the archive and of the earlier runs. The merge is per key and
  per value, so a package which lists a single value keeps every other entry of
  the layers below it.
* The isolation modes of a package (`registry/isolation.json`) are applied on
  top of the modes of the archive: the sandbox reads the isolation files of the
  run in layer order, so the mode of the last file which names a key or a value
  is the mode the sandboxed process observes, while an entry no later file names
  keeps the mode of the layers below it. A file which cannot be parsed is
  logged and skipped, which keeps the modes below it in place.
* A package which carries no resource of the registry domain changes nothing
  here, and a hive which cannot be mounted is skipped with the registry of that
  package: a broken package never fails the run.

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

The table is the whole surface of the isolation: an entry point which is not
named in it is forwarded to the real registry with the path or the handle of the
caller, and the answer of such a call is the answer of the layer the object
belongs to. [Known gaps and limitations](#known-gaps-and-limitations) lists the
points which matter.

## Tests

The unit tests and the end-to-end cases of the registry isolation are
listed in [test/README.md](../test/README.md).

## Known gaps and limitations

The points below are visible in the current code and have to be kept in mind
when the isolation is extended or tested. They fall into two groups. The first
group is about the semantics the view cannot express: the hook answers, but its
answer is the one of a single layer — or of a single mount of the hive — rather
than the one of the composed view. The second group is about the entry points
the isolation does not hook at all: a call which reaches the registry through
them acts on the layer of the object of the call.

### The semantics the view cannot express

1. **A delete is recorded, not replayed.** The host entry which the sandbox
   deleted stays in the real registry; only the view of the sandbox hides it.
   Another process which reads the real registry still sees the entry, and a
   second sandbox which mounts the same hive hides it as well, because the
   marker lives in the hive. The marker is never removed again, so the entry
   stays invisible for the whole life of the hive: a delete the sandbox runs and
   the create which follows it inside the sandbox are consistent, because the
   hive holds the key that create made, while an entry which the host creates
   again after the delete is never reported, which the real registry would
   report.
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
   `HKEY_CURRENT_USER\Software\Classes` are reached through the current user
   root only.
5. **A value mode needs a key of the hive.** The value modes of an isolation
   file are applied by the merged view of a key the hive holds. A value of a
   key which only the host holds is read through the real key, whose handle is
   forwarded unchanged, so a `Full` or `Hide` mode of such a value has no
   effect. A key the archive holds — the normal case of a workspace which lists
   modes for the values of its keys — is opened inside the hive, which is what
   makes the modes apply. The rule is the same for the isolation file of a patch
   package: the mode of a package which names a value of a key only the host
   holds has no effect, because a package adds further files which name modes
   and does not change how a mode is read. The end-to-end cases of the patch
   layers pin that by listing the modes of a key the hive of the archive holds.
6. **A sandbox which creates its own hive has no modes.** An archive without
   the two registry artifacts of the packer still redirects every write into
   the hive, but every entry keeps the default `WriteCopy`, so the host
   registry stays visible.
7. **The registry view of a 32 bit process is not applied to the hive.** The
   kernel separates the views of the registry by rewriting the path of a key
   for `KEY_WOW64_32KEY`, for `KEY_WOW64_64KEY` and for the default view of a
   32 bit process, and the hive lives below `\REGISTRY\A`, which is none of the
   paths it rewrites. Both views therefore address one key of the hive: a value
   a 32 bit process writes to `HKEY_LOCAL_MACHINE\SOFTWARE` is the value a 64
   bit process of the same sandbox reads there, while the real registry keeps
   the two apart. The host layer behaves the other way round: the read through
   passes the access mask of the caller to the open of the real key, so the
   kernel rewrites that open, while the merged enumeration and the merged counts
   open the real key with a fixed mask (`KEY_QUERY_VALUE` and
   `KEY_ENUMERATE_SUB_KEYS`) and therefore with the view of the process. An
   enumeration and a read of one key can therefore describe two different views
   of the real registry, and the entries of the hive are described by neither of
   them.
8. **The hive is mounted by the sandboxed process.** The sandbox library of
   every process of the sandbox mounts the hive file itself, and an application
   hive is private to the process which mounted it. Two processes of one sandbox
   therefore hold two mounts of the same file: a key or a value one of them
   writes is not visible to another one which is already running, and every
   mount writes the file back when its process ends, so the file keeps the state
   of the mount which wrote last. An application which starts a helper process
   reads the registry of the helper only after the run. The mount also fails
   when the token of the process may not load a hive (an AppContainer, for
   example), and a failed mount fails the sandbox of that process, because the
   registry isolation would otherwise stop silently.
9. **A key handle which enters the sandbox from outside is used unchanged.**
   The isolation decides at the open and at the create, and it never hands out a
   handle which permits a modification of a key of the host layer (an open which
   asks for a write right is copied up into the hive). A handle which the
   sandbox receives from another process — a duplicated handle, an inherited
   handle, or a handle a host process passes over an IPC channel — was not
   opened by the hooks, so it addresses the key of the layer it was opened in,
   and the value level API which acts on it (`NtSetValueKey`, for example) is
   not hooked. The isolation covers the keys a sandboxed process opens itself.

### The entry points the isolation does not hook at all

The hooks of the isolation are the ones `sandbox/hook/` installs, so an entry
point which is not named in [Hooked entry points](#hooked-entry-points) reaches
the registry with the path or the handle of the caller, and the answer of such a
call is the answer of the layer the object belongs to. The points below are the
ones which matter for the view.

1. **The transacted open and the transacted create.** `NtOpenKeyTransacted`,
    `NtOpenKeyTransactedEx` and `NtCreateKeyTransacted` are not hooked, so the
    isolation does not see the call at all: the open and the create run against
    the real registry with the path of the caller, no isolation mode and no
    whiteout is consulted, and a key the caller creates this way is a key of the
    real registry. Every other write path of the isolation starts from a key
    handle the hooks handed out; this one does not.
2. **The load and the unload of a hive.** `NtLoadKey`, `NtLoadKey2`,
    `NtLoadKey3` and `NtLoadKeyEx` name the key a hive is loaded into with an
    `OBJECT_ATTRIBUTES` and not with a key handle, and `NtUnloadKey`,
    `NtUnloadKey2` and `NtUnloadKeyEx` name the key they unload the same way, so
    the rule of the handle does not cover them: a sandboxed process which holds
    the right to load a hive (a process which runs elevated, for example) can
    load one at a path of the view and unload a hive of the host, and both
    modify the real registry.
3. **The rename of a key.** `NtRenameKey` is not hooked, so the isolation adds
    no policy of its own to it: a rename of a shadow key renames the key inside
    the hive and leaves the host key of the old name in place, where the merged
    view then shows both. `NtReplaceKey` and `NtRestoreKey` are not hooked
    either, but they act on a key handle, and a handle which permits a
    modification is a handle of the hive.
4. **`NtSaveMergedKeys`.** The call is not hooked, so the layers it writes into
    the file are the ones of the two key handles the caller passes: a read
    through handle of the host layer contributes the entries of the real key,
    including the ones an isolation mode or a whiteout hides, and the merged
    view `NtSaveKey` exports is not assembled here.
5. **The change notification.** `NtNotifyChangeKey` and
    `NtNotifyChangeMultipleKeys` are not hooked. A watch a sandboxed process
    registers on a key it opened for reading is a watch of the real key, so the
    process is notified about a change another process makes to the real
    registry and it is not notified about the change the sandbox itself makes,
    because that one lands in the hive. A watch on a handle of the hive observes
    the hive alone, so neither kind of watch describes the merged view.
6. **The remaining key APIs.** `NtQueryInformationKey`, `NtSetInformationKey`,
    `NtQueryOpenSubKeys`, `NtQueryOpenSubKeysEx`, `NtFlushKey`, `NtCompressKey`,
    `NtLockRegistryKey` and `NtInitializeRegistry` are forwarded unchanged, so
    the property they report or change is the property of the object of the layer
    the call names: the hive of a redirected handle, the real key of a read
    through handle, and the real registry of a key a path names. None of them is
    part of a path an ordinary application uses; they are listed so that a call
    which reaches one of them is not read as a call the isolation answered.
