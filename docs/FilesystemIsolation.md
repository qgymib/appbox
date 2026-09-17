# Filesystem isolation

appbox isolates the filesystem of a sandboxed process by hooking the NT file APIs and
redirecting every path through a **view** that is composed of three kinds of layers:

| Layer | Role | Writable |
| --- | --- | --- |
| **upper** | Overlay filesystem. Every modification made by the sandboxed process lands here. | yes |
| **lower** | Zero or more read-only base filesystems. Each one is mapped to a virtual path prefix. | no |
| **host** | The real filesystem of the machine, used as the last layer of the view. | no |

The lower and host layers are never modified. "Deleted" and "directory is opaque" states
are expressed with **marker files** created inside the upper layer, so the read-only
layers stay untouched and can be shared between runs. Operations that are not redirected
yet are listed in [Known gaps and limitations](#known-gaps-and-limitations).

Isolation is active only when the sandbox DLL was injected by the loader
(`appbox::Sandbox::bIsolationMode`, derived from `DetourRestoreAfterWith()` in
`sandbox/Sandbox.cpp`). Outside isolation mode the hook entry points are still resolved
but never attached, see `appbox::InitHook` in `sandbox/hook/__init__.cpp`.

## Terminology

* **View path** — the NT path the sandboxed process uses, in DOS style:
  `\??\C:\Users\foo\AppData\Roaming\data.txt`. Every hooked API first converts whatever
  the caller passed (a device path, a volume GUID path, a `\SystemRoot` path, a path
  relative to a root handle, or a file ID) into this form.
* **Layer path** — the path of the same object inside a concrete layer
  (upper / lower / host). One view path maps to at most one layer path per layer.
* **Whiteout** — an empty file named `<name>.$APPBOX_DELETE$` placed next to the object
  it hides. It hides that object (file or directory subtree) from every lower layer.
* **Opaque marker** — an empty file named `.$APPBOX_OPAQUE$` placed inside a directory.
  Its presence makes the resolver stop looking into lower layers for that directory.
* **Marker names** — `sandbox/utils/Defines.hpp`:

  ```cpp
  #define APPBOX_SANDBOX_WHITEOUT_SUFFIX_W    L".$APPBOX_DELETE$"
  #define APPBOX_SANDBOX_OPAQUE_NAME_W        L".$APPBOX_OPAQUE$"
  ```

## Configuration

The configuration flows from the loader UI down to the sandbox DLL:

1. `appbox::LoaderConfig` (`loader/Config.hpp`) — the user facing configuration:
   * `base_fs` — list of base filesystem roots (the lower layers).
   * `overlay_fs` — the root of the writable overlay (default `data`).
2. `appbox::MapBaseFS` (`loader/utils/MapBaseFS.cpp`) — for every entry of `base_fs`,
   enumerates `<base_fs>\filesystem\*`. Each child directory is one lower layer, and its
   **directory name is the layer key**:
   * a known folder token such as `%APPDATA%`, `%Windows%`, `%ProgramFiles%` (the full
     list is in `loader/utils/KnownFolder.cpp`); the token is expanded to the real folder
     path and becomes `mapped_nt_path`;
   * or a single drive letter such as `C`, which maps to `C:` itself;
   * `%REGISTRY%` and `%NETWORK%` are reserved for the other isolation domains and are
     skipped (`s_retain`).
   * `host_nt_path` is `<base_fs>\filesystem\<layer key>`;
   * layers are appended sorted by `mapped_nt_path` length, longest first, so that nested
     prefixes are matched before their parents.
3. `appbox::MapOverlayFS` (`loader/Loader.cpp`) — appends `\filesystem` to `overlay_fs`,
   converts it to an NT path, creates the directory and stores it as `fs_upper`. The
   loader also extracts `sandbox32.dll` / `sandbox64.dll` into the overlay root, next to
   (not inside) the `filesystem` subdirectory.
4. `appbox::SandboxConfig` (`sandbox/Config.hpp`) — the injected payload, carrying
   `fs_upper` and `fs_lower` (each entry being a `mapped_nt_path` / `host_nt_path` pair).
5. `appbox::Sandbox::fs` (`sandbox/Sandbox.hpp`, filled by `ParseInjectData` in
   `sandbox/Sandbox.cpp`) — the runtime form used by the resolver:

   ```cpp
   struct ResolveFsMapping { std::wstring mapped_nt_path; std::wstring host_nt_path; };
   struct ResolveFs { std::wstring fs_upper; std::vector<ResolveFsMapping> fs_lower; };
   ```

## On-disk layout

### Upper (writable) layer

```
<overlay_fs>\filesystem\<DRIVE>\<relative path>
```

The drive component is the drive letter **uppercased and without the colon** (`C`, not
`C:`), because a colon is not a legal file name character. `appbox::MappingPathInSandbox`
(`sandbox/utils/MappingPathInSandbox.cpp`) builds this path and rejects anything that is
not an absolute DOS NT path (`\??\X:\...` or `\GLOBAL??\X:\...`); `..` components are
normalized and escaping the layer root is rejected.

Example: the view path `\??\C:\Users\foo\AppData\Roaming\data.txt` maps to
`<overlay_fs>\filesystem\C\Users\foo\AppData\Roaming\data.txt`.

### Lower (read-only) layers

```
<base_fs>\filesystem\<layer key>\<relative path>
```

The view path is rebased by replacing the layer's `mapped_nt_path` prefix with its
`host_nt_path`. The comparison is case insensitive
(`appbox::PrefixCompareExchange` in `common/WString.hpp`, backed by
`CompareStringOrdinal`) and the prefix must be followed by a path separator, so
`...\AppData\RoamingX` does not match the `...\AppData\Roaming` mapping.

Example: with `mapped_nt_path = \??\C:\Users\foo\AppData\Roaming` and
`host_nt_path = \??\D:\Sandbox\Lower1\filesystem\%APPDATA%`, the view path above maps to
`\??\D:\Sandbox\Lower1\filesystem\%APPDATA%\data.txt`.

### Host layer

The view path is used as-is, i.e. the object is looked up in the real filesystem.

### Marker files

* Whiteout: `<name>.$APPBOX_DELETE$` — a sibling of the object it hides. A whiteout on a
  directory hides the whole subtree, because the marker of every ancestor component is
  checked as well (see below).
* Opaque: `<dir>\.$APPBOX_OPAQUE$` — masks, for that directory, everything that only
  exists in the lower layers.

Both markers live in the upper layer. `Resolve` never writes them; they are produced by
the delete and create paths (`NtDeleteFile.cpp`, `NtCreateFile.cpp`).

## Path resolution

Implementation: `sandbox/filesystem/Resolve.hpp` / `Resolve.cpp`.

### Entry points

```cpp
ResolveResult::Ptr Resolve(const std::wstring& vPath, const ResolveOption& option = {});
ResolveResult::Ptr ResolveFull(const ResolveFs& fs, const std::wstring& vPath,
                               const ResolveOption& option);
```

`Resolve` is a thin wrapper that passes the global `appbox::sandbox->fs`; `ResolveFull`
takes an explicit `ResolveFs` and is the layer the sandbox itself is built on.

`ResolveOption` controls the search:

| Field | Default | Meaning |
| --- | --- | --- |
| `bStopOnFirstFound` | `true` | Stop as soon as the object is found in a layer. When `false`, the search continues through all layers so that `hPath` describes every layer containing the object (needed by delete and by directory merging). |
| `NameAttributes` | `OBJ_CASE_INSENSITIVE` | Object attributes used for every internal existence check. |

### Result

| Field | Meaning |
| --- | --- |
| `status` | `Exists`, `NotFound`, `HiddenByWhiteout` or `BlockedByOpaque`. |
| `bParentExist` | The direct parent directory exists; set while the parent level of a candidate layer is checked. |
| `uPath` | The layer path in the **upper** layer (may not exist yet). |
| `uPathBaseSize` | `fs_upper.size()`, the offset of the mutable part of `uPath`; used to create missing parent directories. |
| `bInUpper` | The object itself exists in the upper layer. |
| `hPath` | Layer paths of the object, in layer order, with the `FILE_BASIC_INFORMATION` of each one. Empty when the object does not exist. |
| `whiteoutPath` / `bWhiteoutInUpper` | The whiteout that hid the object, and whether it is in the upper layer. |
| `opaquePath` / `bOpaqueInUpper` | The opaque marker that blocked the lookup, and whether it is in the upper layer. |

`ResolveResult` has a `to_json` overload and is dumped into the trace log on every
resolution.

### Layer candidates

`MapViewPathToHost` builds the ordered candidate list:

1. the upper layer (`fs_upper`);
2. every lower layer whose `mapped_nt_path` is a prefix of the view path, in
   configuration order;
3. the host layer, with `base_fs` = the drive part of the view path (`\??\C`) and
   `file_fs` = the view path itself.

### Per-layer search

For every candidate layer, `Sequence(file_fs, base_fs.size(), L"\\", true)` produces the
cumulative path list from the layer root down to the target. For the upper and lower
layers the first element is the layer root; for the host layer it is the drive
(`\??\C:`), which is why that element gets a trailing backslash before being checked.

Each element is then checked in this order:

1. `<element>.$APPBOX_DELETE$` — if it exists, the search stops immediately with a
   whiteout result (`HiddenByWhiteout` when nothing else was found). This applies to
   every level, so a whiteout on an ancestor hides the whole subtree.
2. for non-final elements: the directory itself must exist, otherwise the layer search
   aborts. When the direct parent exists, `bParentExist` is set. Then
   `<directory>\.$APPBOX_OPAQUE$` is checked; if it exists, `opaque_found` is recorded
   and the **outer** loop stops after this layer, so lower layers are not consulted.
3. for the final element: an existence check; on success the layer path and its basic
   information are appended to `hPath`.

The outer loop over layers runs while no whiteout and no opaque marker was found, and
stops early when `bStopOnFirstFound` is set and `hPath` is not empty. Finding the object
in the first candidate layer sets `bInUpper`; a whiteout or opaque marker found in that
first layer sets `bWhiteoutInUpper` / `bOpaqueInUpper`.

Status is then derived: `hPath` non-empty means `Exists`, otherwise the recorded whiteout
or opaque marker decides between `HiddenByWhiteout` and `BlockedByOpaque`, and everything
else is `NotFound`.

### Worked example

Configuration: `overlay_fs = D:\Tests\Upper`, `base_fs = [D:\Tests\Lower1, D:\Tests\Lower2]`.
Injected `ResolveFs`:

```
fs_upper = \??\D:\Tests\Upper\filesystem
fs_lower = [
  { mapped_nt_path = \??\C:\Users\foo\AppData\Roaming,
    host_nt_path   = \??\D:\Tests\Lower1\filesystem\%APPDATA% },
  { mapped_nt_path = \??\C:\Users\foo\AppData\Roaming,
    host_nt_path   = \??\D:\Tests\Lower2\filesystem\%APPDATA% },
]
```

Resolving `\??\C:\Users\foo\AppData\Roaming\data.txt` yields these candidates:

| Order | Layer | Layer path |
| --- | --- | --- |
| 1 | upper | `\??\D:\Tests\Upper\filesystem\C\Users\foo\AppData\Roaming\data.txt` |
| 2 | lower 1 | `\??\D:\Tests\Lower1\filesystem\%APPDATA%\data.txt` |
| 3 | lower 2 | `\??\D:\Tests\Lower2\filesystem\%APPDATA%\data.txt` |
| 4 | host | `\??\C:\Users\foo\AppData\Roaming\data.txt` |

If only the lower layers contain the file, `hPath` holds the two lower paths and
`bInUpper` is `false`; a subsequent write has to copy the file into the upper layer
first (copy-up).

## File API behavior

### Path normalization

Every hooked API first turns the caller's arguments into a view path:

* `appbox::ConvertToFullNtPath` (`sandbox/utils/ConvertToFullNtPath.cpp`) handles a path
  relative to `ObjectAttributes->RootDirectory` (the root handle is queried for its own
  NT name) and `FILE_OPEN_BY_FILE_ID` (the object is re-opened by file ID to recover its
  real name, with a `\:fid:0x...` fallback).
* `appbox::MappingAsDosNtPath` (`sandbox/utils/MappingAsDosNtPath.cpp`) converts
  `\Device\HarddiskVolumeX\...`, `\??\Volume{GUID}\...`, `\SystemRoot\...` and
  `\??\GLOBALROOT\Device\...` into `\??\X:\...` for local drives.
  UNC paths, named pipes, mailslots and network volumes are rejected on purpose.

If either step fails the hook forwards the call unchanged, i.e. paths that cannot be
expressed as a local view path are **not** isolated (they belong to the network
isolation domain).

### `NtCreateFile` (`sandbox/hook/NtCreateFile.cpp`)

1. Resolve the view path with the default options (`bStopOnFirstFound = true`).
2. `bParentExist == false` fails with `STATUS_OBJECT_PATH_NOT_FOUND`.
3. Disposition checks: `FILE_CREATE` on an existing object fails with
   `STATUS_OBJECT_NAME_COLLISION`; `FILE_OVERWRITE` on a missing object fails with
   `STATUS_OBJECT_NAME_NOT_FOUND`.
4. If the object is hidden by a whiteout **in the upper layer** and the disposition
   allows creation, the whiteout is removed first (`RemoveAll`). If the new object is a
   directory, the resolver runs again; when the directory still exists in a lower layer
   an opaque marker is created so that the lower contents stay hidden.
5. `want_create` (`FILE_SUPERSEDE`, `FILE_CREATE`, `FILE_OPEN_IF`, `FILE_OVERWRITE_IF`)
   or `want_edit` (any of `DELETE`, `FILE_WRITE_DATA`, `FILE_WRITE_ATTRIBUTES`,
   `FILE_WRITE_EA`, `FILE_APPEND_DATA`, `WRITE_DAC`, `WRITE_OWNER`, `GENERIC_WRITE`,
   `GENERIC_ALL`) creates the missing parent directories inside the upper layer
   (`CreateDirectories(DirName(uPath), uPathBaseSize)`).
6. `want_edit` on an object that exists only in a lower or host layer triggers a
   **copy-up**: the object is copied to `uPath` (`appbox::CopyFileNt`).
7. The call is forwarded to the original API with the upper path when the object is
   created or modified, otherwise with the layer path where it was found.

Reentrancy is controlled by `ThreadLocal::disable_NtCreateFile_hook` (see
`appbox::NtCreateFileLock`); all internal NT calls go through the saved original
pointers (`sys_NtCreateFile` and friends), so they bypass the hooks by construction.

### `NtOpenFile` (`sandbox/hook/NtOpenFile.cpp`)

Resolves with `bStopOnFirstFound = false` (so `hPath` lists every layer holding the
object) and returns `STATUS_OBJECT_PATH_NOT_FOUND` / `STATUS_OBJECT_NAME_NOT_FOUND` for a
missing parent / missing object. Opening for write access performs a copy-up and inserts
the upper path at the front of `hPath`. On success the handle is registered in
`appbox::HandleInfo` together with the view path, the resolve result and the object
attributes.

### `NtDeleteFile` and deferred deletion

`sandbox/hook/NtDeleteFile.cpp` forwards to `appbox::DeleteViewPath`, which is also
called by `NtClose`:

* Object does not exist (or parent missing) — the corresponding NTSTATUS is returned and
  no marker is written.
* **File**: if it exists in the upper layer the upper copy is deleted; if it also exists
  in a lower/host layer, or only there, a whiteout is created next to `uPath`.
* **Directory**: all layers that contain the directory are enumerated and must contain
  nothing but marker files, otherwise `STATUS_DIRECTORY_NOT_EMPTY` is returned. Then the
  upper copy (if any) is removed recursively and a whiteout is created if lower layers
  still hold the directory.

`sandbox/hook/NtClose.cpp` covers delete-on-close: for handles registered by
`NtOpenFile`, the handle is inspected for `DeletePending` (`FileStandardInformation`)
before closing; when it was pending and the close succeeded, `DeleteViewPath` runs so the
whiteout is created after the real object is gone.

### `NtQueryDirectoryFileEx` (`sandbox/hook/NtQueryDirectoryFileEx.cpp`)

Directory listings are merged across layers for `FileFullDirectoryInformation`:

1. A per-handle `FullDirectoryInformationMeta` is created from the resolve result; its
   `PendingDir` queue holds the layer paths of the directory that actually exist.
2. Each real directory is opened with
   `FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_FOR_BACKUP_INTENT`
   and queried in turn, so entries from upper and from every lower layer are seen once.
3. `FixNameInfo` rewrites the returned buffer in place through
   `appbox::FileFullDirInformationWalker` and drops entries that are
   * marker files (`*.$APPBOX_DELETE$` or `.$APPBOX_OPAQUE$`), or
   * not visible in the view, i.e. `Resolve(BasePath + "\" + name)` is not `Exists`
     (this is what makes whiteouts and opaque markers effective in listings), or
   * duplicates of a name already emitted (case insensitive comparison when
     `OBJ_CASE_INSENSITIVE` is set).
4. `SL_RESTART_SCAN` discards the cached state and restarts from the upper layer; the
   state is released when the handle is closed.

The plain `NtQueryDirectoryFile` hook is not merged — see the gaps below.

### Behavior matrix (from the test suite)

`test/cases/Fs_*.cpp`, with `Upper` as the overlay and `Lower1` / `Lower2` as base
filesystems. Each case is documented in its own header comment.

| Case | Upper | Lower1 | Lower2 | Operation | Expected |
| --- | --- | --- | --- | --- | --- |
| `DeleteFile_MultiLower_ExistsInLower` | – | `data.txt` | `data.txt` | delete `data.txt` | success, upper whiteout created, no upper file |
| `DeleteFile_MultiLower_ExistsInLowerUpper` | `data.txt` | `data.txt` | `data.txt` | delete `data.txt` | success, upper file deleted, whiteout created |
| `DeleteFile_MultiLower_ExistsInUpper` | `data.txt` | – | – | delete `data.txt` | success, no whiteout (nothing to hide) |
| `DeleteFile_MultiLower_NonExists` | – | `data1.txt` | `data2.txt` | delete `data.txt` | failure, no whiteout |
| `DeleteFile_WhiteoutInLower_ExistsInUpper` | `data.txt` | `data.txt.$APPBOX_DELETE$` | `data.txt` | delete `data.txt` | success, upper file deleted, no whiteout in upper |
| `ListDir_MultiLower_ExistsInLower` | – | `F.txt` | `F.txt` | list `%APPDATA%` | `F.txt` appears exactly once, host entries also listed |
| `ListDir_MultiLower_ExistsInLower_WhiteoutInUpper` | `F.txt.$APPBOX_DELETE$` | `F.txt` | `F2.txt` | list `%APPDATA%` | `F.txt` hidden, `F2.txt` listed once |
| `NewFile_MultiLower_WhiteoutInLower` | – | `data.txt.$APPBOX_DELETE$` | `data.txt` | create `data.txt` (`CREATE_NEW`) | success, file created in upper |
| `NewFile_MultiLower_WhiteoutInUpper` | `data.txt.$APPBOX_DELETE$` | `data.txt` | `data.txt` | create `data.txt` (`CREATE_NEW`) | success, upper whiteout removed, file created in upper |
| `ReadFile_MultiLower_WhiteoutInUpper` | `data.txt.$APPBOX_DELETE$` | `data.txt` | `data.txt` | read `data.txt` | failure |

## Supporting modules

| Module | Responsibility |
| --- | --- |
| `sandbox/filesystem/Resolve.*` | View path resolution (described above). |
| `sandbox/filesystem/Sequence.*` | `Sequence(path, offset, delimiter, bIncludeLast)` — cumulative path prefixes used for the per-level marker checks. |
| `sandbox/filesystem/DirName.*` | Parent directory of an NT path, used to create the missing upper directories. |
| `sandbox/filesystem/CreateDirectory.*` | `CreateDirectories(path, offset)` — creates every missing level with `NtCreateFile(FILE_OPEN_IF \| FILE_DIRECTORY_FILE)`. |
| `sandbox/filesystem/RemoveAll.*` | Recursive deletion: clears the read-only attribute, empties directories, marks entries with `FileDispositionInformation` and does not follow reparse points. |
| `sandbox/utils/MappingPathInSandbox.*` | View path to upper layer path (drive letter uppercased, colon removed, `..` normalized). |
| `sandbox/utils/MappingAsDosNtPath.*` | Device / volume GUID / `\SystemRoot` / `\Device\...` paths to DOS NT paths; rejects non-local namespaces. |
| `sandbox/utils/ConvertToFullNtPath.*` | `RootDirectory`-relative paths and `FILE_OPEN_BY_FILE_ID` to full NT paths. |
| `sandbox/utils/CheckPathExist.*` | Existence probe used by the resolver, built on `NtQueryAttributesFile`. |
| `sandbox/utils/CopyFileNt.*` | Copy-up: NT level copy with 64 KiB chunks, creates missing parents. |
| `sandbox/utils/HandleInfo.*` | Handle to (view path, resolve result, object attributes) map plus per-handle metadata slots. |
| `sandbox/utils/FileFullDirInformationWalker.*` | In-place filtering of `FILE_FULL_DIR_INFORMATION` buffers with `NextEntryOffset` fix-ups. |

## Test coverage

* `test/utils/FsBuilder.*` — declarative tree builder. `FsRoot(root, {Upper, Lower1, Lower2})`
  materializes the directories and returns a `LoaderConfig` whose `overlay_fs` is the
  first entry and whose `base_fs` holds the rest; the lower layer directories are named
  after the known folder token (`%APPDATA%`) so that `MapBaseFS` resolves them.
  `Verify()` re-reads the lower layers and fails if their content changed.
* `test/utils/CommonFixture.*` — gives every case a private working directory.
* `test/utils/ProbeCall.*` — writes the `LoaderConfig` to `config.json`, starts the
  **loader** with `--X-AppBox-ConfigFile`, which launches the test binary as a probe
  process with the sandbox DLL injected; the probe asks the test process for its task
  over a named pipe and reports the result back.
* `test/probe/*` — the operations executed inside the sandbox (`CreateFileW`,
  `CreateDirectoryW`, `DeleteFileW`, `ListDir`, `ReadFileFull`).
* `test/cases/Fs_*.cpp` — the behavior matrix above, plus `ArugmentsPassthrough.cpp`
  and `RPC.cpp`.

## Known gaps and limitations

The following points are visible in the current code and should be kept in mind when
extending or testing the isolation:

1. **Path-based queries are not redirected.** `NtQueryAttributesFile` (marked `// TODO`
   in the source), `NtQueryFullAttributesFile`, `NtQueryInformationByName` and
   `NtQueryDirectoryFile` log their arguments and forward the call unchanged, so the
   query hits the raw path (a lower layer or the host filesystem) instead of the view.
   The resolver itself is not affected, because `CheckPathExist` calls the original
   `NtQueryAttributesFile` with paths that are already rebased into a layer.
2. **Handle-based hooks only log.** `NtQueryInformationFile`, `NtSetInformationFile`,
   `NtQueryVolumeInformationFile`, `NtDeviceIoControlFile` and `NtFsControlFile` forward
   unchanged. The handle already refers to the layer that was selected at open time
   (the upper layer after a copy-up), so most queries are consistent with the view; the
   exceptions are the information classes that carry a path, see the next item.
3. **Metadata writes carrying a path are not redirected.** `NtSetInformationFile` is not
   hooked, so `FileRenameInformation`, `FileLinkInformation` and friends are applied to
   the name exactly as the caller passed it. A full view path (`\??\C:\...`) therefore
   denotes the real filesystem instead of the view, and a rename/move can move an object
   out of the upper layer into the host filesystem. Classes that only act on the handle
   (`FileBasicInformation`, `FileEndOfFileInformation`, `FileDispositionInformation`,
   ...) are consistent with the view, because the handle is already the correct one.
4. **Delete-on-close is only handled for registered handles.** `NtClose` consults
   `HandleInfo`, which is populated by `NtOpenFile` only. A handle opened through the
   `NtCreateFile` hook and marked for deletion has no handle information, so closing it
   deletes the layer object without creating the whiteout that would hide the lower
   layers.
5. **Directory merging is limited to one information class.** Only
   `FileFullDirectoryInformation` is merged, and only on `NtQueryDirectoryFileEx`.
   Callers that use `NtQueryDirectoryFile` or another information class see the single
   layer that the directory handle was opened with.
6. **Opaque marker creation needs verification.** In the re-creation branch of
   `NtCreateFile`, the marker path is derived from the view path
   (`nativate_fs_path + "\\" + APPBOX_SANDBOX_OPAQUE_NAME_W`) instead of the upper layer
   path `uPath`, and the call bypasses the hooks. The marker is therefore not written
   into the upper layer as intended, so masking of a re-created directory's lower
   contents cannot be relied on yet.
7. **Copy-up copies the default data stream only.** Alternate data streams are not
   handled specially: a stream name such as `file.txt:stream` is carried into the upper
   layer path as part of the file name, while copy-up reads only the file content, and
   whiteout / opaque markers are not stream aware.
8. **Non-local paths bypass isolation.** UNC paths, named pipes, mailslots, network
   volumes and drive-relative paths cannot be converted to a view path, so the hooks
   forward them unchanged.
9. **`ResolveFs` is fixed at injection time.** There is no way to add or remove a lower
   layer while a sandboxed process is running; the layers come from the injected
   configuration and live in the `appbox::sandbox` singleton.
