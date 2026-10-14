# Snapshot Capture

The capture of `AppBox.exe` describes an application by the change it made to a
machine instead of by the files the user picks: a snapshot of the state before
the installation is kept, the installation runs, a second snapshot is taken and
the difference of the two is written into the workspaces of the packer.

The feature is the second way to fill a session besides the commands which
import a folder or a `.reg` file by hand, and it is what makes a session which
describes an installed application reproducible.

## Commands

| Command | Where | What it does |
| --- | --- | --- |
| `Capture Before` | Toolbar | Asks for a snapshot file, reads the state of the machine and writes it into that file as the state before the change, which is the baseline of the session. |
| `Capture and Diff` | Toolbar | Reads the state of the machine again, writes it into the same file as the state after the change, compares the two states and writes the difference into the `Filesystem`, the `Registry` and the `Environment` workspace. |
| `Save Snapshot As...` | `File` menu | Writes the state before the change of the file the session uses into another snapshot file. |
| `Load Snapshot...` | `File` menu | Records the path of a snapshot file and uses its state before the change as the baseline, so a comparison can be made against a state which an earlier run captured. |

The baseline is a file and not a state in memory: `Capture Before` asks for the
file before it reads anything, every entry a run reads travels into the file
right away, and `Capture and Diff` opens that file again. The memory of a run
is the size of one entry and of the page cache of the database instead of the
size of the machine.

A comparison needs a baseline: the command reports that one has to be taken
first when the session holds none. A run reports its progress in a dialog which
can be cancelled and shows the part of the machine which is being read, the
number of the entries it recorded and the elapsed time, and the result of a
comparison is presented as a summary.

The comparison does not become the baseline of the next one: the file holds the
state the machine was in before the change and the state it is in now, and the
first of the two is a state the machine has already left behind. The user takes
a new `Capture Before` before comparing again; `Load Snapshot...` is the way to
compare against a state an earlier run captured, and it replaces the state
after the change of that file with the state of the new run.

The path of the snapshot file belongs to the session only: it is not part of
the project file of `File -> Export Configuration...`, because it names a file
of the machine the packer runs on and not a configuration.

## Scope of a capture

| Part | What is read |
| --- | --- |
| Filesystem | Every preset directory of the filesystem workspace (see `src/core/PresetDirectory.cpp`), recursively. |
| Registry | The five root keys of the view, recursively. |
| Environment | The persisted environment of the machine: `HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Environment` and `HKCU\Environment`, with the references of a value resolved. |

The environment is read from the registry and not from the environment block of
the packer, because a capture of the state before an installation and a capture
of the state after it are usually taken by one run of the packer, whose own
environment block never changes in between: reading the block would report no
new variable at all.

A capture reads the metadata of an entry and never its content: the filesystem
part records the path, the kind, the size and the last write time of an entry.
The content of a folder travels into the archive while the session is packed,
which reads the host folder of every import again.

A folder or a registry key which cannot be read is skipped and counted instead
of failing the run, so a capture of a whole machine reports the parts it could
not read. Only a root which does not name a preset directory, a folder which is
not part of it and a name which is not a root key of the view are refused.

### The snapshot file of the run itself

The file a capture writes is left out of the state the run reads, so a snapshot
never describes itself: the file grows while the walk runs, and a capture which
recorded it would report the snapshot as an entry the change added.

Leaving it out of the state is enough to keep it out of the archive. A folder
travels into the archive only when the change created it, and the folder which
holds the snapshot file exists when the baseline is captured, so it is part of
the state before the change and is never the folder a difference imports. The
file of a run in flight is stored where the user picked it before the run
started, so it cannot be inside a folder the run is about to report as new.

A capture of a whole machine reads a few hundred thousand entries and takes
seconds to minutes; the snapshot of such a run is a file of a few hundred
megabytes.

## Snapshot file

A snapshot is a SQLite database written by `src/core/SnapshotStore.*`. The file
holds two states of the machine, the one before a change and the one after it,
which are the phases of the file. Every path is stored relative to its preset
directory (`C:\Program Files\Foo` is the entry `(program_files, "Foo")`), so a
snapshot does not depend on the machine which took it and a difference of two
snapshots can be applied to the models without matching host paths again.

```
meta             key, value            the schema version and the marker of a phase
filesystem       phase, lookup, preset, path, directory, size, write_time
registry_keys    phase, lookup, path
registry_values  phase, lookup, path, name, type, data
environment      phase, lookup, name, value
```

`phase` is `0` for the state before the change and `1` for the state after it,
and `lookup` is the folded identifier of the entry (see
`src/core/SnapshotModel.hpp`). The identifier is the primary key of the row
together with the phase, so the comparison looks an entry up without walking a
state and the entries of a phase are read in a stable order.

Every text is stored as UTF-16, so a path of the host survives the round trip
without a conversion, and the data of a registry value is stored as the blob it
is. A value which holds no byte is stored as a blob of length zero, which is
what tells it apart from a value which the snapshot does not hold at all.

A phase is written inside one transaction and is finished by a marker in the
`meta` table. The marker is what tells a state which was read as a whole from
the rows of a run which was cancelled: a comparison reads a phase only when it
carries a marker, and the next run which writes that phase clears the rows
which were left behind. The journal of the database is turned off, which is the
trade a snapshot makes to keep the write of a whole machine in the range of
seconds; that mode leaves the database without a usable rollback, which is why
a phase which is cancelled is abandoned instead of being rolled back, and why
the marker is written before the transaction is committed.

A file whose write fails is removed again, so a failed baseline leaves nothing
behind, and a file whose schema version is not the one of this build is refused
instead of being interpreted.

## The difference

`src/core/SnapshotDiff.*` compares the two phases of a snapshot file and
reports three lists per part of the machine: the entries which only the newer
state holds, the entries which both states hold with another content and the
entries which only the older state holds.

The comparison reads one entry of a state at a time and looks the matching
entry up in the other state, so neither state is held in memory: the memory of
a comparison is the difference it reports.

The names of the filesystem, of the registry and of the environment of Windows
are compared without regard to the case, so `Foo\Bar.exe` and `foo\bar.exe`
name the same entry. A file counts as modified when its size or its last write
time differs; a directory carries neither, so a directory is only ever added or
removed. A registry value counts as modified when its type or one byte of its
data differs, and an environment variable when its value differs.

The comparison is a pure function of the file: it never touches the host and it
does not depend on the order of the entries of a state.

## What the difference does to the workspaces

`src/core/SnapshotApply.*` writes the difference into the pack model, the
registry model and the environment model:

| Difference | Workspace | Effect |
| --- | --- | --- |
| A folder which is new below a preset directory | Filesystem | The folder is imported, so it and everything below it travel into the archive. |
| An entry the newer snapshot does not hold | Filesystem | The entry is hidden: the archive carries the whiteout marker of the entry, which hides the entry of the host while the packaged application runs. |
| A registry key which is new | Registry | The key is created with the mode a new key starts with, which is `Merge`. |
| A registry value which is new or whose data changed | Registry | The value is written with the type and the bytes of the newer snapshot; a value which is created now starts as `Full`. |
| A registry key or value which is gone | Registry | The entry is written with the `Hide` mode, which keeps it invisible for the packaged application. |
| An environment variable which is new or whose value changed | Environment | The variable is written with the modes a variable of the workspace starts with: `Write Copy` and `Replace`. |

The mode of an entry is the mode the workspace gives it, so the modes can be
changed by hand after a capture, exactly like the modes of an entry which was
imported by hand.

### Entries which cannot be expressed

The report of a run names every entry it had to leave out:

1. **A file which was added directly below a preset directory.** The archive
   carries whole folders, and a single file is only ever imported into a folder
   the archive holds, so an entry of the layer root cannot be imported on its
   own.
2. **An entry which was added to a folder that existed before the capture.**
   The folder is not new, so it is not imported, and an entry below it has no
   import which could carry it. The report names the folder and the number of
   the entries it holds, which keeps a capture of an installation into a folder
   that was there before from reporting thousands of single entries.
3. **A registry value whose type the workspace cannot store.** The model
   carries the types the registry editor offers (`REG_SZ`, `REG_EXPAND_SZ`,
   `REG_BINARY`, `REG_DWORD`, `REG_MULTI_SZ`, `REG_QWORD` and `REG_NONE`); a
   value of another type is reported instead of being written with a wrong
   type.
4. **An environment variable which was removed.** A row of the environment
   workspace reports a value and never the absence of a variable, so the
   removal is reported only (see the known gaps of
   [Environment Isolation](EnvironmentIsolation.md)).

The report lists the first messages and summarizes the rest, because a capture
of a whole machine can leave out thousands of entries.

### The environment keys of the machine

A change of the environment of the machine is part of the registry as well, so
a capture reports it twice. The registry part of the application leaves the two
environment keys out, because the environment workspace is the domain which
expresses them: without that rule a variable would be written into the registry
workspace and into the environment workspace, and the two would have to be kept
in step by hand.

## Variable references

An absolute path of a registry value or of an environment variable which points
into a preset directory is rewritten to the reference of that folder, so the
archive stays correct on a machine whose folders are somewhere else:
`C:\Program Files\Foo\Bar.exe` becomes `%APPBOX:ProgramFiles%\Foo\Bar.exe`. The
rules are the ones of [Variable Expansion](../README.md#variable-expansion) and
they are applied to the value of a registry entry of a string type and to the
value of an environment variable. `src/core/KnownFolderReference.*` is the
counterpart of the expansion of the sandbox: it matches the real path of a
preset directory, requires a boundary around it (so `C:\Program FilesX` is left
alone), prefers the longest path (so `#ProgramFilesCommon#` wins over
`#ProgramFiles#`) and keeps the remainder of the path with its own separator.

## Runtime

The difference reaches the packaged application through the archive:

* A hidden entry travels as the whiteout marker of the entry below
  `app/filesystem/<layer key>/`, which the sandbox honours from any layer, see
  [Filesystem Isolation](FilesystemIsolation.md).
* A hidden registry entry travels in the hidden store of the hive of the
  archive, which the sandbox reports as missing until the packaged application
  writes the entry itself, see [Registry Isolation](RegistryIsolation.md).
* A variable travels in the isolation file of the environment domain, see
  [Environment Isolation](EnvironmentIsolation.md).

## Tests

The rules of the capture live in `src/core`, which is what makes them testable:
the workspace of the packer has no test infrastructure of its own, so the
commands themselves are verified by hand.

* `test/unit/SnapshotModel.cpp` — the identifiers of the snapshot comparison:
  the fold of the case, the keys of the filesystem, of the registry and of the
  environment.
* `test/unit/SnapshotStore.cpp` — the snapshot file: the round trip of every
  part of a phase (including a path of the host which is not ASCII, the raw
  bytes of a registry value and a value which holds no byte, which is stored as
  a blob of length zero and not as the absence of a value), the two phases of a
  file which stay apart, the replacement of the state after the change, the
  baseline which clears both phases, a phase which was started but never
  committed and is therefore reported as absent, the lookup of an entry by a
  name of any case, the counts of a phase, the copy of a baseline into another
  file, the refusal of a copy of a phase which is absent, a file which is not a
  snapshot, a schema version which is not supported, a missing file which no
  call creates, an empty path, a destination whose folder does not exist and a
  write which fails after the file was created, which leaves no file behind.
* `test/unit/SnapshotCapture.cpp` — the walk of the host, which runs against a
  folder of the case below `#USERPROFILE#`: the entries of the folder (the
  scanned folder itself, its subfolders, the size and the write time of a
  file), an empty folder, a reparse point which is not followed and not
  recorded, the file the run was told to leave out, the refusal of a folder
  outside its preset directory, of an unknown preset directory and of an
  unknown root key, the walk of a registry root key with the environment of the
  machine, the reports of every stage and the abort through the progress
  callback, which leaves a phase which carries no marker.
* `test/unit/SnapshotDiff.cpp` — the comparison: the added, the modified and the
  removed entries of the filesystem, of the registry and of the environment, the
  case insensitive names, a directory which is only added or removed, the
  modification of a file by its size and by its write time, the modification of a
  registry value by its type and by its data, the raw data of an added value, the
  independence of the order of the entries of a state and the refusal of a file
  which does not hold both states.
* `test/unit/SnapshotApply.cpp` — the rules which write the difference into the
  models: the import of a folder which is new, a folder which is part of the
  archive already, the report of a folder which existed before the capture and
  of a file of the layer root, the hidden entries, the registry keys and values,
  the hidden registry entries with their subtree, the environment keys which are
  left out, the variables and the summary of the entries which were left out.
* `test/unit/KnownFolderReference.cpp` — the rewrite of a path to a reference:
  the path of a preset directory and of the folder itself, the case, the
  boundary before and behind the path, the longest of several matching paths,
  every path of a text, an unknown path and a text which already carries a
  reference, a value of a string type with its terminator, a multi string value
  and the types which keep their bytes.
* `test/unit/FilesystemMarker.cpp` — the marker names of the packer against the
  macros of the sandbox, which is what keeps the two spellings in step.
* `test/e2e/Snapshot_WhiteoutHidesAHostEntry.cpp` — the runtime side: an entry
  the archive hides through its whiteout marker is invisible for the packaged
  application while the entry of the host and the content of the layer stay in
  place.
* `test/e2e/Snapshot_CapturedFolderIsVisible.cpp` — the whole chain: the state
  before the change is written into a snapshot file, the state after it is
  written into the same file, the two phases are compared, the difference is
  written into the models and packed, and the packaged application reads the
  file of the folder although the folder of the host was removed.

## Related documentation

* [README.md](../README.md) — build, components and artifacts.
* [Filesystem Isolation](FilesystemIsolation.md) — the layers and the markers of
  the virtual filesystem.
* [Registry Isolation](RegistryIsolation.md) — the hive and the isolation modes
  of the virtual registry.
* [Environment Isolation](EnvironmentIsolation.md) — the variables of the
  packaged application.
* [Tests](../test/README.md) — unit tests and end-to-end tests.
