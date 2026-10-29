# Launcher

`AppBoxLauncher.exe` is the program a packed archive starts: a program without a
window of its own which reads the configuration embedded in its own image,
prepares the sandbox of the packaged application and starts the programs the
configuration names. The launcher creates every program itself and injects the
sandbox module into it, so the process tree of a run is the launcher and the
programs it started. The launcher is the part of an archive which a user can drive from a
command line, so this document describes its command line options and the log
files a run writes.

## Self-extracting executable

The `Standalone (EXE)` project type writes one program instead of an archive: the
launcher image itself, followed by a zip archive of the read-only resources below
`app` and by a 32 byte trailer, the way a self-extracting archive of 7-Zip is
built. `data`, `patch` and `log` stay beside the program, so the state of the
sandbox, the patch packages of the user and the log files survive the run while
the resources need no folder beside the program at all.

The trailer is the last block of the file and is fixed and little endian:

|Offset|Size|Field|
|---|---|---|
|0|8|`uint64` offset of the payload from the start of the file|
|8|8|`uint64` size of the payload in bytes|
|16|4|`uint32` version of the trailer layout (`1`)|
|20|4|`uint32` reserved (`0`)|
|24|8|magic `APPBOXSF`|

A launcher whose file is shorter than the trailer or whose last bytes carry no
magic is an ordinary launcher of an archive: it reads its resources from the
`app` directory beside it, exactly as before. A program which carries the magic
but whose fields do not describe the bytes in front of the trailer is treated as
an ordinary launcher as well and logs a warning, so a damaged program reports the
missing resources instead of a failure of its own.

The payload holds the resource tree of the archive below `app` and nothing else:
the launcher is the program itself, so the payload carries neither a launcher
entry nor a `data` directory. The program extracts it into
`<temporary directory>\AppBox-SFX-<program stem>-<pid>` before the sandbox is
prepared, reports the release on the startup screen and takes the `app` directory
of that folder as the resource root of the run. The folder belongs to the run and
is removed when the run ends; a removal which fails because a process of the run
still holds a file is logged and left behind, and a leftover folder of the same
name (a reused process id) is deleted by the next run which meets it.

## Command line

Every option of the launcher is spelled `--X-AppBox-<Name>` and carries its value
after a space (`--X-AppBox-LogLevel info`) or after an equals sign
(`--X-AppBox-LogLevel=info`). The options of the launcher are read first: the
arguments which are left over belong to the packaged application, so they are
appended to the arguments of every startup file, and a run of the shell takes
them as the command of the shell.

| Option | Value | Behavior |
| --- | --- | --- |
| `--X-AppBox-ConfigFile` | Path of a configuration file | Resolve the run with the given file instead of the configuration resource embedded in the launcher. The layout of the sandbox is resolved against the directory of that file, so `data` and `patch` are looked for beside it; the released sandbox modules and the extracted patch packages live in `data/cache`. A self-extracting program still extracts its payload and takes the extraction as its resource root, so the resources stay behind the program image while its state follows the configuration file. |
| `--X-AppBox-LogLevel` | `trace`, `debug`, `info`, `warn`, `err`, `critical` or `off` | Level of the log files of the run (see [Logging](#logging)). A run which names no level writes no log file at all, which is the default. |
| `--X-AppBox-LogDir` | Directory | Directory the log files of the run are written into. A relative path is resolved against the current directory. Default: the `log` directory beside the launcher. |
| `--X-AppBox-Startup` | Trigger of a startup file | Start the startup file with that trigger instead of the files which are marked for auto start. A trigger no startup file carries refuses the run. |
| `--X-AppBox-Shell` | – | Run the shell of the host inside the sandbox instead of the startup files; the remaining arguments are the command of the shell. The option cannot be combined with `--X-AppBox-Startup`, which names a different program to run. |

A run without `--X-AppBox-Startup` starts every startup file which is marked for
auto start; a configuration without such a file refuses the run, because the
sandbox would start nothing. The exit code of the launcher is the exit code of
the run: the first non zero exit code of the programs it started, and zero when
every program exited with zero.

## Sandbox modules

The launcher carries the two sandbox injection modules (`sandbox32.dll` and
`sandbox64.dll`) as RCDATA resources of its own executable. Every start releases
them into `data/cache` of the sandbox before the runtime is created: the files
are created or truncated and written again, without a digest and without
comparing their content with the embedded modules, so a run always injects the
modules of the launcher it was started from.

A module which another run already loaded cannot be truncated by Windows. When
that sharing conflict is the only failure and the file which is already there
has the size of the embedded module, the launcher logs a warning and injects the
existing file. Every other failure, such as a cache directory which cannot be
created, refuses the run with a non zero exit code before any program starts.

## Startup files

A startup file is a file of the packaged application which the launcher runs: a
program (`.exe`), a batch file (`.bat`), a command script (`.cmd`) or a
PowerShell script (`.ps1`). The packer offers these files in its startup files
dialog and stores them in the configuration of the run, and the launcher starts
every file which is marked for auto start, or the single file which
`--X-AppBox-Startup` selects.

The extension of a file decides how the launcher starts it, because the process
creation of Windows is not promised to run a script itself:

* **A program** (`.exe`) is created as the process of the run.
* **A batch file** and **a command script** (`.bat`, `.cmd`) are run by the
  command interpreter of the host: the interpreter is created with `/c` followed
  by the script and by the arguments of the startup file.
* **A PowerShell script** (`.ps1`) is run by Windows PowerShell of the host with
  `-NoProfile -ExecutionPolicy Bypass -File` followed by the script and by the
  arguments of the startup file. The profile is left out to keep a run the same
  on every machine, and the execution policy of a machine refuses a script of
  the machine by default.

The interpreter is resolved when the run starts, so a run uses the command
interpreter and the Windows PowerShell of the machine which runs the sandbox and
not the ones of the machine which packed the archive. A run whose startup file
needs an interpreter the machine does not offer is refused instead of starting
only a part of the application.

The file name of the first startup file names the launcher of the archive. The
entry always carries the `.exe` extension, because the launcher is a program
image: a session whose first startup file is `run.bat` produces the launcher
entry `run.exe`.

### Locating the image of a startup file

The executable a configuration names is a path of the **sandbox view** and not a
path of the machine: the packer stores `<layer key>\<import>\<file>` (for example
`#ProgramFiles#\MyApp\app.exe`), while the files themselves travel below
`app/filesystem/<layer key>` of the archive, which the launcher of a
self-extracting program extracts from its own payload. The launcher therefore
resolves the path before it creates the process, with the very rules a
sandboxed process sees:

* the layer key is expanded to the known folder of the machine which runs the
  archive, and the resulting path is resolved against the layers of the view;
* the overlay (`data/filesystem`) is the layer the view prefers, then the lower
  layers in the order they are mounted (the patch packages first, the resources
  of the archive last) and finally the host filesystem;
* a whiteout marker (`<name>.$APPBOX_DELETE$`) and an opaque marker
  (`.$APPBOX_OPAQUE$`) of the overlay hide the entries below them;
* the isolation modes of the archive mask the layers they hide: `Full` of a
  folder hides the host folder and `Whiteout` hides every layer but the overlay;
* a reparse point of a redirecting tag is expanded by the view itself, so the
  isolation of the target decides which layers the path sees.

A startup file whose image no layer and no host path holds refuses the run: the
launcher reports the reason (missing, hidden by a marker or hidden by the
isolation) and exits with a non zero code without starting a program.

## Processes

Every process of a run is a member of a **job object** of its own from the
moment it is created (`launcher/utils/ProcessJob.cpp`): the creation names the
job through the attribute `PROC_THREAD_ATTRIBUTE_JOB_LIST` of an extended
startup information, so the process cannot run a single instruction outside of
the job. A platform which does not offer the attribute creates the process
suspended, assigns it to the job and resumes it; a creation which cannot put the
process into the job terminates it and the run reports the error.

The job carries `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` and allows no breakaway, so
a sandbox cannot outlive the launcher: the kernel kills every process of the job
when the launcher closes the handle, which includes the launcher being killed or
crashing. The launcher holds the handle until it exits.

The injected module matches the bit size of the program, which the launcher reads
from the image of the startup file before the process exists: a program of the
other bit size than the launcher is injected by the helper of Detours, which
patches the suspended process from a process of the bit size of the program.

The launcher waits for all of its processes with one **IO completion port**
(`launcher/utils/ProcessJobSet.hpp`): every job reports
`JOB_OBJECT_MSG_ACTIVE_PROCESS_ZERO` to the port, so the wait is event driven
and a job which joins the wait later is covered by it as well. The wait reports
why it returned (every job exited, a wakeup, a timeout or an error), because the
launcher of a sandbox serves the requests of the other launchers while it waits:
a queued request wakes the wait up (`ProcessJobSet::Wake()`), and the control
thread of the run handles it at once instead of waiting for the next round of a
poll. A run leaves when every job exited and no request waits any more.

A run leaves with the first non zero exit code of its own programs in the order
the startup files are started, and with zero when every one of them reported
zero; the programs a request started belong to the launcher which asked for them
and do not decide the exit code of this run. A failure to start a startup file or
to wait for the processes is reported the same way.

A request whose programs cannot all be created is **rolled back**: the processes
it already started are terminated and the request is refused, so a run never
carries half a request.

A process inherits the environment and the working directory of the launcher
unless the caller of the job names them. A named environment block is passed as
a Unicode environment, and a named working directory is a **host** path: the
launcher creates the process outside the sandbox, so the system checks the
directory on the host and a directory which exists in a layer only is not
usable.

## Multiple instances of one sandbox

A sandbox is started more than once in the ordinary use of an archive: a user
double clicks the program while it already runs, or the packaged application
starts the launcher of another archive of its own. One of those runs becomes the
**master instance**: it prepares the sandbox, starts the programs of its own
configuration and serves every other run of the sandbox. Every other run is a
**secondary instance**: it hands the request of its command line over and leaves
as soon as the answer arrives. The sandbox therefore keeps one writable layer,
one registry image and one control pipe however often it is started, and the
master instance waits for the processes of every instance.

Two values bound the feature: the identifier and the host root directory of the
sandbox. One identifier in one directory carries at most one master instance,
while a run which names another identifier or another directory belongs to a
sandbox of its own.

### Identity and the control pipe

The identity of a sandbox is the member `sandbox_id` of the configuration (see
[Configuration](#configuration)), and the name of its control pipe is derived
from it: `\\.\pipe\appbox_<sandbox_id>` with the hyphens of the identifier kept
(`appbox::SandboxPipeName()` of `common/SandboxId.hpp`). A configuration which
carries no identifier, or a value which is no identifier, refuses the run with
`ERROR_INVALID_DATA` (13) before a pipe, a `data` directory or a log file exists,
so a run never names a sandbox of its own next to the one the archive describes.

The root of a sandbox is the directory the launcher program lives in, or the
directory of the file `--X-AppBox-ConfigFile` names. It carries `app` and `data`
and is no part of the identity, so two copies of one archive carry the same
identifier in different directories. The master instance compares the root of a
request with its own after `GetFullPathNameW()`, without a trailing separator and
without regard to case, and refuses a request which names another directory: such
a run would write the state of one copy into the state of the other.

The root is always a path of the host: the injection module of the sandbox leaves
every launcher alone (see [The launcher is never isolated](#the-launcher-is-never-isolated)),
so a launcher which a sandboxed program starts resolves its root from the
directory of its own program like every other run, and it never reads the
injection data of a sandbox.

### The race for the control pipe

Every run of a launcher creates the first instance of the control pipe with the
flag `FILE_FLAG_FIRST_PIPE_INSTANCE`, and the outcome of that creation decides
its role:

| Outcome | Behavior of the run |
| --- | --- |
| The instance was created | The run is the master instance of the sandbox. |
| The name is taken | The run is a secondary instance and hands its request over. |
| `ERROR_ACCESS_DENIED` | The pipe belongs to another user. The run leaves with `ERROR_ACCESS_DENIED` (5) instead of joining the pipe silently. |
| Any other failure | The run leaves with the code of the failure and starts nothing. |

A creation which is refused with `ERROR_ACCESS_DENIED` names either a pipe of
another user or a name which a launcher of this user took; the flag cannot tell
the two apart. One connection to the pipe decides it: a name which accepts the
connection belongs to a running sandbox, and so does a name whose instances all
serve a client at that moment, which refuses the connection with
`ERROR_PIPE_BUSY` (`RemoteServerPeerIsReachable()`). Only a name whose access
control refuses the connection belongs to another user. A busy pipe is therefore
no reason to refuse the run: the handover of a secondary instance waits for the
next free instance of the pipe, which the master instance creates as soon as it
accepted a connection.

The master instance keeps at least one instance of the pipe open for its whole
life and creates the next instance once a connection was established
(`RemoteServer::AcceptNext()`), so the name of the sandbox never disappears while
the sandbox runs. A transient failure of that creation, which is a machine out of
resources while an instance still serves, is retried after 50, 100 and 200 ms; a
failure which leaves no instance at all ends the run, because no secondary
instance could reach the sandbox any more. The failure is reported to the control
thread instead of being thrown on the thread of the pipe, so the run ends in
order.

A secondary instance writes no state of the sandbox: it creates no `data`
directory, extracts no payload, releases no sandbox module, rebuilds no registry
image, serves no pipe and starts no program of its own. It leaves with zero once
the master instance reports that the programs of the request were created, and
with the code of the refusal otherwise (see [The answer](#the-answer)).

### The launcher is never isolated

The configuration of a process travels under the GUID of `APPBOX_SANDBOX_GUID`
(`sandbox/utils/Defines.hpp`) and its member `hook_enabled` decides whether the
injection module works in that process: the launcher writes it for every process
it creates with the member turned on, and the module then redirects the file
system, the registry, the network and the environment of the process.

The packer writes the same configuration into the image of every launcher it
produces with the member turned off, stored in a section of its own with the
binary API of Detours. The module reads the configuration of the image before it
creates anything of the sandbox and leaves a process alone whose configuration
turns the hooks off, so the hooks are never attached to a launcher.

A launcher therefore runs in the view of the host even when a sandboxed program
starts it: it reads host paths, it creates the processes of its own sandbox with
the host paths of the image, and it writes its own log file. A packed archive
which is started from inside another sandbox serves a sandbox of its own instead
of joining the sandbox it was started in, and a launcher which was not packed,
such as `AppBoxLauncher.exe` of a build tree, keeps the behavior of a program
which the sandbox isolates.

The payload of the image is not the configuration of the run: the run writes its
own configuration into a module of the process, and the module reads that one
only, so a launcher never runs with the empty configuration of its own image. A
payload which cannot be read - an image of an older packer or a damaged payload -
is no configuration, and the process is isolated the way it was before the
packer wrote one: the decision never trusts a payload which only happens to be
there.

### The start request

A secondary instance sends the message `LauncherStart` of
`launcher/msg/LauncherStart.hpp` over the control pipe; the transport is the one
of the sandbox RPC, which `MsgEnvironment` uses as well.

| Member | Meaning |
| --- | --- |
| `version` | Layout of the request (`1`). A version the master instance does not know is refused with `version_unsupported`, so a later layout may add members without being read as the current one. |
| `sandbox_id` | Identifier of the sandbox of the client, in canonical form. A value which differs from the one of the master instance is refused with `sandbox_mismatch`. |
| `sandbox_root` | Host root directory of the sandbox of the client, compared as described above. Another directory is refused with `sandbox_mismatch` as well. |
| `selection` | Programs the request asks for: `auto`, `trigger:<name>` or `shell`. The command line of the client maps to it: `--X-AppBox-Startup <trigger>` becomes `trigger:<trigger>`, `--X-AppBox-Shell` becomes `shell`, and a command line which names neither becomes `auto`. |
| `args` | Arguments which the launcher did not consume, in UTF-8 and in order. |
| `environment` | The environment of the client as key and value pairs. The entries which start with `=` are left out, because they describe the current directory of a drive and are no variables. |
| `environment_is_composed` | Whether the environment of the client is the environment of a sandbox already. A launcher never runs inside a sandbox (see [The launcher is never isolated](#the-launcher-is-never-isolated)), so a client always sends `false` and the sandbox of the created process composes the environment of the request. The member is kept because the configuration of a sandbox carries it. |
| `cwd` | The current directory the client saw, which is a path of the host. An empty value asks for the directory of the master instance. |

A request carries no path of a program: the master instance starts the startup
files of its own configuration, and the selection of the shell is the only
exception, whose command line is the `args` of the request. A request carries no
log level and no log directory either: the log of the run belongs to the master
instance.

### The answer

The master instance answers with `status`, `sandbox_id`, `pids`, `error_code`,
`error_message` and `win32_error`:

* `started` — the programs of the request were created, and `pids` names all of
  them in the order they were started in.
* `refused` — the request was not served; `error_code` names the reason and
  `error_message` describes it.
* `shutting_down` — the run which serves the sandbox is ending; the client races
  for the pipe again (see [The end of a run](#the-end-of-a-run)).

`sandbox_id` lets the client verify that the answer belongs to its own sandbox.
The client maps `error_code` to its own exit code, because it has no program
whose exit code it could report:

| `error_code` | Reason | Exit code of the client |
| --- | --- | --- |
| `sandbox_mismatch` | Another identifier or another root directory | `ERROR_ACCESS_DENIED` (5) |
| `unauthorized` | The image or the user of the client is not the one of the master instance | `ERROR_ACCESS_DENIED` (5) |
| `integrity_mismatch` | The client runs at another integrity level | `ERROR_ACCESS_DENIED` (5) |
| `server_mismatch` | The client found a server which is not the master instance (decided by the client itself) | `ERROR_ACCESS_DENIED` (5) |
| `version_unsupported` | The version of the request is not known | `ERROR_INVALID_DATA` (13) |
| `malformed_request` | A member is missing or of the wrong type | `ERROR_INVALID_DATA` (13) |
| `payload_too_large` | The request or one of its members is above a limit | `ERROR_INVALID_DATA` (13) |
| `busy` | The queue of the run is full | `ERROR_BUSY` (170) |
| `no_target` | `auto` found no auto start file in the configuration of the master instance | `ERROR_FILE_NOT_FOUND` (2) |
| `trigger_not_found` | The trigger names no startup file | `ERROR_FILE_NOT_FOUND` (2) |
| `shell_not_found` | The master instance resolved no shell | `ERROR_FILE_NOT_FOUND` (2) |
| `image_not_found` | The image of a selected program does not exist or is hidden | `ERROR_FILE_NOT_FOUND` (2) |
| `cwd_not_found` | The directory of the request is no directory of the host | `ERROR_DIRECTORY` (267) |
| `start_failed` | A process could not be created; `win32_error` carries the code | the Win32 code |
| `shutting_down` | The run is ending | `ERROR_TIMEOUT` (1460) once the budget of the race is used up |

### What the master instance starts

The selection names startup files of the configuration of the master instance and
nothing else:

* `auto` starts every file which is marked for auto start; a configuration
  without such a file refuses the request with `no_target`.
* `trigger:<name>` starts the file of that trigger, compared without regard to
  case; a trigger which names no file refuses the request with
  `trigger_not_found`.
* `shell` starts the shell of the host with the `args` of the request as its
  command; a run which resolved no shell refuses the request with
  `shell_not_found`. The window of the shell is always shown, because
  `hide_console` describes a startup file and not the shell.

The arguments of the request are appended to the arguments of every selected
startup file, on a copy: neither the configuration of the run nor the arguments
of the run itself change, so two requests never inherit each other's arguments.
The image of a program is resolved through the view of the run, which is built
once and shared by the programs of the run and by the programs of every request;
an image which no layer and no host path holds refuses the request with
`image_not_found`.

Every process of a request is created with the environment block of the request,
with the working directory the request names (see below) and with a copy of the
`SandboxConfig` of the run whose `pipe_path` is the same control pipe and whose
`environment_is_composed` is the member of the request; every other member of the
configuration describes the sandbox the run prepared. The processes join the job
set of the run, so the master instance waits for them as well and the sandbox
ends only after every process of every instance exited.

A request whose programs cannot all be created is rolled back: the processes it
already created are terminated and the request is answered with `refused`, so a
run never carries half a request. A refused request never terminates a program of
the run or of another request. Only the programs of the configuration decide the
exit code of the run; a program of a request belongs to the launcher which asked
for it, and a refused request is no reason for the run to fail.

### The working directory of a request

The directory of a request is handled in two steps, because the two sides of the
sandbox name a directory differently:

1. **The master instance** resolves the `cwd` of the request through the view of
   the run and creates the process with the resolved **host** directory. The
   launcher works outside the sandbox, so the system checks the directory on the
   host, and a directory which exists in a layer only is not usable. A `cwd` which
   no layer and no host path holds refuses the request with `cwd_not_found`; an
   empty `cwd` creates the process with the directory of the master instance.
2. **The sandbox** of the created process rewrites the directory it was created
   with into the path of the view once its file system hooks are attached
   (`sandbox/filesystem/ViewCurrentDirectory.*`, see
   [Filesystem Isolation](FilesystemIsolation.md#key-behavior-rules)). The
   program therefore reports the path of the view from `GetCurrentDirectoryW()`,
   resolves a relative name against that path and sees the per-drive entries of
   the environment block updated, so a relative read, write or delete follows the
   isolation modes, the whiteout markers and the copy up of the view instead of
   reaching the layer directly.

A directory which belongs to no layer keeps its path, and a rewritten path
belongs to no layer either, so the step is idempotent. A step which fails is
logged and leaves the process in the directory it was created with.

### Limits and timeouts

The values below bound what a client can make the master instance do before its
request is served:

| Limit | Value |
| --- | --- |
| Encoded request | 1 MiB |
| `selection` | 256 bytes |
| `sandbox_root` | 32 KiB |
| `cwd` | 32 KiB |
| `args` together | 64 KiB |
| `environment` together | 512 KiB |
| Requests which wait for the control thread | 64; a further request is answered with `busy` |
| Frame of the transport | 64 MiB (`kMaxRpcPayloadSize` of `common/RpcCodec.hpp`); a longer frame is refused while it is decoded |

The client bounds its own waits:

| Timeout | Value |
| --- | --- |
| Connect to the control pipe | 5 s, with the retries of the transport for a pipe which is busy, missing, not connected or broken |
| Wait for the answer of a request | 120 s, which covers the extraction and the runtime of a master instance that is still preparing its sandbox |
| Budget of the shutdown race | 30 s, with a wait of 200 ms that doubles after every refused handshake up to 2 s |

A request is answered as soon as the control thread handled it, which happens
while the programs of the master instance still work: the queue wakes the wait of
the control thread up instead of letting it poll.

### Who may join

The pipe itself is the first boundary. Its descriptor carries a protected DACL,
so no access control entry of a parent object is inherited, and full access is
granted to `SYSTEM`, to the administrators and to the owner of the object, which
is the user who started the launcher; every other user is refused at the
connection. The descriptor carries the mandatory label of the medium integrity
level with the no write up policy as well, and that label has to be given at the
creation: a label which is set on an instance afterwards is refused by the object
manager.

The identifier and the root of a sandbox are readable by every process the
sandbox runs, so they are no authentication. The master instance therefore reads
the identity of the process at the other end of the session
(`GetNamedPipeClientProcessId`) and accepts a start request only when all three
facts match its own identity:

* the image of the client is the image of the master instance;
* the user of the token of the client is the user of the master instance;
* the integrity level of the token of the client is the level of the master
  instance.

An image or a user which differs is answered with `unauthorized`, another
integrity level with `integrity_mismatch`. The environment messages of the
sandboxed processes are not affected by the check, because every sandboxed
process connects to the same pipe.

The image is the only fact of the program which is compared, because the
injection module leaves every launcher alone: a launcher runs the file it was
started from, never a copy below a layer of a sandbox, and the kernel reports the
same host path to both ends of the pipe.

A secondary instance checks the server as well before it sends its request: it
reads the identity of the process at the other end
(`GetNamedPipeServerProcessId`), requires the image of this run, the same user and
the same integrity level, and compares the `sandbox_id` of the answer with its
own. A server which fails the check is reported as `server_mismatch`, and the
request is not retried.

### The end of a run

The master instance serves its pipe until every process it waits for exited and
no request waits. It then stops accepting requests, answers the requests which
still wait with `shutting_down`, writes the answers which are in flight and closes
the pipe; a run which leaves for a failure does the same, so a client never loses
a request to a pipe which disappeared without an answer.

A client which receives `shutting_down`, or whose connection breaks, races for the
pipe again after the wait of the shutdown race: either the next run of the sandbox
serves the request, or the client wins the race itself and becomes the master
instance which starts the sandbox. The budget of 30 s bounds the retries; a client
which uses it up reports `ERROR_TIMEOUT` (1460).

## Configuration

The packer serializes the run configuration as UTF-8 JSON and writes it into the
launcher payload as the RCDATA resource `IDR_APPBOX_LAUNCHER_CONFIG` of
`launcher/EmbeddedResourceIds.h`. The launcher reads that resource from its own
executable at every start, so the extracted program carries its configuration
itself: no file beside the launcher can go missing or describe a different
application, and renaming the extracted program changes nothing. The document is
written without a byte order mark and keeps characters outside ASCII as their
UTF-8 bytes.

A launcher which was not packed, such as `AppBoxLauncher.exe` of a build tree,
carries no configuration resource and refuses the run with a non zero exit code.
The `--X-AppBox-ConfigFile` option overrides the embedded configuration; a run
which names it reads the given file instead and resolves the sandbox layout
against the directory of that file.

The document carries one member which is required and which no configuration
written by hand may leave out: `sandbox_id`, the identity of the sandbox in the
text form of `common/SandboxId.hpp` (36 characters, `8-4-4-4-12`, lower case
hexadecimal digits, hyphens kept). The packer takes it from the project session,
so every product of one project is one sandbox, and the launcher derives the name
of its control pipe from it. A configuration which does not carry the member, or
which carries a value that is no identifier, refuses the run with
`ERROR_INVALID_DATA` (13) instead of naming its sandbox itself: a run with an
identity of its own would silently open a second sandbox next to the one the
archive describes.

The project file of a session carries the same value as its own member
`sandbox_id`, which `File -> Export Configuration...` writes from the session and
`File -> Import Configuration...` adopts after it checked the value: a member
which is no identifier refuses the import, while a project file of an older
session which carries no member at all is read as an empty value. Packing reads
the identifier of the session and never writes the project file, so an export is
the only step which stores the identity of a project: packing one session twice
produces one identifier, and an export followed by an import and a pack produces
the same one as well. A project which was never exported therefore gets the
identifier of the session which packs it, which is the gap of
[Known gaps and limitations](#known-gaps-and-limitations).

The payload carries a second resource as well: `IDR_APPBOX_STARTUP_IMAGE`,
written when the session sets a startup image. The launcher reads it from its own
executable and decodes the bytes from memory, so no file beside it is opened for
the screen either. A payload without the resource is the normal case of a session
without an image, which shows the name of the application and the progress of the
run only (see [Startup Screen](StartupScreen.md)).

## Logging

**A run writes no log file unless it asks for one.** Without
`--X-AppBox-LogLevel`, with `off` and with a name the launcher does not know, the
run creates neither a directory nor a file: the messages of the launcher are
dropped and the sandbox of every process the run starts receives no log
directory, so no process of the run leaves a log file behind.

A run which names a level writes a log file of its own and hands the same level
and the same directory to the sandbox of every process it starts:

* **Directory** — `--X-AppBox-LogDir`, and `log` beside the launcher program when
  the option is not given. A relative value is resolved against the current
  directory of the launcher, like every other relative path of a command line.
  The directory is created when the run asks for a level, the directories above
  it included.
* **Name** — `<program>.<UTC time>.<process id>.log`, for example
  `AppBoxLauncher.20261012T081500Z.4321.log` for the launcher itself and
  `notepad.20261012T081502Z.7788.log` for a program the sandbox was injected
  into; the UTC time is spelled `YYYYMMDDTHHMMSSZ`. The rule is shared by the
  launcher and by the sandbox (`common/LogFileName.hpp`), so the log files of a
  run are recognized by a single name pattern. The time is part of the name
  because the process id of an earlier process can be reused.
* **One file per process** — every process writes its own file, so the messages
  of two processes never interleave and the tail of a log survives a crash of
  the process which wrote it. The launcher flushes every message it writes, so a
  launcher which crashes leaves its log behind as well.
* **Level** — the level describes the whole run: a run which asks for `info`
  never writes the trace of every kernel call of the sandboxed application into
  its log file.
* **Failure** — a log file which cannot be created never fails the run: the
  failure is reported and the run continues without that file.

## Known gaps and limitations

1. **The log files are never rotated, limited or deleted.** Every run which asks
   for a level adds one file per process to the directory, so a directory which
   is used often has to be cleaned by hand.
2. **A run writes more than one log file.** The launcher writes its own log, and
   every process the sandbox is injected into writes one as well, so a run of a
   single startup file leaves two files in the log directory, one per process.
3. **An unknown level writes no log file.** A misspelled level name is reported
   on the console of a run which has one and is otherwise treated like a run
   which asks for no log, so such a run leaves nothing to inspect.
4. **The option which named the log file itself is gone.** A run names a
   directory now and the file name is derived from the program, the UTC time and
   the process id, so a script which passed `--X-AppBox-LogFile` has to be
   updated.
5. **The launcher has no console of its own.** A failure of a run is reported
   through the exit code and through the log file of the run, so a run which
   asks for no log leaves no message behind at all.
6. **Windows PowerShell cannot start inside the sandbox.** The launcher builds
   the invocation of a `.ps1` file like the invocation of any other script, but
   the sandbox refuses to run the interpreter: Windows PowerShell is a .NET
   Framework application and aborts while it starts inside the sandbox (the
   process leaves with `0xFFFF0000` after the runtime reported
   `E_ACCESSDENIED`), while other .NET Framework programs run inside the same
   sandbox. A startup file which is a PowerShell script is therefore stored and
   packed like every other one, and it cannot be started by a run. The reason is
   the sandbox and not the configuration of the run.
7. **The fallback of a creation which cannot name its job has a small window.**
   A platform without `PROC_THREAD_ATTRIBUTE_JOB_LIST` creates the process
   suspended and assigns it to the job afterwards, so a launcher which dies
   between the two steps leaves a process which no job of the run holds.
8. **A request does not forward the standard handles of its client.** A program
   which a request starts reads the console of the master instance, or no console
   at all, instead of the console of the caller.
9. **A secondary instance has to run at the integrity level of the master
   instance.** A run whose token carries another level is refused, so a user who
   started the sandbox unelevated has to start the second instance unelevated as
   well.
10. **Two users cannot run one archive at the same time.** The name of the
   control pipe lives in the global namespace of the machine, so the creation of
   the second user fails because the name is taken, and the connection is refused
   by the access control of the pipe: the run reports a permission error. Running
   in parallel needs copies which carry different identifiers.
11. **Two copies of one archive cannot run at the same time.** They carry the same
   identifier in different directories, so the second run is refused with
   `sandbox_mismatch`. Running in parallel needs a copy which carries another
   identifier, which is a project of its own or a re-packed archive.
12. **A secondary instance has to run the same program file.** The image check
   accepts the image of the master instance only; a launcher which was copied
   elsewhere and pointed at the sandbox with `--X-AppBox-ConfigFile` is refused
   with `unauthorized`, and the second instance of a self-extracting archive has
   to be that archive itself.
13. **Another process of the same user can reach the control pipe.** The access
   control of the pipe limits connections to the user, and a start request only
   starts the startup files of the configuration after the image and the user of
   the client were checked, while the environment messages can change the state of
   the sandbox. The access control therefore has to stay as strict as it is; a
   process which holds the rights of the user could start the programs of that
   user anyway.
14. **A launcher whose image carries no configuration of the packer is
   isolated.** The configuration is written by the packer, so `AppBoxLauncher.exe`
   of a build tree and a copy whose payload was stripped are programs like every
   other one: the sandbox isolates them, and such a launcher cannot serve a
   sandbox of its own while it runs inside another one. A packed archive is the
   case which matters, and it always carries the configuration.
15. **A test cannot place a request inside the window of a closing run.** The end
   of a run and the closing of its pipe cannot be timed from outside, so the case
   of the shutdown race pins that the request was served and not which of the two
   runs served it.
16. **A project which was never exported gets a new identifier per session.** The
   identifier is written into the project file by an export, so archives which
   were packed from two sessions of one project carry two identifiers and cannot
   join each other. A stable identifier needs an exported project file.
17. **The wait of a client is a local decision.** A client which gives up after the
   120 s of the start response does not cancel its request: the master instance may
   still create the programs, so a user can see a failed client and the program
   afterwards. The request is not retried, because a retry could start a second
   copy of it.
18. **The control pipe has one thread for its IO.** Every method callback runs on
   that thread, so a handler only validates and queues its work: a handler which
   waited for the control thread would block the environment messages of the same
   pipe.
19. **A working directory which the view cannot rewrite keeps the path of a
   layer.** A step of the rewrite which fails is logged and skipped, so
   `GetCurrentDirectoryW()` reports the path of the layer. A relative name still
   follows the view, because the hooks resolve it against the path of the view the
   sandbox recorded for the handle (see
   [Filesystem Isolation](FilesystemIsolation.md#known-gaps-and-limitations)).
20. **`RtlSetCurrentDirectory_U` is an undocumented entry point of `ntdll`.** A
   future version which does not export it turns the rewrite into a step which
   logs and is skipped, so the process starts in the directory it was created with
   instead of failing to start.

## Tests

The end-to-end cases of the command line, of the process job, of the log files
and of the multiple instances of one sandbox are listed in
[test/README.md](../test/README.md).
