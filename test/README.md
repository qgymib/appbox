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
injection modules the build installed below `resource/lib`, the two probe
executables the build installed below `resource/probe`
(`build/<configuration>/resource/probe/AppBoxProbe32.exe` and
`AppBoxProbe64.exe`) and the packer executable `$<TARGET_FILE:AppBox>`.

A test executable which is started by hand needs those arguments as well:
without the launcher the end-to-end cases cannot start it, without the modules the
cases skip themselves (see `appbox::test::CommonFixture::SetUp()`), and without the
packer the unit test of the embedded resources skips itself. A run which was
started without one of the two probes is refused before the first case (see
`appbox::test::ValidateProbePaths()`): the probe is the program a case runs inside
the sandbox, so a missing probe reports a failure instead of skipped cases.

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
    --probe32=<absolute path of AppBoxProbe32.exe> \
    --probe64=<absolute path of AppBoxProbe64.exe> \
    --packer=<absolute path of AppBox.exe>
```

The executable accepts these options; every one of them has an environment
variable counterpart:

| Option | Environment variable | Meaning |
| --- | --- | --- |
| `--launcher` | `APPBOX_TEST_LAUNCHER` | Path of the launcher executable the cases start. |
| `--sandbox32` | `APPBOX_TEST_SANDBOX32` | Path of the 32 bit sandbox injection module the cases put into the resource root of their directory. |
| `--sandbox64` | `APPBOX_TEST_SANDBOX64` | Path of the 64 bit sandbox injection module. |
| `--probe32` | `APPBOX_TEST_PROBE32` | Path of the 32 bit probe executable, which runs the operations of a case inside a 32 bit process. |
| `--probe64` | `APPBOX_TEST_PROBE64` | Path of the 64 bit probe executable, which is the probe of a case unless it asks for the 32 bit one. |
| `--packer` | `APPBOX_TEST_PACKER` | Path of the packer executable, which carries the payloads as resources. |
| `--log-level` | `APPBOX_TEST_LOG_LEVEL` | Level of the launcher and of the sandboxed processes of a case: `trace`, `debug`, `info`, `warn`, `err`, `critical` or `off`; default `info`. The level is handed to the launcher of a case, so `off` leaves the case without a log file (see [Logs of a case](#logs-of-a-case)). The test executable itself stays silent and prints its messages only when a case fails. |
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
the run and, in its `log` directory, the log files of every process the run
started.

A case asks for the log files of its run by default, which is what a reader of
the artifact of a failed case needs: the harness passes `--X-AppBox-LogLevel`
with the level of the run and `--X-AppBox-LogDir` with the `log` directory of
the case (see `appbox::test::LogRun`). A case which checks the log options of the
launcher itself leaves an option out or changes its value
(`test/e2e/Launcher_Log.cpp`, see [Launcher log cases](#launcher-log-cases)).

| File | Writer | Content |
| --- | --- | --- |
| `<program>.<time utc>.<pid>.log` | the launcher itself | The messages of the launcher: the configuration it loaded, the layers it mapped, the processes it started and their exit codes. |
| `<program>.<time utc>.<pid>.log` | one sandboxed process | The messages of one process the sandbox is injected into, in the order that process produced them. |
| `<program>.<time utc>.<pid>.log` | the test executable itself | The program log of the run of the case: the messages of the harness and of the code under test, which the case held in its backtrace until it failed. |

The launcher does not carry the messages of the sandbox: every process the sandbox
is injected into writes a log file of its own, named after the program, the UTC
time it started at and its process id — the launcher names its own file with the
very same rule (`common/LogFileName.hpp`). A run of one end-to-end case therefore
leaves one file per process — the launcher, the process launcher it starts for the
target, the probe of a case, plus the processes a case starts itself — and no two
processes ever write the same file. The file is written by the process which
produces it, so its content survives a crash of that process, and the name tells
which process it belongs to:

```
AppBoxLauncher.20260930T021157Z.76696.log
AppBoxLauncher.20260930T021157Z.76712.log
AppBoxTests.20260930T021157Z.77784.log
```

The file of the test executable is written when a case failed and only then: the
messages of the case are held in the backtrace of the run, and the dump of a case
which failed is written into the `log` directory of the working directory of that
case (`appbox::test::SetCaseLogDirectory`). The file is also what keeps the
directory of a case which started no launcher and wrote nothing — a case of the
realtime capture, for example — part of the artifact of a run, because an empty
directory is not carried by the upload of the artifact.

The level of the messages is the level of the run (`--log-level`), and it is the
level of the launcher and of every sandboxed process at once. `trace` reports
every kernel call the sandbox intercepts, so a run which is started with
`APPBOX_TEST_LOG_LEVEL=trace`, like the presets of `CMakePresets.json` do,
writes the whole path of the case into the logs of its processes. A case which
checks the log files of a run asks for `info` instead of the level of the run, so
it sees the messages it looks for whatever level the run was started with.

### The output of the run itself

The messages the test executable logs — the pipe server which carries the
probes of a case, and the helpers of a case — are not printed while the run
works. They are held in a backtrace of `spdlog` (`test/utils/TestLog.*`) whose
buffer is reset when a case starts, and the buffer is written out only when a
case fails, or when a case times out and the watchdog ends the run. A passing
run therefore shows the GoogleTest lines and nothing else, and the program log
of the case which failed appears between them. The dump is written into the
`log` directory of the case as well, so the working directory of a case which
failed carries the log of the run, see [Logs of a case](#logs-of-a-case).

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
| `test/CMakeLists.txt` | The build of the suite: the sources of the test executable, the two foreign builds of the probe (`probe_x86_build` and `probe_x64_build`, which install below `resource/probe`), and the CTest entries which pass the paths of the products to the executable. |
| `test/unit/` | The in-process unit tests, one file per module (`<Module>.cpp`); the suite of a file is named `Unit_<Module>`. |
| `test/e2e/` | The end-to-end cases, one file per case (`<Domain>_<Case>.cpp`); the suite of a file is named `E2E_<Domain>`. |
| `test/probe/` | The operations which are executed **inside** the sandbox. A probe registers itself by name (`test/probe/__init__.hpp`) and is called from a case through `ProbeCall`; the sources are a program of their own (`test/probe/CMakeLists.txt`), built as `AppBoxProbe32.exe` and `AppBoxProbe64.exe`. |
| `test/probe/CMakeLists.txt` | The project of the probe executable: it builds `AppBoxProbe32` and `AppBoxProbe64` with the static runtime and installs the executable below `resource/probe` of the build tree, which is where the cases find it. The probe is not part of the package, because the only product of the repository which is installed is the packer. |
| `test/probe/Registry.cpp` | The probe objects of the test executable: they carry the name of a probe only, because the work runs in the probe process. |
| `test/utils/` | The builders and helpers which the cases share, see [Test helpers](#test-helpers). |
| `test/utils/ProbeCall.*` | The call of a probe: it starts the launcher for the case, serves the probe over the pipe and reports the logs of the processes of the run when the case fails. The log options of the run (`appbox::test::LogRun`) decide whether a case asks for log files at all and which directory it names. |
| `test/utils/TestTimeout.*` | The timeout of a test case: the GoogleTest hook, the watchdog thread and the options of a run. |
| `test/utils/TestLog.*` | The backtrace of the program log of a run: it silences the messages of the test executable, keeps them in a `spdlog` backtrace and dumps them when a case fails or times out. |
| `test/utils/Coredump.*` | The coredump writer: the full memory dump of a process, the walk of a process tree and the termination of its processes. |
| `test/utils/CommandLine.*` | The command line and the environment of a run, which the coredump writer reads before GoogleTest and CLI11 look at them. |
| `test/utils/LauncherPath.hpp` | The launcher path of a run: `LauncherPath()` answers the `--launcher` value of the configuration. |
| `test/utils/SandboxDll.hpp` | The injection modules of a run: `Sandbox32DllPath()` and `Sandbox64DllPath()` answer the `--sandbox32` and `--sandbox64` values, and `SandboxModulesAvailable()` tells whether the run can start the launcher at all. |
| `test/utils/ProbePath.hpp` | The probe executables of a run: `ProbePath(ProbeBitness)` answers the path of the 32 bit or of the 64 bit probe. A run which cannot find both is refused before the first case, see `ValidateProbePaths()`. |
| `test/utils/RegChildExitCode.*` | The exit code bits of the registry worker (`regchild`) and their description; the worker itself is a subcommand of the probe executable. |
| `test/Test.cpp` / `test/Test.hpp` | The configuration of the run: `--launcher`, `--sandbox32`, `--sandbox64`, `--probe32`, `--probe64`, `--packer`, `--log-level`, `--mode`, `--no-cleanup`, the timeout options, the prefixes which tell the mode of a suite, the guard which keeps the prefix and the directory of a suite together, the mode filter and the check of the probe executables. |
| `test/main.cpp` | The entry point of the test executable. It serves the unit tests and the end-to-end cases, and it also handles the coredump writer and the name resolution probe of the tracer. |

The source files of the executable are an explicit list in
`test/CMakeLists.txt`, not a glob, so a new test file has to be registered
there. The include directories put the project directories first, because
`test/utils` holds headers with the same name as the launcher ones and the launcher
headers have to win; the known folder helper of the cases is called
`TestKnownFolder` for that reason.

## Unit tests

The unit tests cover the modules the end-to-end cases cannot reach: the packer
(`src/core`), the launcher helpers, the modules of the sandbox which run in the
process of the test executable and the tracer. The end-to-end suite reaches the
packer through two helpers — `test/utils/PatchBuilder.*`, which builds a patch
package with the `ZipWriter` the packer writes its archives with, and
`test/e2e/Snapshot_CapturedFolderIsVisible.cpp`, which drives the capture, the
comparison, the fill and the packing of a session itself — while the rules of
the packer stay covered by the unit tests.

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

* `test/unit/ActionThrottle.cpp` — the rate gate of the action dialog: the
  first update passes, an update inside the interval is refused, the boundary
  of the interval itself passes, a refused update does not shift the window of
  the next one, the gate uses the interval it was created with, a non positive
  interval passes every update and the gate without an injected clock passes
  the first update.
* `test/unit/FilesystemIsolation.cpp` — the vocabulary of the filesystem
  isolation (the tokens and the display names of the modes, the explanation the
  isolation dialog shows below the name of a mode, which never repeats the name,
  the modes an entry kind accepts, the default of the view, which is `Merge` and
  which a file cannot hold, the fold of `Write Copy` to `Full` for a file) and
  the model of
  the packer: the path helpers of the virtual filesystem, the explicit mode of
  an entry, the inheritance of the closest folder above an entry, the mode of
  the root of the view for the paths no entry covers, the call which sets that
  mode while it pins the layer roots of the view to the mode they show, the
  recursion which overwrites the folders below a folder, the removal of a
  subtree, the move of the modes below a folder which is renamed together with
  the refusal of a rename which would land on an entry of its own, and the order
  of the entries. It also pins the isolation
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
  `standalone` while it is absent, the optional `compression_level` member,
  which is written for every session and read as the default level while it is
  absent, the optional `metadata` member, which names
  the program the file properties of the launcher are inherited from and the
  fields the user edited, refuses a key which is not a field of a version
  resource and a field which is listed twice, and is read as empty while it is
  absent, the optional `startup` member, which names the flag of the startup
  screen and the image it presents, is written for every session and read as
  the screen of a fresh session while it is absent, and refuses a member which
  does not fit the schema, the optional `directories` member of the folders the
  user created inside the sandbox view, which is omitted while the session holds
  none and refuses a record which does not fit the schema, the optional
  `whiteouts` member of the entries the archive hides, which is omitted while the
  session hides none and refuses a record which does not fit the schema, and the
  atomic read.
* `test/unit/ProjectFile.cpp` — the file layer of a project file: the
  round trip of the configuration, of the folders the user created inside the
  sandbox view, of the entries the archive hides, of the virtual registry, of the
  filesystem isolation modes, of the proxy of the network workspace, of the
  environment variables of the environment workspace, of the project type, of
  the compression level, of
  the file properties the launcher carries and of the startup screen the
  launcher shows,
  the strict UTF-8 encoding, the failures of a malformed document and the
  atomicity of applying a document to the models, including a mode which a file
  cannot hold, a path which is listed twice, a created folder without a folder
  of the view above it, an entry which is hidden with a name the view reserves, a
  proxy the model refuses, a
  variable the model refuses, an unknown project type and an unknown
  compression level.
* `test/unit/ProjectType.cpp` — the vocabulary of the `Project Type` box: the
  token of every project type, which is the text form the project file stores,
  the token which is parsed whatever the case it was written with, the tokens
  which are refused, the label of every entry of the box, and the mapping
  between the entries of the box and the enumeration, including the fallback of
  an index which is outside of the box.
* `test/unit/CompressionLevel.cpp` — the vocabulary of the `Compression` box:
  the token of every level, which is the text form the project file stores, the
  token which is parsed whatever the case it was written with, the tokens which
  are refused, the label of every entry of the box, the zlib level of every
  level, the order of the scale, the default level of a session, and the mapping
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
  environment workspace, the two sandbox injection modules below `app` and, for
  a session which sets one, the startup image of the launcher, which the
  configuration names and which the progress total counts like every other
  entry which does not come from an import; the run refuses a session whose
  image is missing or of a format the launcher cannot decode before the first
  entry is written, and
  the launcher carries the file properties of the program the metadata of the
  session inherits from with the fields the user edited applied on top of them,
  and the launcher entry is named after the first startup file and always carries
  the `.exe` extension, so a session whose first startup file is a script writes
  the launcher as `<name of the script>.exe` beside its configuration;
  the patch package carries the very same resources
  rooted at the archive root, without the launcher, without the injection modules
  and without an `app`
  directory, and needs neither a startup file nor the payloads, which is
  what the patch cases of the suite pin: the root layout, the layer tree, the
  registry artifacts, the imported files, the folders the user created inside
  the filesystem workspace, which are written as directory entries of the layer
  of their preset directory (including the layer root of a preset directory
  which holds no import) and which contribute no file to the progress total, the
  progress total, the cancellation
  and the fact that the archive relative spelling of an entry is the `app`
  prefix plus its resource relative spelling. The compression level of the
  session reaches the entries of both products: a run which stores them writes a
  stored entry while a run which deflates them writes a deflated one, so the
  level cannot be lost on the way from the session to the archive.
  An entry the session hides travels as the whiteout marker of the entry, an
  empty file below the layer of its preset directory whose directory chain is
  written as well, in both products, and the markers are part of the total the
  progress reports announce, so the bar of a `Build and Run` run does not
  overrun while the archive is extracted.
* `test/unit/PackModel.cpp` — the editable model of a packer session: the
  import of a host folder and of individual files, the rules of the import
  names and of the startable files (a program, a batch file, a command script and
  a PowerShell script are accepted, everything else is refused), the startup file
  list with its triggers, the
  folders the user created inside the sandbox view (their creation below the
  layer root, below an import and below another created folder, the default name
  which avoids the names which are taken, the refusal of a name which an import,
  a file or the host folder of the directory already holds, the rename of a
  folder with everything below it, the removal of a folder with the files below
  it, the cascade of removing an import, and the restoration of a project file
  without touching the host filesystem), the
  entries the archive hides (their addition below the layer root and below a
  folder, the normalization of their directory, the refusal of an unknown preset
  directory, of a path which leaves the layer, of an unusable name, of a name the
  view reserves for its markers and of an entry which is hidden already, their
  removal with a subtree and the cascade of removing an import and of removing a
  folder the user created), the question whether a directory of a preset
  directory is a folder of the view, and the
  host folder a folder of the filesystem view maps to (the text the tooltip of
  the tree shows).
* `test/unit/SnapshotModel.cpp` — the identifiers the comparison of two states
  is built on: the fold of the case and the key of an entry of the filesystem,
  of a key and of a value of the registry and of an environment variable.
* `test/unit/SnapshotStore.cpp` — the snapshot file: the round trip of every
  part of a phase (including a path of the host which is not ASCII, the raw
  bytes of a registry value and a value which holds no byte, which is stored as
  a blob of length zero and not as the absence of a value), the two phases of a
  file which stay apart, the replacement of the state after the change, the
  baseline which clears both phases, a phase which was started but never
  committed and is reported as absent, the lookup of an entry by a name of any
  case, the counts of a phase, the copy of a baseline into another file, the
  refusal of a copy of a phase which is absent, a file which is not a snapshot,
  a schema version which is not supported, a missing file which no call
  creates, an empty path, a destination whose folder does not exist and a write
  which fails after the file was created, which leaves no file behind.
* `test/unit/SnapshotCapture.cpp` — the walk of the host, which runs against a
  folder of the case below `#USERPROFILE#` and writes its entries into the phase
  before the change of a snapshot file: the entries of the folder (the scanned
  folder itself, its subfolders, the size and the write time of a file), an
  empty folder, a reparse point which is not followed and not recorded, the file
  the run was told to leave out, the refusal of a folder which is not part of
  its preset directory, of an unknown preset directory and of an unknown root
  key, the walk of a registry root key together with the environment of the
  machine, the reports of every stage and the abort through the progress
  callback, which leaves a phase which carries no marker.
* `test/unit/SnapshotDiff.cpp` — the comparison of the two phases of a snapshot
  file: the added, the modified and the removed entries of the filesystem, of
  the registry and of the environment, the case insensitive names, a directory
  which is only added or removed, the modification of a file by its size and by
  its write time, the modification of a registry value by its type and by its
  data, the raw data of an added value, the independence of the order of the
  entries of a state and the refusal of a file which does not hold both states.
* `test/unit/CaptureWizardModel.cpp` — the steps of the wizard of a capture: the
  step a fresh wizard shows, the mode which a machine without the kernel events
  cannot pick, the first step which waits for the elevation of the process, the
  walk through the steps of a realtime capture, the recording which waits for
  the confirmation of the user that the installation is over and which closes
  again when the confirmation is taken back, the last step which waits for the
  workspaces and which cannot be dropped while they are written and the
  prerequisites with the name of each one and whether the machine meets it. The
  snapshot capture is covered as well: the mode
  which is offered on every machine, the steps of its own walk, the step of the
  file of the baseline which waits for a path, the step of the baseline which
  waits for the run of the source and which shows the step of the file again
  when the run produced none, the path which is set anew and drops the baseline,
  the source which is picked anew and drops the file as well, the installation
  which waits for the confirmation, the comparison which waits for the analysis
  and the names and the descriptions of both modes.
* `test/unit/SnapshotApply.cpp` — the rules which write a difference into the
  models: the file which is placed below a folder that existed before the
  capture, the folders of the way to a file which are created as folders of the
  view, the file of the layer root, the folder which is created on its own, the
  entry which the view carries already, the file which is not on the host
  anymore, the entries which are hidden, the folder which is gone with the
  entries below it left out, the entry which is hidden already, the registry keys
  and values which are created with the type and the bytes of the newer snapshot,
  a removed registry key which hides its whole subtree with one entry, a removed
  registry value which is written with the data of the older snapshot and hidden,
  the registry value of a type the workspace cannot store, the environment keys
  of the machine which the registry part leaves out, the environment variables
  which are written with the default modes and the value of which is rewritten, a
  removed variable which is only reported, the difference which cannot be
  interpreted and which leaves every model untouched, the cap of the two lists of
  the report with the count of the entries they do not name, the work of the fill
  which is reported for every entry and the error a caller reports itself.
* `test/unit/EtwEventDecoder.cpp` — the decoder of the realtime capture, which
  reads the properties of an ETW event by their name through the trace data
  helper of Windows: one event of a provider which declares two versions whose
  payloads lay the same values out differently, which is what an offset into a
  payload cannot read and a name can, a classic event whose event type its
  opcode names and whose object is not the first pointer of its payload, the
  file object and the file key of a write, the path of the event which reports
  that a file came into being, the key handle and the value name of a classic
  registry event, a walk which resolves the length of a property the event names
  and reads the properties behind it, the property a walk of a short payload
  stopped at, an event of a provider no machine knows and the flag which marks a
  WPP message.
* `test/unit/RealtimeCapture.cpp` — the analysis of a realtime capture recording,
  which the cases feed with the events of a synthetic recording: the translation
  of the NT registry paths into the paths of the view and of the NT file paths
  into Win32 paths, which canonicalizes the separators the kernel reported, the
  noise the options leave out, the built-in tables the capture starts with,
  which name the state the shell keeps for itself while they leave the changes
  of the shell in, the
  folder a recording created which is reported on its own and not with the
  content of the host folder it names, the folder and the file inside it which
  are two entries, a change which was made and undone within the recording
  which leaves no entry behind, a rename which is the removal of the old path
  and the addition of the new one, a written file which is reported as modified,
  an alternate data stream which the view cannot name and which is left out, the
  registry values the recording wrote which are read from the host with the
  data it holds, the entries it deleted and the last change of a value which
  wins, the difference of the persisted environment between the start of a
  recording and its analysis and the change which belongs to no preset directory
  and which is reported as an ignored entry instead of being dropped. The enable
  of the file provider is covered as
  well: the keywords of every event a manifest declares are joined, because the
  identifier of an event belongs to the version of that manifest, and the events
  the capture reads which the manifest does not declare are named.
* `test/unit/TestLog.cpp` — the log of the run of a case (`test/utils/TestLog.*`):
  the dump of a case which failed is written into the `log` directory of the
  working directory of that case, which is what keeps the directory of a case
  which started no launcher and wrote nothing else part of the artifact of the
  run.
* `test/unit/KnownFolderReference.cpp` — the rewrite of an absolute path to the
  reference of its preset directory: the path of a preset directory, of the
  folder itself and of a path below it, the case, the boundary before and behind
  the path, the longest of several matching paths, every path of a text, an
  unknown path, a text which already carries a reference, a value of a string
  type which keeps its terminator, a multi string value whose items are rewritten
  on their own and the types which keep their bytes.
* `test/unit/FilesystemMarker.cpp` — the names of the markers of the view as the
  packer spells them against the macros of the sandbox, which is what keeps the
  two spellings from drifting apart while the packer must not include a header of
  the injection module.
* `test/unit/RegistryModel.cpp` — the model of the packer, including the
  mode of a single row, the explicit recursion of the isolation dialog and the
  mode of the container of the tree, which reaches the five preset root keys
  alone or every key and value of the view.
* `test/unit/RegistryHive.cpp` — the hive writer (all seven value types,
  several roots, nested keys, replacement of an existing hive, failure), the
  isolation file writer — the mode of the container of the tree included, which
  has to show up in the file the sandbox reads — and the layer order of the
  isolation files of a run: a
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
* `test/unit/ProxyConfig.cpp` — the `proxy` member of the isolation file of the
  network domain (`sandbox/network/ProxyConfig.*`), which the sandbox reads
  leniently: a proxy which cannot be used leaves a run without a proxy instead
  of failing the document, so a member which is missing, which is not an
  object, which names another protocol, whose server is empty, whose port the
  packer would refuse or whose flag is of another type leaves the configuration
  disabled, while a document of another schema version and a document which is
  not an object are refused as a whole. The two flags, the credentials, a proxy
  without them and the description of a configuration, which names the
  protocols and the server and not the credentials, are pinned as well.
* `test/unit/Socks5.cpp` — the messages of the protocol
  (`sandbox/network/Socks5.*`): the greeting with and without the credentials,
  the method the server selected and the one it refuses, the request of the
  credentials and its reply, the request of an IPv4 address, of an IPv6 address
  and of a domain name, the address of a reply, the rendering of an endpoint
  the way the protocol carries it, the datagram of an association with and
  without a payload, the refusal of a message which does not fit the message or
  which belongs to another protocol, of a fragmented datagram, and the
  comparison of two endpoints by their address and their port.
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
  `/c` unless it is empty, which runs the shell interactively. The suite pins the
  invocation of a startup file as well: a program is started on its own, a batch
  file and a command script are run by the command interpreter of the host, a
  PowerShell script is run by Windows PowerShell with its profile turned off and
  with the execution policy bypassed, the extension is recognized ignoring the
  case, a file which cannot be started is refused, and the resolver of Windows
  PowerShell finds `powershell.exe` of the system root.
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
  layer of the packer, including the zip slip protection of an extraction and
  the compression level of the entries: a level which stores them keeps the
  content of an entry, a level above it deflates a repetitive entry, and an
  entry which does not shrink by being deflated is stored even so.
* `test/unit/BuildReport.cpp` — the elapsed time text of the build report,
  which the elapsed row of an action dialog shows.
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
* `test/unit/StartupFileType.cpp` — the startable files of the packer and of the
  launcher: the extension of a file name decides the kind of the file (`.exe`,
  `.bat`, `.cmd` and `.ps1`, compared ignoring the case), a dot of a directory of
  the path is not taken for the extension of the file, both separators of Windows
  are accepted, and the readable list of the extensions names every one of them.
* `test/unit/StartupTree.cpp` — the startup file tree of the packer, which is the
  tree of the startup files dialog: the container row with the preset directories
  below it (a nested preset hangs below the preset which holds it), the imported
  folders, the host subfolders, the folders the user created and the files which
  were imported on their own, with the startable files below them. It pins that a
  row is offered only while a startable file is below it (a folder which holds
  none is hidden, together with the preset directories above it), that only the
  extensions of `common/StartupFileType.hpp` are listed, that a startup file of
  the model keeps its place although its host file is gone, the order of the rows
  of a folder, the default trigger of a file, the uniqueness of a trigger and the
  auto start flag, and that the list of the tree is accepted by the model.
* `test/unit/StartupScreen.cpp` — the startup screen of a packer session: the
  session which shows the screen without an image, the formats the launcher can
  decode (`.png`, `.jpg`, `.jpeg` and `.bmp`, compared ignoring the case), the
  extension of a file name which is not confused with a dot of a directory, the
  archive entry of an image (`startup/splash.<extension>`, which is the name the
  launcher configuration carries), and the validation of a pack run: a session
  without an image, an image which exists, an image which is missing, a file
  which the launcher cannot decode and a folder which carries the name of an
  image.
* `test/unit/StartupLayout.cpp` — the layout of the startup screen, which is a
  pure function of the sizes it is given: the slices of the progress bar and
  their order, the clamping of a fraction, the screen without an image, a small
  image which keeps its size and is centered, a wide image which widens the
  content, a large image which is scaled down while it keeps its aspect ratio
  and stays inside the work area, an incomplete image size, the scaling of the
  margins and of the bar with the resolution of the monitor, and an empty work
  area which still yields a usable screen.
* `test/unit/StartupImage.cpp` — the decoder of a startup image, which is the
  imaging component of Windows: a bitmap written by the case, a PNG with
  transparency written by the encoder of the same component, the premultiplied
  pixels the screen blends with, a second image which replaces the first, and
  the failures of a file which is not an image, of a file which is not there and
  of an empty path.
* `test/unit/VersionInfo.cpp` — the name the startup screen presents: the
  `ProductName` of a program, the `FileDescription` of a program which carries
  no product name, the file name of a program without a version resource at
  all, and the name of the running program. The cases patch a copy of the real
  launcher with `ApplyApplicationMetadata()`, which is the very call the packer
  writes the file properties with.
* `test/unit/Splash.cpp` — the window of the startup screen: the class of the
  window, the client area which follows the layout, the image which is drawn
  with the size the decoder reported, the order of the name, the image and the
  bar, the bar which advances towards the progress the run reported (the case
  reads the pixels of the window while it pumps its message queue), a screen
  without an image, an image which cannot be decoded, a screen which is turned
  off, where every method is a no-op, and a screen which is closed twice.

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
`--X-AppBox-ConfigFile`, the launcher launches the **probe executable** of the
case with the sandbox DLL injected, and the probe asks the test process for its
task over a named pipe and reports the result back. The probe is
`AppBoxProbe64.exe` unless the case asks for `ProbeBitness::Bit32`, which runs
the operations inside a 32 bit process. One probe call costs about two seconds,
so a case which asks several questions of one sandbox asks them in one call where
the protocol allows it.

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
| `ListDir_DuplicatedHandle` | folder with a whiteout marker for `hidden.txt` | folder with `visible.txt`, `hidden.txt` | enumerate the folder through a handle the probe duplicated and closed the original of, through both NT entry points and with both ways a directory handle reaches the sandbox | every enumeration reports the same merged view as a handle the sandbox opened: the file of the lower layer and the file of the host, while the file the whiteout hides and the marker are not listed |
| `ListDir_InformationClasses` | folder with a whiteout marker for `hidden.txt` | folder with `visible.txt`, `hidden.txt` | enumerate the folder with both NT entry points and with every information class which carries a name | every class reports the same merged view: the file of the lower layer and the file of the host, while the file the whiteout hides and the marker are not listed; a class the view refuses is refused by at least one layer of the view as well, because the view keeps the smaller feature set of its layers |
| `ListDir_EmptyFolderOfTheLayer` | – | folder `logs` without content | list `#USERPROFILE#`, list `logs` and create `created.txt` inside it | `logs` is listed as a folder of the view, its own listing is empty, the new file is created in the state, resources unchanged |
| `ListDir_LowerLayer` | – | `F.txt` | list `#USERPROFILE#` | `F.txt` appears exactly once, host entries also listed |
| `ListDir_UnregisteredNotInView` | folder which is removed while its handle stays open | a file, the character device `NUL` | enumerate a duplicated handle of a file, of the device and of the removed folder | the calls of the file and of the device are forwarded to the file system, the call of the removed folder is refused by the view, and no call reports an entry of a layer |
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

Five cases are not part of the matrices above:
`test/e2e/Fs_LaunchProcess_FromLower.cpp` starts an executable which only a
lower layer holds, `test/e2e/Fs_ListDir_UserPresetLayers.cpp` mounts one layer
per folder below `Current User Directory` (`#Documents#`, `#Desktop#`,
`#AppData#`, `#LocalAppData#`, `#LocalAppDataLow#`, `#Downloads#`,
`#Favorites#`, `#Music#`, `#Pictures#`, `#StartMenu#`, `#Programs#`,
`#Startup#`) and checks that each of
them is mapped to the real folder its layer key names,
`test/e2e/Fs_QueryAttributes_SystemPresetLayers.cpp` does the same for the
system folders (`#Windows#`, `#System32#`),
`test/e2e/Fs_QueryAttributes_ProgramPresetLayers.cpp` for the `Program Data`
folder of the system and the `Common` folder below `Program Files`
(`#ProgramData#`, `#ProgramFilesCommon#`), and
`test/e2e/Fs_QueryAttributes_ProgramFilesX86PresetLayers.cpp` for the 32 bit
program files folders (`#ProgramFiles#`, `#ProgramFilesX86#`,
`#ProgramFilesCommonX86#`), where the 32 bit folder is a sibling of the 64 bit
one. The last four cases include that the file of one layer is not visible in
the folder of the other one.

The cases which exercise the isolation modes of the workspace write the
isolation file of the case into the filesystem domain of the resources
(`test/utils/FsIsolationBuilder.*`, `app/filesystem/isolation.json`) and use a
folder below `#USERPROFILE#` of the host as the entry of the host layer
(`test/utils/RealFsFolder.*`, which removes it again when the case ends). The
cases of the markers (`Marker_*`) write the same file and keep the folder of the
case in the lower layer or in the overlay, so they never reach the host
filesystem: they pin what the view does with the names of its own markers.

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
| `Probe32_DefaultMerge_WritesTheFileOfTheHost` | the same case, run in the **32 bit** probe (`ProbeBitness::Bit32`); the case first checks that the executable it was given is a PE32 image | open the file of the host for writing, which the 32 bit sandbox module serves | the write of the 32 bit process reaches the file of the host filesystem, while the overlay holds no copy of it |
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
| `Stream_FullFileWritesIntoTheSandbox` | files `Full`, host file which carries a stream | read the stream, write it, create a second stream and read both files | the read reports the content of the host filesystem, the write and the create land in the overlay, both files travel into it with their streams so the view keeps reporting the content of the host filesystem for them, and the host filesystem keeps its files and its stream |
| `Stream_MergeKeepsThePackedStreamInTheSandbox` | folder `Merge`, packed file which carries a stream | read the stream, write it and read the file | the read of the stream reports the content of the lower layer, the write lands in the overlay, the packed file travels into it with the stream, and the host filesystem gains no entry |
| `Stream_WhiteoutFileHidesItsStream` | file `Whiteout`, host file which carries a stream | read, query and delete the stream, then create another stream of the file | the read, the query and the delete report `File Not Found`, the create lands in the overlay, and the host file and its stream are unchanged |
| `Stream_DeletedFileHidesItsStream` | file `Full`, host file which carries a stream | delete the file, then read the file and the stream | the delete records the delete in the overlay and both the file and the stream report `File Not Found`, while the host filesystem keeps both |
| `Stream_DeleteOfAStreamWritesTheMarker` | file `Full`, host file which carries a stream | delete the stream, then read the file and the stream | the delete records the marker of the stream, which is a stream of the file itself, so the file travels into the overlay with the content the view reports for it, the stream reports `File Not Found`, and the host filesystem keeps the file and its stream |
| `Marker_ForgeWhiteoutIsRefused` | folder `Write Copy`, packed file | create `victim.txt.$APPBOX_DELETE$` and `victim.txt:stream.$APPBOX_DELETE$` | both creations report `Invalid Name`, because the names of the markers are reserved, the packed entry stays readable, and the overlay records neither a marker nor a copy of the entry |
| `Marker_ForgeOpaqueIsRefused` | folder `Write Copy`, packed folder | create `.$APPBOX_OPAQUE$` inside the folder | the creation reports `Invalid Name`, the packed file is listed and readable, and the overlay holds no marker |
| `Marker_CannotBeAddressedByTheProcess` | folder `Write Copy`, packed file and its whiteout in the overlay | open, query and delete the name of the whiteout | every call reports `File Not Found`, the marker survives, and the entry it hides stays hidden |
| `Marker_PathBelowAReservedName` | folder `Write Copy`, packed folder which carries a reserved name | query the name, read the file below it and list the folder above it | the name reports `File Not Found`, the path below it reports `Path Not Found`, and the listing drops the name as well |
| `Marker_RenameToAReservedNameIsRefused` | folder `Write Copy`, packed file | rename the file onto the name of a marker and onto a name below one, and link it at the name of a marker | the calls report `Object Name Invalid`, `Object Path Not Found` and `Object Name Invalid`, the file keeps its name and its content, and the overlay records no marker |
| `Marker_StreamIsNotReported` | folder `Write Copy`, file of the overlay with a stream and with a marker stream | enumerate the streams of the file, read the marker stream and create a reserved stream name | the enumeration reports the streams of the file without the marker, the marker stream reports `File Not Found`, the creation reports `Invalid Name`, and the overlay keeps the marker stream |

The cases of the reparse points (`Fs_ReparsePoint_*`) write the isolation file
of the case and use a folder below `#USERPROFILE#` of the host as the entry of
the host layer, like the cases of the isolation modes. A host folder which needs
a link carries a junction which `test/utils/RealFsFolder.*` creates with
`FSCTL_SET_REPARSE_POINT`, and the probe creates the links of the sandbox with
the same control code and with `CreateSymbolicLinkW`
(`test/probe/ReparsePoint.*`): the two sides of a link are what the view has to
resolve, so the cases pin the links of the host filesystem and the links a
sandboxed process creates.

| Case | Isolation | Operation | Expected |
| --- | --- | --- | --- |
| `ReparsePoint_TargetIsolationDecides` | folder `Write Copy`, folder `Whiteout` | query the junction of the host, read its data back, read the file of the target directly and through two junctions, query the hidden folder | the read through the junction which names the hidden folder reports `File Not Found`, because the view resolves the link and the isolation of the target decides the answer; the read through the other junction reports the content of its target; the data of the junction names the path of the view; the hidden folder is not visible itself, while the junction is an entry which carries the attribute of a reparse point; the files of the host are unchanged |
| `ReparsePoint_LinkInTheSandboxReachesALowerLayer` | folder `Write Copy`, packed folder | create a junction which names the path of the view of the packed folder, read the file through it, read its data back, remove its data, ask the data of a file which carries no reparse point | the link lands in the overlay, the read reports the packed content because the view resolves the target in the view and the layer mapping applies to it, the data names the path of the view and not the path of the layer, the removal of the data succeeds, and the control code of a file without a link reaches the file system unchanged |
| `ReparsePoint_SymlinkRelativeAndAbsolute` | folder `Write Copy`, packed folder | create a symbolic link whose target is relative to the directory of the link and one which names the path of the view of the packed folder, read the file through both | both reads report the packed content, the data of the relative link carries the name the caller wrote, and the data of the absolute link carries the path of the view |
| `ReparsePoint_DeleteOfALinkKeepsTheTarget` | folder `Write Copy`, junction of the host | query the junction, read the file of its target through it, remove the junction, query it again, read the target again | the junction carries the attribute of a reparse point and the read through it reports the content of the target; the removal acts on the junction and not on the object it names, so the name reports `File Not Found` while the target stays readable; the delete is recorded in the overlay, so the junction of the host survives and the state holds its marker |
| `ReparsePoint_UnresolvableTargetFails` | folder `Write Copy`, junction of the host whose target names a volume no drive letter maps | open a file through the junction and query its attributes | both calls report `STATUS_REPARSE_POINT_NOT_RESOLVED`, because the view refuses a link it cannot resolve instead of letting the layer follow it, while the junction itself stays an entry of the view |
| `ReparsePoint_UnknownTagIsForwarded` | folder `Write Copy` | create a file which carries a reparse point whose tag describes the entry itself, query its attributes, read its data back | the file is an entry of the view which carries the attribute of a reparse point and its data carries the tag the process wrote, so a tag the view does not resolve is neither refused nor rewritten |

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
* `test/e2e/Reg_Hide_WriteThenRestart.cpp` — the packed content of an entry
  which is marked `Hide` lives in the hidden store of the hive, so the key of the
  entry is not part of the view at all and the open reports `File Not Found`; the
  write of the sandbox makes the entry visible in the same run and in the next
  one, because the write belongs to the state directory and not to the hidden
  store.
* `test/e2e/Reg_Merge_WritesTheHostValue.cpp` — a value which the sandbox does
  not hold below a merged key is written in the real registry: the host value
  carries the data of the sandbox, the read back of the sandbox reports the same
  data (the host layer is visible through the merged view) and the value of the
  hive is untouched.
* `test/e2e/Reg_Merge_KeepsTheHiveValue.cpp` — a value the sandbox holds below a
  merged key is written in the sandbox: the read back reports the data of the
  sandbox and the value of the real registry keeps the data of the host.
* `test/e2e/Reg_Merge_DeletesTheHostValue.cpp` — the delete of a value the
  sandbox does not hold below a merged key removes the value of the real
  registry, the read back and the merged enumeration of the sandbox no longer
  report the name, and the value does not come back.
* `test/e2e/Reg_Merge_ValueDeletedInTheSandboxStaysInTheSandbox.cpp` — a value
  which the sandbox holds is deleted in the sandbox (the value of the hive is
  removed and the deletion is recorded, so the value of the host of the same name
  stays hidden); the write which follows lands in the sandbox again, because a
  value the sandbox deleted counts as held, and the real registry keeps the data
  of the host.
* `test/e2e/Reg_Merge_CreatesTheSubKeyInTheHost.cpp` — a sub key which the
  sandbox does not hold below a merged key is created in the real registry, the
  merged view reports the sub key of the host, and the key of the hive keeps its
  own value.
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
* `test/e2e/Reg_RenameKey_ShadowKeyWhiteout.cpp` — a rename of a key which the
  hive and the host both hold leaves the new name with the content of the
  sandbox, removes the old name from the view (the rename records the visible
  host key of the old name as deleted, so the read through does not resurrect
  it), and the host keeps its key under the old name.
* `test/e2e/Reg_RenameKey_HiveOnlyKey.cpp` — a key which only the hive holds is
  renamed: the view reports the new name alone and the real registry holds
  neither name.
* `test/e2e/Reg_RenameKey_ReadHandle.cpp` — a rename through a read through
  handle of the host layer is refused with `STATUS_ACCESS_DENIED`, the key keeps
  its name and its value in the view and the host registry is unchanged.
* `test/e2e/Reg_RenameKey_VisibleDestination.cpp` — a rename onto a name which
  the merged view already holds (the host holds the destination key) is refused
  with `STATUS_CANNOT_DELETE`, which is the status the kernel reports for a
  destination key which exists, and both keys keep their content.
* `test/e2e/Reg_RenameKey_HiddenDestination.cpp` — a rename onto a name which
  only the host holds and which the isolation marks `Full` succeeds: the
  destination does not exist in the merged view, the new name shows the key of
  the sandbox alone and the hidden host key keeps its value.
* `test/e2e/Reg_RenameKey_ViewRootKey.cpp` — a rename of a root key of the view
  (the case addresses `HKEY_CURRENT_USER` through `HKEY_USERS\<SID>`) is refused
  with `STATUS_ACCESS_DENIED`, because the name of a root key is the first
  component of every path of the hive, and the view keeps working afterwards.
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
* `test/e2e/Reg_MalformedIsolationFile_FailsTheStart.cpp` — a document which is
  not JSON, a document of a version the sandbox does not know, a document which
  names no root key of the view and no document at all each fail the start of the
  sandbox: the modes of the view cannot be resolved without a usable document, so
  a run which would show the host registry never starts. A usable document starts
  the sandbox afterwards, which is what makes the four runs a check of the
  refusal instead of a check of a broken case.
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
* `test/e2e/Reg_TwoProcesses_ShareTheHive.cpp` — two processes of one sandbox
  exchange registry values. The probe process writes a value, starts the
  `regchild` worker as a child process of its own, the worker reads that value
  and writes its own while it stays alive, the probe reads the value of the
  worker while the worker runs, and a second sandbox run reads both values back.
  The case pins that the processes of one sandbox share the hive — a file which
  another process already mounted is served by the hive of that mount — and that
  the state of the run survives it, while the real HKCU stays untouched.
* `test/e2e/Reg_SetInformationKey_HiveHandle.cpp` — the probe `RegKeyApi` opens
  a key which only the host holds for writing, which the isolation answers by
  copying the key up into the hive, and calls
  `NtSetInformationKey(KeyWriteTimeInformation)` on that handle of the hive:
  the call is forwarded, the key of the hive carries the time the probe wrote,
  and the last write time of the host key is unchanged, so the modification
  landed in the sandbox.
* `test/e2e/Reg_SetInformationKey_ReadHandle.cpp` — the same call on a read
  access open, which the read through answers with a handle of the host layer,
  is refused with `STATUS_ACCESS_DENIED` and the last write time of the host key
  is unchanged. A handle of the host layer carries read rights only, which the
  kernel refuses for this class as well, so the case pins that the host stays
  untouched and that the isolation is the layer which answers the call.
* `test/e2e/Reg_FlushKey_HiveHandle.cpp` — the flush of a key of the hive is
  forwarded and succeeds, so the sandbox persists the state of the sandbox.
* `test/e2e/Reg_FlushKey_ReadHandle.cpp` — the flush of a key of the host layer
  is refused with `STATUS_ACCESS_DENIED` and the host key keeps its value. The
  call was forwarded and reported `STATUS_SUCCESS` before the isolation answered
  it itself, which flushed the hive file of the host.
* `test/e2e/Reg_CompressKey_HiveHandle.cpp` — the compress of a key of the hive
  is forwarded: the kernel answers the call
  (`STATUS_PRIVILEGE_NOT_HELD` for a caller whose token does not hold the
  privilege the call asks for) and the isolation does not refuse a handle of the
  hive.
* `test/e2e/Reg_CompressKey_ReadHandle.cpp` — the compress of a key of the host
  layer is refused with `STATUS_ACCESS_DENIED`, which is the answer of the
  isolation and not the `STATUS_PRIVILEGE_NOT_HELD` the kernel reports for the
  object of the host layer, and the host key keeps its value.
* `test/e2e/Reg_LockRegistryKey_HiveHandle.cpp` — the lock of a key of the hive
  is forwarded and the kernel answers the call, like the compress of a hive key.
* `test/e2e/Reg_LockRegistryKey_ReadHandle.cpp` — the lock of a key of the host
  layer is refused with `STATUS_ACCESS_DENIED` and the host key keeps its value.
* `test/e2e/Reg_Transacted_CreateIsRefused.cpp` — the probe `RegTransacted`
  creates a key of the view inside a transaction of its own
  (`NtCreateKeyTransacted`) and writes a value through the handle. The call is
  refused with `STATUS_RM_NOT_ACTIVE`: the create lands in the sandbox hive and
  an application hive does not support transactions, so the isolation reports
  the failure of the hive layer instead of answering the call with a plain
  (non transacted) create. The view does not hold the key afterwards and the
  real HKCU never receives it, which is what the call would have done without
  the isolation.
* `test/e2e/Reg_Transacted_OpenIsRefused.cpp` — a read only transacted open of a
  key the hive holds is refused with `STATUS_NOT_SUPPORTED`, once through
  `NtOpenKeyTransacted` and once through `NtOpenKeyTransactedEx`: the hive layer
  cannot answer a transacted open and the isolation never answers one with a
  handle of the host layer, because that handle would enlist the real hive into
  the transaction of the sandboxed process. The key stays readable with the
  value of the sandbox through the plain open, so the view keeps working.
* `test/e2e/Reg_Transacted_ReadThroughIsRefused.cpp` — a key which only the host
  holds: the read only transacted open is refused with `STATUS_NOT_SUPPORTED`
  and the write access one with `STATUS_RM_NOT_ACTIVE`, which is the failure of
  the copy-up create inside the hive. The host key keeps its own value and never
  receives the value of the probe, so neither the read through nor the copy-up
  of a transacted call touched the real registry, while the plain open of the
  key keeps its read through.
* `test/e2e/Reg_Transacted_ForeignHiveIsForwarded.cpp` — the probe mounts an
  application hive of its own, which lives below `\REGISTRY\A` and is therefore
  outside the root keys of the view, and creates a key inside it with
  `NtCreateKeyTransacted`. The call is forwarded unchanged: the kernel answers
  it with the status of the application hive (`STATUS_RM_NOT_ACTIVE`), so the
  arguments of the caller reach the kernel in their own order, and neither the
  hive of the probe nor the hive of the sandbox holds the key afterwards.

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

### Launcher startup script cases

The startup script cases (`test/e2e/Launcher_StartupScript.cpp`) start a script
which lives in the layer of the case: the configuration names
`#USERPROFILE#\AppBoxTest_Script\<script>`, the layer of the case is pinned to
`Write Copy`, and the script writes a marker beside itself and returns 7. The
cases run the launcher with `ProbeProgramRun()` of `test/utils/ProbeCall.*`,
which keeps the programs of the configuration instead of pointing the startup
files at the probe process, and they read the marker from the overlay of the
sandbox afterwards: the exit code of the script and the marker in the overlay
prove that the launcher ran the script inside the isolation, and the missing file
in the folder of the host proves that the run never touched the host. Each case
is documented in its own header comment.

| Case | Startup file | Expected |
| --- | --- | --- |
| `ABatchFileIsRunByTheCommandInterpreter` | `run.bat` | the launcher runs the script through the command interpreter of the host, the run reports the exit code of the script, and the marker is in the overlay of the sandbox and not in the folder of the host |
| `ACommandScriptIsRunByTheCommandInterpreter` | `run.cmd` | the same for a command script |
| `AFileWhichCannotBeStartedRefusesTheRun` | `notes.txt` | no process is started and the launcher reports the failure through a non zero exit code |

A PowerShell script has no case of its own: the launcher builds its invocation,
but the sandbox refuses to run Windows PowerShell (see the limitation in
[docs/Launcher.md](../docs/Launcher.md)), so a case would pin the failure of the
sandbox instead of the behavior of the launcher.

### Launcher startup screen cases

The startup screen cases (`test/e2e/Launcher_StartupScreen.cpp`) pin the screen
the launcher shows while it prepares the sandbox: it presents the name of the
packaged application, the startup image of the session and the progress of the
run, and it disappears as soon as the processes of the run were started. The
image is a resource of the archive below `app` (`app/startup/splash.<extension>`,
see `src/core/StartupScreen.*`), and the configuration of the run names it
(`startup.image`); a session which turns the screen off (`startup.screen`) runs
without a window at all.

The cases read the log the launcher writes into the directory of the run, which
reports that the screen was shown, whether it carried its image and that it was
closed again: the window lives for a few milliseconds only, so its lifetime is
pinned through those reports instead of through the window itself. The cases are
the only ones which turn the screen on, because the harness turns it off for
every other case: a window which pops up for every case would cover the work of
the user, like the console window the harness hides.

| Case | Steps | Expected |
| --- | --- | --- |
| `TheScreenIsShownWhileTheApplicationStarts` | the resources carry `app/startup/splash.bmp` of 8x4 pixels and the configuration names it | the startup file is started, the launcher exits with zero, and the log reports a shown screen which decoded the image of 8x4 pixels and which was closed again |
| `AScreenWithoutItsImageKeepsTheRunAlive` | the configuration names an image which does not exist | the run is not refused, and the log reports a screen which is shown without its image |
| `AnImageWhichCannotBeDecodedKeepsTheRunAlive` | the file of the image holds text instead of an image | the run is not refused, and the log reports a screen which is shown without its image |
| `ASessionWithoutAScreenCreatesNoWindow` | the configuration turns the screen off | the startup file is started, and the log reports neither a shown nor a closed screen |

### Launcher log cases

The log cases (`test/e2e/Launcher_Log.cpp`) pin the log options of the launcher:
a run writes no log file unless it asks for one, and a run which asks for one
writes the log of the launcher itself and the log of every process of its sandbox
into the directory the run names (see
[Launcher](../docs/Launcher.md#logging)). The cases change the log options of the
harness (`appbox::test::LogRun`), so they are the only ones which decide where
their log files land, and every case which looks for a message of a run asks for
`info`, the level the messages are reported at. Each case is documented in its
own header comment.

| Case | Log options | Expected |
| --- | --- | --- |
| `NoLogOptionsWritesNoLogFile` | neither option | the startup file is started, and the case directory holds no log file and no `log` directory |
| `LogLevelOffWritesNoLogFile` | `--X-AppBox-LogLevel off` with the log directory of the case | the startup file is started, and the run writes no log file and creates no directory |
| `LogDirHoldsTheLogsOfTheRun` | the level and the log directory of the case | the `log` directory holds the log of the launcher itself and the log of a process of its sandbox, and no log file lands outside it |
| `TheDefaultLogDirectoryIsBesideTheLauncher` | the level without the directory | the run writes into the `log` directory beside the launcher program; the case removes the files it added |
| `ARelativeLogDirectoryIsResolvedAgainstTheCurrentDirectory` | the level and a relative directory | the run writes into that directory below the current directory of the test executable; the case removes it |

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
| `RegistryIsolationModeOfTheLastLayerWins` | the hive of `app` holds the key with the values `Own` and `Packed` and hides `Packed`, so the content of the value lives in the hidden store of the hive; the real HKCU holds `HostA` and `HostB` of the same key; `01-bar.zip` marks the key `Full` and the value `Packed` `Full` | `HostA` and `HostB` do not exist, because the mode of the package hides the key of the host with everything below it, which is the mode of the last layer; `Own` is the value of the archive; `Packed` is the content of the archive which the package made visible again, which is the materialization the launcher runs for the package; the real key is untouched |
| `RegistryWithoutResourcesKeepsTheApp` | `00-foo.zip` carries a file of the filesystem domain and no resource of the registry domain at all | the value and the mode of the archive stay the value and the mode of the run, and the hive of the resources is byte identical to the one the case built |
| `BrokenRegistryResourcesAreSkipped` | `01-bar.zip` carries a file which is not a hive as `registry/user.hiv` and no isolation file at all | the run succeeds, the value of the archive stays the value of the run, and the mode of the archive keeps hiding the value of the host; a package which carries a document the sandbox cannot use fails the start instead, see `Reg_MalformedIsolationFile_FailsTheStart` |
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

### Snapshot capture cases

The capture cases (`test/e2e/Snapshot_*.cpp`) run the chain of the capture of the
packer: the state of a folder of the host is written into the phase before the
change of a snapshot file, the state after the change is written into the same
file, the two phases are compared, the difference is filled into the models of
the session, and the models are packed into an archive, which is what the
`Snapshot capture` mode of the `Capture` command does (see
[Capture](../docs/Capture.md)). The rules of the capture themselves
live in `src/core` and are covered by the unit tests; these cases are what pins
the runtime side and the whole chain. Each case is documented in its own header
comment.

| Case | Steps | Expected |
| --- | --- | --- |
| `WhiteoutHidesAHostEntry` | the host holds `old.dll` and `keep.dll`; the layer `#USERPROFILE#` of the archive carries `packed.txt` and the whiteout marker of `old.dll` | the listing of the folder reports `keep.dll` and `packed.txt` and not `old.dll`, so the marker of a layer of the archive hides the entry of the host, and the folder of the host is untouched |
| `CapturedFolderTravelsIntoTheArchive` | the state before the change is written into the phase before the change of a snapshot file (the folder of the installation is part of it and its content is not), the installation writes a file into the folder below `#USERPROFILE#`, the state after it is written into the same file, the two phases are compared, the difference is filled (which makes the folder a folder of the view and imports the file at its own path) and the models are packed; the archive is extracted into the directory of the case and the folder of the host is removed | the layer of the archive carries the file at the path of the view the fill wrote, the folder is an entry of the view and not an import, and the packaged application reads the file of the folder although the folder of the host does not exist anymore, so the entry of the capture reached the archive |
| `StrictCaptureFillsOnlyTheCapturedEntries` | both states are read from the machine into the two phases of a snapshot file (a folder below `#USERPROFILE#` holds a file which was there before and a folder which the installation removes), the installation writes a file into the folder and removes the other folder, the difference is filled and the models are packed; the archive is extracted into the directory of the case and the folder of the host is removed | the archive carries the file the installation wrote and the single marker of the folder which is gone and neither the file which was there before nor a marker of the entries inside the hidden folder, and the packaged application reads the captured file and does not find the file which was never captured, so a capture carries the entries of the difference and not the folders which hold them |

### Realtime capture cases

The realtime capture cases (`test/e2e/RealtimeCapture_RecordsTheChangesOfARecording.cpp`)
run the chain of the realtime capture of the packer: the session of the capture
is started, the cases themselves play the installation by writing a folder with
a file below `#USERPROFILE#` and a registry value below the current user, the
recording is stopped and the analysis turns the recorded events into a snapshot
difference, which is filled into the models of a session, which is what the
`Realtime capture` mode of the `Capture` command does (see
[Capture](../docs/Capture.md)). The recordings
which follow each other are part of the cases as well: the recorder of the
window is the one recorder of every capture, so a recording which was cancelled
and one whose events were taken are given up before the next recording starts,
while a recording which still runs is refused. The start of a recording is part
of the cases as well: the events of a change a caller makes right after it are
carried by the buffers of the session until the consumer of the recording drains
them, and a case asserts the counters of the recording, which report the events
the kernel dropped instead of leaving the loss to the log. One case plays what
the user does with the shell while a capture runs: it creates a file of no byte
through a process which carries the name of `explorer.exe`, a file of no byte of
its own and a folder which holds nothing, all of them in the root of
`#USERPROFILE#`, because the shell is the process a user works with and the
capture has to carry the entries it makes. The cases need the elevation of
the test process, because the session of the capture is a system logger session
of the machine, and they are skipped without it or when the machine does not
carry the kernel events. The rules of the analysis live in `src/core` and are
covered by the unit tests; the cases pin the runtime side and the whole chain.
They are documented in their own header comments.

| Case | Steps | Expected |
| --- | --- | --- |
| `RecordsTheChangesOfARecording` | a realtime capture recording is started, the case writes a folder below `#USERPROFILE#` with a file in it and a registry key below the current user with a value while the recording runs, the recording is stopped and analyzed, the entries of the case are filled into the models of a session and the folder of the case becomes a folder of the view | the events of the recording name the folder which was created, the file inside it, the write of that file and the registry value which was written, the difference reports the folder with its file below the preset directory of the user profile and the registry value with the data the host holds while no entry is described twice, and the fill makes the folder an entry of the view and imports the file inside it and writes the registry value into the workspaces |
| `StartsANewRecordingAfterACancelledOne` | a recording is started and stopped without its events being taken, which is what the `Cancel` button of the wizard does, and a second recording is started on the same recorder, which the case plays by writing a folder with a file in it | the second recording starts, so a capture which follows a cancelled one works again, and its events name the folder of the case, so the new recording runs on a session of its own |
| `StartsANewRecordingAfterAFinishedOne` | a recording is started and played by the case, then stopped and taken, which is how a confirmed capture ends, and a second recording is started on the same recorder, which the case plays with another folder | the events of the first recording name the folder of the case, the second recording starts, its events name the second folder and the events of the first recording are not handed out twice |
| `RefusesASecondRecordingWhileOneRuns` | a recording is started and a second recording is started on the same recorder while the first one still runs, after which the case plays one step of an installation | the second recording is refused with `a recording is already running`, and the refusal leaves the running recording alone: the events of the first recording name the folder of the case |
| `RecordsTheRenameAndTheDeletionOfAnEntry` | a folder below `#USERPROFILE#` holds two files before the recording, a recording is started, the case renames one file, deletes the other one and creates a registry key with a value in it which is deleted together with the key, and the recording is stopped and analyzed | the events name the rename with the path the file was renamed to, the deletion of the other file and the write and the deletion of the value and of its key, and the difference reports the file which was renamed away and the file which was deleted as removed, the path the file was renamed to as added and the value with its key as removed |
| `RecordsTheChangeWhichFollowsTheStart` | a recording is started and the case makes its change right away: a registry key below the current user with a value in it and a folder below `#USERPROFILE#` with a file in it, which is the change an installation makes from the moment the capture runs | the events name the value which was written, the folder and the file which were created right after the start, so the recording carries the change which follows it, and the counters of the recording report that the session dropped no event |
| `CapturesTheEntriesTheShellCreated` | a recording is started and the case creates three entries in the root of `#USERPROFILE#`: a file of no byte through a process which carries the name of the shell (a copy of `cmd.exe` named `explorer.exe`, which creates the file of a redirection the way the shell creates a blank document), a file of no byte of its own and a folder which holds nothing, after which the recording is stopped and analyzed and the entries of the case are filled into the models of a session | the events name the file the shell created, the file of the case and the folder, the difference reports the three entries as added entries of the preset directory of the user profile at the root of it with the folder marked as a folder, and the fill imports both files into the layer root and creates the folder as a folder of the view, so the list of the preset directory holds the two files |

## Test helpers

* `test/utils/FsBuilder.*` — declarative tree builder. `FsRoot(root, {dirs})`
  materializes the directories of the case and returns the `LauncherConfig` of the case,
  which carries no path: the launcher resolves the state directory `data` and the resource
  directory `app` against the directory of its configuration file, which is the working
  directory of the case. The resource directories are named after the known folder token
  (`app\filesystem\#USERPROFILE#`) so that `MapBaseFS` resolves them. The builder also
  links the sandbox injection modules of the run into the resource root of the case,
  because every case which starts the launcher needs them there, and writes the registry
  isolation file of a case which declares no registry domain: the sandbox refuses a run
  whose archive carries no isolation file, so the builder writes the five root keys of
  the view with the mode `Write Copy` — which keeps the entries of the host visible and
  redirects every write into the sandbox, the behavior a sandbox without an isolation
  file has, so a case which does not exercise the modes keeps the expectations of such a
  sandbox. A case which declares its own registry domain, with the `FsRoot` content or
  with the `HiveBuilder`, keeps it. `Verify()` re-reads
  everything a case declared and fails if the content changed; the isolation files which
  the helpers of the suite write, the registry domain and the injection modules are not
  part of the declared content.
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
  **launcher** with `--X-AppBox-ConfigFile`, which launches the probe executable of the
  case (`AppBoxProbe32.exe` or `AppBoxProbe64.exe`) with the sandbox DLL injected; the
  probe asks the test process for its task over a named pipe and reports the result back.
  `ProbeCall()` takes the bit size of the probe as its last argument and defaults to the
  64 bit one, so a case which asks for the 32 bit probe names `ProbeBitness::Bit32` and
  nothing else changes. Every configuration it hands to the
  launcher enables `hide_console`, so the probe process of a case does not open a console
  window (see [Launcher console case](#launcher-console-case)). The log options of the run
  (`appbox::test::LogRun`) are passed as `--X-AppBox-LogLevel` and `--X-AppBox-LogDir` and
  collect the log files of a case in the `log` directory of that case; a case which checks
  the options leaves one of them out (see [Launcher log cases](#launcher-log-cases)).
  `ProbeStartupRun()` runs the
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
  listed for a key which the hive does not hold. The builder lists the five root keys of
  the view, which the sandbox requires of every isolation file, and a root key a case does
  not name keeps the mode of the builder, `Write Copy`; the preset of the packer is
  `Merge`, which lets a write reach the real registry, so a case which pins that behavior
  names the mode itself. The content of an entry whose mode is `Hide` is written into the
  hidden store of the hive instead of the view, which is what the packer does, so a case
  observes the packed content of a hidden entry the way the sandbox does.
  `WriteRawIsolation()` writes a text
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
* `test/utils/NtDirectoryQuery.*` — one enumeration of a directory with one
  information class through the NT entry points, which is the operation the
  `ListDirNt` probe performs and which a case may call itself. The function runs
  in the process which calls it: a call of the probe reaches the merged view of
  the sandbox, while a call of a case runs in the process of the test executable,
  which the sandbox is not injected into, and reads the answer of the file system
  for the path itself. That is what a case which checks the feature set of a
  layer needs, because the answer of the view cannot separate the class a layer
  refuses from the class the merge reads. The function opens the directory with
  `NtOpenFile` or with `CreateFileW`, enumerates it with `NtQueryDirectoryFile`
  or `NtQueryDirectoryFileEx`, may enumerate a duplicate of the handle it opened
  and may remove the entry before the query, and it reads the name of an entry
  with its own table of the classes.
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
  `ListDirNt` is a thin adapter over `test/utils/NtDirectoryQuery.*`, the shared
  enumeration of a directory: the probe opens a directory with `NtOpenFile` or,
  when the request asks for it, with `CreateFileW`, and enumerates it with
  `NtQueryDirectoryFile` or
  `NtQueryDirectoryFileEx`, so it pins both entry points of the merged view and
  both ways a directory handle reaches the sandbox, while the user mode wrappers
  may use either of them. A request may also name the information class of the
  query, and the probe reads the name of an entry with its own table of the
  classes, so a case which compares the answer with the names it created itself
  pins the layout of the class as well. A class which carries no name of an
  entry is not read at all: the probe reports the status of the call alone, so a
  case pins the refusal of the view for such a class. A request may ask for a
  handle the sandbox did not open, which the probe builds by duplicating the
  handle it opened and closing the original, and may remove the entry before it
  queries it, which is the state of a handle whose object the view no longer
  holds.
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
* `test/probe/RegTwoProcesses.*` — a probe which runs a second process of the
  same sandbox (`regchild`, the worker of `test/probe/RegChildProcess.*`) and
  exchanges registry values with it. The parent writes a value, the worker reads
  it and writes values of its own while it stays alive, the parent reads the
  value of the worker while the worker runs, and one response carries every
  observation of the run. The worker does not talk to the RPC server of the
  case: it reports its work through the exit code bits of `RegChildExitCode`,
  which is what a case reads when the registry of the worker is not visible to
  the parent.
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
  name on start and `ProbeInit` registers the command which the launcher starts
  in the probe executable (`test/probe/main.cpp`). The test executable defines
  the same objects without a function (`test/probe/Registry.cpp`), because it
  only names the probe a case asks for. `--startup_marker` carries the marker of
  the startup file a probe process was started for, which `StartupMarker()`
  returns.
* `test/probe/main.cpp` — the entry point of the probe executable: the command
  line of the probe (`probe --probe_pipe … --probe_key …`), the `regchild`
  worker of the registry cases and the MinGW wrapper of the entry point.

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

## Known gaps and limitations

* **The `...BypassAccessCheck` classes have no case.** A rename and a link are
  redirected for the eight classes of the two families, but
  `FileRenameInformationBypassAccessCheck`,
  `FileRenameInformationExBypassAccessCheck`,
  `FileLinkInformationBypassAccessCheck` and
  `FileLinkInformationExBypassAccessCheck` belong to kernel mode callers, which
  no case of the suite can drive; the cases pin the four classes a user mode
  caller reaches.
* **The 32 bit probe is covered by one case.** The suite runs the 64 bit probe
  unless a case asks for `ProbeBitness::Bit32`, and only
  `E2E_Fs.Probe32_DefaultMerge_WritesTheFileOfTheHost` asks for it: the case
  proves that a 32 bit process starts under the sandbox of the suite and that a
  write of it reaches the host filesystem. The other domains (registry,
  environment, network, fonts) are only driven in a 64 bit process so far, so a
  regression which touches a 32 bit structure layout would reach the suite only
  through the filesystem path.
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
* **The capture of the packer is verified by hand.** The rules of the capture
  live in `src/core` and are covered by the unit tests and by the two cases of
  `test/e2e/Snapshot_*.cpp` which drive the chain of a snapshot capture; the
  `Capture` command of the toolbar and the wizard it opens, with the pages of its
  steps, the progress of a run and the two lists of the last step, belong to
  `src/widget`, which no test executable links (see
  [Capture](../docs/Capture.md)).
* **The entries a capture leaves out are not driven end to end.** An entry the
  workspaces cannot express, a change which belongs to no preset directory and a
  file which the capture could not write are listed in the report of the last
  step of the wizard instead of reaching the models; the report is covered by
  `test/unit/SnapshotApply.cpp` and `test/unit/RealtimeCapture.cpp`, and a case
  which drove it would have to make the host or the model refuse a write, which
  is not reproducible on every machine.
* **The snapshot file of a run is not stored below a preset directory end to
  end.** The rule which keeps a run from recording the file it writes is covered
  by `test/unit/SnapshotCapture.cpp`; a case which drove it would have to store
  the file of the run in a folder of the host, which is where the file of a
  case does not belong.

## Related documentation

* [README.md](../README.md) — build, components and artifacts.
* [Capture.md](../docs/Capture.md) — the capture of the packer: the command and
  the steps of the wizard, the realtime capture, the snapshot capture, the
  snapshot file, the rules which apply a difference and the runtime side.
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
