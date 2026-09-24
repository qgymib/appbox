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
   * a known folder token such as `#APPDATA#`, `#Windows#`, `#ProgramFiles#` (the full
     list is in `loader/utils/KnownFolder.cpp`); the token is expanded to the real folder
     path and becomes `mapped_nt_path`;
   * or a single drive letter such as `C`, which maps to `C:` itself;
   * `#REGISTRY#` and `#NETWORK#` are reserved for the other isolation domains and are
     skipped (`s_retain`).
   * `host_nt_path` is `<base_fs>\filesystem\<layer key>`;
   * layers are appended sorted by `mapped_nt_path` length, longest first, so that nested
     prefixes are matched before their parents.

   A layer key is a `#Name#` delimited token, where `#` is an ordinary file name
   character. A packed path therefore never contains `%`, which a shell (`%%` is the
   escape sequence) or an environment expanding API would otherwise reinterpret. An
   archive which was packed with the former `%Name%` form has to be packed again:
   `MapBaseFS` rejects the unknown layer name.
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
6. `appbox::Sandbox::fs_isolation` (`sandbox/filesystem/IsolationTable.*`,
   filled by the isolation module) — the modes of the virtual filesystem,
   translated from the virtual paths of the isolation file into paths of the
   view with the layer mapping of step 5. An empty table keeps every layer of
   the view visible.

## Workspace isolation modes

The packer offers an isolation mode for every file and folder of the virtual
filesystem. The mode is picked in the `Isolation` column of the filesystem
workspace (see [README.md](../README.md)), stored in the project file and
written into the isolation file of the archive, which the sandbox reads back
and enforces (see [The isolation file](#the-isolation-file)).

| Kind | Modes | Default |
| --- | --- | --- |
| folder | `Full`, `Write Copy`, `Whiteout` | `Write Copy` |
| file | `Full`, `Whiteout` | `Full` |

* **Full** (folder) - only the virtual filesystem is visible, even when the
  host holds the folder: the host entry of the folder and of everything below
  it is masked, so a modification of the folder or of the files below it lands
  in the sandbox.
* **Write Copy** (folder) - the host filesystem and the virtual filesystem are
  both visible with the virtual one taking precedence. Every modification lands
  in the sandbox.
* **Whiteout** (folder and file) - the entry is invisible for the sandboxed
  process, even when the host or the packed content holds it: opening, reading,
  writing and deleting report `File Not Found`. Creating the entry succeeds
  inside the sandbox, and the entry is readable and writable afterwards while
  the hidden layers stay hidden.
* **Full** (file) - every write of the file lands in the sandbox, while the
  host file stays readable through the view.

The mode of a folder reaches the entries below it: an entry which carries no
mode of its own follows the closest folder above it which does, so the scope of
a folder mode is its own subtree and a folder below it can override it. A
conflict between the virtual filesystem and the host filesystem is resolved in
favour of the virtual filesystem.

The mode of a path and the kind of the entry which carries it decide which
layers of the view stay visible (see
[`IsolationPolicy`](#supporting-modules)):

| Mode of the closest listed entry | Kind of that entry | host layer | lower layers | upper layer |
| --- | --- | --- | --- | --- |
| `Full` | folder | masked | visible | visible |
| `Full` | file | visible | visible | visible |
| `Write Copy` | folder | visible | visible | visible |
| `Whiteout` | folder or file | masked | masked | visible |

The vocabulary is shared (`common/FilesystemIsolation.hpp`), the modes of the
workspace are held by `src/core/FilesystemIsolationModel.*` of the packer.

### The isolation file

`BuildFilesystemIsolationFile` (`src/core/FilesystemIsolationFile.cpp`) writes
the modes of the workspace as a JSON document, which `Pack` stores in the
overlay of the archive as `data/filesystem-isolation.json` — next to the
`registry` folder, not below `filesystem`, because the loader treats every child
of that folder as a layer of the view:

```json
{
  "version": 1,
  "entries": [
    { "path": "#ProgramFiles#\\MyApp", "kind": "directory", "isolation": "full" },
    { "path": "#ProgramFiles#\\MyApp\\app.exe", "kind": "file", "isolation": "whiteout" }
  ]
}
```

The `path` of an entry is a path of the virtual filesystem, which is the path
the `Source Path` column shows: the first component is the layer key of a preset
directory and the remaining ones are the path below it. Only the entries the
user set a mode for are listed; an entry which the document does not mention
follows the closest listed folder above it and falls back to the default of its
kind.

The loader derives the path of the file from the overlay
(`MapFilesystemIsolationFile` in `loader/Loader.cpp`) and passes it to the
sandbox as `SandboxConfig.filesystem_isolation_dos_path`. The sandbox module
`appbox::filesystem::Isolation` reads the document while it initializes — before
the hooks are attached — and fills `appbox::sandbox->fs_isolation`. A missing
file, a missing configuration or a malformed document is not an error: the
sandbox then behaves like one without an isolation file, in which every entry
keeps the default of its kind and the host filesystem stays visible.

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
`host_nt_path = \??\D:\Sandbox\Lower1\filesystem\#APPDATA#`, the view path above maps to
`\??\D:\Sandbox\Lower1\filesystem\#APPDATA#\data.txt`.

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

### `CreateProcessInternalW` (`sandbox/hook/CreateProcessInternalW.cpp`)

Process creation hands the application path to `NtCreateUserProcess`, which
is not hooked and resolves the path in the host filesystem. The hook
therefore rewrites `lpApplicationName` into the host layer path which holds
the image (resolved through the view) before the creation is forwarded and
the sandbox DLL is injected. Applications which only exist in a lower layer
become launchable this way; the process is still injected, so every file
operation of the child stays inside the view.

## Packer archives

`AppBox` produces self-contained archives which double as a base
filesystem. The layout matches the loader runtime conventions:

```
<startup file name>                    embedded loader payload
<startup file name>.json               launch configuration:
                                       base_fs = ["."], overlay_fs = "data",
                                       launch.executable = <layer key>\<import>\<exe>
data/filesystem-isolation.json         isolation modes of the filesystem workspace
filesystem/<layer key>/<import>/...    imported folder content
```

The loader program and its configuration carry the file name of the startup
file selected in the startup file tree, so a startup file `foo.exe` (even when
it lives in a subdirectory of its imported folder) is packed as the archive
entries `foo.exe` and `foo.exe.json`. The loader resolves its configuration as
`<own file name>.json` in its own directory and does not fall back to another
name, so renaming the extracted loader program requires renaming the
configuration file as well.

The archive root is the single `base_fs` entry (relative `.`, expanded by
the loader against the executable directory), so `MapBaseFS` maps every
`filesystem/<layer key>` child directory as a lower layer, and
`launch.executable` is expanded by `ExpandKnownFolder` into the sandbox
view. Running the extracted loader program therefore shows the imported
folders at their preset locations (`#ProgramFiles#\<import>`,
`#USERPROFILE#\<import>`) and starts the selected main program inside the
isolation.

## Path resolution

Implementation: `sandbox/filesystem/Resolve.hpp` / `Resolve.cpp`.

### Entry points

```cpp
ResolveResult::Ptr Resolve(const std::wstring& vPath, const ResolveOption& option = {});
ResolveResult::Ptr ResolveFull(const ResolveFs& fs, const std::wstring& vPath,
                               const ResolveOption& option,
                               const IsolationTable* isolation = nullptr);
```

`Resolve` is a thin wrapper that passes the global `appbox::sandbox->fs` and the
global `appbox::sandbox->fs_isolation`; `ResolveFull` takes an explicit
`ResolveFs` and an explicit isolation table (null resolves the view without an
isolation) and is the layer the sandbox itself is built on.

`ResolveOption` controls the search:

| Field | Default | Meaning |
| --- | --- | --- |
| `bStopOnFirstFound` | `true` | Stop as soon as the object is found in a layer. When `false`, the search continues through all layers so that `hPath` describes every layer containing the object (needed by delete and by directory merging). |
| `NameAttributes` | `OBJ_CASE_INSENSITIVE` | Object attributes used for every internal existence check. |

### Result

| Field | Meaning |
| --- | --- |
| `status` | `Exists`, `NotFound`, `HiddenByWhiteout`, `BlockedByOpaque` or `HiddenByIsolation`. |
| `bParentExist` | The direct parent directory exists; set while the parent level of a candidate layer is checked. |
| `uPath` | The layer path in the **upper** layer (may not exist yet). |
| `uPathBaseSize` | `fs_upper.size()`, the offset of the mutable part of `uPath`; used to create missing parent directories. |
| `bInUpper` | The object itself exists in the upper layer. |
| `hPath` | Layer paths of the object, in layer order, with the `FILE_BASIC_INFORMATION` and the index of the layer of each one. Empty when the object does not exist. |
| `whiteoutPath` / `bWhiteoutInUpper` | The whiteout that hid the object, and whether it is in the upper layer. |
| `opaquePath` / `bOpaqueInUpper` | The opaque marker that blocked the lookup, and whether it is in the upper layer. |
| `isolation` / `isolationSource` / `bIsolationListed` | The mode of the closest listed entry of the isolation table, the kind of that entry, and whether such an entry decided the mode. |
| `bIsolationMasked` | Whether the isolation hides a layer of the path; a call which does not create the entry then reports a missing file instead of a missing path. |

`ResolveResult` has a `to_json` overload and is dumped into the trace log on every
resolution.

### Layer candidates

`MapViewPathToHost` builds the ordered candidate list:

1. the upper layer (`fs_upper`);
2. every lower layer whose `mapped_nt_path` is a prefix of the view path, in
   configuration order;
3. the host layer, with `base_fs` = the drive part of the view path (`\??\C`) and
   `file_fs` = the view path itself.

The isolation does not change the candidate list: it masks the **hits** of the
layers the mode of the path hides (see [Isolation of a path](#isolation-of-a-path)).
Masking instead of dropping a candidate keeps the parent check intact, so a
parent directory which only a hidden layer holds still decides whether the path
can be created.

A drive root is a special case: `ResolveFull` keeps its separator (`\??\C:\`) instead of
stripping it, because `\??\C:` alone is a drive relative path which the layer mapping
rejects. Without that the root looks like a missing file, and every caller of `Resolve()`
(`NtOpenFile`, `NtCreateFile`, `NtQueryAttributesFile`, ...) reports a failure for a path
the operating system opens successfully. Applications do open drive roots, for example to
query the free space or to watch a volume.

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
or opaque marker decides between `HiddenByWhiteout` and `BlockedByOpaque`, an entry which
the isolation hides reports `HiddenByIsolation`, and everything else is `NotFound`.

### Isolation of a path

`ApplyIsolation` (`sandbox/filesystem/Resolve.cpp`) runs after the search and before the
status is derived. It asks the isolation table for the **closest listed entry** at or
above the view path, which yields the mode of the path and the kind of the entry that
carries it, and then drops the hits of the layers that mode hides (see the table of
[Workspace isolation modes](#workspace-isolation-modes)):

* the host layer is masked when the mode is `Whiteout` or when it is `Full` and the
  listed entry is a folder, so the host folder and its whole subtree are invisible;
* the lower layers are masked when the mode is `Whiteout`, so the packed content of a
  hidden entry is invisible as well;
* the upper layer is never masked: it holds the entries the sandboxed process created
  itself, which is what makes a `Whiteout` entry visible after its creation.

An entry without a listed entry keeps the default of its kind, which hides nothing, so a
sandbox without an isolation file resolves every path exactly like before.

A `Whiteout` entry has no visible layer of its own, so the resolver reports
`HiddenByIsolation`; the hooks turn that into `File Not Found` for every call which does
not create the entry, while a create lands in the upper layer (see
[`NtCreateFile`](#ntcreatefile-sandboxhookntcreatefilecpp)) and the entry is visible from
then on. The mask is recorded in `bIsolationMasked`, so a call which does not create an
entry that only a hidden layer holds reports a missing **file** instead of a missing
**path** (its parent directory is hidden as well).

The inheritance rule follows from the lookup: the closest listed entry decides, so a
folder below a `Full` folder which is set to `Write Copy` shows the host content of its
own subtree again, and an entry which a `Whiteout` folder covers stays invisible until
the sandboxed process creates it.

### Worked example

Configuration: `overlay_fs = D:\Tests\Upper`, `base_fs = [D:\Tests\Lower1, D:\Tests\Lower2]`.
Injected `ResolveFs`:

```
fs_upper = \??\D:\Tests\Upper\filesystem
fs_lower = [
  { mapped_nt_path = \??\C:\Users\foo\AppData\Roaming,
    host_nt_path   = \??\D:\Tests\Lower1\filesystem\#APPDATA# },
  { mapped_nt_path = \??\C:\Users\foo\AppData\Roaming,
    host_nt_path   = \??\D:\Tests\Lower2\filesystem\#APPDATA# },
]
```

Resolving `\??\C:\Users\foo\AppData\Roaming\data.txt` yields these candidates:

| Order | Layer | Layer path |
| --- | --- | --- |
| 1 | upper | `\??\D:\Tests\Upper\filesystem\C\Users\foo\AppData\Roaming\data.txt` |
| 2 | lower 1 | `\??\D:\Tests\Lower1\filesystem\#APPDATA#\data.txt` |
| 3 | lower 2 | `\??\D:\Tests\Lower2\filesystem\#APPDATA#\data.txt` |
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
4. An entry which the isolation hides (`HiddenByIsolation`) or which only a hidden layer
   holds (`bIsolationMasked`) does not exist in the view: a call which does not ask for a
   creation fails with `STATUS_OBJECT_NAME_NOT_FOUND`, while a creating disposition
   writes the entry into the upper layer and the entry is visible from then on.
5. If the object is hidden by a whiteout **in the upper layer** and the disposition
   allows creation, the whiteout is removed first (`RemoveAll`). If the new object is a
   directory, the resolver runs again; when the directory still exists in a lower layer
   an opaque marker is created next to `uPath` so that the lower contents stay hidden.
6. `want_create` (`FILE_SUPERSEDE`, `FILE_CREATE`, `FILE_OPEN_IF`, `FILE_OVERWRITE_IF`)
   or `want_edit` (any of `DELETE`, `FILE_WRITE_DATA`, `FILE_WRITE_ATTRIBUTES`,
   `FILE_WRITE_EA`, `FILE_APPEND_DATA`, `WRITE_DAC`, `WRITE_OWNER`, `GENERIC_WRITE`,
   `GENERIC_ALL`) creates the missing parent directories inside the upper layer
   (`CreateDirectories(DirName(uPath), uPathBaseSize)`).
7. `want_edit` on an object that exists only in a lower or host layer triggers a
   **copy-up**: the object is copied to `uPath` (`appbox::CopyFileNt`).
8. The call is forwarded to the original API with the upper path when the object is
   created or modified, otherwise with the layer path where it was found.

Reentrancy is controlled by `ThreadLocal::disable_NtCreateFile_hook` (see
`appbox::NtCreateFileLock`); all internal NT calls go through the saved original
pointers (`sys_NtCreateFile` and friends), so they bypass the hooks by construction.

### `NtQueryAttributesFile` (`sandbox/hook/NtQueryAttributesFile.cpp`)

Path-based attribute queries are redirected through the view:

1. The view path is extracted (root handle / file ID resolution, DOS NT
   conversion).
2. The resolver decides the layer; a missing parent yields
   `STATUS_OBJECT_PATH_NOT_FOUND`, a missing or whiteouted object yields
   `STATUS_OBJECT_NAME_NOT_FOUND`.
3. The query is forwarded with the layer path of the first layer holding
   the object.

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

### `NtQueryDirectoryFileEx` and `NtQueryDirectoryFile`

Directory listings are merged across layers by
`sandbox/filesystem/DirectoryMerge.*`, which both entry points share
(`sandbox/hook/NtQueryDirectoryFileEx.cpp` and
`sandbox/hook/NtQueryDirectoryFile.cpp` call it, so a caller may mix them on the same
handle):

1. A per-handle `FullDirectoryInformationMeta` is created from the resolve result; its
   `PendingDir` queue holds the layer paths of the directory that actually exist, which
   is the merge of the layers the isolation of the directory leaves visible.
2. Each real directory is opened with
   `FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_FOR_BACKUP_INTENT`
   and queried in turn with the entry point and the information class of the caller, so
   entries from upper and from every visible lower layer are seen once.
3. `FixNameInfo` rewrites the returned buffer in place through
   `appbox::DirectoryInformationWalker` and drops entries that are
   * marker files (`*.$APPBOX_DELETE$` or `.$APPBOX_OPAQUE$`), or
   * not visible in the view, i.e. `Resolve(BasePath + "\" + name)` is not `Exists`
     (this is what makes whiteouts, opaque markers and the isolation of the view
     effective in listings), or
   * duplicates of a name already emitted (case insensitive comparison when
     `OBJ_CASE_INSENSITIVE` is set).
4. `SL_RESTART_SCAN` discards the cached state and restarts from the upper layer; the
   restart flag is applied to the first query of a call only, because the merge queries
   the layer again after it dropped an entry. The state is released when the handle is
   closed.

The merge reads and rewrites the entry list, so it covers the information classes which
carry the name of an entry: `FileDirectoryInformation`,
`FileFullDirectoryInformation` and `FileBothDirectoryInformation`. Every other class —
and every call whose handle was not registered by `NtOpenFile` — is forwarded unchanged
and therefore sees the layer the handle was opened with (see the gaps below).

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
| `sandbox/utils/DirectoryInformationWalker.*` | Layout of the directory information classes plus in-place filtering of their buffers with `NextEntryOffset` fix-ups. |
| `sandbox/filesystem/DirectoryMerge.*` | Shared merge of the layers of a directory for both enumeration entry points. |
| `sandbox/filesystem/IsolationTable.*` | The isolation modes of the virtual filesystem: the document of the packer, translated into paths of the view, with the lookup of the closest listed entry. |
| `sandbox/filesystem/IsolationPolicy.hpp` | The decision table of the modes (`HidesHost`, `HidesLower`, `HidesEntry`), shared by the resolver and the hooks. |
| `sandbox/filesystem/Isolation.*` | The sandbox module which reads the isolation file of the injected configuration. |

## Tests

The unit tests and the end-to-end cases of the filesystem isolation are
listed in [test/README.md](../test/README.md).

## Hook robustness contract

A hook runs inside a kernel call of the application, so a failure inside the hook is a
failure of the application. Two rules follow from that, both of them are covered by the
unit tests listed in [test/README.md](../test/README.md):

1. **Never read more than the caller declared.** The structures which arrive in a hook
   belong to the application and can be inconsistent. A `UNICODE_STRING` whose `Length`
   exceeds `MaximumLength` is not read at all (`appbox::UnicodeStringToUTF8`), and
   `ConvertToFullNtPath` rejects such a call with `STATUS_INVALID_PARAMETER` so that the
   hook forwards it unchanged. Applications pass such values on purpose to find a hook
   which follows the buffer blindly.
2. **Never throw.** Every parameter parser of the sandbox returns a placeholder instead
   of throwing: the conversions of `sandbox/utils/Log.cpp` answer with an empty string
   for a value they can not read, `appbox::DumpJson` replaces bytes which are not valid
   UTF-8 instead of failing the serialization, and `appbox::Log` treats an exception of
   its sink like a failed delivery (the message is counted by
   `appbox::DroppedLogCount()` and never leaves the log path). The RPC transport follows
   the same rule, `appbox::PipeClient::Call` reports a malformed response through its
   boolean result. `appbox::LoggerF::Log` keeps a hard `abort()` as an unreachable
   sentinel: reaching it means that a parser regressed, which the death test
   `UnitLog.LoggerAbortsWhenAParameterParserThrows` pins down. An exception which leaves
   a hook unwinds through the hooked call and terminates the application, which is how a
   packaged application can fail before it shows a window.

## Known gaps and limitations

The following points are visible in the current code and should be kept in mind when
extending or testing the isolation:

1. **Path-based queries are not redirected.** `NtQueryFullAttributesFile` and
   `NtQueryInformationByName` log their arguments and forward the call unchanged, so the
   query hits the raw path (a lower layer or the host filesystem) instead of the view.
   `NtQueryAttributesFile` and both directory enumeration entry points are redirected
   (see above). The resolver itself is not affected, because `CheckPathExist` calls the
   original `NtQueryAttributesFile` with paths that are already rebased into a layer.
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
5. **Directory merging covers the name carrying information classes only.** The merge
   reads and rewrites the entry list, so it understands `FileDirectoryInformation`,
   `FileFullDirectoryInformation` and `FileBothDirectoryInformation`. A caller which uses
   one of the `FileId...DirectoryInformation` classes, an information class which does
   not carry a name, or a directory handle which was not registered by `NtOpenFile`
   (a handle of `CreateFileW`, for example) sees the single layer the handle was opened
   with.
6. **A mode does not reach the alternate data streams of its file.** The lookup of the
   isolation walks the path upwards component by component, and a stream name such as
   `file.txt:stream` is the last component of its own path, so it does not inherit the
   mode of `file.txt`. The mode of a folder still covers the streams of the files below
   it.
7. **Copy-up copies the default data stream only.** Alternate data streams are not
   handled specially: a stream name such as `file.txt:stream` is carried into the upper
   layer path as part of the file name, while copy-up reads only the file content, and
   whiteout / opaque markers are not stream aware.
8. **Non-local paths bypass isolation.** UNC paths, named pipes, mailslots, network
   volumes and drive-relative paths cannot be converted to a view path, so the hooks
   forward them unchanged.
9. **`ResolveFs` is fixed at injection time.** There is no way to add or remove a lower
   layer while a sandboxed process is running; the layers come from the injected
   configuration and live in the `appbox::sandbox` singleton. The isolation modes are
   loaded once as well, so a mode which is changed in the packer afterwards needs a new
   archive.
