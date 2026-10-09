# Tests

The repository has one test executable, registered with CTest in
`test/CMakeLists.txt` under two entries:

| CTest entry | Mode | What it runs |
| --- | --- | --- |
| `AppBoxUnitTests` | `--mode=unit` | The in-process unit tests below `test/unit`. They do not start the launcher and do not inject the sandbox DLL, so they run on their own in about a minute. |
| `AppBoxTests` | `--mode=e2e` | The end-to-end cases below `test/e2e`. Every case owns a private working directory, writes the artifacts of the sandbox it runs and drives a probe process through the real launcher. |

Both entries run the same executable and only differ in the range of the run,
so a failure of one side is still reported on its own. `--mode=all` runs both
sides in one process; it is the default of a run which is started by hand.

Both configurations have to pass: Debug while a change is developed, Release
before it is finished.

## Running the tests

```bash
# From the repository root, with the build directory of the configuration:
ctest --test-dir build/Debug -C Debug --output-on-failure
ctest --test-dir build/Release -C Release --output-on-failure

# A single entry:
ctest --test-dir build/Debug -C Debug -R AppBoxUnitTests --output-on-failure
```

The presets of `CMakePresets.json` wrap the same steps:
`cmake --workflow --preset Debug` configures, builds and tests the Debug
configuration, whose workflow carries no package step, while
`ctest --preset Debug` runs the tests alone. The test presets set
`APPBOX_TEST_LOG_LEVEL=trace`, stop at the first failure and fail when no test
is registered at all.

CTest passes `--launcher=$<TARGET_FILE:AppBoxLauncher>` to both entries, the two
injection modules the build installed below `resource/lib` and the packer
executable `$<TARGET_FILE:AppBox>`.

A test executable which is started by hand needs those arguments as well:
without the launcher the end-to-end cases cannot start it, without the modules the
cases skip themselves (see `appbox::test::CommonFixture::SetUp()`), and without
the packer the unit test of the embedded resources skips itself.

Two end-to-end cases need the `SeBackupPrivilege` of the process, because
`RegSaveKeyW` / `RegSaveKeyExW` reach `NtSaveKey` / `NtSaveKeyEx`, which the
kernel only serves for a token which holds it:
`test/e2e/Reg_SaveKey_MergedSnapshot.cpp` skips both of its cases with a
message when the run does not hold the privilege, so an unelevated run stays
green (an elevated run verifies the save).

```bash
build/Debug/test/Debug/AppBoxTests.exe --mode=e2e \
    --launcher=<absolute path of AppBoxLauncher.exe> \
    --sandbox32=<absolute path of AppBoxSandbox32.dll> \
    --sandbox64=<absolute path of AppBoxSandbox64.dll> \
    --packer=<absolute path of AppBox.exe>
```

The executable accepts these options; every one of them has an environment
variable counterpart:

| Option | Environment variable | Meaning |
| --- | --- | --- |
| `--launcher` | `APPBOX_TEST_LAUNCHER` | Path of the launcher executable the cases start. |
| `--sandbox32` | `APPBOX_TEST_SANDBOX32` | Path of the 32 bit sandbox injection module the cases put into the resource root of their directory. |
| `--sandbox64` | `APPBOX_TEST_SANDBOX64` | Path of the 64 bit sandbox injection module. |
| `--packer` | `APPBOX_TEST_PACKER` | Path of the packer executable, which carries the payloads as resources. |
| `--log-level` | `APPBOX_TEST_LOG_LEVEL` | Level of the launcher and of the sandboxed processes of a case: `trace`, `debug`, `info`, `warn`, `err`, `critical` or `off`; default `info`. The test executable itself stays silent and prints its messages only when a case fails. |
| `--mode` | `APPBOX_TEST_MODE` | `all`, `unit` or `e2e`; default `all`. |
| `--no-cleanup` | `APPBOX_TEST_NO_CLEANUP` | Keep the working directory of a case instead of removing it. |
| `--test-timeout` | `APPBOX_TEST_TIMEOUT` | Timeout of one test case, in seconds; `0` turns the watchdog off. Default `300`. |
| `--test-dump-dir` | `APPBOX_TEST_DUMP_DIR` | Directory of the coredumps of a timed out test case; default a `coredump` directory below the test executable. |

The environment is read before the command line, so an option wins over a
variable of the same name.

### Modes and the GoogleTest filter

The name of a suite carries the mode it is run in: a unit test suite starts with
`Unit_` and an end-to-end suite with `E2E_` (`test/Test.hpp`). A mode is applied
as a GoogleTest filter, so a run of one side stays a single process:

* `--mode=unit` runs the suites whose name starts with `Unit_`.
* `--mode=e2e` runs the suites whose name starts with `E2E_`.
* `--mode=all` leaves the filter of the command line untouched.

The mode is the range of a run and `--gtest_filter` only narrows that range: the
suites of the other mode are appended to the negative part of the filter, which
GoogleTest applies with precedence over the positive part. A run therefore never
leaves its mode, whatever the filter names, while a filter which names the mode
keeps its usual meaning:

```bash
# One case of the end-to-end mode:
AppBoxTests.exe --mode=e2e --gtest_filter=E2E_Reg.Full_* --launcher=<launcher path>

# Every unit test but the cases of one suite:
AppBoxTests.exe --mode=unit --gtest_filter=*-Unit_AboutInfo.* --launcher=<launcher path>
```

Every other switch of GoogleTest keeps its usual meaning as well, so
`--gtest_list_tests`, `--gtest_repeat`, `--gtest_shuffle`, `--gtest_random_seed`
and `--gtest_output` can be combined with a mode.

The run refuses a suite whose prefix does not match the directory of its file: a
suite below `test/e2e` has to start with `E2E_`, a suite below `test/unit` with
`Unit_`, and a suite below any other directory stops the run. A case which was
put in the wrong directory or which was spelled without its prefix is therefore
reported before the first test case instead of being skipped silently.

## Timeouts and coredumps

Every test case has a timeout. A case which runs longer than the timeout is
stopped, and the run writes a coredump of the test process and of its child
processes first, so a debugger can show what the case was waiting for.

| Item | Value |
| --- | --- |
| Default timeout | 300 seconds per test case |
| Exit code of a run which was stopped | `124`, the convention of the `timeout` utility of GNU |
| Directory of the dumps | `--test-dump-dir` / `APPBOX_TEST_DUMP_DIR`, default `<test executable>\coredump` |

A case whose own budget is longer than the budget of the run, like the cases of
`Unit_TracerIntegration`, asks for it from the test body with
`appbox::test::SetTestTimeout(seconds)`.

The watchdog is a listener of the GoogleTest events `OnTestStart` and
`OnTestEnd` plus one thread which waits for the deadline of the running case
(`test/utils/TestTimeout.*`). It is armed between those two events only, so a
run whose cases finish in time pays nothing for it.

A case which times out ends the run:

1. The watchdog starts the coredump writer, which is the test executable
   itself (`test/utils/Coredump.*`). `MiniDumpWriteDump` suspends every thread
   of the process it dumps, so it must not run inside that process.
2. The writer takes one snapshot of the process list, walks the tree below the
   test process - the test process, the launcher, the sandbox host and the probe
   of an end-to-end case - and writes one full memory dump per process.
3. The watchdog terminates the child processes of the test and then the test
   process itself with the exit code `124`, so CTest reports the failure at
   once instead of waiting for a run which will not end.

A dump holds the full memory, the thread information, the handle table and the
unloaded modules. Its name carries the test case, the process id, the name of
the image and the time of the dump:

```
coredump/E2E_E2E_Reg.WriteValue_NewKey-66956-AppBoxTests-20260924-124051.dmp
```

`cdb` opens a dump and shows the stacks of every thread of it:

```bash
"C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe" -z <dump> -c "~*k; q"
```

The limits which are worth knowing:

* The watchdog is armed between `OnTestStart` and `OnTestEnd` only. A hang
  outside of a test case, in a global environment or in a static initializer,
  is left to the timeout of CTest itself.
* A stopped run does not run the destructors of its fixture and does not flush
  the buffered output of GoogleTest, so the working directory of the case and
  the dumps stay behind and the log is incomplete. The messages of the watchdog
  and of the writer go to the standard error stream and are flushed, so they
  are always part of the log.
* The dumps of an end-to-end case reach several hundred megabytes, because
  every process of the probe chain is dumped with its full memory.
* The dump of a process which is still starting up fails; the writer tries
  three times before it reports the failure, and the dumps of the other
  processes of the tree are written either way.
* The watchdog, the dumps of a timed out case and the termination of a process
  tree have no automated coverage, because the test suite does not test itself;
  they are verified by hand.

## Logs of a case

Every case runs in a working directory of its own, and a case which fails keeps
it: the fixture of the cases asks for the cleanup only when the case passed
(`appbox::test::CommonWD::NoCleanup`). The directory holds the configuration of
the run and the logs of every process the run started.

| File | Writer | Content |
| --- | --- | --- |
| `log.txt` | the launcher | The messages of the launcher itself: the configuration it loaded, the layers it mapped, the processes it started and their exit codes. |
| `<program>.<time utc>.<pid>.log` | one sandboxed process | The messages of one process the sandbox is injected into, in the order that process produced them. |

The launcher does not carry the messages of the sandbox: every process the sandbox
is injected into writes a log file of its own, named after the program, the UTC
time it started at and its process id. A run of one end-to-end case therefore
leaves one file per process — the launcher and the probe of a case, plus the
processes a case starts itself — and no two processes ever write the same file.
The file is written by the process which produces it, so its content survives a
crash of that process, and the name tells which process it belongs to:

```
AppBoxLauncher.20260930T021157Z.76696.log
AppBoxTests.20260930T021157Z.77784.log
```

The level of the messages is the level of the run (`--log-level`), and it is the
level of the launcher and of every sandboxed process at once. `trace` reports
every kernel call the sandbox intercepts, so a run which is started with
`APPBOX_TEST_LOG_LEVEL=trace`, like the presets of `CMakePresets.json` do,
writes the whole path of the case into the logs of its processes.

### The output of the run itself

The messages the test executable logs — the pipe server which carries the
probes of a case, and the helpers of a case — are not printed while the run
works. They are held in a backtrace of `spdlog` (`test/utils/TestLog.*`) whose
buffer is reset when a case starts, and the buffer is written out only when a
case fails, or when a case times out and the watchdog ends the run. A passing
run therefore shows the GoogleTest lines and nothing else, and the program log
of the case which failed appears between them.

`--log-level` is the level of the log files of a case, not of the test
executable: see [Logs of a case](#logs-of-a-case).

### The report of a crash

The sandbox installs a handler of fatal exceptions into every process it is
injected into (`sandbox/utils/CrashReport.*`). The handler writes the exception,
the registers, the parameters of the exception and the stack of the process into
the log file of that process, and every address is named with the module it
belongs to, so a report can be read without a debugger:

```
=== crash === first chance
process=77784 thread=70456 code=0xc0000005 exception=0x7ff9f8010a40 flags=0x0
params=0x0,0xffffffffffffffff
rip=0x7ff9f8010a40 rsp=0x86948fd0f8 rbp=0x0 rax=0x543a7961646e6f4d ...
stack=32
  0x7ff9cdd4b67f sandbox64.dll+0x5b67f
  0x7ff9f8010a40 MSVCP140.dll+0x60a40
  ...
=== end crash ===
```

The handler also reports the modules of the process with their base addresses
once, when it is installed, so `module+offset` of a report can be resolved
against the modules of the build of the run. A module of the build has its
debug information next to it, and `cdb` resolves an offset of a module without a
running process:

```bash
"C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe" -z <dump of a process> -c "ln <module>+0x<offset>; q"
```

A run which fails names the log files it left behind, so the file of the process
which died is the first one to open:

```
[error] log of the run: AppBoxLauncher.20260930T021157Z.76696.log
[error] log of the run: AppBoxTests.20260930T021157Z.77784.log
[error] Probe exited with code 3221225477
```

## Layout

| Path | Content |
| --- | --- |
| `test/unit/` | The in-process unit tests, one file per module (`<Module>.cpp`); the suite of a file is named `Unit_<Module>`. |
| `test/e2e/` | The end-to-end cases, one file per case (`<Domain>_<Case>.cpp`); the suite of a file is named `E2E_<Domain>`. |
| `test/probe/` | The operations which are executed **inside** the sandbox. A probe registers itself by name (`test/probe/__init__.hpp`) and is called from a case through `ProbeCall`. |
| `test/utils/` | The builders and helpers which the cases share, see [Test helpers](#test-helpers). |
| `test/utils/ProbeCall.*` | The call of a probe: it starts the launcher for the case, serves the probe over the pipe and reports the logs of the processes of the run when the case fails. |
| `test/utils/TestTimeout.*` | The timeout of a test case: the GoogleTest hook, the watchdog thread and the options of a run. |
| `test/utils/TestLog.*` | The backtrace of the program log of a run: it silences the messages of the test executable, keeps them in a `spdlog` backtrace and dumps them when a case fails or times out. |
| `test/utils/Coredump.*` | The coredump writer: the full memory dump of a process, the walk of a process tree and the termination of its processes. |
| `test/utils/CommandLine.*` | The command line and the environment of a run, which the coredump writer reads before GoogleTest and CLI11 look at them. |
| `test/utils/LauncherPath.hpp` | The launcher path of a run: `LauncherPath()` answers the `--launcher` value of the configuration. |
| `test/utils/SandboxDll.hpp` | The injection modules of a run: `Sandbox32DllPath()` and `Sandbox64DllPath()` answer the `--sandbox32` and `--sandbox64` values, and `SandboxModulesAvailable()` tells whether the run can start the launcher at all. |
| `test/Test.cpp` / `test/Test.hpp` | The configuration of the run: `--launcher`, `--sandbox32`, `--sandbox64`, `--packer`, `--log-level`, `--mode`, `--no-cleanup`, the timeout options, the prefixes which tell the mode of a suite, the guard which keeps the prefix and the directory of a suite together and the mode filter. |
| `test/main.cpp` | The entry point of the test executable. It serves both sides, and it also handles the coredump writer, the name resolution probe of the tracer and the `probe` subcommand the launcher starts. |

The source files of the executable are an explicit list in
`test/CMakeLists.txt`, not a glob, so a new test file has to be registered
there. The include directories put the project directories first, because
`test/utils` holds headers with the same name as the launcher ones and the launcher
headers have to win; the known folder helper of the cases is called
`TestKnownFolder` for that reason.

## Unit tests

The unit tests cover the modules the end-to-end cases cannot reach: the packer
(`src/core`), the launcher helpers, the modules of the sandbox which run in the
process of the test executable and the tracer. No case includes a header of
`src/core`, so the packer has no end-to-end coverage of its own; the only
contact of the end-to-end suite with it is the helper
`test/utils/PatchBuilder.*`, which builds a patch package with the `ZipWriter`
the packer writes its archives with.

A module whose execution path an end-to-end case already drives keeps no unit
test of its own. The suites which were dropped for that reason are the
isolation tables and decision tables of the three domains
(`Unit_RegistryEnumMerge`, `Unit_RegistryIsolationPolicy`,
`Unit_RegistryWhiteout`, `Unit_RegistryKeyPath`, `Unit_RegistryRootMap`,
`Unit_RegistryIsolation`, `Unit_FilesystemIsolationPolicy`,
`Unit_FilesystemIsolationTable`, `Unit_DnsTable`, `Unit_NetworkIsolation`) and
the base filesystem mapping (`Unit_LauncherPath`). The boundaries which matter are
covered by the end-to-end cases which were added with them, see
[End-to-end tests](#end-to-end-tests).

The build information the About dialog shows:

* `test/unit/AboutInfo.cpp` — the values which are compiled into the binary
  by `cmake/GenerateAboutInfo.cmake`: the one sentence summary of the
  application, the dotted project version, the timestamp of the build, the git
  revision and branch (including the degradation to `unknown` of a build which
  was made without git) and the list of the linked third-party libraries with
  their versions and their order.

The unit tests of the packer:

* `test/unit/FilesystemIsolation.cpp` — the vocabulary of the filesystem
  isolation (the tokens and the display names of the modes, the modes an entry
  kind accepts, the default of the view, which is `Merge` and which a file
  cannot hold, the fold of `Write Copy` to `Full` for a file) and the model of
  the packer: the path helpers of the virtual filesystem, the explicit mode of
  an entry, the inheritance of the closest folder above an entry, the mode of
  the root of the view for the paths no entry covers, the call which sets that
  mode while it pins the layer roots of the view to the mode they show, the
  recursion which overwrites the folders below a folder, the removal of a
  subtree and the order of the entries. It also pins the isolation
  file the packer writes, including the root entry without a path and the round
  trip through the table of the sandbox, so the two sides of the schema cannot
  drift apart. The table itself is pinned as well: the files of the layers of a
  run are applied in order, so the file of a later layer overrides the mode of
  the path it names while a path it does not name keeps the mode of the layers
  below it, the root entry of a document answers for every path no other entry
  names, a document without a root entry answers no such path at all, and a
  document which cannot be used leaves the layers below it alone.
* `test/unit/ProjectDocument.cpp` — the document of a project file: the
  round trip of every member of the schema, the version and the order of the
  written members, the paths as UTF-8 bytes, the members an entry needs, the
  path of a rejected entry, the unknown isolation, kind, value type, proxy,
  project type and environment tokens, malformed value data, the
  `startup_files` array of the startup files, the `environment` array of the
  environment variables, which is always written, the optional `proxy` member,
  which is omitted while it is absent and read back as absent, the optional
  `project_type` member, which is written for every project type and read as
  `standalone` while it is absent, the optional `metadata` member, which names
  the program the file properties of the launcher are inherited from and the
  fields the user edited, refuses a key which is not a field of a version
  resource and a field which is listed twice, and is read as empty while it is
  absent, and the atomic read.
* `test/unit/ProjectFile.cpp` — the file layer of a project file: the
  round trip of the configuration, of the virtual registry, of the
  filesystem isolation modes, of the proxy of the network workspace, of the
  environment variables of the environment workspace, of the project type and of
  the file properties the launcher carries,
  the strict UTF-8 encoding, the failures of a malformed document and the
  atomicity of applying a document to the models, including a mode which a file
  cannot hold, a path which is listed twice, a proxy the model refuses, a
  variable the model refuses and an unknown project type.
* `test/unit/ProjectType.cpp` — the vocabulary of the `Project Type` box: the
  token of every project type, which is the text form the project file stores,
  the token which is parsed whatever the case it was written with, the tokens
  which are refused, the label of every entry of the box, and the mapping
  between the entries of the box and the enumeration, including the fallback of
  an index which is outside of the box.
* `test/unit/EmbeddedResource.cpp` — the payloads the packer carries as RCDATA
  resources of its own executable: the launcher program and the two sandbox
  injection modules are read from the executable opened as a data file and
  compared byte for byte with the files of the build tree they were embedded
  from, and a resource which an executable does not carry is reported instead of
  being handed out as an empty payload. The suite is the only automated coverage
  of the resource script of the build.
* `test/unit/PackService.cpp` — the archive carries the hive, the isolation
  file of the registry, the isolation file of the filesystem workspace, the
  isolation file of the network workspace, the isolation file of the
  environment workspace and the two sandbox injection modules below `app`, and
  the launcher carries the file properties of the program the metadata of the
  session inherits from with the fields the user edited applied on top of them;
  the patch package carries the very same resources
  rooted at the archive root, without the launcher, without the injection modules
  and without an `app`
  directory, and needs neither a startup file nor the payloads, which is
  what the patch cases of the suite pin: the root layout, the layer tree, the
  registry artifacts, the imported files, the progress total, the cancellation
  and the fact that the archive relative spelling of an entry is the `app`
  prefix plus its resource relative spelling.
* `test/unit/PackModel.cpp` — the editable model of a packer session: the
  import of a host folder and of individual files, the rules of the import
  names and of the executables, the startup file list with its triggers, the
  restoration of a project file without touching the host filesystem, and the
  host folder a folder of the filesystem view maps to (the text the tooltip of
  the tree shows).
* `test/unit/RegistryModel.cpp` — the model of the packer, including the
  mode of a single row and the explicit recursion of the isolation dialog.
* `test/unit/RegistryHive.cpp` — the hive writer (all seven value types,
  several roots, nested keys, replacement of an existing hive, failure), the
  isolation file writer and the layer order of the isolation files of a run: a
  later file overrides the entry it names while an entry it does not name keeps
  the mode of the layers below it, and a document which cannot be parsed leaves
  those layers alone.
* `test/unit/RegFile.cpp` — the `.reg` parser and the merge of a file into
  the model.
* `test/unit/NetworkModel.cpp` — the model of the network workspace: the
  insertion order of the DNS redirections, the uniqueness of a hostname
  (ignoring the case and a trailing dot), the rejection of an empty field, of a
  field which carries a whitespace character, of a redirect which is not an
  IPv4 or an IPv6 address literal and of an index outside the model, the in
  place replacement of an entry, and the fact that a refused call leaves the
  model untouched. The proxy of the workspace is pinned as well: the two
  protocols are the switch of the configuration while the server, the port and
  the credentials stay in the model either way, the port has to be a decimal
  number between 1 and 65535 without a leading zero, a protocol may be enabled
  only with a server and a port, the credentials are free text, the type token
  round trips, and a refused configuration leaves the proxy and the
  redirections of the model untouched.
* `test/unit/NetworkIsolationFile.cpp` — the isolation file the packer
  writes for the network workspace: the schema of an empty model, the order and
  the content of the entries and the hostname as UTF-8 bytes.
* `test/unit/EnvironmentModel.cpp` — the model of the environment workspace:
  the insertion order of the variables, the rejection of an empty name, of a
  name which carries an equals sign and of a name which is listed twice
  (ignoring the case), the in place replacement of an entry, the removal, the
  fill of the search path variable with the merge mode `Prepend` and the
  separator `;` whatever the case of its name is, the fact that the model stores
  the mode it is given - so a mode the user picked by hand survives an import -
  and the display names and the tokens of the isolation and merge modes.
* `test/unit/EnvironmentIsolation.cpp` — the vocabulary which the packer and
  the sandbox share: the composition of a value of the host with the value of a
  row for every isolation mode and every merge mode, a variable the host does
  not hold, a value which is empty, and the schema of the isolation file and of
  the state file.
* `test/unit/EnvironmentIsolationFile.cpp` — the isolation file the packer
  writes for the environment workspace: the schema of an empty model, the order
  and the members of the entries, the mode the model was given, the name and the
  value as UTF-8 bytes and the refusal of an entry the model refuses.

The unit tests of the launcher and of the sandbox modules which the test
executable carries itself:

* `test/unit/HiveMerge.cpp` — the merge of a hive into another, which is the
  launcher side of the registry domain of a patch layer: a target which does not
  exist yet is created, the entries of the source override the entries of the
  same name while the other entries of the target stay, every value type and
  the default value of a key are copied, nested keys are merged, an empty
  source keeps the target, a source which does not exist is refused without
  creating a file, and the whiteout store of the target survives the merge.
* `test/unit/ExpandKnownFolder.cpp` — the expansion of a `#Name#` layer
  key of a path, including a path which carries no key, the rejection of the
  historical `%Name%` delimiter and the set of known layer keys: a key is
  matched exactly as the table spells it, so only the keys the packer produces
  are known and a mapping which no preset directory uses is removed from the
  table. It pins the variables the sandbox expands in the
  values of the workspace as well: one per known folder, named after the layer
  key without its `#` delimiters, with the real path of the folder of this
  machine.
* `test/unit/GetExecutableDir.cpp`, `test/unit/ProcessLauncher.cpp`,
  `test/unit/ProcessJob.cpp`, `test/unit/Shell.cpp` — the launcher helpers which
  locate the executable, start it, own the job of a started process and resolve
  the shell of the host together with the arguments of the command it runs. The
  resolver falls back from `%COMSPEC%` to the command processor of the system
  root and refuses a value which names a folder; the command is run through
  `/c` unless it is empty, which runs the shell interactively.
* `test/unit/Md5.cpp` — the digest of a patch package: the digest of an empty
  file, of the text `abc` and of one million characters, which crosses the
  blocks the file is read in, plus the failure of a missing file. The digest of
  a package decides whether the cache entry of the package is still usable,
  which the end-to-end cases drive but cannot pin to a known digest, because
  the bytes of a zip archive are not stable.
* `test/unit/ModuleTable.cpp`, `test/unit/HookTransaction.cpp` — the
  module table of the sandbox and the transaction which attaches and detaches
  its hooks.
* `test/unit/EnvironmentTable.cpp` — the private table of the environment
  isolation and the two documents the sandbox reads: the block of the host with
  its drive relative current directory, the names which are compared ignoring
  the case, the order of the table, the round trip of a block, the block of the
  ANSI code page, the refusal of a malformed isolation file without a partial
  result, and the state with its one modification per variable.
* `test/unit/VariableExpansion.cpp` — the `%APPBOX:<NAME>%` expansion the
  sandbox applies to the values of the workspace: a known reference, the case
  insensitive prefix and name, several references of one text, a name which is
  not listed, a `%NAME%` reference of the shell, a reference without a closing
  `%`, a lone `%` and `%%`, an empty text and an empty table, and the fact that
  an expansion is never resolved again. The registry side pins the three string
  types (`REG_SZ` with and without its terminator, `REG_EXPAND_SZ` and
  `REG_MULTI_SZ` with an empty item), that every other type keeps its data, and
  that a blob which is not made of whole wide characters is left alone.
* `test/unit/IsolationDocument.cpp` — the schema of the five documents of
  `common/` (the four isolation files and the environment state file): the
  round trip of a document through its text, the refusal of a member which is
  missing or of another type, of an unknown mode, of a root entry which is not a
  folder, of an empty hostname, of a mode a kind cannot hold and of a name which
  carries an equals sign, with the error text of every refusal, the lenient
  reading of the network proxy, and the two documents the packer writes which
  the sandbox reads back (network and environment).
* `test/unit/Log.cpp`, `test/unit/LogFile.cpp`, `test/unit/PipeClient.cpp` — the
  hook robustness contract: never read more than the caller declared, never
  throw. The abort sentinel of the parameter parsers is part of it
  (`Unit_Log.LoggerAbortsWhenAParameterParserThrows`). The level of a run and
  the log file of a process are covered as well: the name of the file, the
  directory which is created for it, the level which suppresses a message, and
  the report a fatal exception leaves in the file of the process which raised
  it (`Unit_LogFile.CrashReportNamesTheExceptionOfTheProcess`).
* `test/unit/RemoteClient.cpp`, `test/unit/RpcCodec.cpp` — the RPC
  layer which carries the requests of the probes.
* `test/unit/WString.cpp` — the UTF-8 and UTF-16 conversions of `common/`.
* `test/unit/ZipReader.cpp`, `test/unit/ZipWriter.cpp` — the zip
  layer of the packer, including the zip slip protection of an extraction.
* `test/unit/BuildReport.cpp` — the progress and the result texts of the
  build report.
* `test/unit/ApplicationIcon.cpp` — the icon the packer writes into the first
  startup file of an archive, including the round trip through the real launcher
  payload.
* `test/unit/ApplicationMetadata.cpp` — the file properties the packer writes
  into the launcher of an archive: the fields of a version resource and their
  labels, the lookup and the merge of a field list, the overrides a session
  derives from the values it shows, the program the information is inherited
  from by default (the first program marked for auto start, in ascending order
  of its executable file name), the read of a program without version
  information, and the round trip of the information through a payload,
  including the replacement of a resource a payload already carries and the
  round trip through the real launcher payload.
* `test/unit/StartupTree.cpp` — the startup file tree of the packer: the rows
  of the imports, the default trigger of a file, the uniqueness of a trigger
  and the auto start flag.

The independence of the products from the runtime library of the toolchain has
a suite of its own:

* `test/unit/StaticRuntime.cpp` — the import table of the four products of the
  build (the packer, the launcher and the two sandbox injection modules) and of
  the test executable itself: the reader parses the image of the file, so it
  works for the 32 bit and the 64 bit products alike, and no table may name a
  module of the Visual C++ runtime (`VCRUNTIME`, `MSVCP`, `CONCRT`, `VCCORLIB`,
  `ucrtbase` and the `api-ms-win-crt-*` API sets). The suite pins the promise of
  `cmake/StaticRuntime.cmake`, so a dependency which is linked with the DLL form
  of the runtime is reported instead of being noticed on a machine which carries
  no redistributable. The reader is only trusted while it reports the modules
  every product imports (`kernel32.dll` or `ntdll.dll` among them), so a reader
  which failed silently cannot pass the check with an empty table.

The timeout and the coredumps of a run have a unit test of their own:

* `test/unit/TestTimeout.cpp` — the helpers of the timeout and of the
  coredump writer: the scan of an option of the command line, the name of a
  dump, the timeout and the dump directory of the environment, the default
  directory of the dumps, and the request of the writer together with the
  command line it is built from. The suite holds no case which drives a test run
  of its own, so the watchdog, the dumps of a timed out case and the termination
  of a process tree have no automated coverage; they are verified by hand (see
  [Timeouts and coredumps](#timeouts-and-coredumps)).

## End-to-end tests

`AppBoxTests` runs every case through the real chain: the case writes its
`LauncherConfig`, `test/utils/ProbeCall.*` starts the **launcher** with
`--X-AppBox-ConfigFile`, the launcher launches the test binary as a probe process
with the sandbox DLL injected, and the probe asks the test process for its task
over a named pipe and reports the result back. One probe call costs about two
seconds, so a case which asks several questions of one sandbox asks them in one
call where the protocol allows it.

### Filesystem isolation cases

The filesystem cases (`test/e2e/Fs_*.cpp`) run with `data` as the state of the
sandbox and `app` as the read-only resources of the packaged application, which
is the layout of a packed archive (see
[Filesystem Isolation](../docs/FilesystemIsolation.md)). The resources carry
their content below `app\filesystem\<layer key>`, the state of the sandbox
below `data\filesystem\<drive>`. Each case is documented in its own header
comment.

| Case | State (`data`) | Resources (`app`) | Operation | Expected |
| --- | --- | --- | --- | --- |
| `DeleteFile_LowerLayer` | – | `data.txt` | delete `data.txt` | success, whiteout created in the state, no file in the state, resources unchanged |
| `DeleteFile_LowerLayerAndUpper` | `data.txt` | `data.txt` | delete `data.txt` | success, file of the state deleted, whiteout created |
| `DeleteFile_UpperOnly` | `data.txt` | – | delete `data.txt` | success, no whiteout (nothing to hide) |
| `DeleteFile_NonExists` | – | `data1.txt` | delete `data.txt` | failure, no whiteout |
| `DeleteFile_WhiteoutInLower_ExistsInUpper` | `data.txt` | `data.txt.$APPBOX_DELETE$` | delete `data.txt` | success, file of the state deleted, no whiteout in the state |
| `DeleteOnClose_LowerLayer` | – | `data.txt` | open `data.txt` with `CreateFileW` (`DELETE` and `FILE_FLAG_DELETE_ON_CLOSE`) and close the handle | success, the close records the delete: no file in the state, whiteout created, the view reports `File Not Found`, resources unchanged |
| `DeleteOnClose_MergeHost` | – | folder `Merge`, `packed.txt` | open the file `data.txt` of the host filesystem the same way | success, the file of the host filesystem is really removed, no whiteout in the state |
| `DeleteOnClose_UpperOnly` | `data.txt` | – | open `data.txt` the same way | success, file of the state deleted, no whiteout (nothing to hide) |
| `DeleteOnClose_WhiteoutInUpper` | `data.txt.$APPBOX_DELETE$` | `data.txt` | create `data.txt` again (`CREATE_NEW`) the same way | success, file of the state deleted, the whiteout is written again, the view reports `File Not Found` |
| `ListDir_CreateFileHandle` | folder with a whiteout marker for `hidden.txt` | folder with `visible.txt`, `hidden.txt` | enumerate the folder with a handle of `CreateFileW` and with a handle of `NtOpenFile`, through both NT entry points and with two information classes | both handles report the same merged view: the file of the lower layer and the file of the host, while the file the whiteout hides and the marker are not listed |
| `ListDir_InformationClasses` | folder with a whiteout marker for `hidden.txt` | folder with `visible.txt`, `hidden.txt` | enumerate the folder with both NT entry points and with every information class which carries a name | every class reports the same merged view: the file of the lower layer and the file of the host, while the file the whiteout hides and the marker are not listed |
| `ListDir_LowerLayer` | – | `F.txt` | list `#USERPROFILE#` | `F.txt` appears exactly once, host entries also listed |
| `ListDir_UnsupportedClass` | folder with a whiteout marker for `hidden.txt` | folder with `visible.txt`, `hidden.txt` | enumerate the folder with the information classes which carry no name of an entry (`FileObjectIdInformation`, `FileReparsePointInformation`), through both NT entry points and with both ways a directory handle reaches the sandbox | every call is refused by the view with `STATUS_NOT_SUPPORTED` and reports no entry, so the view never answers with the content of the single layer the handle was opened with |
| `ListDir_WhiteoutInUpper` | `F.txt.$APPBOX_DELETE$` | `F.txt`, `F2.txt` | list `#USERPROFILE#` | `F.txt` hidden, `F2.txt` listed once |
| `NewFile_WhiteoutInLower` | – | `data.txt.$APPBOX_DELETE$` | create `data.txt` (`CREATE_NEW`) | success, file created in the state |
| `NewFile_WhiteoutInUpper` | `data.txt.$APPBOX_DELETE$` | `data.txt` | create `data.txt` (`CREATE_NEW`) | success, whiteout removed, file created in the state |
| `ReadFile_WhiteoutInUpper` | `data.txt.$APPBOX_DELETE$` | `data.txt` | read `data.txt` | failure |
| `QueryAttributes_LowerLayer` | – | `data.txt` | query the attributes of `data.txt` | success, a regular file is reported |
| `QueryAttributes_NonExists` | – | `other.txt` | query the attributes of `data.txt` | failure with `File Not Found` |
| `QueryFullAttributes_LowerLayer` | – | `data.txt` | query the full attributes of `data.txt` with `NtQueryFullAttributesFile` | success, a regular file with the size of the packed content is reported |
| `QueryFullAttributes_NonExists` | – | `other.txt` | query the full attributes of a missing file and of a file below a missing folder | failure with `File Not Found` and `Path Not Found` |
| `QueryInformationByName_LowerLayer` | – | `data.txt` | query the information of `data.txt` with `NtQueryInformationByName` and `FileStatInformation` | success, a regular file with the size of the packed content is reported |
| `QueryInformationByName_NonExists` | – | `other.txt` | query the information of a missing file and of a file below a missing folder | failure with `File Not Found` and `Path Not Found` |

Four cases are not part of the matrices above:
`test/e2e/Fs_LaunchProcess_FromLower.cpp` starts an executable which only a
lower layer holds, `test/e2e/Fs_ListDir_UserPresetLayers.cpp` mounts one layer
per folder below `Current User Directory` (`#Documents#`, `#Desktop#`,
`#AppData#`, `#LocalAppData#`, `#LocalAppDataLow#`, `#Downloads#`,
`#Favorites#`, `#StartMenu#`, `#Programs#`, `#Startup#`) and checks that each of
them is mapped to the real folder its layer key names,
`test/e2e/Fs_QueryAttributes_SystemPresetLayers.cpp` does the same for the
system folders (`#Windows#`, `#System32#`), and
`test/e2e/Fs_QueryAttributes_ProgramPresetLayers.cpp` for the `Program Data`
folder of the system and the `Common` folder below `Program Files`
(`#ProgramData#`, `#ProgramFilesCommon#`). The last three cases include that the
file of one layer is not visible in the folder of the other one.

The cases which exercise the isolation modes of the workspace write the
isolation file of the case into the filesystem domain of the resources
(`test/utils/FsIsolationBuilder.*`, `app/filesystem/isolation.json`) and use a
folder below `#USERPROFILE#` of the host as the entry of the host layer
(`test/utils/RealFsFolder.*`, which removes it again when the case ends):

| Case | Isolation | Operation | Expected |
| --- | --- | --- | --- |
| `Full_HidesTheHostFolder` | folder `Full` | read the packed file and a host file, query a host folder, list the folder, create a file | the packed content is visible, the host entries report `File Not Found` and are not listed, the new file lands in the overlay |
| `Full_SubFolderWriteCopyShowsTheHost` | folder `Full`, folder below `Write Copy` | read the host files of both folders | the folder below shows the host content again, because the mode of a folder below overrides the folder above |
| `Whiteout_FileIsNotFound` | file `Whiteout` | read, query, delete and list | every call reports `File Not Found`, the host file and the packed file are unchanged |
| `Whiteout_CreateInSandbox` | file `Whiteout` | create the file, read it back, query it, list the folder | the create lands in the overlay, the entry is visible afterwards, the host file keeps its content |
| `Whiteout_FolderHidesItsSubtree` | folder `Whiteout` | query and read the folder, create it, read the packed file, create a file inside it | the folder and its packed content are hidden until the sandbox creates the folder, the created file lands in the overlay |
| `ListDir_IsolationFiltersTheEntries` | folder `Full`, file `Whiteout` | enumerate the folder with `std::filesystem`, `FindFirstFile`, `_findfirst` and both NT entry points | every enumeration reports the visible entry of the lower layer only; the hidden file, the host file and the host folder are not listed |
| `QueryFullAttributes_IsolationHidden` | folder `Full`, file `Whiteout` | query the full attributes of the packed file, of the host file and of the hidden packed file | the packed file of the visible folder is reported, while the host file and the hidden file report `File Not Found` |
| `QueryInformationByName_IsolationHidden` | folder `Full`, file `Whiteout` | query the information of the packed file, of the host file and of the hidden packed file | the packed file of the visible folder is reported, while the host file and the hidden file report `File Not Found` |
| `MalformedIsolationFile_FallsBack` | document which cannot be read | read a host file while the isolation file is not JSON, while it carries an unknown version and while it is the valid document which hides the folder | the host file stays visible for both refused documents and is hidden for the readable one, which is what makes the run a check of the fallback; the content of the lower layer is visible in every one of them |
| `WriteLowerLayerFile_CopyUp` | file of a lower layer only | open the file for writing and write another content into it | the open copies the file up into the overlay, the write and the read back report the new content, and the layer it was copied from keeps its own content |
| `Directory_CreateAndDelete` | layer `Write Copy`, folder of the host only | create a directory below the folder, query it, create a file inside it, remove the directory, remove the file, remove the directory again | the directory is created in the overlay and reported as a directory, the removal of a directory which still holds a visible entry reports `Directory Not Empty`, and the second removal succeeds and leaves neither the view nor the overlay with the entry; the host folder stays empty |
| `Merge_WritesTheFileOfTheHost` | folder `Merge`, host file and packed file | open the file of the host for writing | the write is applied to the host file, which carries the new content, the overlay holds no copy of it, and the packed file is not carried into the host |
| `Merge_KeepsThePackedFileInTheSandbox` | folder `Merge`, packed file which the host does not hold | open the packed file for writing | the file is copied up into the overlay and the copy carries the new content, while the host gains no entry and the lower layer keeps its own content |
| `Merge_CreatesTheFoldersOfANewFileInTheHost` | folder `Merge`, packed folder which the host does not hold | create `fresh\new.txt`, which no layer holds | the host gains the folder `fresh` and the file, the overlay holds neither of them, and the lower layer keeps its content |
| `Merge_DeletesTheFileOfTheHost` | folder `Merge`, host file which no layer holds | delete the file | the file is really removed from the host filesystem and the lower layer keeps its content |
| `Merge_DeletesThePackedFileInTheSandbox` | folder `Merge`, packed file which the host does not hold | delete the file | the whiteout marker lands in the overlay, while the host gains no entry and the lower layer keeps its content |
| `RootIsolation_MergeWritesTheFileOfTheHost` | root of the view `Merge`, host file, no entry names the folder | open the file of the host for writing | the mode of the root reaches the write, so the host file carries the new content |
| `DefaultMerge_CreatesTheEntriesInTheHost` | no entry names the folder, so the default of the view applies | create a directory and a file below a folder of the host | the directory and the file are created in the host filesystem, while the overlay holds neither of them |
| `DefaultMerge_WritesTheFileOfTheHost` | no entry names the file, so the default of the view applies | open the file of the host for writing | the write is applied to the file of the host filesystem, while the overlay holds no copy of it |
| `Rename_LowerLayer` | layer `Write Copy`, packed file | rename the packed file inside its folder | the new name is an entry of the overlay which carries the packed content, the old name is hidden by a whiteout marker, and the lower layer keeps its file |
| `Rename_MergeWritesTheHostFile` | folder `Merge`, host file | rename the host file inside its folder | the host carries the new name and not the old one any more, and the overlay holds neither of the two names |
| `Rename_TargetExists` | layer `Write Copy`, two packed files | rename one file onto the name of the other one, once without and once with the replace flag | the rename without the flag reports `Name Collision`, the one with it succeeds and leaves the new name in the overlay, the hidden source and the untouched lower layer |
| `Rename_IsolationHidden` | folder `Full`, host file which carries the name of the rename | rename the packed file onto the name of the host file | the rename succeeds because the view does not hold the name, the new entry is the file of the overlay, and the host file keeps its own content |
| `Rename_RelativeName` | layer `Write Copy`, two packed files | rename one file with a name relative to a directory the call names, and one with a name relative to the directory of the object | both renames succeed and resolve the name against the same base as the file system uses, so both new names are entries of the overlay and both sources are hidden |
| `Rename_Directory` | layer `Write Copy`, folder of the overlay and folder of the lower layer | rename the folder of the overlay and the folder of the read-only layer | the folder of the overlay carries its new name together with the file it holds, while the second rename fails and leaves both layers as they were, because the open of a directory cannot copy it up |
| `Rename_SameName` | layer `Write Copy`, packed file | rename the packed file onto the name it already has, once without and once with the replace flag | both renames succeed, as the file system reports for an object moved onto its own name, and the entry keeps its content and is not hidden by a marker |
| `Link_LowerLayer` | layer `Write Copy`, packed file | link the packed file to a second name of its folder | the link is an entry of the overlay which carries the packed content, while the source is neither carried into the overlay nor hidden, which is what tells a link from a rename |
| `QueryInformationFile_ViewPath` | layer `Write Copy`, packed file and host file | ask `FileNameInformation`, `FileNormalizedNameInformation` and `FileAllInformation` for the packed file, for the copy a write opened and for the host file | every class reports the name of the view for every handle, so the layout of the sandbox never reaches the application |
| `QueryInformationFile_SmallBuffer` | layer `Write Copy`, packed file | ask the three name classes with a buffer which holds the name of the view and not the one of the layer | every class reports the name of the view and succeeds, so a caller which sized its buffer after the view is never told that the buffer is too small |

### Registry isolation cases

* `test/e2e/Reg_WriteValue_NewKey.cpp` — the closed loop: the sandboxed
  probe creates a key, writes a value and reads it back; the test process
  verifies that the real HKCU does **not** contain the key and that the hive
  file exists in the state directory of the sandbox.
* `test/e2e/Reg_ReadValue_RealFallback.cpp` — a key and value which only
  exist in the real HKCU (created by the test process with RAII cleanup) are
  readable inside the sandbox, and the key still exists afterwards.
* `test/e2e/Reg_EnumerateKey_Merged.cpp` — the sandboxed probe enumerates
  the merged sub key view (real `RealA`/`RealB` plus sandbox `SandboxC`),
  `RegQueryInfoKeyW` reports the merged count, and the real registry still
  holds only the real sub keys.
* `test/e2e/Reg_EnumerateValue_Merged.cpp` — the sandboxed probe enumerates
  the merged value view and reads every enumerated value back (enumeration and
  query stay consistent); the real registry does not gain the sandbox value.
* `test/e2e/Reg_EnumerateValue_DefaultValue.cpp` — the default value of a key
  is an entry of the merged value enumeration like every other value: the
  enumeration of a hive key reports the default value and the named values
  exactly once each with their own data and the count agrees, the merged view
  of a shadow key lists the default value of the real key, and the default
  value of a name both layers hold is answered by the hive layer.
* `test/e2e/Reg_ShadowKey_HidesRealEntriesOfSameName.cpp` — a shadow key
  hides the value and the sub key of the real registry which carry the same
  name: the value enumeration and the sub key enumeration report the name once
  each with the data of the hive layer, the shadowed sub key answers from the
  hive, and the real entries keep their own data. The case pins the intended
  merge semantics of a name both layers hold.
* `test/e2e/Reg_ShadowKey_ReadRealValue.cpp` — a shadow key created inside
  the sandbox reads a value which only exists in the real key, the create
  reports `REG_OPENED_EXISTING_KEY` (the merged view of `WriteCopy` holds the
  key), and the real value is unchanged.
* `test/e2e/Reg_WriteCopy_CreateDisposition.cpp` — the disposition of an
  isolated create follows the merged view: a key which only the host holds and
  a key which the hive holds report `REG_OPENED_EXISTING_KEY`, while a key
  which neither layer holds and a `Full` key which the host holds report
  `REG_CREATED_NEW_KEY`; the host keys keep their own values.
* `test/e2e/Reg_QueryKeyName_ViewPath.cpp` — `NtQueryKey` and
  `NtQueryObject` on a redirected handle report the view path and never the
  private mount prefix.
* `test/e2e/Reg_Full_HidesRealKey.cpp` — a key of the host which is marked
  `Full` and which the hive does not hold reports `ERROR_FILE_NOT_FOUND`
  inside the sandbox for a read access open and for a write access open (the
  key is never copied up), and the host key is unchanged.
* `test/e2e/Reg_WriteCopy_WriteOpen_CopyUp.cpp` — a write access open of a
  key which only the host holds copies the key up into the hive: the write
  lands in the hive, the host key keeps its own values, and a write access
  open of a key which neither layer holds still reports a missing key.
* `test/e2e/Reg_Hide_CreateInSandbox.cpp` — a key which the host holds and
  which is marked `Hide` is created inside the hive, the closed loop of the
  write and the read back runs against the hive, and the host key is
  unchanged.
* `test/e2e/Reg_Full_HiveWinsOverReal.cpp` — a key which exists in the hive
  and in the host is answered by the hive.
* `test/e2e/Reg_Full_HidesRealValue.cpp` — a single value of the host which
  is marked `Full` is not readable, while the other values of the same key
  keep their read through.
* `test/e2e/Reg_Full_EnumerateHidesReal.cpp` — the merged enumeration and
  the merged count hide the isolated host sub key and show the visible one.
* `test/e2e/Reg_Full_NonHkcuRoot.cpp` — the hive is visible through
  `HKEY_LOCAL_MACHINE`, `HKEY_CLASSES_ROOT` and `HKEY_CURRENT_CONFIG`, and an
  isolated key of the machine root is not.
* `test/e2e/Reg_DeleteKey_WhiteoutHostKey.cpp` — a key which only the host
  holds is deleted inside the sandbox: the delete succeeds, the key is gone from
  the view (the read through does not resurrect it) and the real registry keeps
  the key and its value.
* `test/e2e/Reg_DeleteKey_ShadowKeyRemoved.cpp` — a delete removes the shadow
  key of the hive and the key stays gone, so the read through of the host key of
  the same name does not bring it back; the host key is unchanged.
* `test/e2e/Reg_DeleteKey_NonEmpty.cpp` — a key which still holds a visible sub
  key is refused with `ERROR_ACCESS_DENIED` (the kernel status
  `STATUS_CANNOT_DELETE`), after the sub key was deleted inside the sandbox the
  delete of the key succeeds, and the host keeps the key, its sub key and its
  value.
* `test/e2e/Reg_DeleteKey_ReadHandle.cpp` — a delete through a read through
  handle of the host layer is refused with `STATUS_ACCESS_DENIED` and the host
  key survives; a read access handle never becomes a delete.
* `test/e2e/Reg_DeleteValue_WhiteoutHostValue.cpp` — a value which only the
  host holds is deleted inside the sandbox: the read reports
  `ERROR_FILE_NOT_FOUND`, the merged value enumeration no longer lists the name,
  the other value of the key keeps its read through and the host keeps both.
* `test/e2e/Reg_DeleteValue_ShadowValue.cpp` — the value of the hive is
  removed and the value of the host of the same name does not reappear (the
  delete records it as deleted), while the host keeps its own value.
* `test/e2e/Reg_QueryMultipleValues_Mixed.cpp` — a batch which mixes a hive
  value and a host value is answered completely with the data and the type of
  every entry, a batch of hive values is answered as well, a batch which names a
  value the isolation hides fails as a whole with `ERROR_FILE_NOT_FOUND`, and
  the host is unchanged.
* `test/e2e/Reg_SaveKey_MergedSnapshot.cpp` — a save exports the merged view
  of the key (the hive value and the visible host value), the host value which
  the isolation hides is not part of the file, a key which only the hive holds
  is exported with its content, and the host registry is unchanged. The second
  case of the file saves through `RegSaveKeyExW` and therefore reaches
  `NtSaveKeyEx`, which has to export the same merged view. Both cases need
  `SeBackupPrivilege`, which `RegSaveKeyW` / `RegSaveKeyExW` reach through
  `NtSaveKey` / `NtSaveKeyEx`: a run which does not hold it skips the two cases
  with a message instead of failing, so the save behaviour is verified by an
  elevated run (the CI is elevated).
* `test/e2e/Reg_IsolationInheritance.cpp` — the first case pins that the mode
  of a parent key reaches a child key which lists no mode of its own (the host
  value of the child key is not readable) and that a mode of the child key
  overrides the mode of its parent; the second one pins that the mode of a
  single value hides the value of the host without touching the other values of
  the key.
* `test/e2e/Reg_MalformedIsolationFile_FallsBack.cpp` — a document which is
  not JSON and a document of an unknown version are both ignored, so the host
  value stays readable; the valid document of the last half hides it, which is
  what makes the run a check of the fallback instead of a check of the default
  mode. The value of the hive is visible in every one of them.
* `test/e2e/Reg_UsersRoot_MapsToTheCurrentUserHive.cpp` — a write through
  `HKEY_USERS` and the SID of the current user is visible through
  `HKEY_CURRENT_USER`, so the two roots name the same key of the hive layer,
  and the real registry never gains the key.
* `test/e2e/Reg_VariableExpansion.cpp` — the values of the packed hive which
  reference a known folder of this machine with `%APPBOX:<NAME>%` are read
  inside the sandbox with the real path of the folder: the `REG_SZ`, a second
  `REG_SZ` which references the system folder of the machine, the
  `REG_EXPAND_SZ` and every item of a `REG_MULTI_SZ` value, while a `REG_DWORD`
  and a `REG_BINARY` value whose bytes spell the same reference keep their own
  bytes. The hive of the resources is byte identical afterwards, so the archive
  keeps the reference and the expansion happens while the sandbox runs.

### Network isolation cases

The network cases (`test/e2e/Net_*.cpp`) write the isolation file of the case
into the network domain of the resources
(`test/utils/NetworkIsolationBuilder.*`, `app/network/isolation.json`) and
resolve a hostname
inside the sandbox with the probe `ResolveName`, which calls the name resolution
of winsock and the one of the DNS client. The probe answers a list of questions
in one probe process, so a case pays for the chain of the launcher and of the
sandbox once.

The proxy cases use the probe `SocketTraffic`, which performs the socket calls
of a case inside the sandbox, and two helpers of the test process:
`test/utils/Socks5Server.*` is a minimal SOCKS5 server which answers the
handshake, establishes a connection and relays datagrams, and keeps every
request it received, while `test/utils/EchoServer.*` echoes a connection and a
datagram back and keeps the address the last datagram came from. Both listen on
the loopback address with an ephemeral port, which the case writes into the
isolation file as the server of the proxy.

| Case | Proxy | Steps | Expected |
| --- | --- | --- | --- |
| `Net_Proxy_TcpConnectIsCarriedByTheProxy` | TCP on | connect to the echo server and echo a payload | the payload comes back, and the server of the proxy received a `CONNECT` request which names the echo server |
| `Net_Proxy_UdpDatagramIsCarriedByTheProxy` | UDP on | send a datagram to the echo server and read the answer | the answer reports the echo server as its source, while the echo server saw the datagram come from the relay of the association |
| `Net_Proxy_CredentialsAreSent` | TCP on, with credentials | connect to the echo server | the connection succeeds and the server saw the credential exchange |
| `Net_Proxy_DisabledTrafficKeepsTheDirectPath` | both protocols off | connect and send a datagram to the echo server | both reach the echo server and the server of the proxy received nothing |
| `Net_Proxy_UnreachableServerFailsTheConnect` | TCP on, pointing at a free port | connect to the echo server | the call fails and the echo server never accepted a connection, so the sandbox does not fall back to the direct path |
| `Net_Proxy_NonBlockingSocketIsProxied` | TCP on | connect on a non-blocking socket | the call succeeds and the socket is still non-blocking afterwards |
| `Net_Proxy_MalformedConfigurationFallsBack` | a `proxy` member whose port carries a leading zero | connect to the echo server | the call reaches the echo server and the server of the proxy received nothing |

| Case | Redirections | Question | Expected |
| --- | --- | --- | --- |
| `Net_Dns_RedirectIsReturned` | `appbox-spike.invalid` → `127.0.0.1` | the name, with the winsock resolution and with the DNS client | both calls answer with `127.0.0.1` without asking the host |
| `Net_Dns_MissIsResolvedByTheHost` | `appbox-spike.invalid` → `10.9.9.9` | `localhost` | the host answers, so a name the file does not list keeps the resolution of the host |
| `Net_Dns_HostnameIsNormalized` | `Update.Example.COM.` → `127.0.0.1` | the name in three spellings | every question is answered, because the case and the trailing dot do not matter |
| `Net_Dns_FamilyOfTheRedirectIsHonoured` | `v4.…` → `127.0.0.1`, `v6.…` → `::1` | both names, both families | a question is answered by the entry of its family; the other family keeps the resolution of the host, which fails for a name only the file knows |
| `Net_Dns_EveryResolutionApiIsRedirected` | `appbox-spike.invalid` → `127.0.0.1` | the name with every entry point the sandbox hooks: `GetAddrInfoW`, `getaddrinfo`, `GetAddrInfoExW`, `gethostbyname`, `DnsQuery_UTF8`, `DnsQuery_A` and `DnsQuery_W` | every API answers the redirect address, and the two ANSI entry points keep the resolution of the host for a name the file does not list |

### Environment isolation cases

The environment cases (`test/e2e/Env_*.cpp`) write the isolation file of the case
into the environment domain of the resources
(`test/utils/EnvironmentIsolationBuilder.*`, `app/environment/isolation.json`)
and read the environment of the sandbox with the probe `EnvironmentRead`, which
answers the value of a variable through the wide and the ANSI entry point, the
entries of the block which enumerates the environment, and the expansion of a
`%NAME%` reference; the probe `EnvironmentWrite` stores and removes variables and
reads them back.

The value of the host of a case is set in the environment of the test process
before the launcher starts, because the environment of the sandboxed application is
the environment of the launcher, which the test process passes to it
(`test/utils/EnvironmentIsolationBuilder.*`, `HostEnvironmentVariable`, which
removes the variable again when the case ends). The names the cases use are
prefixed with `APPBOX_ENV_`, so they cannot collide with a variable of the
machine the cases run on.

| Case | Isolation of the row | Steps | Expected |
| --- | --- | --- | --- |
| `Env_Full_HidesTheHostValue` | `Full`, host holds `foo` | read the configured variable and a variable the file does not list | the configured variable carries the value of the row, the unlisted one carries the value of the host, and the ANSI entry point reports the same view |
| `Env_WriteCopy_Replace` | `Write Copy` + `Replace` | read the variable, and ask the lowest reader of the process environment for its size while bringing no buffer | the value of the row, and a refusal (`STATUS_BUFFER_TOO_SMALL`) with the size the caller has to provide, which is what the loader of the operating system relies on while it computes the search path of a DLL |
| `Env_WriteCopy_Host` | `Write Copy` + `Host` | read the variable | the value of the host |
| `Env_WriteCopy_Prepend` | `Write Copy` + `Prepend` + `;` | read the variable, enumerate the environment, expand `%NAME%` | `bar;foo` from every entry point |
| `Env_WriteCopy_Append` | `Write Copy` + `Append` + `;` | read the variable | `foo;bar` |
| `Env_MissingHostValue_KeepsTheConfiguredValue` | `Prepend` and `Host` for two variables the host does not hold | read both variables | the joined value carries the row alone (no separator), the `Host` row is not visible at all |
| `Env_ModificationStaysInTheSandbox` | `Replace` | store the variable, remove a variable of the host, read both back | the stored value is read back, the removed variable is gone, and the environment of the test process still holds the values it set |
| `Env_StateIsKeptAcrossRuns` | `Replace` | store the variable in one run, read it in the next one | the second run sees the stored value, which is what the state directory of the sandbox carries |
| `Env_StateFileIsApplied` | – | read the variables while the state directory carries the document of an earlier run | the stored value is seen and the removed variable is gone |
| `Env_VariableExpansion` | `Write Copy` + `Replace`, one row with `Prepend` and a value of the host | read variables whose values reference a known folder of this machine with `%APPBOX:<NAME>%`, a name the sandbox does not know and `%PATH%` | the known references carry the real path of the folder of this machine (also in another spelling of the prefix and of the name, and for the system folder of the machine), the merged row carries `<path>;<host>`, the unknown reference and the reference of the shell keep their spelling, and the block which enumerates the environment reports the expanded value |
| `Env_MalformedIsolationFile_FallsBack` | document which cannot be read | read the variable | the value of the host stays visible and the sandbox still runs |

The child of a sandboxed process sees the view of its parent, which every one of
the cases above pins: the launcher starts the relay process, the relay is a
sandboxed process, and the probe process of a case is started by that relay. The
value the probe reads is therefore the environment the sandbox handed over to a
child, and a composition which ran twice would report `bar;bar;foo` for the
`Prepend` case.

### Fonts isolation cases

The font cases (`test/e2e/Font_*.cpp`) put a font into the `#Fonts#` layer of the
resources, which the launcher maps to the font folder of the machine
(`FOLDERID_Fonts`, resolved by `test/utils/TestKnownFolder.*`), and read the font
table of the sandboxed process with the probe `Fonts`, which enumerates the families,
creates a font for a family and reports the face it chose, reads the file of the view
and adds it with the path of the view and `FR_PRIVATE`. The domain itself is
documented in [FontsIsolation.md](../docs/FontsIsolation.md).

The font of a case has to carry a family the host does not carry, otherwise the
family would be enumerated without the sandbox as well. `test/utils/TestFont.*`
builds it from an installed font by rewriting the family strings of its `name` table
in place and recomputing the checksums of the file, and every case pins that the
family is not installed before it runs.

| Case | Steps | Expected |
| --- | --- | --- |
| `Font_PackedFontIsUsable` | enumerate the families, create a font for the family of the packed font, read the file of the view | the family is enumerated, the face of the created font is the family, so the packed font is usable like an installed one, and the file of the view carries the content of the layer |
| `Font_AddFontResourceEx_ViewPath` | add the file of the view with `AddFontResourceExW(path of the view, FR_PRIVATE, 0)`, while the file is not a file of the font folder of the host | the call reports the font, so the sandbox resolved the path of the view to the file of the layer: the font driver of the system opens the file without passing the file hooks |
| `Font_HostIsNotModified` | run the sandbox, then check the host and the font table of the test process | the font folder of the host carries no file of the layer, the font registry of the host carries no entry of the font, and the family is gone once the sandboxed process ended |

### Launcher startup cases

The startup cases (`test/e2e/Launcher_Startup.cpp`) describe three startup files in
one `LauncherConfig`: `one` and `two` are marked for auto start, `manual` is not.
The arguments of every startup file carry its own marker, and the probe
`test/probe/StartupStarted.cpp` reports the marker of the file which started it,
so a case can tell which of the files the launcher started; the file name of the
first startup file decides the launcher entry of a packed archive. Each case is
documented in its own header comment.

| Case | `--X-AppBox-Startup` | Expected |
| --- | --- | --- |
| `AutoStartAll` | – | `one` and `two` start, `manual` does not, the launcher exits with zero |
| `SelectedByTrigger` | `manual` | only `manual` starts, the auto start files do not, the launcher exits with zero |
| `UnknownTrigger` | `missing` | nothing starts, not even the auto start files, and the launcher reports a non zero exit code |

### Launcher shell cases

The shell cases (`test/e2e/Launcher_Shell.cpp`) run the launcher with
`--X-AppBox-Shell`, which starts the `cmd.exe` of the host inside the sandbox
instead of the application of the configuration: without a command the shell
runs interactively, with a command it runs `cmd /c <command>`. The cases use
`ProbeShellRun()` of `test/utils/ProbeCall.*`, which points the startup files of
the configuration at the probe process and collects the markers of the probes
which reported until the launcher left, so a marker proves that the launcher started
a startup file although the run had to ignore it. The command of a case has to
return on its own, because the call waits for the launcher: the run without a
command waits for the input of a user, so it has no automated coverage and is
verified by hand. Each case is documented in its own header comment.

| Case | Command | Expected |
| --- | --- | --- |
| `CommandRunsAndStartupsAreIgnored` | `exit 42` | the shell runs the command and the launcher exits with `42`; no startup file is started, not even an auto start one |
| `CommandRunsInsideTheSandbox` | `echo hello> <known folder>\AppBoxTest_Shell\shell.txt` | the file is written into the overlay of the sandbox and never into the folder of the host, so the command ran inside the isolation, and the resources of the application are untouched |
| `ShellAndStartupAreMutuallyExclusive` | `exit 0` together with `--X-AppBox-Startup manual` | nothing runs, because the two options name different programs, and the launcher reports a non zero exit code |

### Launcher console case

The launcher is a GUI program without a console, so a console program it starts
gets a console window of its own: without a countermeasure the probe process of
every case would pop up on the desktop of the machine the cases run on. The
harness therefore enables `hide_console` in the configuration it writes, and
`test/e2e/Launcher_HideConsole.cpp` pins the result. The case asks the probe
`ConsoleWindow` for the console of the probe process: the console is still
attached, so the standard streams of the probe keep working, while its window
is not visible.

### Launcher registry state cases

The registry state cases (`test/e2e/Launcher_RegistryState*.cpp`) pin what the
launcher does with the hive of the resources. The packed hive is a read-only
resource, while the sandbox mounts a hive which it modifies (copy-up, whiteouts
and the transaction log files), so the launcher seeds a copy into the state
directory of the sandbox on the first run and mounts that copy. Every case
builds the resources with `test/utils/HiveBuilder.*` and reads the value inside
the sandbox with the probe `RegReadValue`, which is the only way to tell which
hive the sandbox mounted.

| Case | Steps | Expected |
| --- | --- | --- |
| `RegistryStateIsSeeded` | the resources carry the value `packed`, the state directory does not exist at all | the read returns `packed`, the state directory carries the hive the sandbox mounted, and the hive of the resources is byte identical to the one the case built |
| `RegistryStateIsKept` | the resources carry `packed`, the first run writes `sandbox` into the key | the second run returns `sandbox`, so the state of the first run survives the next one |
| `RegistryStateIsReset` | the resources carry `packed`, the first run writes `sandbox`, then the state directory is deleted | the second run returns `packed`, so deleting the state directory resets the sandbox to the registry of the archive |

### Launcher sandbox module cases

The module cases (`test/e2e/Launcher_MissingSandboxDll.cpp` and
`test/e2e/Launcher_SandboxDllFromTheApp.cpp`) pin the contract of the injection
modules: they are resources of the archive below `app`, so the launcher injects
them from there and writes no copy into the state directory, and a run without
them is refused before anything starts. The harness links the modules of the run
into the resource root of every case, so a case which describes a run without
them removes them again.

| Case | Steps | Expected |
| --- | --- | --- |
| `MissingSandboxDll.BothModulesAreMissing` | the resource root carries neither module, the configuration holds an auto start file | the launcher reports the module which is missing, starts nothing and exits with a non zero code |
| `MissingSandboxDll.The32BitModuleIsMissing` | the resource root carries the 64 bit module only | the run is refused as well, because a packaged application may start a 32 bit process which has to be injected |
| `SandboxDllFromTheApp.TheRunInjectsFromTheResourceRoot` | the resource root carries both modules, the state root is empty | the startup file runs inside the sandbox, so the modules of the resource root were injected, and the state root carries no module afterwards |

### Launcher runtime library case

The case (`test/e2e/Launcher_NoVcRuntime.cpp`) runs the probe inside the sandbox
and asks it for the modules its process has loaded with the probe
`test/probe/LoadedModules.*`: the list carries the modules of the probe, the
injection modules of the resource root and every module they depend on, so it
tells whether a sandboxed process needs the runtime library of the toolchain.
The case is the runtime side of the promise of `cmake/StaticRuntime.cmake`,
whose static side `test/unit/StaticRuntime.cpp` pins on the files of the build.

| Case | Steps | Expected |
| --- | --- | --- |
| `TheSandboxedProcessLoadsNoVcRuntime` | the resource root carries both injection modules, the configuration holds no startup file, so the launcher starts the probe once | the module list carries `sandbox64.dll`, which proves that the probe reported from inside the sandbox, and no module the Visual C++ Redistributable installs, so the sandboxed process needs no redistributable |

The case checks the list against `IsVcRedistributableModule()`, not against the
strict `IsVcRuntimeModule()` of the import tables: the modules of Windows which a
process loads import the runtime of the operating system themselves, so
`ucrtbase.dll`, `msvcrt.dll` and `msvcp_win.dll` appear in the list of a process
which needs no redistributable at all.

### Patch layer cases

The patch cases (`test/e2e/Patch_*.cpp`) run the layout of the filesystem cases
and add a `patch` directory which carries the packages of the case;
`test/utils/PatchBuilder.*` writes a package with the resources a case
describes. The launcher validates a package against the `cache` directory of the
case, mounts its filesystem layers on top of the layers of `app`, merges its
hive into the hive the sandbox mounts and hands the isolation files of the four
domains to the sandbox, which merges them in layer order, which is the contract
of [PatchLayer.md](../docs/PatchLayer.md): the layers, the hives and the
isolation files of a run are consumed by the launcher and by the sandbox only, so
the cases of this suite are the coverage of `launcher/utils/PatchLayer.cpp`, of
`launcher/utils/HiveMerge.cpp` and of the layer handling of the four modules of
`sandbox/` which apply an isolation file. Each case is documented in its own
header comment.

| Case | Steps | Expected |
| --- | --- | --- |
| `ContentOverridesTheApp` | `app` carries a file both packages carry as well and a file of its own; `00-foo.zip` carries the shared file with a content of its own and a file of its own; `01-bar.zip` carries the shared file with a third content | the shared file holds the content of `01-bar.zip`, the listing of the folder holds the file of `app` and the file of `00-foo.zip`, both packages were extracted into `cache`, and the resources of `app` are untouched |
| `IsolationModeOfTheLastLayerWins` | two folders of the host exist; the archive keeps the first visible and hides the second; `01-bar.zip` hides the first | both reads report `File Not Found`, because the mode of the folder the package names is the mode of the package while the mode of the folder it does not name stays the mode of the archive, and the host folders are untouched |
| `MergeOfTheLastLayerWins` | the host holds the file of the folder; the archive isolates the folder with `Full` and `01-bar.zip` names the same path with `Merge` | the write reaches the file of the host filesystem, so the mode of the package replaced the mode of the archive and the file carries the new content |
| `CacheIsReusedAndRefreshed` | four runs: with the package as it is, with a file placed inside its cache entry, after the package was replaced and after the cache directory was deleted | the first run reads `one` and records the digest of the package, the second run reads `one` and keeps the placed file, so the extraction was reused, the third run reads `two` and drops the placed file, so a package whose digest changed is extracted again, and the fourth run reads `two` as well and extracts the package again, so deleting the cache only costs the extraction |
| `BrokenPackageIsSkipped` | `00-bad.zip` is not an archive and `01-good.zip` carries the file of the case | the read returns the content of the good package, the run succeeds, and the package which cannot be read left no cache entry behind |
| `NoPackageKeepsTheCacheEmpty` | the user created the `patch` directory, but it holds a file which is not a package | the read returns the content of the archive, and the `cache` directory was not created |
| `RegistryHiveOfTheLastLayerWins` | the hive of `app` carries the values `Value` and `Kept` of a key; `00-foo.zip` overrides `Value` and adds `Added`, `01-bar.zip` overrides `Value` and adds `AddedByBar` | `Value` is the value of `01-bar.zip`, because the hives of the packages are applied in ascending order, `Added` is the value of `00-foo.zip`, `Kept` is the value of the archive, and the hive of the resources is byte identical to the one the case built |
| `RegistryIsolationModeOfTheLastLayerWins` | the hive of `app` holds the key, the real HKCU holds its three values; the archive keeps `ByThePackage` visible and hides `OfTheArchive` and `ByTheArchive`, `01-bar.zip` hides `ByThePackage` and keeps `ByTheArchive` visible | `ByThePackage` and `OfTheArchive` do not exist, because the mode of the package wins over the mode of the archive while a value no package names keeps the mode of the layers below it, `ByTheArchive` is the value of the host again, and the real key is untouched |
| `RegistryWithoutResourcesKeepsTheApp` | `00-foo.zip` carries a file of the filesystem domain and no resource of the registry domain at all | the value and the mode of the archive stay the value and the mode of the run, and the hive of the resources is byte identical to the one the case built |
| `BrokenRegistryResourcesAreSkipped` | `01-bar.zip` carries a file which is not a hive as `registry/user.hiv` and a document which is not an isolation file as `registry/isolation.json` | the run succeeds, the value of the archive stays the value of the run, and the mode of the archive keeps hiding the value of the host |
| `RegistryStateBelowThePatchIsKept` | `01-bar.zip` overrides `Value` of the key, the first run writes `Runtime` into the same key, which no package names | the second run returns `package` for `Value`, because the hives of the packages are applied at every start, and `written` for `Runtime`, because an entry no package names keeps the state of the sandbox |
| `NetworkDnsOfTheLastLayerWins` | the archive redirects a shared hostname and a hostname of its own; `00-foo.zip` redirects the shared hostname and adds a hostname of its own, `01-bar.zip` redirects the shared hostname a third time | the shared hostname resolves to the address of `01-bar.zip`, the hostname only the archive redirects keeps the address of the archive, and the hostname only the first package redirects is answered as well |
| `NetworkProxyOfTheLastLayerWins` | two SOCKS5 servers of the case are listening; `00-foo.zip` configures the first one as its proxy and `01-bar.zip` the second one | the connection is carried by the server of `01-bar.zip` and the server of `00-foo.zip` received no request at all, so the proxy of a package replaces the proxy below it |
| `NetworkProxyBelowIsKept` | the archive configures a SOCKS5 server of the case as its proxy; `00-foo.zip` carries the network document of a workspace which lists nothing | the connection is carried by the proxy of the archive, so a layer which names no proxy keeps the proxy below it |
| `BrokenNetworkResourcesAreSkipped` | the archive redirects a hostname of its own; `00-foo.zip` carries a network document which is not valid JSON; `01-bar.zip` redirects a hostname of its own | both hostnames are answered, so the layers below and above the broken package stay in place |
| `EnvironmentLayersComposeInOrder` | the host holds `APPBOX_PATCH_ENV=vx`; `00-foo.zip` prepends `v0` and replaces a variable of its own, `01-bar.zip` appends `v1` | the variable reports `v0;vx;v1`, so every layer composes with the value below it, and the variable only the first package names reports its value |
| `EnvironmentFullDropsTheLayersBelow` | the host holds both variables; `00-foo.zip` prepends `v0` to the first one; `01-bar.zip` isolates both as `Full` | both report the value of `01-bar.zip` alone, so `Full` drops the value of the layer below it as well as the value of the host |
| `EnvironmentHostPassesBelowThrough` | the host holds the first variable and not the second one; `00-foo.zip` replaces the first one with `v0`; `01-bar.zip` lists both with the merge mode `Host` | the first reports `v0`, so the mode passes the value below the layer through, and the second is not part of the environment at all |
| `BrokenEnvironmentResourcesAreSkipped` | `00-foo.zip` configures a variable, `01-bar.zip` carries an environment document which is not valid JSON, `02-baz.zip` configures another variable | both variables report their value, so the layers below and above the broken package stay in place |
| `FontOfTheLastLayerWins` | the archive and `00-foo.zip` carry a font of their own under the same name in the `#Fonts#` layer of the resources, and the two fonts carry different families | the sandboxed process enumerates both families: the family of the package is the one of the font table and the family of the archive is not loaded at all, because the layer of the package comes first in the view |

## Test helpers

* `test/utils/FsBuilder.*` — declarative tree builder. `FsRoot(root, {dirs})`
  materializes the directories of the case and returns the `LauncherConfig` of the case,
  which carries no path: the launcher resolves the state directory `data` and the resource
  directory `app` against the directory of its configuration file, which is the working
  directory of the case. The resource directories are named after the known folder token
  (`app\filesystem\#USERPROFILE#`) so that `MapBaseFS` resolves them. The builder also
  links the sandbox injection modules of the run into the resource root of the case,
  because every case which starts the launcher needs them there. `Verify()` re-reads
  everything a case declared and fails if the content changed; the isolation files which
  the helpers of the suite write and the injection modules are not part of the declared
  content.
* `test/utils/CommonFixture.*` — gives every case a private working directory, and
  skips the case when the run provided no sandbox injection modules, because the launcher
  of a case cannot inject anything without them.
* `test/utils/SandboxDll.hpp` — the injection modules of a run:
  `Sandbox32DllPath()` and `Sandbox64DllPath()` answer the paths of the run and
  `SandboxModulesAvailable()` tells whether both of them exist.
* `test/utils/ModuleList.hpp` — the module list checks of the runtime
  independence cases: `IsVcRuntimeModule()` reports whether a module name is a
  runtime library of the C++ toolchain (`VCRUNTIME`, `MSVCP`, `CONCRT`,
  `VCCORLIB`, `ucrtbase`, the `api-ms-win-crt-*` API sets and `msvcrt`), which
  is the check of the import tables of the products, while
  `IsVcRedistributableModule()` reports the narrower set the Visual C++
  Redistributable installs, which is the check of the module list of a running
  process; `HasModule()` looks a module up in a list, all of them ignoring the
  case. The unit test of the import tables and the end-to-end case of the module
  list of a sandboxed process share them, so the two cannot drift apart.
* `test/utils/CWD.*` — the working directory itself: `Create()` makes it,
  `NoCleanup()` keeps it after the case.
* `test/utils/ProbeCall.*` — writes the `LauncherConfig` to `config.json`, starts the
  **launcher** with `--X-AppBox-ConfigFile`, which launches the test binary as a probe
  process with the sandbox DLL injected; the probe asks the test process for its task
  over a named pipe and reports the result back. Every configuration it hands to the
  launcher enables `hide_console`, so the probe process of a case does not open a console
  window (see [Launcher console case](#launcher-console-case)). `ProbeStartupRun()` runs the
  launcher for a startup file selection instead: it passes `--X-AppBox-Startup` when a
  trigger is given and reports the markers of the startup files the launcher started
  together with its exit code. `ProbeShellRun()` passes `--X-AppBox-Shell` with the
  command of the case and collects the markers which arrived until the launcher left; it
  does not wait for a probe, because the shell runs the command of the case instead of
  the probe (see [Launcher shell cases](#launcher-shell-cases)).
* `test/utils/HiveBuilder.*` — builds the registry artifacts of a case, so an
  end-to-end test owns what the sandbox mounts. The builder writes the hive file and
  the isolation file into the registry domain of the resources of the case
  (`app/registry`), which is where the launcher looks for them: it seeds the hive into
  the state directory of the sandbox, so a case exercises the seeding as well. The
  content of the hive and the isolation modes are tracked apart, so a mode can be
  listed for a key which the hive does not hold. `WriteRawIsolation()` writes a text
  which is not the document of the builder, which is what a case about a refused
  document needs, and `BuildRegistryIsolationText()` returns the document of a
  list of modes without writing it, which is what the builder of a patch package
  needs.
* `test/utils/RealHkcuKey.*` — RAII helper which owns a key below the real
  HKCU of the test process, so a case which needs a host entry leaves nothing
  behind.
* `test/utils/RegistryRootKey.*` — resolves the predefined handle of a registry
  root key, which is what a probe passes to the registry API.
* `test/utils/TestKnownFolder.*` — path of a known folder. The helper is named
  `TestKnownFolder` because the launcher ships a header of the name `KnownFolder`
  with a different API, and the single test executable carries both.
* `test/utils/TestFont.*` — a font whose family the host does not carry, which the
  cases of the font isolation need. The helper copies an installed font of
  `%windir%\Fonts`, rewrites the family strings of its `name` table in place — the
  replacement has the same length, so no offset of the file moves — recomputes the
  checksum of the table and the adjustment of the `head` table, and reports the
  family it built. `HostCarriesFamily()` answers whether the font table of the test
  process, which is the one of the host, carries a family.
* `test/utils/RealFsFolder.*` — RAII helper which owns a folder below a known
  folder of the host, so a case which needs an entry of the host filesystem
  leaves nothing behind.
* `test/utils/FsIsolationBuilder.*` — writes the isolation file of a test
  case (`<case root>/app/filesystem/isolation.json`) from the modes of the case;
  an entry without a path is the root of the view, which decides the mode of the
  paths no other entry covers. `WriteRawFsIsolationFile()` writes a text which
  is not the document of the builder, which is what a case about a refused
  document needs, and `BuildFsIsolationText()` returns the text without writing
  it, which is what the builder of a patch package needs.
* `test/utils/PatchBuilder.*` — writes a patch package
  (`<case root>/patch/00-foo.zip`) from the files, the filesystem isolation
  modes and the four domains of a case. The package holds the resource tree of a
  filesystem, a registry, a network and an environment workspace rooted at the
  archive root, which is the layout the packer writes; a domain is written only
  when the case describes it, so a case about a package which carries no
  resource of a domain gets a package without that domain, and a case about a
  broken package writes the bytes of the hive and the text of an isolation file
  itself. The helper builds the archive below the temporary directory of the
  machine and copies it into the destination, because a process which watches the
  working directory of a case holds a package which exists for a while open long
  enough to break the temporary file rename libzip writes an archive with.
* `test/utils/NetworkIsolationBuilder.*` — writes the isolation file of a test
  case (`<case root>/app/network/isolation.json`) from the DNS redirections and
  the proxy of the case. `BuildNetworkIsolationText()` returns the text of the
  document without writing it, which is what the builder of a patch package
  needs, and `WriteNetworkIsolationFileText()` writes a text which is not the
  document of the builder, which is what a case about a refused document needs.
* `test/utils/EnvironmentIsolationBuilder.*` — writes the isolation file of a
  test case (`<case root>/app/environment/isolation.json`) from the variables of
  the case, the state file of the sandbox
  (`<case root>/data/environment/state.json`) from the modifications of the
  case, and `BuildEnvironmentIsolationText()` returns the text of the document
  without writing it, like the builder of the network domain. The class
  `HostEnvironmentVariable` stores a variable in the environment of the test
  process for the length of a case, which is the environment of the host of the
  run.
* `test/utils/LauncherPath.hpp` — the launcher path of a run, which the tests of
  the real launcher payload read from the configuration.
* `test/utils/ReadFileFull.*` / `test/utils/WriteFileFull.*` — file I/O
  helpers.
* `test/utils/Semaphore.*` — synchronization between the test process and the
  probe process.
* `test/probe/*` — the operations executed inside the sandbox (`CreateFileW`,
  `CreateDirectoryW`, `DeleteFileW`, `RemoveDirectoryW`, `WriteFile`,
  `ListDir`, `ListDirNt`, `ReadFileFull`), plus the probes of the remaining
  cases (`LaunchProcess`, `QueryAttributes`, `QueryFullAttributes`,
  `QueryInformationByName`, `QueryInformationFile`, `SetInformationFile`,
  `ConsoleWindow`).
  `ListDirNt` opens a directory with `NtOpenFile` or, when the request asks for
  it, with `CreateFileW`, and enumerates it with `NtQueryDirectoryFile` or
  `NtQueryDirectoryFileEx`, so it pins both entry points of the merged view and
  both ways a directory handle reaches the sandbox, while the user mode wrappers
  may use either of them. A request may also name the information class of the
  query, and the probe reads the name of an entry with its own table of the
  classes, so a case which compares the answer with the names it created itself
  pins the layout of the class as well. A class which carries no name of an
  entry is not read at all: the probe reports the status of the call alone, so a
  case pins the refusal of the view for such a class.
  `QueryAttributes` asks the user mode
  wrapper of the attributes, while `QueryFullAttributes` and
  `QueryInformationByName` call `NtQueryFullAttributesFile` and
  `NtQueryInformationByName` themselves, so every name based query of the view is
  pinned directly. `QueryInformationFile` calls `NtQueryInformationFile` with the
  three classes which report a name and reports every name together with the size
  of the buffer it was asked with, and `SetInformationFile` calls
  `NtSetInformationFile` with the classes of a rename and of a link, so both entry
  points which act on a handle are pinned directly as well. The probes answer one
  item per question of their request, which keeps the number of calls into the
  sandbox low.
* `test/probe/RegWriteValue.cpp` / `RegReadValue.cpp` — the operations
  executed inside the sandbox; both address the key through a root key of the
  view, so a case can pin that two roots name the same key.
* `test/probe/RegReadValues.cpp` — several values of one key in one call, with
  the type, the text, the items of a list and the raw bytes of every value, so a
  case which pins a string type next to a `REG_DWORD` and a `REG_BINARY` pays
  for the chain of the launcher and of the sandbox once.
* `test/probe/RegEnumKey.cpp` / `RegEnumValue.cpp` — sub key and value
  enumeration inside the sandbox.
* `test/probe/RegShadowRead.cpp` / `RegQueryKeyName.cpp` — shadow key value
  reads and kernel object name queries inside the sandbox.
* `test/probe/RegOpenWriteValue.cpp` — a write access open of an existing key
  inside the sandbox, followed by a value write and a read back.
* `test/probe/RegSaveKey.cpp` — a save of a key of the view, with
  `RegSaveKeyW` or, when the request asks for it, with `RegSaveKeyExW`.
* `test/probe/ResolveName.*` — resolves hostnames inside the sandbox with one
  of the seven entry points the sandbox hooks and reports the return code and
  the addresses of every answer.
* `test/probe/Fonts.*` — the font table and the font files of the view inside the
  sandbox: it enumerates the families of the process, creates a font for a family
  and reports the face the system chose, reads a file of the view and adds it with
  `AddFontResourceExW` and `FR_PRIVATE`. A request names the family the case
  expects, an optional family which must not be carried and the path of the view,
  so one call answers the whole case.
* `test/probe/LoadedModules.*` — the modules the probe process has loaded, in
  load order, taken with a tool help snapshot of the process itself, which is
  the list the case about the runtime library of a sandboxed process reads (see
  [Launcher runtime library case](#launcher-runtime-library-case)).
* `test/probe/__init__.hpp` — the probe registry: a probe registers itself by
  name on start and `ProbeInit` registers the command which the launcher starts.
  `--startup_marker` carries the marker of the startup file a probe process was
  started for, which `StartupMarker()` returns.

## Tracer tests

* `test/unit/Tracer*.cpp` — the parser, the PE reader, the scope table
  (which is verified against the export tables of `ntdll`, `ws2_32` and
  `dnsapi`), the breakpoint plan, the arm helpers, the category vocabulary and
  the workspace model (`TracerModel.cpp`), all without a debugger.
* `test/unit/TracerIntegration.cpp` — real runs below the real debugger: a
  run of `cmd.exe`, a run with a child process, and a run of the name resolution
  probe of this executable (`test/utils/NameResolutionProbe.*`), which loads the
  DNS client on demand and therefore proves that the breakpoints of a module
  which the loader maps after the initial break are armed. The workspace case
  (`TheViewOfARealRunIsMarked`) built a view, ran `cmd.exe` through
  `appbox::RunTracerSession` and checked that every name the run collected is a
  row of the view and that exactly those rows are marked. The suite skips itself
  when `cdb.exe` is not installed.

## Known gaps

* **The `...BypassAccessCheck` classes have no case.** A rename and a link are
  redirected for the eight classes of the two families, but
  `FileRenameInformationBypassAccessCheck`,
  `FileRenameInformationExBypassAccessCheck`,
  `FileLinkInformationBypassAccessCheck` and
  `FileLinkInformationExBypassAccessCheck` belong to kernel mode callers, which
  no case of the suite can drive; the cases pin the four classes a user mode
  caller reaches.
* **The timeout and the coredumps of a run are verified by hand.** The test
  suite does not test itself (see
  [Timeouts and coredumps](#timeouts-and-coredumps)).
* **Two packages which share a cache entry are not covered.** Two package names
  can only name one cache entry when they differ in the case of their extension
  (`00-foo.zip` and `00-foo.ZIP`), and a directory of a case-insensitive
  filesystem cannot hold both, so the rule of `launcher/utils/PatchLayer.cpp`
  which logs and skips the second one is defensive and has no case.
* **A value mode needs a key of the hive.** A value mode of an isolation file
  only reaches the merged view of a key the hive holds; the values of a key
  which only the host holds are read through a real handle, which the hooks
  forward unchanged. The registry cases of the patch suite pin the supported
  path, in which the hive holds the key (see the known gaps of
  [RegistryIsolation.md](../docs/RegistryIsolation.md)).

## Related documentation

* [README.md](../README.md) — build, components and artifacts.
* [FilesystemIsolation.md](../docs/FilesystemIsolation.md) — filesystem
  isolation architecture.
* [RegistryIsolation.md](../docs/RegistryIsolation.md) — registry isolation
  architecture.
* [NetworkIsolation.md](../docs/NetworkIsolation.md) — network isolation
  architecture.
* [EnvironmentIsolation.md](../docs/EnvironmentIsolation.md) — environment
  isolation architecture.
* [PatchLayer.md](../docs/PatchLayer.md) — patch packages: layout, merge rules
  and the launcher side.
* [Debug.md](../docs/Debug.md) — Debug workspace, Trace tab: usage, mechanism
  and measured cost.
