# Filesystem isolation

appbox isolates the filesystem of a sandboxed process by hooking the NT file APIs and
redirecting every path through a **view** that is composed of three kinds of layers:

| Layer | Role | Writable |
| --- | --- | --- |
| **upper** | Overlay filesystem. Every modification made by the sandboxed process lands here. | yes |
| **lower** | Zero or more read-only base filesystems. Each one is mapped to a virtual path prefix. | no |
| **host** | The real filesystem of the machine, used as the last layer of the view. | no |

A modification lands in the upper layer, unless the isolation mode of the path is
`Merge`: that mode applies the modification to the host layer when the host holds the
entry or when no layer holds it at all, which is what lets a packaged application change
the real filesystem on purpose (see
[Workspace isolation modes](#workspace-isolation-modes)). Every other mode keeps the
lower and the host layers untouched: "deleted" and "directory is opaque" states are
expressed with **marker files** created inside the upper layer, so the read-only layers
stay untouched and can be shared between runs. Operations that are not redirected yet are
listed in [Known gaps and limitations](#known-gaps-and-limitations).

Isolation is active only when the sandbox DLL was injected by the launcher
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
filesystem. The mode of a folder is picked in the `Isolation` column of the
filesystem workspace or through the isolation dialog of the tree, which reaches
every node of the view (see [README.md](../README.md)). The modes are stored in
the project file and written into the isolation file of the archive, which the
sandbox reads back and enforces.

| Kind | Modes | Default |
| --- | --- | --- |
| folder | `Full`, `Write Copy`, `Merge`, `Whiteout` | `Merge` |
| file | `Full`, `Whiteout` | `Full` |

* **Full** (folder) - only the virtual filesystem is visible, even when the
  host holds the folder: the host entry of the folder and of everything below
  it is masked, so a modification of the folder or of the files below it lands
  in the sandbox.
* **Write Copy** (folder) - the host filesystem and the virtual filesystem are
  both visible with the virtual one taking precedence. Every modification lands
  in the sandbox.
* **Merge** (folder) - the host filesystem and the virtual filesystem are both
  visible with the virtual one taking precedence, like `Write Copy`, yet a
  modification does not always land in the sandbox: an entry which the host
  filesystem does not hold while a sandbox layer does is modified inside the
  sandbox, and every other modification is applied to the host filesystem. The
  host folders above an entry which is written are created when they are
  missing, so an application which installs a file into the real system can do
  so; a folder which the host does not allow to be changed makes the write fail
  with the error of the host filesystem.
* **Whiteout** (folder and file) - the entry is invisible for the sandboxed
  process, even when the host or the packed content holds it: opening, reading,
  writing and deleting report `File Not Found`. Creating the entry succeeds
  inside the sandbox, and the entry is readable and writable afterwards while
  the hidden layers stay hidden.
* **Full** (file) - every write of the file lands in the sandbox, while the
  host file stays readable through the view.

`Write Copy` and `Merge` are modes of a folder: a file carries neither of them,
because both describe the merge of a folder with the host filesystem, which a
single file cannot express. A file below a `Merge` folder follows the rule of
the folder all the same.

The mode of a folder reaches the entries below it: an entry which carries no
mode of its own follows the closest folder above it which does, so the scope of
a folder mode is its own subtree and a folder below it can override it. A path
which no listed folder covers follows the **root of the view**, which is the
entry whose path is empty: the mode picked for the `Sandbox Filesystem`
container of the workspace decides the behaviour of every location outside the
recorded paths, including the locations which are not part of the virtual
filesystem at all (for example `C:\Temp`). A conflict between the virtual
filesystem and the host filesystem is resolved in favour of the virtual
filesystem.

The mode of a path and the kind of the entry which carries it decide which
layers of the view stay visible:

| Mode of the closest listed entry | Kind of that entry | host layer | lower layers | upper layer |
| --- | --- | --- | --- | --- |
| `Full` | folder | masked | visible | visible |
| `Full` | file | visible | visible | visible |
| `Write Copy` | folder | visible | visible | visible |
| `Merge` | folder | visible | visible | visible |
| `Whiteout` | folder or file | masked | masked | visible |

The upper layer is never masked: it holds the entries the sandboxed process
created itself, which is what makes a `Whiteout` entry visible after its
creation.

The layers which stay visible decide what the sandboxed process reads; the mode
decides where its modifications land. Only `Merge` lets them reach the host
filesystem, and it does so by the rule of the mode: the entry of the host
filesystem is modified when the host holds it or when no layer holds it at all,
while an entry only a sandbox layer holds is modified in the upper layer. A
delete follows the same rule, so an entry the host holds is really removed from
the host filesystem while an entry only the sandbox holds is recorded as
deleted inside the sandbox.

### The isolation file

The packer writes the modes of the workspace as a JSON document, which `Build`
stores in the filesystem domain of the resources of the archive as
`app/filesystem/isolation.json` — next to the layers it describes. The launcher
skips the file while it enumerates the layers of that folder, because every
other child of the folder is a layer of the view:

```json
{
  "version": 1,
  "entries": [
    { "path": "", "kind": "directory", "isolation": "write_copy" },
    { "path": "#ProgramFiles#\\MyApp", "kind": "directory", "isolation": "full" },
    { "path": "#ProgramFiles#\\MyApp\\app.exe", "kind": "file", "isolation": "whiteout" }
  ]
}
```

The `path` of an entry is a path of the virtual filesystem, which is the path
the `Source Path` column shows: the first component is the layer key of a preset
directory and the remaining ones are the path below it. The document lists the
modes the workspace holds; an entry which it does not mention follows the
closest listed folder above it, then the root of the view, and falls back to the
default of the view.

An entry whose `path` is **empty** is the root of the view: it is a folder and
it decides the mode of every path no other entry covers, including the locations
which are not part of the virtual filesystem at all. A document which lists no
such entry leaves the behaviour outside the recorded paths to the default of the
view, which is the state of a workspace whose container was never given a mode.

The document is read and written as the structure of the schema:
`common/FilesystemIsolation.hpp` describes an entry and the document, and both
sides convert it with `to_json()` and `from_json()` instead of reading or
writing the members of a JSON object. An entry whose members are incomplete,
which are of another type, or which names a mode its kind cannot hold is
therefore refused while the file is read, which is what keeps the packer and the
sandbox in step.

The launcher derives the path of the file from the resources of the archive and
passes it to the sandbox. A missing file, a missing configuration or a malformed document is not
an error: the sandbox then behaves like one without an isolation file, in which
every entry follows the default of the view, which is `Merge`: the host
filesystem stays visible and a modification is applied to it when the host holds
the entry or when no layer holds it at all.

A run has one isolation file per layer: the file of the resources of the archive
comes first and the file of every patch package follows in the order the
packages take effect in (see [Patch Layers](PatchLayer.md)). The sandbox reads
them in that order and applies every document on top of the modes it already
holds, so the mode of a path a later file names is the mode the sandboxed
process observes while a path no later file names keeps the mode of the layer
below it. A file which cannot be used is logged and skipped, which is what keeps
a broken package from failing the run.

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

A layer key is a `#Name#` delimited token (`#ProgramFiles#`,
`#ProgramFilesCommon#`, `#USERPROFILE#`, `#Documents#`, `#Desktop#`, `#AppData#`,
`#LocalAppData#`, `#LocalAppDataLow#`, `#Downloads#`, `#Favorites#`,
`#StartMenu#`, `#Programs#`, `#Startup#`, `#ProgramData#`, `#Windows#`,
`#System32#`, `#Fonts#`, a single drive letter);
`#REGISTRY#` and `#NETWORK#` are reserved for the other isolation domains. The packer offers one
layer per preset directory of its filesystem workspace and names it after the
layer key of that preset, so the launcher knows exactly the keys the packer
produces: a directory which is named after any other key is rejected with
`Unknown folder`. A key is matched exactly as the table spells it, both by that
lookup and by the expansion of the path of a startup file, so a differently
spelled key names no layer and no folder. Layers are matched longest prefix
first, so nested prefixes are matched before their parents. An archive which was
packed with the former `%Name%` form has to be packed again: the launcher
rejects the unknown layer name.

A layer key can be held by several layers of one run, because a patch package
carries layers of its own: the launcher mounts the layers of the packages before
the layers of `app`, the last package of the ascending name order first, and the
resolution prefers the layer which is mounted first. A file which exists in a
package and in `app` is therefore the file of the package, and a file only `app`
holds stays readable.

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
sandbox does not travel at all — the launcher creates the `data` directory at run
time, next to `app`, so deleting it resets the sandbox to the state the archive
carries:

```
<startup file name>                    embedded launcher payload
<startup file name>.json               startup configuration
app/filesystem/isolation.json          isolation modes of the filesystem workspace
app/filesystem/<layer key>/<import>/... imported folder content
app/registry/user.hiv                  virtual registry of the workspace
app/registry/isolation.json            isolation modes of the registry
app/network/isolation.json             network configuration of the workspace
```

A patch package carries the same resource tree rooted at the archive root and is
applied on top of the archive: the layers and the isolation modes of a package
override the resources it carries and keep the resources it does not carry (see
[Patch Layers](PatchLayer.md)).

The launcher resolves its configuration as `<own file name>.json` in its own
directory, so renaming the extracted launcher program requires renaming the
configuration file as well. Running the extracted launcher program shows the
imported folders at their preset locations (`#ProgramFiles#\<import>`,
`#ProgramFilesCommon#\<import>`, `#USERPROFILE#\<import>`,
`#Documents#\<import>`, `#Desktop#\<import>`, `#AppData#\<import>`,
`#LocalAppData#\<import>`, `#LocalAppDataLow#\<import>`, `#Downloads#\<import>`,
`#Favorites#\<import>`, `#StartMenu#\<import>`, `#Programs#\<import>`,
`#Startup#\<import>`, `#ProgramData#\<import>`, `#Windows#\<import>`,
`#System32#\<import>`, `#Fonts#\<import>`) and starts the selected startup files
inside the isolation. The folders of the user hang below `Current User
Directory` in the tree of the packer, `Programs` hangs below `Start Menu` and
`Startup` below `Programs`, while the system directory hangs below `Windows` and
`Common` below `Program Files`; `Program Data` is a top level preset next to
`Program Files`, `Current User Directory` and `Windows`. Yet every preset
directory owns a layer of its own: `Documents` and `Desktop` are resolved from
their own known folder id, which keeps them correct when the shell redirects
them (for example into OneDrive), `Local Application Data Low` is resolved from
its own id instead of being a subdirectory of `Local Application Data`,
`Programs` and `Startup` are resolved from their own ids instead of being
subdirectories of the `Start Menu` layer, and `System32` is resolved from its own
id instead of being a subdirectory of the `Windows` layer. The `Fonts` folder of
the same section is the layer of the fonts domain, whose fonts are loaded into
the font table of a sandboxed process: see
[Fonts Isolation](FontsIsolation.md).

## Key behavior rules

These rules constrain every hooked API; the implementation lives in
`sandbox/filesystem/` and `sandbox/hook/`.

* **Modifications land in the upper layer.** An object that exists only in a
  read-only layer and is opened for modification is **copied up** into the upper
  layer first; the read-only layers are never written. A path whose isolation is
  `Merge` is the exception: its modification is applied to the host filesystem
  when the host holds the entry or when no layer holds it, and the folders above
  the entry are created in that layer first.
* **A delete is recorded, not performed.** Deleting an object removes the upper
  copy and places a whiteout marker next to it, so lower and host layers keep
  their data while the view reports the object as gone. Deleting a directory
  requires that every layer holding it contains nothing but marker files. A
  delete of a `Merge` path removes the entry of the host filesystem as well,
  which is the layer the mode names, and writes the marker only while a layer
  below the upper one still holds the entry.
* **A delete on close is recorded when the handle is closed.** A handle which
  the caller may mark for deletion is recorded by the hook which opens it,
  whatever entry point that is, so the close which removes the object the
  handle denotes records the delete as well. The layer object is gone by the
  time the close runs, so the record names the layers as they were once the
  open succeeded, and those layers decide whether a whiteout marker has to
  hide the ones which still hold the name.
* **Directory listings are merged.** The enumeration of a directory merges its
  entries across all layers which the isolation leaves visible: upper layer
  first, then every visible lower layer, then the host layer. A name which an
  upper layer already emitted hides the same name of the lower layers, and
  entries which a whiteout, an opaque marker or the isolation hides are dropped.
  A hook which runs inside a kernel call must never read more than the caller
  declared and never throw: a failure inside a hook is a failure of the
  application.
* **Name based queries are redirected.** `NtQueryAttributesFile`,
  `NtQueryFullAttributesFile` and `NtQueryInformationByName` resolve the name
  they were given through the view and query the first layer which the
  isolation leaves visible, so a file only a lower layer holds is found and an
  entry which a whiteout, an opaque marker or the isolation hides reports
  `File Not Found`. The three entry points share the lookup of
  `sandbox/filesystem/QueryPath.*`, and a name which cannot be expressed as a
  view path is forwarded unchanged.
* **Handle based calls are redirected where they carry a name.** A rename and a
  link carry the name the object is moved to or linked at, so
  `NtSetInformationFile` resolves that name through the view and forwards the
  call against the path of the layer the isolation names, which is the layer a
  create of the same entry would land in: the entry of the host filesystem is
  renamed when the mode is `Merge` and the host holds the name or no layer holds
  it, and the overlay is used otherwise. The object the handle denotes is copied
  up first when only a read-only layer holds it and it is a file (a directory is
  the exception, see [Known gaps and limitations](#known-gaps-and-limitations)),
  and a rename records the delete of the name it moved away from with a whiteout
  marker, so the layers below the overlay stop showing it. `NtQueryInformationFile`
  answers the classes which report a name (`FileNameInformation`,
  `FileNormalizedNameInformation` and `FileAllInformation`) with the path of the
  view instead of the path of the layer the handle was opened in, so the layout
  of the sandbox never reaches the application. The file system reports the name
  of an object relative to the root of the volume of its handle, and the hook
  keeps that shape: the answer carries the path of the view without its drive,
  which is what a process of the host would receive for the same object. Every
  other class of the two entry points acts on the handle alone, which already
  denotes the layer the view selected, so it is forwarded unchanged.
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

1. **Directory merging covers the name carrying information classes only.** The merge
   reads and rewrites the entry list, so it understands `FileDirectoryInformation`,
   `FileFullDirectoryInformation` and `FileBothDirectoryInformation`. A caller which uses
   one of the `FileId...DirectoryInformation` classes, an information class which does
   not carry a name, or a directory handle which was not registered by `NtOpenFile`
   (a handle of `CreateFileW`, for example) sees the single layer the handle was opened
   with.
2. **A mode does not reach the alternate data streams of its file.** The lookup of the
   isolation walks the path upwards component by component, and a stream name such as
   `file.txt:stream` is the last component of its own path, so it does not inherit the
   mode of `file.txt`. The mode of a folder still covers the streams of the files below
   it.
3. **Copy-up copies the default data stream only.** Alternate data streams are not
   handled specially: a stream name such as `file.txt:stream` is carried into the upper
   layer path as part of the file name, while copy-up reads only the file content, and
   whiteout / opaque markers are not stream aware.
4. **Non-local paths bypass isolation.** UNC paths, named pipes, mailslots, network
   volumes and drive-relative paths cannot be converted to a view path, so the hooks
   forward them unchanged. A rename or a link whose name is such a path is forwarded as
   well.
5. **`ResolveFs` is fixed at injection time.** There is no way to add or remove a lower
   layer while a sandboxed process is running; the layers come from the injected
   configuration and live in the `appbox::sandbox` singleton. The isolation modes are
   loaded once as well, so a mode which is changed in the packer afterwards needs a new
   archive.
6. **A root mode which hides the host filesystem stops the process.** The mode of the
   root of the view covers every path no other entry names, so `Full` or `Whiteout` at
   the root hides the whole host filesystem: the sandboxed process can no longer load
   the modules of the host and fails to start, which is the behaviour the mode asks for
   but which no end-to-end case can drive. A root mode of `Write Copy` or `Merge` is the
   one a workspace normally uses.
7. **A rename of a directory which only a read-only layer holds fails.** The open of the
   directory with the access a rename needs is a modification, and the copy-up such a
   call asks for copies a file: a directory stays in the layer which holds it, so the
   open of the overlay entry fails and the caller is told that the entry is missing.
   Neither layer changes, so the read-only layers stay untouched. A directory the
   overlay or the host filesystem holds is renamed as usual.
8. **A delete on close of a handle the sandbox did not open is not recorded.** The
   delete of such a handle is recorded while it is closed, and the sandbox only knows
   the layers of a handle it opened itself: a handle which was inherited or duplicated
   from another process removes its layer object without hiding the layers below it.
