# Launcher

`AppBoxLauncher.exe` is the program a packed archive starts: a program without a
window of its own which resolves the configuration beside itself, prepares the
sandbox of the packaged application and starts the programs the configuration
names. The launcher creates every program itself and injects the sandbox module
into it, so the process tree of a run is the launcher and the programs it
started. The launcher is the part of an archive which a user can drive from a
command line, so this document describes its command line options and the log
files a run writes.

## Command line

Every option of the launcher is spelled `--X-AppBox-<Name>` and carries its value
after a space (`--X-AppBox-LogLevel info`) or after an equals sign
(`--X-AppBox-LogLevel=info`). The options of the launcher are read first: the
arguments which are left over belong to the packaged application, so they are
appended to the arguments of every startup file, and a run of the shell takes
them as the command of the shell.

| Option | Value | Behavior |
| --- | --- | --- |
| `--X-AppBox-ConfigFile` | Path of a configuration file | Resolve the run with the given file instead of `<launcher>.exe.json` beside the launcher. The layout of the sandbox is resolved against the directory of that file, so `app`, `data`, `patch` and `cache` are looked for beside it. |
| `--X-AppBox-LogLevel` | `trace`, `debug`, `info`, `warn`, `err`, `critical` or `off` | Level of the log files of the run (see [Logging](#logging)). A run which names no level writes no log file at all, which is the default. |
| `--X-AppBox-LogDir` | Directory | Directory the log files of the run are written into. A relative path is resolved against the current directory. Default: the `log` directory beside the launcher. |
| `--X-AppBox-Startup` | Trigger of a startup file | Start the startup file with that trigger instead of the files which are marked for auto start. A trigger no startup file carries refuses the run. |
| `--X-AppBox-Shell` | – | Run the shell of the host inside the sandbox instead of the startup files; the remaining arguments are the command of the shell. The option cannot be combined with `--X-AppBox-Startup`, which names a different program to run. |

A run without `--X-AppBox-Startup` starts every startup file which is marked for
auto start; a configuration without such a file refuses the run, because the
sandbox would start nothing. The exit code of the launcher is the exit code of
the run: the first non zero exit code of the programs it started, and zero when
every program exited with zero.

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
entry `run.exe` and the configuration `run.exe.json`.

### Locating the image of a startup file

The executable a configuration names is a path of the **sandbox view** and not a
path of the machine: the packer stores `<layer key>\<import>\<file>` (for example
`#ProgramFiles#\MyApp\app.exe`), while the files themselves travel below
`app/filesystem/<layer key>` of the archive. The launcher therefore resolves the
path before it creates the process, with the very rules a sandboxed process
sees:

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

## Tests

The end-to-end cases of the command line and of the log files are listed in
[test/README.md](../test/README.md).
