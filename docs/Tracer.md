# Tracer

`AppBoxTracer.exe` runs a program and reports which **lowest level entry points**
of the filesystem, the registry and the network the program **actually used**,
including the calls of its child processes. It answers the question "which of the
entry points an isolation layer has to intercept does this program really call?".

The scope is the set of entry points of the three isolation domains, and it is
**independent of the hooks the sandbox implements**: the sandbox may cover a part
of the set, the tracer still reports the whole domain. That is why the scope
holds the native entry points (`ntdll!NtOpenFile`) and not the Win32 wrappers
(`kernel32!CreateFileW`): the tracer is a debugging tool for the domains, not a
mirror of one implementation of them. The wrappers are reachable through
`--all-exports`.

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
| `--all-exports` | Trace every executable export of the traced modules instead of the categories, which includes the Win32 wrappers. Much slower. |
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
# Which entry points of the three domains does cmd.exe use, including its child?
AppBoxTracer --output cmd.txt cmd.exe /c cmd.exe /c echo child

# Review the scope before a run, with the categories of every function.
AppBoxTracer --list-scope --with-categories --output scope.txt cmd.exe

# Only the registry domain.
AppBoxTracer --categories registry --with-categories notepad.exe

# Which name resolution entry points does a lookup use? The DNS client is loaded
# on demand, so its breakpoints are armed when the loader maps it.
AppBoxTracer --categories network --with-categories --output dns.txt ping.exe -n 1 localhost

# Trace a 32 bit program: the WOW64 copies of the modules are used.
AppBoxTracer --timeout 120 "C:\Program Files (x86)\app\app.exe"
```

## How it works

The tracer drives `cdb.exe` as an interactive debugging engine: it feeds a
breakpoint script through the standard input of the debugger and decodes the
events of its output.

### Address based breakpoints

A breakpoint is placed on the absolute address of a function:

```
bp /1 0x7ffc4259ae30 ".echo APPBOXHIT ntdll!NtCreateFile; g"
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
* `ws2_32!WSARecv` forwards to `mswsock!WSARecv`.

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

The base addresses are kept per session: a module which a child process loads
must not be armed in the address space of its parent.

### Modules which are loaded on demand

`ws2_32` and `dnsapi` are loaded when a program uses them, which is after the
initial break of a process, so their breakpoints can not be armed at the first
prompt of a session. The tracer therefore installs a module load filter of the
debugger for every module of the plan which is not mapped yet:

```
sxe ld:dnsapi
```

The debugger then reports the load, prints its `ModLoad:` line and waits for
input, which is the moment the tracer arms the breakpoints of that module and
continues the program. The filter is a property of the debugger rather than of
one process, so it stays in effect for the child processes as well.

`ntdll`, `kernel32` and `kernelbase` are always mapped before the initial break,
so a plain run needs no filter at all.

### Recognising an idle debugger

The debugger prints one prompt per executed command, which makes the accounting
simple: while the program runs, the number of prompts equals the number of
commands which were fed, and a prompt beyond that means the debugger waits for
input. The tracer reacts to such a prompt by arming what is still missing in that
session (a module which was just loaded), or by continuing the program when it is
an unexpected stop (a first chance exception, for example). The behaviour was
measured with `cmd.exe` and a break on a module load: seven commands produced
eight prompts, exactly one more than were fed.

### Scope classification

The scope is a table of explicit names (`tracer/ScopePatterns.cpp`), one list per
domain and module, and a name is only classified for the module which exports it.
A table is used instead of a name pattern because a pattern is far too broad: a
keyword match cannot tell a file section from an ALPC section
(`NtAlpcCreatePortSection`), a registry key from a synchronization object
(`NtCreateKeyedEvent`) or an object manager directory from a file directory
(`NtOpenDirectoryObject`).

| Category | Module | Entry points |
| --- | --- | --- |
| file | ntdll | The `Nt*` entry points whose object is a file, a section, a symbolic link, a device or a volume (`NtCreateFile`, `NtOpenFile`, `NtReadFile`, `NtWriteFile`, `NtQueryInformationFile`, `NtSetInformationFile`, `NtQueryDirectoryFile`, `NtCreateSection`, `NtMapViewOfSection`, `NtCreateSymbolicLinkObject`, `NtQueryVolumeInformationFile`, ...), plus `NtClose`, because a delete on close is committed there. |
| registry | ntdll | Every `Nt*` entry point which takes a key (`NtOpenKey`, `NtCreateKey`, `NtQueryValueKey`, `NtSetValueKey`, `NtEnumerateKey`, ...), the hive APIs (`NtLoadKey`, `NtSaveKey`, `NtRestoreKey`, `NtReplaceKey`, ...), the key transactions, and `NtQueryObject`, which the registry isolation uses to translate the name of a redirected handle back into a view path. |
| network | ntdll | `NtCreateNamedPipeFile`, `NtCreateMailslotFile`, `NtDeviceIoControlFile` and `NtFsControlFile`, which is how the socket requests reach the ancillary function driver. |
| network | ws2_32 | The name resolution entry points of the socket library: `getaddrinfo`, `GetAddrInfoW`, `GetAddrInfoExA`, `GetAddrInfoExW`, `gethostbyname`, `gethostbyaddr`, `gethostname`, `GetHostNameW`, `GetNameInfoW`, `WSAAsyncGetHostByName`, `WSAAsyncGetHostByAddr`, `WSALookupServiceBeginA`, `WSALookupServiceBeginW`, `WSALookupServiceNextA`, `WSALookupServiceNextW`, `WSALookupServiceEnd`. |
| network | dnsapi | `DnsQuery_A`, `DnsQuery_W`, `DnsQuery_UTF8`, `DnsQueryEx`, `DnsQueryExA`, `DnsQueryExW`, `DnsQueryExUTF8`, `DnsCancelQuery`, `DnsServiceResolve`, `DnsServiceResolveCancel`. |

Two entries belong to more than one category: `NtDeviceIoControlFile` is the file
I/O entry point and the way socket requests reach the kernel, so it is a file and
a network function at the same time.

The `Zw` alias of an entry point inherits the categories of its `Nt` name,
because both names share one address and one implementation.

Name resolution is the only part of a domain which reaches beyond ntdll: a DNS
query has **no NT entry point**, it is issued by user mode code, so the socket
library and the DNS client carry the lowest landing points a lookup can have.
`socket`, `send` and `recv` are not part of the scope, because they are not the
points an isolation layer can intercept; a program which uses the socket library
shows up through `ntdll!NtDeviceIoControlFile` and `ntdll!NtCreateFile`, which are
the calls the socket library makes.

Out of the scope are, deliberately:

* the Win32 wrappers (`CreateFileW`, `ReadFile`, `RegOpenKeyExW`,
  `CreateNamedPipeW`, ...) and the `Rtl*` path helpers (`RtlDosPathNameToNtPathName_U`),
  which are only reachable through `--all-exports`;
* the helpers of the resolver which only free, validate or configure something
  (`FreeAddrInfoW`, `DnsValidateName_W`, `DnsQueryConfig`), because they are not
  lookups;
* the entry points which only look related: the object manager namespace
  (`NtOpenDirectoryObject`), the ALPC sections (`NtAlpcCreatePortSection`), the
  keyed events (`NtCreateKeyedEvent`), the I/O completion ports and I/O rings
  (`NtCreateIoCompletion`, `NtCreateIoRing`), the storage partitions
  (`NtCreatePartition`) and the NLS sections (`NtGetNlsSectionPtr`).

A unit test loads the export tables of ntdll, ws2_32 and dnsapi and verifies
that every name of the table is an executable export of the module the table
assigns it to, which is what keeps the table honest.

## Measured cost

Measured on Windows 10.0.22621 with the debugger 10.0.28000.2705:

| Scope | Breakpoints | Names | Cost |
| --- | --- | --- | --- |
| `file, registry, network` (default) | 120 | 214 | `--list-scope` takes 0.08 s with warm files (1.9 s cold) |
| every executable export (`--all-exports`) | 5747 | 6564 | arming takes about 245 s before the program starts |

The default scope is 94 addresses in ntdll (188 names, the `Zw` alias included),
16 in `ws2_32` and 10 in `dnsapi`. Arming breakpoints is super-linear in the
debugger (1000 breakpoints take 0.56 s, 3000 take 17.3 s), which is why the
default scope is the category set and why `--all-exports` warns about its cost
before the run starts.

| Run | Duration | Result |
| --- | --- | --- |
| `cmd.exe /c echo hi` | about 0.7 s | 1 process, 94 breakpoints, 40 functions |
| `cmd.exe /c cmd.exe /c echo child` | about 1 s | 2 processes, both armed with 94 breakpoints, 40 functions |
| `ping.exe -n 1 localhost` | about 1.3 s | 1 process, 120 breakpoints (10 of them armed when the DNS client was loaded), 46 functions |

## Report

```
AppBoxTracer report
  program     : C:\Windows\SYSTEM32\ping.exe
  debugger    : C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe
  scope       : file, registry, network
  processes   : 1
  breakpoints : 120 per process
  result      : completed

dnsapi.dll (1 functions)
  dnsapi!DnsQueryEx

ntdll.dll (40 functions)
  ntdll!NtClose
  ntdll!NtCreateFile
  ntdll!NtCreateKey
  ...
```

`breakpoints` is the number of breakpoints the first process was armed with,
including the ones which were armed when a module was loaded later. `result` is
`completed` or `aborted: <reason>`; an aborted run still reports what was
collected, so a timeout or an interrupted run never loses its result.

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
4. **The Win32 wrappers are not traced by default.** A run reports
   `ntdll!NtCreateFile`, not `kernel32!CreateFileW`; the wrapper layer is only
   reachable through `--all-exports`.
5. **The exhaustive scope is slow.** `--all-exports` arms 5747 addresses, which
   takes about four minutes before the program even starts.
6. **A module which is loaded again is not armed again.** A session arms a module
   once; a module which is unloaded and mapped again at a different base address
   keeps its breakpoints only when the address is unchanged.
7. **A debugger is required.** The tracer needs `cdb.exe` of the Debugging Tools
   for Windows (`--cdb` or the default search). No other debugger is supported.
8. **The scope is a table.** An entry point which is related to a domain but is
   missing from the table is not reported; `--list-scope --with-categories` shows
   what a run covers, and the table is a single file
   (`tracer/ScopePatterns.cpp`).
9. **The categories describe entry points, not semantics.** A call which ends up
   in `NtQueryInformationFile` is reported as a file function even when it
   queries something else, because the entry point is what an isolation layer
   has to intercept.
10. **A run which reaches `--timeout` is aborted.** A program which runs longer
    than the limit (an interactive program, for example) has to be given a larger
    `--timeout`.

## Tests

The unit tests of the tracer are listed in
[test/README.md](../test/README.md).
