# Registry Isolation

appbox isolates the registry of a sandboxed process by hooking the key open /
create entry points of the NT registry API and redirecting them onto a private
**hive** file inside the overlay. The method is known as *hive redirection*:
instead of replaying every registry operation on a copy of the real data, the
sandbox mounts one real registry file and points every redirected key at it.
Value level APIs (`NtQueryValueKey`, `NtSetValueKey`, ...) are not hooked at
all — they operate on whatever key handle the open / create returned, which is
already a handle into the hive.

This iteration implements the closed loop: the read hooks (`NtOpenKey` /
`NtOpenKeyEx`), the write hook (`NtCreateKey`) and the query / enumerate hooks
(`NtEnumerateKey`, `NtEnumerateValueKey`, `NtQueryKey`, `NtQueryValueKey`,
`NtQueryObject`). The remaining registry hooks (delete, save, ...) are listed
in [Known gaps and limitations](#known-gaps-and-limitations).

## Scope

Only `HKEY_CURRENT_USER` (`\Registry\User\<SID>` of the sandboxed user) is
redirected. Every other root (`\Registry\Machine`, other user profiles, ...)
is forwarded unchanged, so reads and writes of e.g. HKLM reach the real
registry with their own access checks.

Reads and writes behave differently:

| Operation | Hive holds the key | Hive does not hold the key |
| --- | --- | --- |
| **open / read** (`NtOpenKey(Ex)`) | redirected into the hive | falls back to the real registry (read through) |
| **create / write** (`NtCreateKey`) | redirected into the hive | redirected into the hive, intermediate keys are created automatically |
| **enumerate** (`NtEnumerateKey`, `NtEnumerateValueKey`) | merged view: hive entries first, then the non shadowed real entries | real entries only |
| **value query** (`NtQueryValueKey`) | answered from the hive | falls back to the real registry (read through) |

The real HKCU is therefore never modified by a sandboxed process, while values
that only exist in the real registry remain readable — both through a direct
query and through a shadow key (see
[Query and enumeration hooks](#query-and-enumeration-hooks)).

## Configuration

The configuration flows from the loader down to the sandbox DLL:

1. `appbox::LoaderConfig` (`loader/Config.hpp`) — no new user facing field. The
   hive is derived from `overlay_fs`.
2. `appbox::MapRegistryHive` (`loader/Loader.cpp`) — creates the
   `<overlay_fs>\registry` directory next to the `filesystem` overlay directory
   and stores `<overlay_fs>\registry\user.hiv` in the injected configuration.
   The hive file itself is **not** created here; the mount creates it on first
   use, so an empty overlay needs no template hive.
3. `appbox::SandboxConfig` (`sandbox/Config.hpp`) — carries
   `registry_hive_dos_path` (DOS path of the hive file, UTF-8).
4. `appbox::Sandbox` (`sandbox/Sandbox.hpp`, filled by `ParseInjectData` in
   `sandbox/Sandbox.cpp`) — `wRegistryHiveDOSPath`, the runtime form used by
   the registry module.

## On-disk layout

```
<overlay_fs>\registry\user.hiv
```

The file is a real registry hive. It is created by the first mount and grows
as the sandboxed process writes keys and values; it survives process restarts,
so a sandbox can be reused. Deleting the overlay (or just this file) discards
every registry modification the sandboxed process ever made.

## Loader registry browser

The admin UI of the loader (`enable_admin_ui`) contains a read-only registry
browser which mirrors the layout of the Windows registry editor: a key tree on
the left, the value list (name / type / data) of the selected key on the right
and the full path of the selected key in a bar above both.

Implementation: `loader/registry/HiveReader.*` (mounting, enumeration and
formatting, free of UI dependencies) and `loader/widget/RegistryBrowser.*`
(the browser panel). Unit tests: `test/unit/Unit_HiveReader.cpp`.

Behavior:

* **Only the sandbox hive is shown.** The browser mounts `<overlay_fs>
  \registry\user.hiv` itself through `RegLoadAppKeyW` with `KEY_READ` and
  reads everything relative to the returned root handle — the host registry
  is never touched, and keys which only exist in the real HKCU (the read
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

Because the module is initialized before the hook table resolved the entry
points, every NT function used by `Hive::Init()` is resolved locally through
`GetProcAddress` instead of through the `sys_*` pointers of the hook modules.

## Key path resolution

Implementation: `sandbox/registry/KeyPath.*` (pure helpers) and
`appbox::registry::Hive::MapKeyPath` (`sandbox/registry/__init__.cpp`).

Every hooked API first turns the caller's `OBJECT_ATTRIBUTES` into a logical
**view path** (`\Registry\User\<SID>\Software\Vendor`):

* `RootDirectory == NULL` — the object name is already the full path.
* `RootDirectory != NULL` — the path of the root handle is queried with
  `appbox::QueryHandlePath` and the object name is appended.
* **Hive handles are translated back into the view.** When the root handle
  points below the private hive mount, the mount prefix is replaced with the
  HKCU prefix before the object name is appended. A key handle which was
  handed out by the hooks therefore behaves exactly like the key it shadows:
  an open relative to it is resolved like an open of the original view path
  again.

`MapKeyPath` returns `NotHkcu` (forward unchanged) or `Isolated` together with
the view path and the hive-relative subpath. The helpers are case insensitive
(registered names are case insensitive by definition) and respect the
component boundary, so the SID `S-1-5-21` never matches `S-1-5-212\...`
(`appbox::registry::StripKeyPrefix`, unit tested in
`test/unit/Unit_RegistryKeyPath.cpp`).

## Hook behavior

### `NtOpenKey` / `NtOpenKeyEx` (`sandbox/hook/NtOpenKey.cpp`, `sandbox/hook/NtOpenKeyEx.cpp`)

`RegOpenKeyExW` reaches `NtOpenKeyEx` on current Windows versions
(`advapi32` first opens the HKCU root by full path through `NtOpenKey`, then
opens the subkey relative to that handle), older components use `NtOpenKey`.

1. Resolve the view path (see above). `NotHkcu` → forward unchanged.
2. Open the key relative to the hive root with the caller's access mask and
   options (`Hive::OpenKey` / `Hive::OpenKeyEx`). Opening HKCU itself is
   answered with a duplicate of the hive root handle (`NtDuplicateObject`).
3. On success the redirected handle is returned — value reads and writes
   through it land in the hive by construction.
4. On failure the call falls back to the **real registry through the view
   path** (`Hive::OpenRealKey` / `Hive::OpenRealKeyEx` build object attributes
   with `RootDirectory = NULL` and the full view path). This is the read
   through: values which only exist in the real HKCU stay visible.

### `NtCreateKey` (`sandbox/hook/NtCreateKey.cpp`)

`NtCreateKeyEx` is **not exported** by `ntdll` (verified on the reference
system), so `NtCreateKey` is the only create entry point.

1. Resolve the view path. `NotHkcu` → forward unchanged.
2. Create / open the key relative to the hive root with the caller's access
   mask, class and options (`Hive::CreateKey`). The real registry is never
   touched.

`NtCreateKey` opens the key when it exists and creates it — including every
intermediate key of the multi component relative name — when it does not, so
no separate path walk is needed. `Disposition` reports whether the key was
created inside or already existed inside **the hive**.

### Value level APIs

`NtSetValueKey`, `NtDeleteValueKey` and friends are not hooked. They act on
the key handle, which is a hive handle for every redirected open, so value
writes follow the key automatically. `NtQueryValueKey` **is** hooked — it
provides the read through for values which only exist in the real registry
(see below).

## Query and enumeration hooks

Implementation: `sandbox/hook/NtEnumerateKey.cpp`,
`sandbox/hook/NtEnumerateValueKey.cpp`, `sandbox/hook/NtQueryKey.cpp`,
`sandbox/hook/NtQueryValueKey.cpp`, `sandbox/hook/NtQueryObject.cpp` and the
helpers of `appbox::registry::Hive` (`sandbox/registry/__init__.cpp`,
`sandbox/registry/EnumMerge.cpp`, `sandbox/registry/KeyGuard.hpp`).

Every hook first classifies the key handle through `Hive::MapHandleView`:
handles below the private hive mount run the merged logic, every other handle
(real fallback handles, handles of other roots) is forwarded unchanged. Calls
which cannot resolve the handle path degrade to the plain forwarded call, so
the isolation never fails a query outright.

### Merged two layer view (`NtEnumerateKey` / `NtEnumerateValueKey`)

The enumeration below a hive handle presents the union of both layers:

```
merged = hive entries ++ (real entries whose name is not in the hive)
```

Names are compared case insensitively and the hive layer wins a conflict, so a
shadow key hides the real key of the same name — consistent with the open
hook, which answers shadowed keys from the hive. Index `i < H` (the hive
count) is forwarded to the hive handle with the same index; higher indices are
mapped onto the *n*-th non shadowed entry of the real layer, which is queried
through a real handle opened by path with the enumeration access right. An
index past the merged view answers `STATUS_NO_MORE_ENTRIES`. The index
mapping itself is the pure function `appbox::registry::MapMergedIndex`
(`sandbox/registry/EnumMerge.cpp`, unit tested in
`test/unit/Unit_RegistryEnumMerge.cpp`).

Every call collects both name lists, so a full enumeration walks the layers
quadratically. Registry key sets are small, and the collector
(`Hive::CollectSubKeyNames` / `Hive::CollectValueNames`) queries the minimal
information classes only.

### Value read through (`NtQueryValueKey`)

A value query below a hive handle first runs against the hive. Only when the
hive answers `STATUS_OBJECT_NAME_NOT_FOUND` is the query replayed against the
real key which the view path addresses — a shadow key therefore no longer
hides the values of the real key. Buffer size results are returned as they
are: the hive holds the value, only the caller buffer is too small.

### Key name translation and merged counts (`NtQueryKey`)

* `KeyNameInformation` / `KeyBasicInformation` / `KeyNodeInformation`: the key
  name is translated from the private mount prefix `\REGISTRY\A\{GUID}` back
  into the view prefix `\REGISTRY\USER\<SID>`, so a redirected handle names
  the key it shadows. A translated name which does not fit the caller buffer
  reports `STATUS_BUFFER_OVERFLOW` with the exact required length.
* `KeyFullInformation` / `KeyCachedInformation`: the `SubKeys` and `Values`
  counts are corrected to the merged two layer view, and the maximum length
  fields grow to the maximum of both layers.
* Every other information class is forwarded unchanged.

### Object name translation (`NtQueryObject`)

`ObjectNameInformation` queries translate names below the private hive mount
into the view path as well, so `NtQueryObject` on a redirected handle reports
`\REGISTRY\USER\<SID>\...` instead of leaking the private mount path. Names of
unrelated objects are returned verbatim.

## Supporting modules

| Module | Responsibility |
| --- | --- |
| `sandbox/registry/__init__.*` | Hive module: mount / unmount, HKCU prefix, view path resolution, open / create helpers, handle classification, name collection, merged index resolution. |
| `sandbox/registry/KeyPath.*` | Pure string helpers: `StripKeyPrefix` (case insensitive, boundary aware) and `JoinKeyPath`. |
| `sandbox/registry/EnumMerge.*` | Pure index mapping of the merged two layer enumeration view. |
| `sandbox/registry/KeyGuard.hpp` | RAII owner of the real key handles which the hooks open. |
| `sandbox/hook/NtOpenKey.*` | Read hook. |
| `sandbox/hook/NtOpenKeyEx.*` | Read hook, `RegOpenKeyExW` entry point. |
| `sandbox/hook/NtCreateKey.*` | Write hook. |
| `sandbox/hook/NtEnumerateKey.*` | Sub key enumeration merge hook. |
| `sandbox/hook/NtEnumerateValueKey.*` | Value enumeration merge hook. |
| `sandbox/hook/NtQueryKey.*` | Key name translation and merged count hook. |
| `sandbox/hook/NtQueryValueKey.*` | Value read through hook. |
| `sandbox/hook/NtQueryObject.*` | Object name translation hook. |
| `sandbox/utils/QueryHandlePath.*` | Reused from the filesystem isolation: object name of a handle. |

## Test coverage

* `test/unit/Unit_RegistryKeyPath.cpp` — prefix stripping (exact match, child,
  case, boundary, other user, longer prefix, trailing separator) and path
  joining.
* `test/unit/Unit_RegistryEnumMerge.cpp` — merged index mapping (layer order,
  dedup with hive priority, case insensitivity, empty layers, boundaries) and
  merged count.
* `test/probe/RegWriteValue.cpp` / `test/probe/RegReadValue.cpp` — the
  operations executed inside the sandbox.
* `test/probe/RegEnumKey.cpp` / `test/probe/RegEnumValue.cpp` — sub key and
  value enumeration inside the sandbox.
* `test/probe/RegShadowRead.cpp` / `test/probe/RegQueryKeyName.cpp` — shadow
  key value reads and kernel object name queries inside the sandbox.
* `test/cases/Reg_WriteValue_NewKey.cpp` — the closed loop: the sandboxed
  probe creates a key, writes a value and reads it back; the test process
  verifies that the real HKCU does **not** contain the key and that the hive
  file exists in the overlay.
* `test/cases/Reg_ReadValue_RealFallback.cpp` — a key and value which only
  exist in the real HKCU (created by the test process with RAII cleanup) are
  readable inside the sandbox, and the key still exists afterwards.
* `test/cases/Reg_EnumerateKey_Merged.cpp` — the sandboxed probe enumerates
  the merged sub key view (real `RealA`/`RealB` plus sandbox `SandboxC`),
  `RegQueryInfoKeyW` reports the merged count, and the real registry still
  holds only the real sub keys.
* `test/cases/Reg_EnumerateValue_Merged.cpp` — the sandboxed probe enumerates
  the merged value view and reads every enumerated value back (enumeration and
  query stay consistent); the real registry does not gain the sandbox value.
* `test/cases/Reg_ShadowKey_ReadRealValue.cpp` — a shadow key created inside
  the sandbox reads a value which only exists in the real key, and the real
  value is unchanged.
* `test/cases/Reg_QueryKeyName_ViewPath.cpp` — `NtQueryKey` and
  `NtQueryObject` on a redirected handle report the view path and never the
  private mount prefix.

## Known gaps and limitations

1. **Write access opens leak through the fallback.** When the hive does not
   hold the key, `NtOpenKey(Ex)` falls back to the real registry even when the
   caller asked for write access. A value write through such a handle modifies
   the real registry. A later iteration will create a shadow key (copy-up) for
   write access opens.
2. **Shadow keys hide the real sub keys.** `NtCreateKey` always creates the
   key in the hive when it does not exist there. The values of the real key
   are visible through the shadow (`NtQueryValueKey` read through), and the
   non shadowed sub keys appear in the merged enumeration, but a real sub key
   with the same name as a hive sub key is answered from the hive and the
   real values below it stay hidden until copy-up is implemented.
3. **Disposition reflects the hive, not the real registry.** For a key which
   exists in the real HKCU, `RegCreateKeyEx` reports `REG_CREATED_NEW_KEY`,
   because the shadow was just created.
4. **Delete and save hooks are missing.** `NtDeleteKey`,
   `NtQueryMultipleValueKey`, `NtSaveKey` and friends are not hooked: deletes
   hit the redirected key (and fail for pure shadow-less handles). `NtEnumerateKeyEx`
   and `NtEnumerateValueKeyEx` are not exported by `ntdll` on the reference
   system and therefore not hooked either.
5. **The private mount path is visible to other name queries.**
   `NtQueryObject` and `NtQueryKey` translate names of redirected handles back
   into the view path, but other name sources (for example
   `NtQueryKey(KeyFlagsInformation)` variants or handle duplication across
   processes) may still expose the mount.
6. **Only HKCU is redirected.** HKLM and other profiles are forwarded with
   their real access checks. The `HKEY_CLASSES_ROOT` predefined handle resolves
   to `\Registry\Machine\Software\Classes` on the HKLM side, so it passes
   through unchanged; only explicit writes below `HKCU\Software\Classes` are
   redirected.
7. **`NtCreateKeyEx` is not covered.** The function is not exported by
   `ntdll` on the reference system; if a future Windows version exports it,
   the hook must be added.
