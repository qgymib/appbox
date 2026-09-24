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
registry remain readable. The disposition a create reports follows the merged
view of the mode: a key which only the host holds is reported as an existing
key for `WriteCopy`, while `Full` and `Hide` report a creation because the host
entry is invisible for them (see the `NtCreateKey` hook below).

The rows of a delete which only the host holds apply to the modes which keep
the host entry visible: `Full` and `Hide` report that the entry does not exist,
which is the same answer the open of that entry gives. A delete which runs on a
handle of the host layer is refused with `STATUS_ACCESS_DENIED`, because a
handle which the caller opened without the right to delete must not become the
right to delete (see the `NtDeleteKey` hook below).

## Isolation modes

The mode of a key or of a value describes how the sandboxed process sees the
entry:

| Mode | Visible for the sandbox | Writes |
| --- | --- | --- |
| `Full` | only the hive; the host entry is invisible even when the host holds it | redirected into the hive |
| `WriteCopy` | host and hive, the hive wins | redirected into the hive; a host key which the hive does not hold is copied up first |
| `Hide` | only the hive; the host entry is invisible | redirected into the hive |

`WriteCopy` keeps the hive in front. A name which both layers hold is therefore
answered by the hive: the value query and the sub key open return the entry of
the hive, and the merged enumeration lists the name **once**, so a shadow key
of the hive hides the host entry of the same name. That is the same rule the
directory merge of the filesystem isolation applies — a name which an upper
layer already emitted is dropped
([FilesystemIsolation.md](FilesystemIsolation.md), `NtQueryDirectoryFileEx`).
Everything the host key holds below a name which the hive does not shadow stays
visible: the read through answers the values which only the host holds and the
merged enumeration lists the host sub keys which the hive does not shadow.

`WriteCopy` is the default of every entry which the isolation file does not
mention, so a sandbox without an isolation file keeps the read through
behaviour of the previous iteration. `WriteCopy` never hands out a real key
handle for a write access open: when the hive does not hold the key but the
host does, the key is **copied up** — a shadow key is created inside the hive
and returned to the caller, so the modification lands in the sandbox while the
merged enumeration and the read through keep the entries of the host key
visible. A key which neither layer holds is still reported as missing, because
an open never creates a key.

A delete is a modification as well, and it follows the same rule: the entry of
the hive is removed, and a host entry which the mode keeps **visible** is
recorded as deleted, so it cannot come back through the read through. For
`Full` and `Hide` the host entry is invisible anyway, so no marker is needed;
the delete of an entry which only the host holds reports that the entry does
not exist for those modes.

Every key and every value of the workspace carries an explicit mode, and the
isolation file lists all of them. An entry which the file does not mention — a
key the sandboxed process creates while it runs, or an entry of a hand written
isolation file — is resolved by walking the path upwards:

1. the entry itself when the file lists it,
2. otherwise the closest listed key above it,
3. otherwise `WriteCopy`.

That is what makes `Full` of a key hide the host entries below it as well,
including the ones the workspace of the packer does not even know about.

## Configuration

The configuration flows from the loader down to the sandbox DLL:

1. `appbox::LoaderConfig` (`loader/Config.hpp`) — no new user facing field. Both
   files are derived from `overlay_fs`.
2. `appbox::MapRegistryFiles` (`loader/Loader.cpp`) — creates the
   `<overlay_fs>\registry` directory next to the `filesystem` overlay directory
   and stores `<overlay_fs>\registry\user.hiv` and
   `<overlay_fs>\registry\isolation.json` in the injected configuration. The
   hive file itself is **not** created here; the mount creates it on first use,
   so an empty overlay needs no template hive.
3. `appbox::SandboxConfig` (`sandbox/Config.hpp`) — carries
   `registry_hive_dos_path` and `registry_isolation_dos_path` (DOS paths,
   UTF-8).
4. `appbox::Sandbox` (`sandbox/Sandbox.hpp`, filled by `ParseInjectData` in
   `sandbox/Sandbox.cpp`) — `wRegistryHiveDOSPath` and
   `wRegistryIsolationDOSPath`, the runtime forms used by the registry module.

## On-disk layout

```
<overlay_fs>\registry\user.hiv            the hive of the virtual registry
<overlay_fs>\registry\isolation.json      the isolation modes
```

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

The hive is a real registry file. It is created by the first mount when it does
not exist and grows as the sandboxed process writes keys and values; it
survives process restarts, so a sandbox can be reused — a delete survives with
it, because the marker lives in the same file. Deleting the overlay (or
just these files) discards every registry modification the sandboxed process
ever made.

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

## AppBox registry workspace

The packer has a registry workspace next to the filesystem workspace
(`src/widget/RegistryPanel.*`) which describes the registry the packaged
application will see. Its layout mirrors the registry editor: the key tree on
the left, the child view of the selected key on the right.

* **Tree.** The top item is the `Sandbox Registry` container; the five root
  keys (`HKEY_CLASSES_ROOT`, `HKEY_CURRENT_USER`, `HKEY_LOCAL_MACHINE`,
  `HKEY_USERS`, `HKEY_CURRENT_CONFIG`) are always shown, even when nothing was
  imported. They can neither be renamed nor removed.
* **Child view.** The table shows the sub keys and the values of the selected
  key with the columns `Name`, `Isolation`, `Type` and `Value`. The `Name`
  column carries an icon before the name: a folder for a sub key and a plain
  file for a value, so the kind of a row is visible at a glance. Both icons
  come from the art provider of wxWidgets, the table never reads the icon of a
  host registry entry. A sub key row leaves the type and the value empty, the
  default value of a key is shown as `(Default)`. The toolbar above the table
  holds `Add value`, `Add key` and `Remove`; removing a key removes everything
  below it after a confirmation.
* **Editing.** The isolation mode of a row is picked from a dropdown in the
  row itself and reaches that row alone; every other column is read only and
  edited by double clicking the row, which opens the key dialog (name) or the
  value dialog (name, type and data). The data editor follows the type: a
  single line field for `REG_SZ`, `REG_EXPAND_SZ`, `REG_DWORD` and
  `REG_QWORD`, a multi line field for `REG_MULTI_SZ` (one string per line) and
  `REG_BINARY` (hexadecimal bytes) and a read only field for `REG_NONE`, whose
  data is preserved but never rewritten.
* **Isolation modes.** `Full`, `Write Copy` and `Hide`, default
  `Write Copy`. Every key and every value carries its own explicit mode; a key
  or a value which is created later follows the key it is created in. The
  dropdown of a row changes the row alone, the sub keys and the values below it
  keep their modes. To overwrite a whole subtree the context menu of the tree
  offers `Isolation Mode...`: the dialog picks the mode, `Apply to all sub
  keys` makes it reach every sub key of every level — no matter which mode they
  held before — and `Apply to all values in the subtree` (checked by default
  and only available together with the sub key option) makes it reach the
  values of the whole subtree as well.
* **Import.** `File -> Import Registry...` reads a single `.reg` file and
  merges it into the view: keys are created together with their intermediate
  keys, same named keys are merged, same named values are replaced and the
  isolation modes of existing keys and values are kept. Both the version 5
  format and the legacy `REGEDIT4` format are accepted, including the
  `hex(0)`, `hex(1)`, `hex(2)`, `hex(3)`, `hex(4)`, `hex(7)` and `hex(b)` data
  forms, the abbreviated root names (`HKCR`, `HKCU`, `HKLM`, `HKU`, `HKCC`)
  and the line continuations of long key paths and hexadecimal blocks. A file
  which cannot be read, parsed or applied leaves the view unchanged and
  reports the line it failed on.
* **Scope.** The workspace holds the model and the model reaches the archive:
  `Build` writes the hive and the isolation file of the workspace into the
  overlay of the archive, so the modes the user picked are the ones the
  packaged application runs with. `File -> Export Configuration` stores the
  whole model, including the values and the modes, in the project file, and
  `File -> Import Configuration` restores it.

Implementation: `src/core/RegistryModel.*` (keys, values and the isolation
rules, free of UI dependencies), `src/core/RegFile.*` (the `.reg` parser and
the merge into the model), `src/core/RegistryHive.*` (the hive writer),
`src/core/RegistryIsolationFile.*` (the isolation file writer) and
`src/widget/RegistryPanel.*` with `RegistryKeyDialog.*`,
`RegistryValueDialog.*` and `RegistryIsolationDialog.*`.

## Packing the registry

`appbox::Pack` (`src/core/PackService.cpp`) writes the two artifacts into the
overlay folder of the archive, which is where the loader looks for them:

```
data/registry/user.hiv            the virtual registry of the workspace
data/registry/isolation.json      the isolation modes of the workspace
```

The hive is produced by `appbox::WriteRegistryHive`
(`src/core/RegistryHive.cpp`), which creates a fresh file, mounts it with
`RegLoadAppKeyW`, writes every key and every value of the model — the type code
of the model is the `REG_*` code of the API, so the raw bytes are written
unchanged — and flushes and closes the mount, which writes the file back to
disk. The file lives in the temporary directory of the system while it is
built and is removed again, together with its transaction logs, once its bytes
are in the archive.

The modes are produced by `appbox::BuildRegistryIsolationFile`
(`src/core/RegistryIsolationFile.cpp`), which lists every key and every value
of the workspace with its mode (see
[Isolation modes](#isolation-modes) for the resolution rule of an entry which
the file does not mention).

## Mounting the hive

Implementation: `sandbox/registry/__init__.cpp` (`appbox::registry::Hive`).

The hive module is initialized before the hooks are attached (module table
order in `sandbox/Sandbox.cpp`: `HandleInfo`, registry, hooks), because a hook
must never fire before the hive exists.

`Hive::Init()` runs only in isolation mode and:

1. resolves `advapi32!RegLoadAppKeyW` dynamically and mounts
   `wRegistryHiveDOSPath` with `KEY_ALL_ACCESS`. `RegLoadAppKeyW` creates the
   file when it does not exist — no template is needed. A mount failure is
   fatal for the sandbox, because the registry isolation would silently stop
   working (the same rule as an unresolvable hook entry point).
2. queries the object name of the returned root handle with a locally resolved
   `NtQueryObject`. Application hives mount below `\REGISTRY\A\{GUID}` and are
   **process private**: opening them by path fails with
   `STATUS_ACCESS_DENIED`, even inside the mounting process. All redirected
   operations therefore open keys relative to the root *handle*, which is
   exactly what the redirection needs.
3. collects the HKCU prefix of the current user: `NtOpenProcessToken` +
   `NtQueryInformationToken(TokenUser)` + `RtlConvertSidToUnicodeString` yield
   the SID, and the prefix is `\REGISTRY\USER\<SID>`.
4. creates the five root keys of the view inside the hive with a locally
   resolved `NtCreateKey`, so a hive which the sandbox created itself (an
   archive without a packed registry) behaves exactly like a hive the packer
   wrote: opening a root key lands inside the hive as well and a handle of a
   root key is never a handle of the real registry.
5. loads the isolation modes from `wRegistryIsolationDOSPath`. A missing file
   is the normal case of an archive without modes; a malformed file is logged
   and ignored. Neither of them fails the start of the sandbox.

Because the module is initialized before the hook table resolved the entry
points, every NT function used by `Hive::Init()` is resolved locally through
`GetProcAddress` instead of through the `sys_*` pointers of the hook modules.

## Key path resolution

Implementation: `sandbox/registry/RootMap.*` (pure helpers),
`sandbox/registry/KeyPath.*` (pure string helpers) and
`appbox::registry::Hive::MapKeyPath` (`sandbox/registry/__init__.cpp`).

Every hooked API first turns the caller's `OBJECT_ATTRIBUTES` into a logical
**view path** (`\REGISTRY\USER\<SID>\Software\Vendor`,
`\REGISTRY\MACHINE\SOFTWARE`) and into the **hive relative path**
(`HKEY_CURRENT_USER\Software\Vendor`, `HKEY_LOCAL_MACHINE\SOFTWARE`):

* `RootDirectory == NULL` — the object name is already the full path.
* `RootDirectory != NULL` — the path of the root handle is queried with
  `appbox::QueryHandlePath` and the object name is appended.
* **Hive handles are translated back into the view.** When the root handle
  points below the private hive mount, the mount prefix is replaced with the
  view prefix of the root key the sub path names (`MapHivePathToView`). A key
  handle which was handed out by the hooks therefore behaves exactly like the
  key it shadows: an open relative to it is resolved like an open of the
  original view path again.

`MapKeyPath` returns `NotIsolated` (forward unchanged) or `Isolated` together
with the view path and the hive relative path. The helpers are case
insensitive (registered names are case insensitive by definition) and respect
the component boundary, so the SID `S-1-5-21` never matches
`S-1-5-212\...` (`appbox::registry::StripKeyPrefix`), and the longest prefix
wins (`appbox::registry::MapViewPathToHive`).

## Hook behavior

### `NtOpenKey` / `NtOpenKeyEx` (`sandbox/hook/NtOpenKey.cpp`, `sandbox/hook/NtOpenKeyEx.cpp`)

`RegOpenKeyExW` reaches `NtOpenKeyEx` on current Windows versions
(`advapi32` first opens the root key by full path through `NtOpenKey`, then
opens the subkey relative to that handle), older components use `NtOpenKey`.

Both hooks only resolve the view path (see above), forward the call unchanged
when the path is not isolated and delegate everything else to
`Hive::OpenIsolatedKey` / `Hive::OpenIsolatedKeyEx`, so the open policy exists
once:

1. The key is opened relative to the hive root first (`Hive::OpenKey` /
   `Hive::OpenKeyEx`). On success the redirected handle is returned — value
   reads and writes through it land in the hive by construction.
2. When the hive does not hold the key the isolation mode decides
   (`appbox::registry::FallbackForKey`, see
   `sandbox/registry/IsolationPolicy.hpp`):
   * `Full` and `Hide` (`OpenFallback::ReportHiveFailure`): the host key does
     not exist for the sandboxed process, so the status of the hive open
     (`STATUS_OBJECT_NAME_NOT_FOUND`, Win32 `ERROR_FILE_NOT_FOUND`) is the
     result of the call.
   * `WriteCopy` with a read access mask (`OpenFallback::UseHost`): the call
     falls back to the **real registry through the view path**
     (`Hive::OpenRealKey` / `Hive::OpenRealKeyEx` build object attributes with
     `RootDirectory = NULL` and the full view path). This is the read through:
     values which only exist in the real registry stay visible.
   * `WriteCopy` with a write access mask (`OpenFallback::CopyUp`): the real key
     is opened first to prove that the host holds it. When it does, the real
     handle is closed again, a **shadow key** is created inside the hive
     (`Hive::CreateKey`) and that handle is returned to the caller, so the
     modification of the caller lands in the sandbox. A failure of the copy-up
     is reported instead of falling back to the real handle, because a fallback
     would let the write escape into the host registry. When the host does not
     hold the key either, the open reports a missing key — an open never
     creates a key. `appbox::registry::PickOpenFailure` reports the failure
     which is not a not-found result, or the failure of the host when both
     layers report a missing key.

The access mask is classified by `appbox::registry::RequestsWrite`, which is
deliberately conservative: every right which can modify a key, a sub key or a
value counts as a write, `MAXIMUM_ALLOWED` and the generic rights included. An
unnecessary copy-up costs one empty shadow key which the merged enumeration and
the read through hide completely, while a missed write access would let a
modification escape into the host registry.

### `NtCreateKey` (`sandbox/hook/NtCreateKey.cpp`)

1. Resolve the view path. `NotIsolated` → forward unchanged.
2. Every other case: create / open the key relative to the hive root with the
   caller's access mask, class and options (`Hive::CreateIsolatedKey`, built on
   the raw `Hive::CreateKey`). The create never reaches the real registry: the
   shadow key of the hive hides a host key of the same name, which is the
   behaviour of `WriteCopy`, `Full` and `Hide` alike.

`Hive::CreateKey` walks the path component by component and creates every
intermediate key inside the hive before the last component is created or opened
with the mask of the caller (`Disposition` reports that last component). A
create of a sandboxed process therefore also works for a path which only the
real registry holds so far, which is what the copy-up of a write access open
needs. The right to create a sub key is added to the mask of the caller,
because the kernel checks it against the parent of the key it creates; the
extra right only widens the handle of a key which lives inside the hive.

`Hive::CreateIsolatedKey` reports the disposition of the **merged view**
instead of the disposition of the hive layer
(`appbox::registry::ViewCreateDisposition`, the create counterpart of
`IsolationTable::HidesHost()`, which the open path and the merged enumeration
use):

| Hive disposition | Mode | Host holds the key | Reported disposition |
| --- | --- | --- | --- |
| `REG_OPENED_EXISTING_KEY` | every mode | not consulted | `REG_OPENED_EXISTING_KEY` |
| `REG_CREATED_NEW_KEY` | `Full`, `Hide` | not consulted | `REG_CREATED_NEW_KEY` |
| `REG_CREATED_NEW_KEY` | `WriteCopy` | yes | `REG_OPENED_EXISTING_KEY` |
| `REG_CREATED_NEW_KEY` | `WriteCopy` | no | `REG_CREATED_NEW_KEY` |

A key which the hive already holds exists in the view of every mode, so the
disposition of the hive is reported unchanged: the hive wins the merged view. A
key which the hive just created does not exist in the view of `Full` and
`Hide`, because those modes keep the host entry invisible — reporting a
creation is exactly what the sandboxed process has to observe. `WriteCopy`
keeps the host entry visible, so a key which only the host holds is an existing
key for the caller: without the correction a caller which initializes a key it
believes to be new would write defaults into the shadow key, and the hive wins
those values against the real values of the host key.

The host layer is only probed (`Hive::HostHoldsKey`, an open of the view path
with `KEY_QUERY_VALUE | KEY_ENUMERATE_SUB_KEYS` which is closed again) when the
hive created the key and the mode keeps the host entry visible, so a create of
a key which the hive holds costs no extra call. A failed probe never fails the
create: the status and the handle are the ones of the hive create. A key which
the sandbox deleted (a whiteout) is reported as created as well: the host entry
is invisible, so a delete followed by a create yields an empty key — exactly
what the real registry does.

### `NtDeleteKey` / `NtDeleteValueKey` (`sandbox/hook/NtDeleteKey.cpp`, `sandbox/hook/NtDeleteValueKey.cpp`)

A delete is the one modification which the host registry cannot receive: the
isolation must not remove an entry of the real registry, so it removes the entry
of the hive and records the deletion of the host entry — a **whiteout**. The
route of both hooks is the decision table
`appbox::registry::DeleteOutcomeOf` (`sandbox/registry/IsolationPolicy.hpp`):

| Hive holds the entry | Host holds the entry | Mode | Result |
| --- | --- | --- | --- |
| no | no | every mode | the entry does not exist: the failure of the hive layer is reported |
| no | yes | `Full`, `Hide` | the host entry is invisible, so the entry does not exist |
| no | yes | `WriteCopy` | the host entry is recorded as deleted |
| yes | no | every mode | the entry of the hive is removed |
| yes | yes | `Full`, `Hide` | the entry of the hive is removed |
| yes | yes | `WriteCopy` | the entry of the hive is removed and the host entry is recorded as deleted |

Both hooks run the same steps:

1. Classify the handle through `Hive::MapHandleView`. `NotIsolated` → forward
   unchanged. A handle of the **host layer** is refused with
   `STATUS_ACCESS_DENIED`: it is a read through handle which the caller opened
   without the right to delete or to write (an open which asks for `DELETE` or
   `KEY_SET_VALUE` is copied up into the hive), so the real call would refuse it
   as well — and a delete must never reach the real registry.
2. The kernel removes the entry of the hive (`NtDeleteKey` /
   `NtDeleteValueKey` on the handle of the caller), which also checks the rights
   of that handle: a handle which does not permit the delete reports the failure
   of the real call, and a value which the hive does not hold is a not-found
   result the isolation answers itself.
3. The delete of a key runs against the merged view for one more reason: the
   kernel refuses to delete a key which holds sub keys, and the sandboxed
   process sees the sub keys of both layers. The hook therefore counts the
   visible sub keys of both layers and reports `STATUS_CANNOT_DELETE` when the
   view holds one, which is the status the real `NtDeleteKey` reports (Win32
   maps it to `ERROR_ACCESS_DENIED`). Values do not block a delete.
4. A host entry which the mode keeps visible is recorded as deleted. A failed
   marker **fails the whole call**: reporting a success would let the host entry
   reappear in the view of the sandbox.

### Deletion and whiteouts

The markers live in the whiteout store of the hive (see
[On-disk layout](#on-disk-layout)), which the view never reaches: no path of the
view maps onto the reserved key, and no handle of it is ever handed out. The
store has two namespaces:

* `K\<hive relative path>` — the key of that path was deleted. The lookup walks
  the path upwards, so a deleted key hides its whole subtree: the delete of a
  key removes everything below it as well.
* `V\<hive relative path>` — one value per deleted value name of that key, the
  empty name for the default value. The name is stored as a **value name** and
  not as a path component, because a value name may contain a backslash.

The store is consulted wherever the merged view decides whether a host entry
stays visible, so the deletion is consistent for every read:

| Decision point | Rule |
| --- | --- |
| open (`NtOpenKey(Ex)`) | a deleted key does not exist: the host layer is neither read through nor copied up |
| create (`NtCreateKey`) | the host probe of the disposition is skipped, so the key is reported as created and the host content stays hidden |
| value query (`NtQueryValueKey`, `NtQueryMultipleValueKey`) | a deleted value is missing in the view |
| merged enumeration (`NtEnumerateKey`, `NtEnumerateValueKey`) | the deleted host entries are dropped |
| merged counts (`NtQueryKey`) | the same filter the enumeration applies |

`Hive::IsKeyWhitedOut` and `Hive::IsValueWhitedOut` implement the lookups;
`Hive::FilterHiddenEntries` and the per entry test of
`Hive::ResolveMergedIndex` share one predicate, so the enumeration and the
counts can never disagree.

The lookups cost nothing for a sandbox which never deleted anything: the store
is created by the first whiteout, and a store which does not exist when the hive
is mounted stays empty for the whole run, because the sandbox is its only
writer. The module probes the store once while it mounts the hive and skips
every lookup afterwards (`Hive::Data::whiteout_possible`).

### `NtQueryMultipleValueKey` (`sandbox/hook/NtQueryMultipleValueKey.cpp`)

A batch query follows the read through rule of `NtQueryValueKey` for every entry
of the batch: the hive layer wins, a value which only the host holds is read
through, and a value which the isolation mode hides or which the sandbox deleted
is missing. The policy lives in
`appbox::registry::Hive::QueryMultipleValues`:

* every entry is answered by the hive layer → the call is forwarded to the hive
  handle with the parameters of the caller, so the kernel computes the data
  offsets;
* every entry is answered by the host layer → the call is forwarded to a real
  key which is opened by path;
* the batch mixes both layers → the answer is assembled: every value is read
  from the layer which holds it and copied into the caller buffer in the order
  of the entries. The offsets are the ones the kernel produces (the entries
  follow each other without padding), a missing value fails the whole batch with
  `STATUS_OBJECT_NAME_NOT_FOUND`, and a buffer which is too small reports
  `STATUS_BUFFER_OVERFLOW` together with the size the caller needs.

### `NtSaveKey` / `NtSaveKeyEx` (`sandbox/hook/NtSaveKey.cpp`, `sandbox/hook/NtSaveKeyEx.cpp`)

The saved file holds the **merged view** of the key at the time of the call: the
entries of the hive layer plus the host entries which the isolation mode keeps
visible. `appbox::registry::Hive::SaveIsolatedKey` implements it:

1. A key whose real layer contributes nothing to the view (the host does not
   hold it at all) is saved directly from its hive key, which is the common case
   and costs no extra file.
2. Every other key is copied into a temporary hive next to the sandbox hive: the
   merged values and the merged sub keys are walked through
   `Hive::ResolveMergedIndex`, so only the entries the sandboxed process sees
   reach the file. The copy uses the raw entry points of the module
   (`NtQueryValueKey`, `NtSetValueKey`), which never re-enter a detour.
3. That key is saved with the parameters of the caller (`NtSaveKey`, or
   `NtSaveKeyEx` with the format of the caller) into the file handle of the
   caller.
4. The temporary hive, its transaction logs and every handle are released
   before the call returns. The mount of a hive file resolves its path outside
   the filesystem view, so the temporary file lives at the path of the overlay
   and is removed through the raw delete entry point.

The caller keeps its privileges: the save entry points of the kernel require
`SeBackupPrivilege`, which the caller has to enable, exactly like a direct save
does.

### Value level APIs

`NtSetValueKey` and the remaining value APIs are not hooked. They act on the key
handle, which is a hive handle for every redirected open, so value writes follow
the key automatically. `NtQueryValueKey` provides the read through,
`NtQueryMultipleValueKey` the read through of a batch and `NtDeleteValueKey` the
recorded deletion (see above).

## Query and enumeration hooks

Implementation: `sandbox/hook/NtEnumerateKey.cpp`,
`sandbox/hook/NtEnumerateValueKey.cpp`, `sandbox/hook/NtQueryKey.cpp`,
`sandbox/hook/NtQueryValueKey.cpp`, `sandbox/hook/NtQueryObject.cpp`,
`sandbox/hook/NtQueryMultipleValueKey.cpp` and the helpers of
`appbox::registry::Hive` (`sandbox/registry/__init__.cpp`,
`sandbox/registry/EnumMerge.cpp`, `sandbox/registry/IsolationTable.cpp`,
`sandbox/registry/Whiteout.cpp`, `sandbox/registry/KeyGuard.hpp`).

Every hook first classifies the key handle through `Hive::MapHandleView`:
handles below the private hive mount run the merged logic, every other handle
(real fallback handles, handles of other roots) is forwarded unchanged. Calls
which cannot resolve the handle path degrade to the plain forwarded call, so
the isolation never fails a query outright.

### Merged two layer view (`NtEnumerateKey` / `NtEnumerateValueKey`)

The enumeration below a hive handle presents the union of both layers:

```
merged = hive entries ++ (real entries which the hive does not shadow
                          and which stay visible)
```

Names are compared case insensitively and the hive layer wins a conflict, so a
shadow key hides the real key of the same name — consistent with the open
hook, which answers shadowed keys from the hive. Index `i < H` (the hive
count) is forwarded to the hive handle with the same index; higher indices are
mapped onto the *n*-th visible entry of the real layer, which is queried
through a real handle opened by path with the enumeration access right. An
index past the merged view answers `STATUS_NO_MORE_ENTRIES`.

The merge works on names, and the default value of a key carries the empty
name: the value enumeration reports it as an ordinary entry of its layer, it
takes part in the dedup — a default value of the hive layer shadows the default
value of the real key — and it is counted like every other value. The index
which is handed to a layer is the position of the entry inside the collected
names of that layer, so nothing may be skipped while the names are collected;
the collectors therefore report every enumerated entry, the default value
included.

An entry whose mode hides the host registry and an entry which the sandbox
deleted (a whiteout) are **not** part of the merged view: both are skipped while
the visible entries are collected, so the index which is handed to the kernel is
the position of the entry inside the real key, not the position inside the
visible subset. The merge rules themselves are the pure function
`appbox::registry::MapMergedIndex` (`sandbox/registry/EnumMerge.cpp`).

Every call collects both name lists, so a full enumeration walks the layers
quadratically. Registry key sets are small, and the collector
(`Hive::CollectSubKeyNames` / `Hive::CollectValueNames`) queries the minimal
information classes only.

### Value read through (`NtQueryValueKey`)

A value query below a hive handle first runs against the hive. Only when the
hive answers `STATUS_OBJECT_NAME_NOT_FOUND` is the isolation consulted: when the
value may only be seen through the hive (`Full` or `Hide`), or when the sandbox
deleted the value (a whiteout), the not-found result is returned; otherwise the
query is replayed against the real key which the view path addresses — a shadow
key therefore no longer hides the values of the real key. Buffer size results
are returned as they are: the hive holds the value, only the caller buffer is
too small.

### Key name translation and merged counts (`NtQueryKey`)

* `KeyNameInformation` / `KeyBasicInformation` / `KeyNodeInformation`: the key
  name is translated from the private mount prefix `\REGISTRY\A\{GUID}` back
  into the view prefix, so a redirected handle names the key it shadows. A
  translated name which does not fit the caller buffer reports
  `STATUS_BUFFER_OVERFLOW` with the exact required length.
* `KeyFullInformation` / `KeyCachedInformation`: the `SubKeys` and `Values`
  counts are corrected to the merged two layer view with the **same filter the
  enumeration applies**, so the counts never announce an entry which the
  enumeration hides, and the maximum length fields grow to the maximum of both
  layers.
* Every other information class is forwarded unchanged.

### Object name translation (`NtQueryObject`)

`ObjectNameInformation` queries translate names below the private hive mount
into the view path as well, so `NtQueryObject` on a redirected handle reports
`\REGISTRY\USER\<SID>\...` instead of leaking the private mount path. Names of
unrelated objects are returned verbatim.

## Loader registry browser

The admin UI of the loader (`enable_admin_ui`) contains a read-only registry
browser which mirrors the layout of the Windows registry editor: a key tree on
the left, the value list (name / type / data) of the selected key on the right
and the full path of the selected key in a bar above both. The top item of the
tree is the `Sandbox Registry` container; the five root keys of the view are
its children.

Implementation: `loader/registry/HiveReader.*` (mounting, enumeration and
formatting, free of UI dependencies) and `loader/widget/RegistryBrowser.*`
(the browser panel).

Behavior:

* **Only the sandbox hive is shown.** The browser mounts `<overlay_fs>
  \registry\user.hiv` itself through `RegLoadAppKeyW` with `KEY_READ` and
  reads everything relative to the returned root handle — the host registry
  is never touched, and keys which only exist in the real registry (the read
  through of the sandbox) are not part of the view.
* **Read-only.** The loader never writes to the hive, so browsing cannot
  damage the file. `Open()` checks that the file exists first, because
  `RegLoadAppKeyW` would otherwise create it — an empty overlay must stay
  without a hive until the sandbox mounts it.
* **Snapshot semantics with refresh.** The mounted view reflects the hive
  file at mount time. `Refresh()` (the *Refresh* toolbar button or F5)
  releases and remounts the file, picking up everything the sandboxed
  process flushed to disk. Changes which the sandbox still holds in memory
  only become visible after they are flushed and the view is refreshed.
* **Missing hive.** When the sandbox has not created the hive yet, the
  browser shows an empty tree with a hint instead of an error.
* **The whiteout store stays hidden.** The root of the hive carries the whiteout
  store next to the five root keys of the view; the root enumeration and the
  root sub key check of `HiveReader` skip it, so the tree keeps showing the five
  root keys of the view only.

## Supporting modules

| Module | Responsibility |
| --- | --- |
| `common/RegistryIsolation.hpp` | Isolation mode enumeration, the schema of the isolation file and the names of the whiteout store, shared by the packer, the loader and the sandbox. |
| `src/core/RegistryHive.*` | Writes the virtual registry of the packer into a hive file. |
| `src/core/RegistryIsolationFile.*` | Writes the isolation modes of the packer into a JSON document. |
| `sandbox/registry/__init__.*` | Hive module: mount / unmount, HKCU prefix, view path resolution, the open / create / delete / save policy of the isolation modes (`OpenIsolatedKey(Ex)`, `CreateIsolatedKey`, `DeleteIsolatedKey`, `DeleteIsolatedValue`, `SaveIsolatedKey`, `QueryMultipleValues`), the host layer existence probes (`HostHoldsKey`, `HostHoldsValue`), the whiteout store (`IsKeyWhitedOut`, `IsValueWhitedOut`), handle classification, name collection, merged index resolution, isolation lookup. |
| `sandbox/registry/RootMap.*` | Pure mapping between the NT paths of the five root keys and the sub keys of the hive. |
| `sandbox/registry/IsolationTable.*` | Pure isolation table: parses the isolation file and resolves the effective mode of an entry. |
| `sandbox/registry/KeyPath.*` | Pure string helpers: `StripKeyPrefix` (case insensitive, boundary aware), `JoinKeyPath` and `SplitKeyPath`. |
| `sandbox/registry/EnumMerge.*` | Pure index mapping of the merged two layer enumeration view. |
| `sandbox/registry/Whiteout.*` | Pure paths of the whiteout store: the marker key of a key, the marker key of its deleted values and the walk from a path to its ancestors. |
| `sandbox/registry/IsolationPolicy.hpp` | Pure decision tables of the isolation modes: open fallback, write access classification, not-found classification, failure selection and the delete route. |
| `sandbox/registry/KeyGuard.hpp` | RAII owner of the real key handles which the hooks open. |
| `sandbox/hook/NtOpenKey.*` | Read hook. |
| `sandbox/hook/NtOpenKeyEx.*` | Read hook, `RegOpenKeyExW` entry point. |
| `sandbox/hook/NtCreateKey.*` | Write hook. |
| `sandbox/hook/NtDeleteKey.*` | Delete hook, records the deletion of the key (a whiteout). |
| `sandbox/hook/NtDeleteValueKey.*` | Delete hook, records the deletion of the value (a whiteout). |
| `sandbox/hook/NtEnumerateKey.*` | Sub key enumeration merge hook. |
| `sandbox/hook/NtEnumerateValueKey.*` | Value enumeration merge hook. |
| `sandbox/hook/NtQueryKey.*` | Key name translation and merged count hook. |
| `sandbox/hook/NtQueryValueKey.*` | Value read through hook. |
| `sandbox/hook/NtQueryMultipleValueKey.*` | Batch value read through hook. |
| `sandbox/hook/NtSaveKey.*` | Save hook, exports the merged view of a key. |
| `sandbox/hook/NtSaveKeyEx.*` | Save hook with the format of the caller. |
| `sandbox/hook/NtQueryObject.*` | Object name translation hook. |
| `sandbox/utils/QueryHandlePath.*` | Reused from the filesystem isolation: object name of a handle. |

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
