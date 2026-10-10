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
* **Alternate data stream** — the content a file carries beside its default data
  stream, addressed by a colon after the name of the file:
  `\??\C:\dir\file.txt:stream` names the stream `stream` of the file `file.txt`.
  A stream belongs to that file: the entry the view decides the mode, the
  visibility and the layer of a modification from is the entry of the file which
  carries the stream.
* **Whiteout** — an empty marker file named `<name>.$APPBOX_DELETE$` placed next to
  the object it hides. It hides that object (file or directory subtree) from every
  lower layer.
* **Opaque marker** — an empty marker file named `.$APPBOX_OPAQUE$` placed inside a
  directory. Its presence makes the resolver stop looking into lower layers for that
  directory.

Both markers are written into the upper layer only; the resolver never writes them
itself — they are produced by the delete and create paths of the hooks. A marker
which any layer of the view holds is honoured, which is what lets an archive carry
the delete of an entry. The names of the markers are reserved: no entry of the view
carries one, see [Key behavior rules](#key-behavior-rules).

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
  ancestor component is checked as well. A whiteout of a file hides the streams
  the file carries as well, because the marker of the file is checked for a path
  which names one of them.
* Whiteout of a stream: `<name>:<stream>.$APPBOX_DELETE$` — a stream of the file
  itself, because a file name cannot carry a colon. The file therefore has to be
  in the upper layer before the marker is written, which is what makes the file
  travel into the overlay together with the stream it carries.
* Opaque: `<dir>\.$APPBOX_OPAQUE$` — masks, for that directory, everything that
  only exists in the lower layers.

The three shapes are names of the view and not of the entries it holds, so they are
reserved: a sandboxed process can neither create, open, query, delete nor rename an
entry which carries one, see [Key behavior rules](#key-behavior-rules).

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
* **An alternate data stream belongs to the file which carries it.** The path of
  a stream (`file.txt:stream`) is decided by the entry of `file.txt`, so the mode
  of a file covers the streams of that file, a whiteout of the file hides them,
  and a modification of a stream is a modification of the file. The file is
  therefore copied into the upper layer together with the stream it carries
  before the stream is written there: a stream cannot exist without its file, and
  the file the file system creates on its own for a stream the upper layer does
  not hold would carry nothing but that stream and shadow the content the view
  reports for the file. The mode of a folder keeps covering the streams of the
  files below it.
* **A reparse point is resolved by the view.** A path is mapped to the layers by
  text, so a junction, a symbolic link or a volume mount point below a view path
  would be followed by the file system of the layer which holds it: the object
  the caller reaches would be the object of the path the reparse point names,
  while the isolation decides the visible layers and the layer a modification
  lands in from the path the caller spelled. The view therefore resolves the
  reparse points of a path itself. It reads the data of the link out of the
  layer which holds it, turns the target into a path of the view — a target which
  the link declares as relative is resolved against the directory of the link —
  and resolves that path again, so the isolation of the **target** decides the
  answer, whether the target is an entry of the host filesystem, of a lower layer
  or of the upper layer. The tags which redirect the namespace are
  `IO_REPARSE_TAG_MOUNT_POINT` and `IO_REPARSE_TAG_SYMLINK`; every other tag
  describes the entry itself and is left to the layer, so an entry which carries
  one is reported and modified like the layer reports it. A target which cannot
  be expressed as a path of the view (a volume no drive letter maps, a device
  path, a network path), data which cannot be read and a chain of links which is
  deeper than the limit fail the call with `STATUS_REPARSE_POINT_NOT_RESOLVED`
  instead of being forwarded, because the file system of the layer would follow a
  link the view could not resolve and reach an object the view never decided
  about. The **final** component of a path is only followed by a call which asks
  for the object the link names: `NtCreateFile` and `NtOpenFile` follow it unless
  the call carries `FILE_OPEN_REPARSE_POINT`, the creation of a process follows
  the image path, and a delete, the target name of a rename or a link, an
  attribute query and every call which asks for the reparse point itself reach
  the link. The components above the entry are resolved by the view for every
  call.
* **The data of a reparse point names a path of the view.** `NtFsControlFile`
  translates `FSCTL_SET_REPARSE_POINT`, so the target of a link which redirects
  the namespace is turned into a path of the view before it is stored: a caller
  which links an object it holds writes the path it knows, and a target which
  names a path of a layer is stored as the path of the view that layer maps to.
  The data the view reports is the data it stored, so reading a reparse point
  back never shows the layout of the sandbox, and the target the view reads is
  resolved exactly like the target of a link of the host. `FSCTL_GET_REPARSE_POINT`
  and `FSCTL_DELETE_REPARSE_POINT` act on that data and are forwarded unchanged,
  and a control code which carries no reparse point reaches the object of the
  layer with the buffers of the caller.
* **A delete is recorded, not performed.** Deleting an object removes the upper
  copy and places a whiteout marker next to it, so lower and host layers keep
  their data while the view reports the object as gone. Deleting a directory
  requires that every layer holding it contains nothing but marker files. A
  delete of a `Merge` path removes the entry of the host filesystem as well,
  which is the layer the mode names, and writes the marker only while a layer
  below the upper one still holds the entry.
* **The names of the markers are reserved.** A whiteout marker is named after the
  entry it hides and an opaque marker carries a name of its own, so both shapes name
  the view rather than an entry it holds: no entry of the view carries a reserved
  name, and no entry hangs below a component which carries one. A call which carries
  such a name is answered from that rule, which the placement of the name inside the
  path decides: a call which may create an entry reports `Object Name Invalid`, a
  call which looks an entry up reports `File Not Found`, and a name below a reserved
  component reports `Path Not Found`. The comparison ignores the case, because the
  volumes of the layers do, and a component which names an alternate data stream
  carries a reserved name when the name of the file or the name of the stream is
  reserved. The hooks of the view write the markers themselves with the entry points
  of the system, so the reservation never blocks the view: a sandboxed process can
  neither forge a marker, nor remove one to unhide an entry, nor see the markers in a
  listing, and a class which enumerates the streams of a file
  (`FileStreamInformation`) reports the streams of the file without the markers of
  the view.
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
  The merge covers every information class which reports the name of an entry
  and every handle which denotes a directory of the view, whatever call opened
  it. The layers of a handle the sandbox opened itself come from the record of
  that open, and a handle the sandbox did not open, which is the handle a
  process inherited or duplicated from another process, is adopted: the path of
  the view the handle denotes is looked up in the file system and translated
  back into the path of the view, which is what makes an enumeration through
  such a handle report the view as well. The object the handle denotes decides
  the answer: a handle which denotes no directory of a disk file system, or an
  object another isolation domain owns, is forwarded unchanged, like a path
  which cannot be expressed as a view path, and the file system reports its own
  failure; a directory the view does not hold reports `File Not Found`, and a
  local object the view cannot name at all is refused with
  `STATUS_NOT_SUPPORTED`, because the answer of the single layer the handle was
  opened with would show the entries the view hides and the markers of the view
  themselves. A directory which was removed while its handle stayed open is one
  of the objects the view cannot name, which is why a listing through such a
  handle is refused as well: the file system reports it below its own metadata
  directory (`\??\C:\$Extend\$Deleted\...`), which no layer of the view holds.
  No answer of the view shows an entry of the single layer the handle was opened
  with. A class which reports no name of an entry is refused as well (see the
  rule below).
  `NtQueryDirectoryFile` and `NtQueryDirectoryFileEx` share the merge, so
  both entry points answer the same view. A hook which runs inside a kernel call
  must never read more than the caller declared and never throw: a failure
  inside a hook is a failure of the application.
* **A class which reports no name of an entry is refused.** The merge reads and
  rewrites the entry list, so it understands every class which reports the name
  of an entry: `FileDirectoryInformation`, `FileFullDirectoryInformation`,
  `FileBothDirectoryInformation`, `FileNamesInformation`,
  `FileIdBothDirectoryInformation`, `FileIdFullDirectoryInformation`,
  `FileIdExtdDirectoryInformation`, `FileIdExtdBothDirectoryInformation` and
  `FileIdGlobalTxDirectoryInformation`. A class which carries no name cannot be
  merged, because the view cannot decide which layer holds an entry it cannot
  name. The classes of that kind report the identity of a storage object of the
  volume of one layer rather than an entry of a directory — the report of an
  object identity (`FileObjectIdInformation`) and the report of a reparse point
  (`FileReparsePointInformation`) — and an entry of the view has no such
  identity: the view is the composition of its layers, so an answer which names
  one of them would be the answer of the single layer the handle was opened
  with, which the view never gives. The view therefore refuses such a class with
  `STATUS_NOT_SUPPORTED` instead of forwarding it. The file system refuses both
  classes for a directory of a volume as well, with `STATUS_INVALID_INFO_CLASS`
  (the report of a reparse point is only valid for the metadata stream
  `\$Extend\$Reparse:$R:$INDEX_ALLOCATION` of an NTFS or ReFS volume, whose
  records name the reparse points of the volume rather than the entries of a
  directory), so an application which asks for one of them through a directory of
  the view receives a refusal either way. A handle which denotes no directory of
  the view is not part of the view, so a query through it is forwarded unchanged
  (see the merge rule above).
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
  marker, so the layers below the overlay stop showing it. The path of the view
  a handle denotes is looked up in the file system when the sandbox did not open
  the handle itself, which is the same lookup the merge of a directory listing
  uses (see above), so a rename or a link of an object the application inherited
  reaches the view as well. `NtQueryInformationFile`
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
extending or testing the isolation. They fall into two groups. The first group is about
the semantics the view cannot express: the hook answers, but its answer is the one of a
single layer rather than the one of the composed view. The second group is about the
entry points the isolation does not hook at all: a call which reaches the file system
through them acts on the layer the object of the call belongs to.

### The semantics the view cannot express

1. **Copy-up carries the default data stream of a file and nothing else.** The helper
   which copies an entry into the overlay (`sandbox/utils/CopyFileNt.cpp`) reads the
   content of the source and writes it into a file it creates, so the copy carries
   neither the attributes nor the metadata of the source: alternate data streams,
   extended attributes, the security descriptor and the ACL, the timestamps, the
   compression and the sparse state, the short name and the identity of the file are not
   carried over, and the hard links of the source are not re-created at the copy. What
   the copy holds is the content of the file and the metadata the volume of the overlay
   assigns to a new file. The streams a copy does not carry stay readable through the
   layers below the overlay, so a file whose default data stream was copied into the
   overlay still reports the streams of the layer it was copied from, while a class
   which enumerates the streams of a file (`FileStreamInformation`) reports the streams
   of the layer the handle was opened in. The one property of the source which the copy
   does carry is the reparse point of the entry: a source which is a link is not copied
   by the content it reaches, because the copy would shadow the link with a different
   entry. The copy creates the object and writes the data of the link into it, and a
   link whose data cannot be carried fails the copy.
2. **A reparse point which the view cannot express fails the call.** The view resolves
   the tags which redirect the namespace itself (see the rules above). A target which
   cannot be expressed as a path of the view — a volume no drive letter maps, a device
   path, a network path — the data of a link which cannot be read, and a chain of links
   which is deeper than the limit fail the call with `STATUS_REPARSE_POINT_NOT_RESOLVED`,
   because the file system of the layer would follow a link the view could not resolve
   and reach an object the view never decided about. A caller which asks for such an
   entry therefore receives the failure of the view instead of the answer of the layer.
   The tags which describe the entry itself (a cloud placeholder, a layer of a container
   image, an application execution alias) are left to the layer, so their data is
   neither resolved nor translated by the view.
3. **The identity of an object is the identity of the layer.** The hook of
   `NtQueryInformationFile` answers the classes which report the name of an object with
   the path of the view, while every other class reports a property of the object of the
   layer the handle was opened in. The identity of an entry is therefore the one the
   volume of its layer assigns to it: `FileInternalInformation`, `FileIdInformation`,
   `FileStatInformation`, `FileStatLxInformation`, `FileStatBasicInformation`, the
   internal record of `FileAllInformation`, `FileHardLinkInformation` and
   `FileHardLinkFullIdInformation`, and the identity a directory entry carries, all
   report the object of the layer, and the names a class of that family reports are the
   names of the layer as well. An entry which was copied up is a different object than
   the entry of the host filesystem it shadows, so an application which compares
   identities observes a change the host filesystem never made. A call which opens an
   object by its file id is resolved through the path the id names rather than through
   the id itself, so the id of an entry of the host filesystem reaches the copy of the
   overlay once the overlay holds one.
4. **A short name is the short name of the layer.** The short name (the 8.3 form) of a
   directory entry and the answer of `FileAlternateNameInformation` are the ones of the
   object of the layer, and the resolver matches the components of a path against the
   volume of a layer, so whether a short name resolves is decided by the volume which
   holds the entry. An entry the overlay shadows may therefore not be found by the short
   name the host volume gave it, and a lookup which mixes the two forms of a name reads
   the entry of the host filesystem instead of the copy of the overlay. A caller which
   sets a short name is not redirected either, because `FileShortNameInformation` is not
   a class which carries a path (see the rule of `NtSetInformationFile` above).
5. **A directory which only a read-only layer holds cannot be copied up.** A
   modification of an entry is applied to the overlay, and the entry is copied into it
   first when only a read-only layer holds it. The copy-up copies a file, so a directory
   stays in the layer which holds it: the hook opens the entry of the overlay after the
   copy-up, which no layer holds, and reports `File Not Found` while neither layer
   changed. A rename of such a directory fails for that reason, and so does every other
   modification which reaches the directory through a handle: a delete of the folder
   through a handle (`RemoveDirectoryW`), a change of its attributes and a change of its
   ACL. A delete which names the path of the folder works, because `NtDeleteFile` does
   not open the entry for modification: it records the delete with a marker and the
   read-only layer keeps its content. A hard link fails for a neighbouring reason: the
   name of a link lands in the layer of its destination while the object it links stays
   in the layer of the handle, and a link cannot cross the boundary of a file system,
   which is the boundary between two layers of the view.
6. **A root mode which hides the host filesystem stops the process.** The mode of the
   root of the view covers every path no other entry names, so `Full` or `Whiteout` at
   the root hides the whole host filesystem: the sandboxed process can no longer load
   the modules of the host and fails to start, which is the behaviour the mode asks for
   but which no end-to-end case can drive. A root mode of `Write Copy` or `Merge` is the
   one a workspace normally uses.
7. **A delete on close of a handle the sandbox did not open is not recorded.** The
    delete of such a handle is recorded while it is closed, and the sandbox only knows
    the layers of a handle it opened itself: a handle which was inherited or duplicated
    from another process removes its layer object without hiding the layers below it.

### The entry points the isolation does not hook

The hooks of the view are the ones `sandbox/hook/` installs, so an entry point which is
not listed there reaches the file system with the path or the handle of the caller, and
the answer of such a call is the answer of the layer the object belongs to. The points
below are the ones which matter for the view.

1. **A path which cannot be expressed as a view path is forwarded unchanged.** UNC
    paths, named pipes, mailslots, network volumes and drive-relative paths cannot be
    converted to a view path, so the hooks forward them, and a rename or a link whose
    name is such a path is forwarded as well. The creation of a named pipe and of a
    mailslot is not redirected either, because `NtCreateNamedPipeFile` and
    `NtCreateMailslotFile` are not hooked: the pipe and the mailslot namespace is the one
    of the machine, so a sandboxed process creates an object every process of the host
    can open, and it can open an object another process created.
2. **The volume of an object is the volume of its layer.** `NtQueryVolumeInformationFile`
    is not hooked, so the volume a handle reports is the one which holds the object of
    the layer the handle was opened in: the serial number and the label of the volume
    (`FileFsVolumeInformation`), its size and its free space (`FileFsSizeInformation`,
    `FileFsFullSizeInformation`), its attributes (`FileFsAttributeInformation`) and the
    record of the volume which `FileAllInformation` carries at its end. A file the
    overlay holds therefore reports the volume of the sandbox, which is the volume the
    `data` directory of the run lives on, while the drive letter of its path of the view
    may name another volume: a caller which asks the free space of a folder it wrote into
    receives the free space of the volume of the sandbox.
3. **The control codes are translated for the reparse points and for nothing else.**
    `NtFsControlFile` is hooked for `FSCTL_SET_REPARSE_POINT`, whose target is turned
    into a path of the view before it is stored (see the rules above); every other
    control code reaches the object of the layer with the buffers of the caller. The
    codes which act on the object itself are the ones which matter:
    `FSCTL_GET_OBJECT_ID` and `FSCTL_SET_OBJECT_ID` report and change the identity of the
    object of the layer. The codes which name the volume or the storage below the object
    — `FSCTL_GET_NTFS_VOLUME_DATA`, `FSCTL_QUERY_USN_JOURNAL`, `FSCTL_ENUM_USN_DATA`,
    `FSCTL_READ_USN_JOURNAL`, `FSCTL_GET_NTFS_FILE_RECORD`,
    `FSCTL_GET_RETRIEVAL_POINTERS`, `FSCTL_MOVE_FILE`, `FSCTL_SET_ZERO_DATA` and
    `FSCTL_DUPLICATE_EXTENTS_TO_FILE` — report and change the volume of the layer as
    well.
4. **`NtQueryObject` names a file handle after its layer.** The hook translates the name
    of an object the hive of the registry isolation mounts and leaves every other name as
    it is, so `ObjectNameInformation` of a file handle reports the path of the layer the
    handle was opened in, which is the path of the sandbox. An application which verifies
    that a handle it holds names the entry it asked for reads that path, and the layout of
    the sandbox reaches it.
5. **The data of a read and of a write is never inspected.** `NtReadFile` and
    `NtWriteFile` are attached for the trace only: their hook record carries no hook
    function, because the layer a read and a write act on is the one the handle was
    opened in, which the open already decided. A handle the sandbox did not open is
    served by the layer of that handle as well, so the isolation never sees the bytes of
    a transfer.
