# Capture

The capture of `AppBox.exe` describes an application by the change it made to a
machine instead of by the files the user picks. The feature is the second way to
fill a session besides the commands which import a folder or a `.reg` file by
hand, and it is what makes a session which describes an installed application
reproducible.

The `Capture` command of the toolbar offers two ways to read the change of an
installation:

* **The realtime capture** records the changes while they happen: the packer
  subscribes to the kernel events of Windows and writes the changes it observed
  into the workspaces. The recording covers the machine as a whole, so it
  reports the changes of the installer itself and of every program it starts.
* **The snapshot capture** reads the state of the machine before the
  installation and again after it, and the difference of the two reads is
  written into the workspaces of the packer.

Both modes end in the same step, which analyzes the difference and writes it
into the workspaces of the session without asking the user anything, and which
lists the entries it could not write.

## Command

The `Capture` command of the toolbar opens the wizard of the capture, which
offers both ways to read the changes of an installation: the realtime capture
and the snapshot capture.

The realtime capture is the mode which observes the machine, so it needs:

* Windows 10 or newer, because the kernel events of the file system and of the
  registry came with it. The first step of the wizard draws the mode of a
  machine which is older as an option which cannot be picked, and the
  explanation of the mode names what it would need, so the snapshot capture is
  the one the user picks.
* An elevated process, because the kernel session of the machine answers only
  to one. The first step of the wizard lists the state of the process and offers
  the restart of the packer as an administrator: the work of the session should
  be saved before the restart, because the elevated copy opens as a session of
  its own.

The snapshot capture is the mode which asks for the machine alone, so it is
offered on every machine and needs no elevation.

The command records on a system logger session of the machine which the packer
opens for itself, so a capture no longer takes the shared kernel logger of the
machine away from another tool. A leftover session of a crashed run is stopped
before the recording starts, and a second capture is refused while a recording
of the packer still runs.

## The steps of the wizard

The dialog of the capture follows the steps of a setup wizard: the tree on the
left names the steps of both capture modes and marks the one the dialog shows,
the right side carries what the dialog says about that step above the page of the
step itself, and the `Cancel` button ends the capture on every step without
writing anything. The tree carries both branches while only the branch of the
mode which is picked holds the state of its steps, so the user sees what the
other mode would ask for.

The last step of both modes is the same: the difference the capture produced is
analyzed and written into the workspaces of the session. The page names the
phase the capture is in, the entry which is being written and how that entry
ended, and it lists the entries the capture could not write, see [What the
difference does to the workspaces](#what-the-difference-does-to-the-workspaces).
The `Confirm` button ends the capture and is enabled once the whole work of the
step is over; the `Cancel` button drops the capture while its work is being
prepared and is greyed while the workspaces are written, because a capture which
reached that point cannot be taken back.

### The steps of the realtime capture

1. **The mode.** The capture modes are listed, each with the explanation of how
   it reads the changes, and the branch of the mode which is picked carries the
   steps of that mode. A mode the machine cannot run is an option which cannot
   be picked. The prerequisites of the realtime capture are listed below the
   realtime option, each with a check when the machine meets it and a cross when
   it does not, so the step explains itself instead of reporting a failure after
   the user pressed the button which leaves it; the `Restart as administrator`
   button follows the list and is greyed when the process already holds the
   elevation or when the machine cannot run the realtime capture at all. The
   snapshot capture asks for nothing and carries no list of its own, and picking
   it leaves the list of the realtime option and its button on the step, so what
   the realtime capture asks for stays in sight.
2. **The recording.** The packer observes the machine while the user works
   outside of it: the installation is started and run as usual, and the page
   counts the changes as they arrive and names the process and the entry the
   capture saw last. The box below the state of the recording is the
   confirmation of the user that the installation is complete; the `Next`
   button, which ends the installation phase, waits for it and stays disabled
   until the box is ticked, and it is disabled again when the confirmation is
   taken back.
3. **The analysis and the fill.** The session of the kernel logger is stopped,
   the recorded events are merged into one state per path and the difference of
   that state is written into the workspaces of the session. The page names the
   phase the capture is in, the entry which is being written and how it ended,
   and it lists the entries the capture could not write. The step is the one the
   snapshot capture ends in as well.

### The steps of the snapshot capture

1. **The source of the baseline.** The baseline of the comparison is picked:
   `Create a new snapshot` reads the state of the machine into a snapshot file,
   `Use an existing snapshot` uses the state before the change a snapshot file an
   earlier run wrote holds. Picking one only stores it: the work of the source is
   asked for on the step of the baseline, so switching the option neither opens a
   file dialog nor starts a run.
2. **The file of the baseline.** The path of the snapshot file is shown in a
   control which the user can type into, and the `Browse...` button beside it
   fills the control in from a file dialog: the dialog of `Create a new snapshot`
   asks for a file to write, the one of `Use an existing snapshot` for a file an
   earlier run wrote. `Next` stays disabled until a path is there. The step names
   the file and nothing else, so it is left once and the work of the source is
   asked for by the step which follows.
3. **The baseline.** The step is entered by the step of the file and asks for the
   work of the source which was picked: the state of the machine is read into the
   file, or the state the file holds is accepted. The page counts the entries of
   the run, names the entry it works on and shows the time the run takes; `Next`
   stays disabled until the baseline is there, and it then walks on to the
   installation. A path which cannot be written or a file which does not hold a
   baseline is reported and shows the step which names the file again, which
   keeps the path, so the work can be asked for again.
4. **The installation.** The program is installed outside AppBox, which is
   where the machine changes. The box below the note is the confirmation of the
   user that the installation is complete; `Next`, which reads the state of the
   machine again, waits for it and stays disabled until the box is ticked, and
   it is disabled again when the confirmation is taken back.
5. **The comparison.** The state of the machine is read again into the phase
   after the change of the same file and compared with the baseline; the page
   counts the entries the run recorded. A run which failed offers the
   comparison again.
6. **The analysis and the fill.** The difference of the two states is written
   into the workspaces of the session, which is the step the realtime capture
   ends in as well.

## The realtime capture

### How the recording works

The recorder (`src/core/RealtimeCapture.*`) drives a system logger session of
Windows which it opens for itself: the session enables the manifest provider of
the file events of the kernel (`Microsoft-Windows-Kernel-File`) and the classic
group of its registry events, and consumes their events on a thread of its own.
The manifest providers of the kernel answer only to a session a program opens
for itself, so the capture no longer takes the shared kernel logger of the
machine away from another tool; the session of the capture belongs to the
packer, and a leftover session of a crashed run is stopped before the new one
starts.

The registry is read through the classic group of the kernel rather than through
the manifest provider `Microsoft-Windows-Kernel-Registry`: the kernel leaves the
`KeyName` of the value and key events of that provider empty, its create and
open events name a key relative to the base object they carry, and it has no
event which enumerates the keys which existed before the recording began. The
classic group names the key of a value event through its key control block, and
the rundown it reports when the session starts names the blocks of the keys
which exist, so a change of a key which was opened before the recording is
resolved as well.

The payload of an event is decoded with the trace data helper of Windows (TDH)
by the name of its properties: the metadata of an event is asked for once per
provider, event identifier and version, and the properties of the events which
follow are read by their name. The layout of an event belongs to the manifest of
its provider and differs between the versions of the machine, so the capture
never reads a value by an offset into a payload. A property whose count, length
or structure the event itself carries is sized with the trace data helper as
well, so the properties behind it stay readable.

The consumer of a recording takes the session before the file provider is
enabled, and the recorder returns only once it did: an event a provider writes
is carried by the buffers of the session until the consumer drains them, and a
burst which fills the buffers while nothing consumes the session is dropped by
the kernel. The file provider reports the rundown of the names of the files the
kernel holds when it is enabled, which is such a burst, so a change the caller
makes right after the start of the capture is never written into a session
nobody reads yet. The registry group is enabled with the flags of the session
and reports its rundown when the session starts, which is before the consumer
takes it, so the buffers of the session are sized to carry that burst
(`kSessionBufferSize` and the number of buffers around it).

The events the kernel dropped are reported: the counters of the session are read
the moment it is stopped, they are named by a warning of the recording, and they
are part of the report of a recording (see [The report of a
recording](#the-report-of-a-recording)). A capture which lost events is a
capture whose difference may be missing a change, which is what the report tells
apart from the events the capture could not resolve.

The file provider is asked for the events of the machine with the keywords the
manifest of the machine declares for them (`FileProviderEnableFrom`): the
manifest of a version of Windows may declare an event the capture reads with
another keyword or another level, and a capture which named the keywords of the
version it was written against would silently read no event of that kind. The
classic groups of the file I/O are enabled with the flags of the session as
well, because a version of the kernel decides whether it writes the events which
report the path of a file from those groups.

The events the capture reads are found by the keyword the manifest of the
machine declares for them as well (`ReadFileEventIds`): the identifier of an
event belongs to the version of the manifest of the machine, and the kernel of a
version of Windows numbers the events which report the path of a file - the
deletion, the rename and the file which came into being - differently from the
version the capture was written against. A capture which switched on the
identifiers of that version would read no such event on the other one, while the
keyword of an event belongs to its declaration and is what the kernel tags the
event with. An event the machine declares with no keyword the capture reads is
named by a warning and by the report.

The recorder of the window is the one recorder of every capture of a session: a
recording which ended is given up when the next one starts, which stops its
session and drops its state, so a capture which follows a cancelled or a
confirmed one finds the session free again. The events of a recording which were
not taken are dropped with its state, which is why the last step of the wizard
takes them when the recording ends.

* The file events arrive with the file object of the kernel, which is what ties
  the events of one open file together, and with the file key of the file, which
  stays the same while the file is renamed or opened twice. A file which was
  opened before the recording began is named by the rundown of its file key. The
  path of an event names the volume device of the kernel
  (`\Device\HarddiskVolume4\workspace\...`), which is exchanged for the drive
  letters of the host. The name keeps the separators of the caller, so the
  translation canonicalizes it: a folder which arrives with a trailing
  separator and a name which was joined onto a folder with a doubled one name
  the same entry as their canonical spelling, and one entry must not be
  described twice.
* The event which reports that a file or a folder came into being is the create
  new file event of the file provider, which is the signal of a new entry; the
  write events of a file are the signal of a change of an entry which was there
  before. The kernel events of a create alone do not tell a file which was
  brought into being from a file which was only opened, which is why the capture
  reads the event the provider declares for the first of the two. The event
  names an entry whatever its content is: a file which holds no byte and a
  folder which holds nothing are entries like the others, so a document the
  user created with the shell and never wrote to is part of the capture.
* The registry events name the key control block of their key. The blocks are
  enumerated once when the session starts, which yields the paths of the keys
  whose blocks the kernel holds at that moment, and the block of a key which the
  kernel creates while the recording runs carries the path of its key: together
  they are the map which turns the value and key events of a key into the path
  of the registry view. The kernel reports the paths in the canonical spelling
  of their blocks, which is upper case.
* The environment is not part of the event stream: the persisted environment
  of the machine is read once before the session starts and once during the
  analysis, and the difference of the two reads is the part of the environment
  the recording observed.

### The report of a recording

The end of a recording writes a report of what it did, which is what a reader of
a capture needs when a change is missing. The report is written at the debug
level of the log, so the log of a run of the packer does not carry it, while the
log of a case which failed does, see [../test/README.md](../test/README.md).

* The counters of the session: the number of the buffers it held, the number of
  the events and of the buffers the kernel dropped, the volumes of the host and
  the devices which matched no volume.
* The events the manifest of the machine declares for the file provider which
  the capture reads, with the level and the keyword of each of them, the
  identifier the capture read for every one of them and the keywords the
  provider was enabled with: an event the capture reads which the manifest does
  not declare, or which was declared and never delivered, is named by it.
* What the recording did with the events of every kind: how many of them
  arrived with which level and keyword, how many became a change, how many the
  noise tables left out and how many could not be resolved, with the reason of
  each of them.
* The first events of a kind which could not be resolved, with the process which
  made the change, the object of the event, the path the kernel reported, the
  first bytes of its payload and the properties the machine declares for that
  kind of event.
* The events of the providers the recording does not read, with the name the
  machine gives them: a machine which reports a change through another provider
  is the one case a reader has to tell apart from a machine which reports no
  change at all.
* The first changes of every kind the recording reported, with the process which
  made each of them.

The report is bounded, so a log which carries it stays readable.

### The noise the recording leaves out

A recording observes every process of the machine, so the noise of the system
itself is filtered before an event is kept (`DefaultRealtimeCaptureOptions`):

* The packer itself and the services which rewrite the machine on their own:
  the system processes, the malware protection, the search indexer and the
  component installer of Windows Update. An installer runs as a process of its
  own, so the noise of the system does not hide the change of an installation.
* The state the shell of the machine keeps for itself, which it rewrites while a
  user works: the items the user opened recently with their jump lists, the
  libraries of the shell, the thumbnails and the icons it caches, the caches of
  the content it shows and the database of the notifications it draws, which
  live below the preset directories of the roaming and of the local application
  data of the user, together with the registry keys which hold that state (the
  settings of the shell, the store it syncs, the files it opened recently, the
  bags of the folder views and the cache of the names of the programs it
  shows).

  The shell is the process the user works with and not noise: the file a user
  creates, renames or removes with it is a change of the machine, and a capture
  which dropped the events of the shell would lose the very file the user just
  made. What is left out is the state the shell keeps for itself, which the
  folders and the keys above name.
* The places of the host which carry the transient state of the machine and no
  application content: the temp folders of the user and of the machine, the
  prefetcher, the update queue and the recycle bin. The temp folder of the
  user lives below a preset directory, so the extracts of an installer would
  travel into the archive without the entry of their own.

### What the analysis does with the events

The analysis (`HarvestRealtimeEvents`) reduces the events to one state per
path — a change which was made and undone within the recording leaves no entry
behind — and turns the states into a snapshot difference. The difference is what
the last step of the wizard writes into the workspaces, through the fill the
snapshot capture uses as well, see [What the difference does to the
workspaces](#what-the-difference-does-to-the-workspaces).

* Every entry the recording reported is an entry of the difference: a file or a
  folder which was created is added, an entry whose content changed is modified
  and an entry which was deleted is removed. Nothing is read from the folder
  which holds an entry, so the difference carries the changes of the recording
  and not the rest of the folder the host holds.
* A path the recording reported more than once is reduced to its final state, so
  a file which was created and written is one entry and a file which was created
  and deleted again leaves no entry behind.
* A rename is the removal of the old path and the addition of the new one.
* An alternate data stream of a file (`App.exe:Zone.Identifier`) is left out:
  the kernel reports a stream as a path of its own, while the view names files
  and folders alone and neither the archive nor the workspaces can carry a
  stream.
* A registry value which was written is reported with the data the host holds
  at the time of the analysis, together with the key which holds it. A value
  or a key which was deleted is reported as removed.
* A key which is new is not reported on its own: the kernel events cannot tell
  a key which was created from a key which was only opened, so the keys of the
  difference are the ones a value was written to.
* A change whose path belongs to no preset directory cannot be expressed by the
  workspaces. The recording observes the whole machine, so those changes are
  reported as ignored entries instead of being dropped, see [Entries which
  cannot be written](#entries-which-cannot-be-written).

## The snapshot capture

The snapshot capture is the mode which asks for the machine alone: it reads the
state of the machine before the installation and again after it, and the
difference of the two reads is the capture.

The baseline is a file and not a state in memory: the step of the file of the
baseline asks for the path before the machine is read, every entry a run reads
travels into the file right away, and the comparison opens that file again. The
memory of a run is the size of one entry and of the page cache of the database
instead of the size of the machine.

A run reports its progress on the page of the step it belongs to, which can be
cancelled: the page names the part of the machine which is being read, the entry
the run is working on and the number of the entries it recorded, and the wizard
keeps the elapsed time on its own clock.

The comparison does not become the baseline of the next one: the file holds the
state the machine was in before the change and the state it is in now, and the
first of the two is a state the machine has already left behind. A capture
therefore starts from its own baseline, and the way to compare against a state
an earlier run captured is to pick that file as the baseline: the state after
the change of the new run replaces the state after the change the file holds.

The path of the snapshot file belongs to the capture alone: it is not part of
the project file of `File -> Export Configuration...`, because it names a file
of the machine the packer runs on and not a configuration.

### Scope of a capture

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

### Snapshot file

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

Both captures reach it: the snapshot capture compares the two states it read,
and the realtime capture turns the states of its recording into the same
difference, see [What the analysis does with the events](#what-the-analysis-does-with-the-events).

## What the difference does to the workspaces

`src/core/SnapshotApply.*` writes the difference into the pack model, the
registry model and the environment model:

| Difference | Workspace | Effect |
| --- | --- | --- |
| A file which is new or whose content changed | Filesystem | The file is imported at its own path below its preset directory, and the folders of the way to it are created as folders of the view when the model does not hold them yet. Only the entry itself travels: the folder which holds it is never read. |
| A folder which is new | Filesystem | The folder and the folders of the way to it are created as folders of the view. The entries below the folder travel as the entries of their own which the difference reports. |
| An entry the newer snapshot does not hold | Filesystem | The entry is hidden: the archive carries the whiteout marker of the entry, which hides the entry of the host while the packaged application runs. A folder which is gone is marked on its own and the entries below it are left out, because the marker of the folder hides them already. |
| A registry key which is new | Registry | The key is created with the mode a new key starts with, which is `Merge`. |
| A registry value which is new or whose data changed | Registry | The value is written with the type and the bytes of the newer snapshot; a value which is created now starts as `Full`. |
| A registry key or value which is gone | Registry | The entry is written with the `Hide` mode, which keeps it invisible for the packaged application; the sub keys and the values of a hidden key are left out, because the key hides them already. |
| An environment variable which is new or whose value changed | Environment | The variable is written with the modes a variable of the workspace starts with: `Write Copy` and `Replace`. |

A capture is a fill and not an import of the folders of the host: it places the
entries of the difference at the very paths the host holds them at and reads
nothing around them. A file which was added to a folder that existed before the
capture is therefore part of the archive like a file of a folder the capture
created, and the folder of a captured file is a folder of the view and not an
import of the host folder, so the entries which were there before do not travel
with it.

The fill can be run twice: an entry which the view carries already is left where
it is, so a file which an imported folder carries, an entry which is hidden
already and a file which was imported before are not written twice.

The mode of an entry is the mode the workspace gives it, so the modes can be
changed by hand after a capture, exactly like the modes of an entry which was
imported by hand.

### Entries which cannot be written

The report of a capture names every entry it had to leave out, and the last step
of the wizard lists them in two pages.

The ignored entries are the ones the workspaces cannot express:

1. **A change whose path belongs to no preset directory.** A capture only
   carries the parts of the machine the workspaces describe, and a recording
   observes the whole machine, so the changes of another place are reported
   instead of being dropped.
2. **A registry value whose type the workspace cannot store.** The model
   carries the types the registry editor offers (`REG_SZ`, `REG_EXPAND_SZ`,
   `REG_BINARY`, `REG_DWORD`, `REG_MULTI_SZ`, `REG_QWORD` and `REG_NONE`); a
   value of another type is reported instead of being written with a wrong
   type.
3. **An environment variable which was removed.** A row of the environment
   workspace reports a value and never the absence of a variable, so the
   removal is reported only (see the known gaps of
   [Environment Isolation](EnvironmentIsolation.md)).

The errors are the entries the capture could not write, and each of them carries
the reason: a file which is not on the host anymore, a file which the model
refused, a registry entry which could not be written and a change whose preset
directory this machine does not offer.

Both lists name the first entries and count the rest, because a capture of a
whole machine can leave out thousands of them.

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

The cases of the realtime capture:

* `test/unit/RealtimeCapture.cpp` — the analysis of a synthetic recording: the
  path translations, the noise the options leave out, the tables the capture
  starts with (the shell is not noise, while the state it keeps for itself is
  left out by the folders and the keys the tables name), the entries a recording
  reported (a folder is reported on its own and not with the content of the host
  folder it names), a change which was undone, a rename, a written file, the
  registry values which are read from the host, the deleted entries, the last
  change of a value, the difference of the persisted environment and the change
  which belongs to no preset directory and is reported as an ignored entry.
* `test/unit/EtwEventDecoder.cpp` — the decoder of the payloads, which reads the
  properties of an event by their name: one event of a provider which declares
  two versions whose payloads lay the same values out differently, which is what
  an offset into a payload cannot read and a name can, a classic event whose
  event type its opcode names and whose object is not the first pointer of its
  payload, the file object and the file key of a write, the path of the event
  which reports that a file came into being, the key handle and the value name
  of a classic registry event, a walk which reads the property behind a property
  whose length the event carries, a walk which stops at the property a short
  payload does not hold, an event of a provider no machine knows and the flag
  which marks a WPP message.
* `test/unit/CaptureWizardModel.cpp` — the steps of the wizard, which the
  dialog of the capture renders: the first step of a fresh wizard, the mode a
  machine without the kernel events cannot pick, the step which waits for the
  elevation of the process, the walk through the steps of a realtime capture,
  the recording which waits for the confirmation of the user and closes again
  when the confirmation is taken back, the last step which waits for the
  workspaces and which cannot be dropped while they are written and the
  prerequisites with the name of each one and whether the machine meets it.
  The snapshot capture is covered as well: the mode which is offered on every
  machine, the steps of its own walk, the step of the file of the baseline which
  waits for a path, the step of the baseline which waits for the run of the
  source and which shows the step of the file again when the run produced none,
  the path which is set anew and drops the baseline, the source which is picked
  anew and drops the file as well, the installation which waits for the
  confirmation, the comparison which waits for the analysis and the names and the
  descriptions of both modes.
* `test/e2e/RealtimeCapture_RecordsTheChangesOfARecording.cpp` — the whole
  chain against the kernel of the machine: the session is started, the case
  plays the installation, the recording is stopped and analyzed and the
  difference is filled, which makes the folder of the case a folder of the view
  and imports the file inside it. The entries a user makes with the shell are
  covered as well: the case creates a file with no byte in it through a process
  which carries the name of the shell, a file with no byte in it of its own and
  a folder which holds nothing, all of them in the root of the preset directory
  of the user profile, which the recording names, the difference reports as
  entries of the root and the fill imports as files and as a folder of the
  view. The recordings which follow each other are
  covered as well: a
  recording which was cancelled and one whose events were taken are given up by
  the next recording, which records on a session of its own, and a recording
  which still runs is refused. The rename and the deletion of an entry are
  covered as well: the case renames a file and deletes another one while a
  registry value and its key are written and deleted again, which the events of
  the recording and the difference of it name. The cases need an elevated test
  process and are skipped without it.

The cases of the snapshot capture:

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
  models: the file which is placed below a folder that existed before the
  capture, the folders of the way to a file, the file of the layer root, the
  folder which is created on its own, the entry which the view carries already,
  the file which is not on the host anymore, the hidden entries, the folder
  which is gone with the entries below it left out, the entry which is hidden
  already, the registry keys and values, the hidden registry entries with their
  subtree, the registry value of a type the workspace cannot store, the
  environment keys which are left out, the variables, the variable which was
  removed, the difference which cannot be interpreted, the cap of the two lists
  with the count of the rest, the work of the fill which is reported and the
  error a caller reports itself.
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
* `test/e2e/Snapshot_CapturedFolderIsVisible.cpp` — the whole chain the wizard
  of the capture drives: the state before the change is written into a snapshot
  file, the state after it is written into the same file, the two phases are
  compared, the difference is filled (which makes the folder which existed
  before the capture a folder of the view and imports the file the installation
  wrote), the models are packed, and the packaged application reads the file of
  the folder although the folder of the host was removed.
* `test/e2e/Snapshot_StrictCaptureFillsOnlyTheCapturedEntries.cpp` — what a
  capture carries: both states are read from the machine, the installation
  writes a file into a folder which existed before the capture and removes
  another folder of the case, and the archive carries the file the installation
  wrote and the single marker of the folder which is gone and neither the file
  next to it nor a marker of the entries inside the hidden folder. The packaged
  application reads the captured file although the folder of the host was
  removed and does not find the file which was never captured.

## Known gaps and limitations

### 1. The noise of a quiet machine cannot end a recording

The end of a recording is reported through the events themselves, so a machine
which reports nothing for a while is polled: the wizard is asked every 250
milliseconds whether it was dismissed, and the `Next` button of the recording
step ends the recording through the wizard. The delay between a cancel and the
end of the session is the interval of the poll.

### 2. The map of the objects of the kernel is bounded

The events of a write name the file object of the kernel, which the recorder
maps to the path it was created with, and the events of a registry value name
the key control block of its key, which the recorder maps to the path of the
key. A recording which walks through more than 100000 objects drops one of the
two maps and fills it again, and the events whose object was dropped are counted
as unresolved instead of changes.

### 3. A registry key the kernel did not report again is left out

The registry events name the block of their key, so the capture resolves a value
through the map of the blocks it saw. The kernel reports the block of a key when
it creates it, and it reports the blocks of the keys it holds when the session
starts, but the path of such a rundown entry is the name of the key relative to
its hive for every key below a hive root: the capture keeps the blocks whose
path it can turn into a path of the view, which are the ones of the hive roots
and the ones the kernel created while the recording ran. A value which is
written to a key which existed before the recording is therefore left out of the
capture; the report of the capture counts such an entry instead of describing
it, and the report of the recording names the event and the reason.

### 4. The events of the kernel are coarser than the changes

A file which was created and overwritten is reported as created, a folder
which an installer only opens and never creates is not reported at all, and a
registry value which was written and left as it was is reported with the data
of the host. The analysis reads the host at the time it runs, so a change
which was made after the recording ended is part of the result as well.

### 5. The registry paths are upper case

The kernel reports the paths of the key control blocks in their canonical
spelling, which is upper case, so the registry entries of a realtime capture
carry upper case paths while the snapshot capture keeps the spelling of the
host. The comparison of the registry is case insensitive, so both spellings
name the same entries.

### 6. A WPP message is not decoded

The capture subscribes to the providers of the kernel, which write the events of
a manifest and never a WPP message, so a message of the software trace
preprocessor is recognized and counted instead of being decoded. A capture which
would have to read the messages of a driver needs the manifest of that driver
and a decoder of its own.

### 7. An event the machine numbers differently is found by its keyword

The capture finds the events it reads by the keyword the manifest of the machine
declares for them, because the identifier of an event belongs to the version of
that manifest and the kernel of a version of Windows numbers the events which
report the path of a file differently from the version the capture was written
against. Two events which share a keyword are therefore read as both of them:
the two events which name the file of a file key fill the same table, and the two
events which report the path of a rename are both read as a rename. A version of
Windows which declared the events of the capture with keywords of its own would
need the table of that version, which the report of a recording names.

### 8. An event the kernel dropped cannot be read back

The kernel carries the events of a session in its buffers until the consumer of
the session drains them, and it drops an event when a burst fills every buffer
while nothing consumes it. The buffers of the capture are sized for the bursts a
recording sees, the consumer of the recording takes the session before the file
provider is enabled, and the events which were dropped are reported by the
counters of the recording, but a change whose event was dropped is a change the
capture cannot recover: the difference of such a recording may be missing an
entry, which is what the report of the recording names.

### 9. The state of the shell outside the named locations is captured

The noise of the shell is left out by the folders and the registry keys the
capture names (`AddShellStateFolders()` and `AddShellStateKeys()` of
`src/core/RealtimeCapture.cpp`), because the shell is the process the user works
with and its events cannot be dropped as a whole. A state location those tables
do not name is therefore part of the capture: a `Desktop.ini` the shell writes
into a folder it displays, for example, travels into the difference like an
entry of an installation. The tables are the one place which decides this, so a
machine which shows such an entry adds its location to them.

## Related documentation

* [README.md](../README.md) — build, components and artifacts.
* [Filesystem Isolation](FilesystemIsolation.md) — the layers and the markers of
  the virtual filesystem.
* [Registry Isolation](RegistryIsolation.md) — the hive and the isolation modes
  of the virtual registry.
* [Environment Isolation](EnvironmentIsolation.md) — the variables of the
  packaged application.
* [Tests](../test/README.md) — unit tests and end-to-end tests.
