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
(`appbox::Sandbox::bIsolationMode`). Outside isolation mode the hooks are never
attached.

A path that cannot be expressed as a local view path (UNC paths, named pipes,
mailslots, network volumes) is forwarded unchanged: it belongs to the network
isolation domain, not this one.

## Terminology

* **View path** — the NT path the sandboxed process uses, in DOS style:
  `\??\C:\Users\foo\AppData\Roaming\data.txt`. Every hooked API converts whatever
  the caller passed into this form first.
* **Layer path** — the path of the same object inside a concrete layer
  (upper / lower / host). One view path maps to at most one layer path per layer.
* **Whiteout** — an empty marker file named `<name>.$APPBOX_DELETE$` placed next to
  the object it hides. It hides that object (file or directory subtree) from every
  lower layer.
* **Opaque marker** — an empty marker file named `.$APPBOX_OPAQUE$` placed inside a
  directory. Its presence makes the resolver stop looking into lower layers for that
  directory.

Both markers live in the upper layer only; the resolver never writes them itself —
they are produced by the delete and create paths of the hooks.

## Workspace isolation modes

The packer offers an isolation mode for every file and folder of the virtual
filesystem. The mode is picked in the `Isolation` column of the filesystem
workspace (see [README.md](../README.md)), stored in the project file and
written into the isolation file of the archive, which the sandbox reads back
and enforces.

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
layers of the view stay visible:

| Mode of the closest listed entry | Kind of that entry | host layer | lower layers | upper layer |
| --- | --- | --- | --- | --- |
| `Full` | folder | masked | visible | visible |
| `Full` | file | visible | visible | visible |
| `Write Copy` | folder | visible | visible | visible |
| `Whiteout` | folder or file | masked | masked | visible |

The upper layer is never masked: it holds the entries the sandboxed process
created itself, which is what makes a `Whiteout` entry visible after its
creation.

### The isolation file

The packer writes the modes of the workspace as a JSON document, which `Build`
stores in the filesystem domain of the resources of the archive as
`app/filesystem/isolation.json` — next to the layers it describes. The loader
skips the file while it enumerates the layers of that folder, because every
other child of the folder is a layer of the view:

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

The loader derives the path of the file from the resources of the archive and
passes it to the sandbox. A missing file, a missing configuration or a malformed document is not
an error: the sandbox then behaves like one without an isolation file, in which
every entry keeps the default of its kind and the host filesystem stays visible.

## On-disk layout

### Upper (writable) layer

```
data\filesystem\<DRIVE>\<relative path>
```

The drive component is the drive letter uppercased and without the colon
(`C`, not `C:`), because a colon is not a legal file name character. `..`
components are normalized and escaping the layer root is rejected.

### Lower (read-only) layers

```
app\filesystem\<layer key>\<relative path>
```

The layer root is the filesystem domain of the resources of the archive; the
view path is rebased by replacing the layer's `mapped_nt_path` prefix with its
`host_nt_path`. The comparison is case insensitive and the prefix must be
followed by a path separator, so `...\AppData\RoamingX` does not match the
`...\AppData\Roaming` mapping.

A layer key is a `#Name#` delimited token (`#ProgramFiles#`, `#USERPROFILE#`,
`#Documents#`, `#Desktop#`, a single drive letter); `#REGISTRY#` and
`#NETWORK#` are reserved for the other isolation domains. The packer offers one
layer per preset directory of its filesystem workspace and names it after the
layer key of that preset, so the loader knows exactly the keys the packer
produces: a directory which is named after any other key is rejected with
`Unknown folder`. Layers are matched longest prefix first, so nested prefixes
are matched before their parents. An archive which was packed with the former
`%Name%` form has to be packed again: the loader rejects the unknown layer
name.

### Host layer

The view path is used as-is, i.e. the object is looked up in the real
filesystem.

### Marker files

* Whiteout: `<name>.$APPBOX_DELETE$` — a sibling of the object it hides. A
  whiteout on a directory hides the whole subtree, because the marker of every
  ancestor component is checked as well.
* Opaque: `<dir>\.$APPBOX_OPAQUE$` — masks, for that directory, everything that
  only exists in the lower layers.

## Packer archives

`AppBox` produces self-contained archives which double as a base filesystem.
The layout is the fixed convention of `common/SandboxLayout.hpp`: the read-only
resources of the packaged application travel below `app`, while the state of the
sandbox does not travel at all — the loader creates the `data` directory at run
time, next to `app`, so deleting it resets the sandbox to the state the archive
carries:

```
<startup file name>                    embedded loader payload
<startup file name>.json               startup configuration
app/filesystem/isolation.json          isolation modes of the filesystem workspace
app/filesystem/<layer key>/<import>/... imported folder content
app/registry/user.hiv                  virtual registry of the workspace
app/registry/isolation.json            isolation modes of the registry
app/network/isolation.json             network configuration of the workspace
```

The loader resolves its configuration as `<own file name>.json` in its own
directory, so renaming the extracted loader program requires renaming the
configuration file as well. Running the extracted loader program shows the
imported folders at their preset locations (`#ProgramFiles#\<import>`,
`#USERPROFILE#\<import>`, `#Documents#\<import>`, `#Desktop#\<import>`) and
starts the selected startup files inside the isolation. The folders of the user
hang below `Current User Directory` in the tree of the packer, yet every preset
directory owns a layer of its own: `Documents` and `Desktop` are resolved from
their own known folder id, which keeps them correct when the shell redirects
them (for example into OneDrive).

## Key behavior rules

These rules constrain every hooked API; the implementation lives in
`sandbox/filesystem/` and `sandbox/hook/`.

* **Modifications land in the upper layer.** An object that exists only in a
  read-only layer and is opened for modification is **copied up** into the upper
  layer first; the read-only layers are never written.
* **A delete is recorded, not performed.** Deleting an object removes the upper
  copy and places a whiteout marker next to it, so lower and host layers keep
  their data while the view reports the object as gone. Deleting a directory
  requires that every layer holding it contains nothing but marker files.
* **Directory listings are merged.** The enumeration of a directory merges its
  entries across all layers which the isolation leaves visible: upper layer
  first, then every visible lower layer, then the host layer. A name which an
  upper layer already emitted hides the same name of the lower layers, and
  entries which a whiteout, an opaque marker or the isolation hides are dropped.
  A hook which runs inside a kernel call must never read more than the caller
  declared and never throw: a failure inside a hook is a failure of the
  application.
* **Process creation is redirected.** The application image is resolved through
  the view before the child process is created and the sandbox DLL is injected
  into it, so applications which only exist in a lower layer become launchable
  and every file operation of the child stays inside the view.
* **`ResolveFs` is fixed at injection time.** The layers and the isolation
  modes come from the injected configuration; there is no way to change them
  while a sandboxed process is running.

## Tests

The unit tests and the end-to-end cases of the filesystem isolation are
listed in [test/README.md](../test/README.md).

## Known gaps and limitations

The following points are visible in the current code and should be kept in mind when
extending or testing the isolation:

1. **Path-based queries are not redirected.** `NtQueryFullAttributesFile` and
   `NtQueryInformationByName` log their arguments and forward the call unchanged, so the
   query hits the raw path (a lower layer or the host filesystem) instead of the view.
   `NtQueryAttributesFile` and both directory enumeration entry points are redirected
   (see above).
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
