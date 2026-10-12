# Launcher

`AppBoxLauncher.exe` is the program a packed archive starts: a program without a
window of its own which resolves the configuration beside itself, prepares the
sandbox of the packaged application and starts the programs the configuration
names. The launcher is the part of an archive which a user can drive from a
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
| `--X-AppBox-ProcessLauncher` | – | Internal mode: start the program the remaining arguments name and exit with its exit code. The launcher uses it for the target of a run, which it starts inside a job object (see `launcher/utils/ProcessJob.*`); the mode is not meant to be used by hand. |
| `--X-AppBox-HideConsole` | – | Start the target of the process launcher mode without a console window, which is what an unattended run of a console program needs. |

A run without `--X-AppBox-Startup` starts every startup file which is marked for
auto start; a configuration without such a file refuses the run, because the
sandbox would start nothing. The exit code of the launcher is the exit code of
the run: the first non zero exit code of the programs it started, and zero when
every program exited with zero.

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
   the process launcher it starts for the target of a run is injected into the
   sandbox as well, so even a run of a single startup file leaves more than one
   file in the log directory.
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

## Tests

The end-to-end cases of the command line and of the log files are listed in
[test/README.md](../test/README.md).
