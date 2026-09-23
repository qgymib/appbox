# Tracer

`AppBoxTracer.exe` runs a program and reports which functions of `ntdll.dll`,
`kernel32.dll` and `kernelbase.dll` the program **actually used**, including the
functions used by its child processes. It is the tool which answers the question
"which of the APIs an isolation layer has to intercept does this program
really call?".

The scope of a run is the union of the three isolation domains of appbox
(filesystem, registry and network), which mirrors the isolation layers the
sandbox implements; `--all-exports` widens the run to every export of the three
modules, and `--list-scope` prints what a run would arm without running
anything.

```
AppBoxTracer [options] <program> [program arguments...]
```

Options must precede the program; everything from the program on is passed to it
unchanged.

| Option | Meaning |
| --- | --- |
| `--cdb <path>` | Debugger to drive. Searched on the `PATH` and below the Windows Kits directory when it is omitted. |
| `--output <path>` | Write the report as UTF-8 to this file instead of the standard output. |
| `--categories <list>` | Comma separated categories to trace: `file`, `registry`, `network`. Default: all three. |
| `--all-exports` | Trace every executable export of the three modules instead of the categories. Much slower. |
| `--list-scope` | Print the functions which would be armed and exit. |
| `--with-categories` | Annotate every reported function with its categories. |
| `--timeout <seconds>` | Hard limit of the whole run. Default: 600. |
| `--stall-timeout <seconds>` | Seconds the debugger may stay stopped before the run is aborted. Default: 30. |
| `--keep-raw <path>` | Write the raw debugger output as UTF-8 to this file. |
| `-h`, `--help` | Print the usage text. |

Exit codes: `0` the run completed, `1` the trace could not be completed (the
report is still written), `2` invalid command line.

## Examples

```
# Which file, registry and network functions does cmd.exe use, including its child?
AppBoxTracer --output cmd.txt cmd.exe /c cmd.exe /c echo child

# Review the scope before a run, with the categories of every function.
AppBoxTracer --list-scope --with-categories --output scope.txt cmd.exe

# Only the registry domain, and only the two modules which implement it.
AppBoxTracer --categories registry --with-categories notepad.exe

# Trace a 32 bit program: the WOW64 copies of the three modules are used.
AppBoxTracer --timeout 120 "C:\Program Files (x86)\app\app.exe"
```

## How it works

The tracer drives `cdb.exe` as an interactive debugging engine: it feeds a
breakpoint script through the standard input of the debugger and decodes the
events of its output.

### Address based breakpoints

A breakpoint is placed on the absolute address of a function:

```
bp /1 0x7ffc4259ae30 ".echo APPBOXHIT kernel32!CreateFileW; g"
```

The address is computed from the export table of the module file (RVA) plus the
base address the debugger reported in its `ModLoad:` line. Symbol names are
**not** used: `bp kernel32!ActivateActCtx` and `bp kernel32!GetCommandLineW` fail
with `Couldn't resolve error` on the reference system while
`bp kernel32!CreateFileW` works, so symbol resolution is not reliable enough.
Address based breakpoints have a second advantage: a function which a program
obtains through `GetProcAddress` is caught as well, because the call enters the
same address.

`/1` makes the breakpoint a one-shot breakpoint, so a hot function interrupts
the program once per process instead of on every call. The command of the
breakpoint prints a marker line and continues (`g`), which keeps the output free
of disassembly and breakpoint messages.

### Aliases and forwarders

A breakpoint can only be placed on an address, while one address can carry
several names:

* `ntdll!NtClose` and `ntdll!ZwClose` are two exports of the same code;
* `kernel32!CreateFileW` is a stub which forwards the call to
  `kernelbase!CreateFileW`, and `kernel32!AddDllDirectory` forwards through an
  API set DLL to `kernelbase!AddDllDirectory`;
* `kernel32!HeapAlloc`, `kernelbase!HeapAlloc` and `ntdll!RtlAllocateHeap` can
  end up in the same implementation.

The plan groups every in-scope name by the address of its implementation and
follows forwarder chains (parsing API set DLLs on demand), so a hit reports
every name the call could have used. Only addresses inside an executable section
are armed: the trap byte of a breakpoint must never land in data.

### Sessions and child processes

Every process is a debugger session of its own (`0:000>` becomes `1:004>`), and
a child process **does not inherit** the breakpoints of its parent. The tracer
therefore arms the plan again in every session, using the module base addresses
of that session, which are taken from the `ModLoad:` lines that precede the
prompt of the child. `cdb -o` makes the debugger trace the children at all, and
`cdb -G` makes it exit when the program ends.

The three traced modules are loaded before the initial breakpoint of a session,
so no breakpoint on a module load is needed.

### Recognising an idle debugger

The debugger prints one prompt per executed command, which makes the accounting
simple: while the program runs, the number of prompts equals the number of
commands which were fed, and a prompt beyond that means the debugger waits for
input. The tracer reacts to such a prompt by arming the session when it is a new
one, or by continuing the program when it is an unexpected stop (a first chance
exception, for example). The behaviour was measured with `cmd.exe` and a break
on a module load: seven commands produced eight prompts, exactly one more than
were fed.

### Scope classification

The categories are a table of explicit rules (`tracer/ScopePatterns.cpp`), not a
substring match: `reg` would match `EtwEventRegister`, `connect` would match
`DbgUiConnectToDbg`, `directory` would match `NtCreateDirectoryObject` and `key`
would match the synchronization object `NtCreateKeyedEvent`. The rules are:

| Category | Rules |
| --- | --- |
| file | `Nt`/`Zw` entry points whose name carries `File`, `Directory`, `Volume`, `Section`, `Symlink`, `Reparse`, `Stream`, `Mapped` or `Mapping`; the documented hooks without such a keyword (`NtClose`, `NtQueryInformationByName`); the path helpers of `ntdll` (`RtlDosPathName*`, `RtlGetFullPathName*`, `RtlIsDosDeviceName*`); the Win32 wrappers (`CreateFile*`, `ReadFile*`, `FindFirstFile*`, `CreateFileMapping*`, `MapViewOfFile*`, `DeviceIoControl`, ...). |
| registry | `Nt`/`Zw` entry points with `Key` or `ValueKey` in their name (excluding `KeyedEvent`), `Reg` followed by an uppercase letter (`RegOpenKeyExW`, but not `RegisterWaitForSingleObject`), `Rtl*Registry*`, and `NtQueryObject` (the name translation of the registry isolation). |
| network | `Nt`/`Zw` entry points with `NamedPipe`, `Mailslot`, `DeviceIoControl` or `FsControl` in their name, the named pipe and mailslot wrappers (`CreateNamedPipe*`, `ConnectNamedPipe`, `PeekNamedPipe`, `CreateMailslot*`, ...), and name resolution (`*AddrInfo*`, `*NameInfo*`). |

A name can belong to more than one category: `NtDeviceIoControlFile` is the file
I/O entry point and the way socket requests reach the kernel, so it is a file
and a network function at the same time.

`socket`, `send` and `recv` are not part of the scope because they live in
`ws2_32.dll`; a program which uses the socket library still shows up through
`ntdll!NtDeviceIoControlFile` and `ntdll!NtCreateFile`, which are the calls the
socket library makes.

## Measured cost

Measured on Windows 10.0.22621 with the debugger 10.0.28000.2705:

| Scope | Breakpoints | Names | Arming |
| --- | --- | --- | --- |
| `file, registry, network` (default) | 515 | 604 | about 0.2 s |
| every executable export (`--all-exports`) | 5239 | 5831 | more than 80 s |

| Run | Duration | Result |
| --- | --- | --- |
| `cmd.exe /c echo hi` | about 2 s | 1 process, 84 functions |
| `cmd.exe /c cmd.exe /c echo child` | about 4.5 s | 2 processes, both armed with 515 breakpoints |

Arming breakpoints is super-linear in the debugger (1000 breakpoints take
0.56 s, 3000 take 17.3 s), which is why the default scope is the category set
and why `--all-exports` warns about its cost before the run starts.

## Report

```
AppBoxTracer report
  program     : C:\Windows\SYSTEM32\cmd.exe
  debugger    : C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe
  scope       : file, registry, network
  processes   : 1
  breakpoints : 515 per process
  result      : completed

kernel32.dll (7 functions)
  kernel32!CreateFileW
  ...
```

`result` is `completed` or `aborted: <reason>`; an aborted run still reports
what was collected, so a timeout or an interrupted run never loses its result.

## Known limitations

1. **The process start is not traced.** The debugger stops the program at its
   initial breakpoint, which is reached after the loader mapped the images of
   the process. File and section calls of that phase are not observed; the
   traced window starts at the entry break.
2. **No call counts and no call sites.** A one-shot breakpoint reports that a
   function was used, not how often or from where.
3. **Calls which never reach an armed address are invisible.** A function which
   the loader bound to a different address, and code which is inlined into its
   caller, are not seen.
4. **The exhaustive scope is slow.** `--all-exports` arms 5239 addresses, which
   takes more than 80 seconds before the program even starts.
5. **A debugger is required.** The tracer needs `cdb.exe` of the Debugging Tools
   for Windows (`--cdb` or the default search). No other debugger is supported.
6. **The scope is a table.** A function which is related to an isolation domain
   but does not match a rule is not reported; `--list-scope --with-categories`
   shows what a run covers, and the table is a single file
   (`tracer/ScopePatterns.cpp`).
7. **The categories describe entry points, not semantics.** A call which ends up
   in `NtQueryInformationFile` is reported as a file function even when it
   queries something else, because the entry point is what an isolation layer
   has to intercept.
8. **A run which reaches `--timeout` is aborted.** A program which runs longer
   than the limit (an interactive program, for example) has to be given a larger
   `--timeout`.

## Tests

* `test/unit/Unit_Tracer*.cpp` — the parser, the PE reader, the scope rules, the
  breakpoint plan, the report and the command line, all without a debugger.
* `test/unit/Unit_TracerIntegration.cpp` — a real run of `cmd.exe` below the real
  debugger, including a child process; it skips itself when `cdb.exe` is not
  installed.

```
ctest --test-dir build/Debug -C Debug --output-on-failure
```
