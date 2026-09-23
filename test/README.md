# Tests

The repository has two test executables, both of them registered with CTest in
`test/CMakeLists.txt`:

| Executable | Kind | What it runs |
| --- | --- | --- |
| `AppBoxUnitTests` | In-process | Pure unit tests. It does not start the loader and does not inject the sandbox DLL, so it runs without the end-to-end probe chain of `AppBoxTests`. |
| `AppBoxTests` | End-to-end | Every case owns a private working directory, writes the artifacts of the sandbox it runs and drives a probe process through the real loader. |

Both configurations have to pass: Debug while a change is developed, Release
before it is finished.

## Running the tests

```bash
# From the repository root, with the build directory of the configuration:
ctest --test-dir build/Debug -C Debug --output-on-failure
ctest --test-dir build/Release -C Release --output-on-failure

# A single suite:
ctest --test-dir build/Debug -C Debug -R AppBoxUnitTests --output-on-failure
```

The presets of `CMakePresets.json` wrap the same steps:
`cmake --workflow --preset Debug` configures, builds, tests and packages, while
`ctest --preset Debug` runs the tests alone. The test presets set
`APPBOX_TEST_LOG_LEVEL=trace`, stop at the first failure and fail when no test
is registered at all.

CTest passes `--loader=$<TARGET_FILE:AppBoxLoader>` to both executables. A test
executable which is started by hand needs that argument as well: without it the
end-to-end cases cannot start the loader, and the unit tests which work on the
real loader payload skip themselves.

```bash
build/Debug/test/Debug/AppBoxTests.exe --loader=<absolute path of AppBoxLoader.exe>
```

`AppBoxTests` accepts these options; every one of them has an environment
variable counterpart:

| Option | Environment variable | Meaning |
| --- | --- | --- |
| `--loader` | `APPBOX_TEST_LOADER` | Path of the loader executable the cases start. |
| `--log-level` | `APPBOX_TEST_LOG_LEVEL` | `trace`, `debug`, `info`, `warn`, `err`, `critical` or `off`; default `info`. |
| `--no-cleanup` | `APPBOX_TEST_NO_CLEANUP` | Keep the working directory of a case instead of removing it. |

`AppBoxUnitTests` takes `--loader=<path>` as well: it names the loader
executable for the tests which work on the real loader payload.

## Layout

| Path | Content |
| --- | --- |
| `test/unit/` | The in-process unit tests, one file per module (`Unit_<Module>.cpp`). |
| `test/cases/` | The end-to-end cases. |
| `test/probe/` | The operations which are executed **inside** the sandbox. A probe registers itself by name (`test/probe/__init__.hpp`) and is called from a case through `ProbeCall`. |
| `test/utils/` | The builders and helpers which the cases share, see [Test helpers](#test-helpers). |
| `test/Test.cpp` / `test/Test.hpp` | The configuration of `AppBoxTests`: `--loader`, `--log-level` and `--no-cleanup`. |
| `test/unit/LoaderPath.hpp` | The loader path of `AppBoxUnitTests`. |
| `test/main.cpp` | Entry point of `AppBoxTests`. |
| `test/unit/main.cpp` | Entry point of `AppBoxUnitTests`. |

The source files of both executables are explicit lists in
`test/CMakeLists.txt`, not a glob, so a new test file has to be registered
there. The include directories of `AppBoxUnitTests` put the project
directories first, because `test/utils` holds headers with the same name as the
loader ones and the loader headers have to win.

## Unit tests

The unit tests of the registry isolation:

* `test/unit/Unit_RegistryIsolation.cpp` — the tokens of the isolation file,
  the schema of the file and the effective mode resolution (exact match,
  ancestor, default, case insensitivity, value level, malformed documents).
* `test/unit/Unit_RegistryRootMap.cpp` — the mapping of the five root keys in
  both directions (root keys, longest prefix, current user against
  `HKEY_USERS`, boundaries, non isolated paths, round trip) and the root key
  list of a fresh hive.
* `test/unit/Unit_RegistryHive.cpp` — the hive writer (all seven value types,
  several roots, nested keys, replacement of an existing hive, failure) and
  the isolation file writer.
* `test/unit/Unit_RegistryKeyPath.cpp` — prefix stripping (exact match, child,
  case, boundary, other user, longer prefix, trailing separator), path joining
  and path splitting (order, single component, empty components).
* `test/unit/Unit_RegistryEnumMerge.cpp` — merged index mapping (layer order,
  dedup with hive priority, case insensitivity, empty layers, boundaries, the
  empty name of a default value) and merged count.
* `test/unit/Unit_RegistryIsolationPolicy.cpp` — the decision table of the
  modes: the fallback of every mode for a read and for a write access mask,
  the agreement of that rule with `IsolationTable::HidesHost()`, the write
  access classification, the not-found classification, the selection of the
  failure of an open which both layers refused, the disposition of a create
  for the merged view (the host layer only changes the answer when the hive
  created the key and the mode keeps the host entry visible) and the delete
  route of a key or of a value.
* `test/unit/Unit_RegistryWhiteout.cpp` — the paths of the whiteout store: the
  marker key of a key, the marker key of its deleted values (a namespace of its
  own, so a deleted key and a deleted value never collide) and the walk from a
  path to its ancestors, which is what makes a deleted key hide its whole
  subtree.
* `test/unit/Unit_RegistryModel.cpp` — the model of the packer, including the
  mode of a single row and the explicit recursion of the isolation dialog.
* `test/unit/Unit_RegFile.cpp` — the `.reg` parser and the merge of a file into
  the model.
* `test/unit/Unit_ProjectFile.cpp` — the round trip of the virtual registry
  through a project file.
* `test/unit/Unit_PackService.cpp` — the archive carries the hive and the
  isolation file of the workspace.
* `test/unit/Unit_HiveReader.cpp` — the mounting, the enumeration and the
  formatting of the loader registry browser, including the root of the hive
  which hides the whiteout store of the sandbox.

The hook robustness contract of the sandbox — never read more than the caller
declared, never throw — is pinned down by `test/unit/Unit_Log.cpp` and
`test/unit/Unit_PipeClient.cpp`, the abort sentinel of the parameter parsers
included (`UnitLog.LoggerAbortsWhenAParameterParserThrows`).

The remaining unit tests cover one module of the packer, the loader, the
sandbox or the tracer each; the complete list is the source list of
`AppBoxUnitTests` in `test/CMakeLists.txt`. The tracer has its own section
below.

## End-to-end tests

`AppBoxTests` runs every case through the real chain: the case writes its
`LoaderConfig`, `test/utils/ProbeCall.*` starts the **loader** with
`--X-AppBox-ConfigFile`, the loader launches the test binary as a probe process
with the sandbox DLL injected, and the probe asks the test process for its task
over a named pipe and reports the result back.

### Filesystem isolation cases

The filesystem cases (`test/cases/Fs_*.cpp`) run with `Upper` as the overlay and
`Lower1` / `Lower2` as base filesystems. Each case is documented in its own
header comment.

| Case | Upper | Lower1 | Lower2 | Operation | Expected |
| --- | --- | --- | --- | --- | --- |
| `DeleteFile_MultiLower_ExistsInLower` | – | `data.txt` | `data.txt` | delete `data.txt` | success, upper whiteout created, no upper file |
| `DeleteFile_MultiLower_ExistsInLowerUpper` | `data.txt` | `data.txt` | `data.txt` | delete `data.txt` | success, upper file deleted, whiteout created |
| `DeleteFile_MultiLower_ExistsInUpper` | `data.txt` | – | – | delete `data.txt` | success, no whiteout (nothing to hide) |
| `DeleteFile_MultiLower_NonExists` | – | `data1.txt` | `data2.txt` | delete `data.txt` | failure, no whiteout |
| `DeleteFile_WhiteoutInLower_ExistsInUpper` | `data.txt` | `data.txt.$APPBOX_DELETE$` | `data.txt` | delete `data.txt` | success, upper file deleted, no whiteout in upper |
| `ListDir_MultiLower_ExistsInLower` | – | `F.txt` | `F.txt` | list `#APPDATA#` | `F.txt` appears exactly once, host entries also listed |
| `ListDir_MultiLower_ExistsInLower_WhiteoutInUpper` | `F.txt.$APPBOX_DELETE$` | `F.txt` | `F2.txt` | list `#APPDATA#` | `F.txt` hidden, `F2.txt` listed once |
| `NewFile_MultiLower_WhiteoutInLower` | – | `data.txt.$APPBOX_DELETE$` | `data.txt` | create `data.txt` (`CREATE_NEW`) | success, file created in upper |
| `NewFile_MultiLower_WhiteoutInUpper` | `data.txt.$APPBOX_DELETE$` | `data.txt` | `data.txt` | create `data.txt` (`CREATE_NEW`) | success, upper whiteout removed, file created in upper |
| `ReadFile_MultiLower_WhiteoutInUpper` | `data.txt.$APPBOX_DELETE$` | `data.txt` | `data.txt` | read `data.txt` | failure |

`test/cases/Fs_LaunchProcess_FromLower.cpp` and
`test/cases/Fs_QueryAttributes_MultiLower.cpp` are the remaining filesystem
cases; they are not part of the matrix above.

### Registry isolation cases

* `test/cases/Reg_WriteValue_NewKey.cpp` — the closed loop: the sandboxed
  probe creates a key, writes a value and reads it back; the test process
  verifies that the real HKCU does **not** contain the key and that the hive
  file exists in the overlay.
* `test/cases/Reg_ReadValue_RealFallback.cpp` — a key and value which only
  exist in the real HKCU (created by the test process with RAII cleanup) are
  readable inside the sandbox, and the key still exists afterwards.
* `test/cases/Reg_EnumerateKey_Merged.cpp` — the sandboxed probe enumerates
  the merged sub key view (real `RealA`/`RealB` plus sandbox `SandboxC`),
  `RegQueryInfoKeyW` reports the merged count, and the real registry still
  holds only the real sub keys.
* `test/cases/Reg_EnumerateValue_Merged.cpp` — the sandboxed probe enumerates
  the merged value view and reads every enumerated value back (enumeration and
  query stay consistent); the real registry does not gain the sandbox value.
* `test/cases/Reg_EnumerateValue_DefaultValue.cpp` — the default value of a key
  is an entry of the merged value enumeration like every other value: the
  enumeration of a hive key reports the default value and the named values
  exactly once each with their own data and the count agrees, the merged view
  of a shadow key lists the default value of the real key, and the default
  value of a name both layers hold is answered by the hive layer.
* `test/cases/Reg_ShadowKey_HidesRealEntriesOfSameName.cpp` — a shadow key
  hides the value and the sub key of the real registry which carry the same
  name: the value enumeration and the sub key enumeration report the name once
  each with the data of the hive layer, the shadowed sub key answers from the
  hive, and the real entries keep their own data. The case pins the intended
  merge semantics of a name both layers hold.
* `test/cases/Reg_ShadowKey_ReadRealValue.cpp` — a shadow key created inside
  the sandbox reads a value which only exists in the real key, the create
  reports `REG_OPENED_EXISTING_KEY` (the merged view of `WriteCopy` holds the
  key), and the real value is unchanged.
* `test/cases/Reg_WriteCopy_CreateDisposition.cpp` — the disposition of an
  isolated create follows the merged view: a key which only the host holds and
  a key which the hive holds report `REG_OPENED_EXISTING_KEY`, while a key
  which neither layer holds and a `Full` key which the host holds report
  `REG_CREATED_NEW_KEY`; the host keys keep their own values.
* `test/cases/Reg_QueryKeyName_ViewPath.cpp` — `NtQueryKey` and
  `NtQueryObject` on a redirected handle report the view path and never the
  private mount prefix.
* `test/cases/Reg_Full_HidesRealKey.cpp` — a key of the host which is marked
  `Full` and which the hive does not hold reports `ERROR_FILE_NOT_FOUND`
  inside the sandbox for a read access open and for a write access open (the
  key is never copied up), and the host key is unchanged.
* `test/cases/Reg_WriteCopy_WriteOpen_CopyUp.cpp` — a write access open of a
  key which only the host holds copies the key up into the hive: the write
  lands in the hive, the host key keeps its own values, and a write access
  open of a key which neither layer holds still reports a missing key.
* `test/cases/Reg_Hide_CreateInSandbox.cpp` — a key which the host holds and
  which is marked `Hide` is created inside the hive, the closed loop of the
  write and the read back runs against the hive, and the host key is
  unchanged.
* `test/cases/Reg_Full_HiveWinsOverReal.cpp` — a key which exists in the hive
  and in the host is answered by the hive.
* `test/cases/Reg_Full_HidesRealValue.cpp` — a single value of the host which
  is marked `Full` is not readable, while the other values of the same key
  keep their read through.
* `test/cases/Reg_Full_EnumerateHidesReal.cpp` — the merged enumeration and
  the merged count hide the isolated host sub key and show the visible one.
* `test/cases/Reg_Full_NonHkcuRoot.cpp` — the hive is visible through
  `HKEY_LOCAL_MACHINE`, `HKEY_CLASSES_ROOT` and `HKEY_CURRENT_CONFIG`, and an
  isolated key of the machine root is not.
* `test/cases/Reg_DeleteKey_WhiteoutHostKey.cpp` — a key which only the host
  holds is deleted inside the sandbox: the delete succeeds, the key is gone from
  the view (the read through does not resurrect it) and the real registry keeps
  the key and its value.
* `test/cases/Reg_DeleteKey_ShadowKeyRemoved.cpp` — a delete removes the shadow
  key of the hive and the key stays gone, so the read through of the host key of
  the same name does not bring it back; the host key is unchanged.
* `test/cases/Reg_DeleteKey_NonEmpty.cpp` — a key which still holds a visible sub
  key is refused with `ERROR_ACCESS_DENIED` (the kernel status
  `STATUS_CANNOT_DELETE`), after the sub key was deleted inside the sandbox the
  delete of the key succeeds, and the host keeps the key, its sub key and its
  value.
* `test/cases/Reg_DeleteKey_ReadHandle.cpp` — a delete through a read through
  handle of the host layer is refused with `STATUS_ACCESS_DENIED` and the host
  key survives; a read access handle never becomes a delete.
* `test/cases/Reg_DeleteValue_WhiteoutHostValue.cpp` — a value which only the
  host holds is deleted inside the sandbox: the read reports
  `ERROR_FILE_NOT_FOUND`, the merged value enumeration no longer lists the name,
  the other value of the key keeps its read through and the host keeps both.
* `test/cases/Reg_DeleteValue_ShadowValue.cpp` — the value of the hive is
  removed and the value of the host of the same name does not reappear (the
  delete records it as deleted), while the host keeps its own value.
* `test/cases/Reg_QueryMultipleValues_Mixed.cpp` — a batch which mixes a hive
  value and a host value is answered completely with the data and the type of
  every entry, a batch of hive values is answered as well, a batch which names a
  value the isolation hides fails as a whole with `ERROR_FILE_NOT_FOUND`, and
  the host is unchanged.
* `test/cases/Reg_SaveKey_MergedSnapshot.cpp` — a save exports the merged view
  of the key (the hive value and the visible host value), the host value which
  the isolation hides is not part of the file, a key which only the hive holds
  is exported with its content, and the host registry is unchanged.

### Other cases

* `test/cases/ArugmentsPassthrough.cpp` and `test/cases/RPC.cpp` — the cases
  which do not exercise an isolation domain.

## Test helpers

* `test/utils/FsBuilder.*` — declarative tree builder. `FsRoot(root, {Upper, Lower1, Lower2})`
  materializes the directories and returns a `LoaderConfig` whose `overlay_fs` is the
  first entry and whose `base_fs` holds the rest; the lower layer directories are named
  after the known folder token (`#APPDATA#`) so that `MapBaseFS` resolves them.
  `Verify()` re-reads the lower layers and fails if their content changed.
* `test/utils/CommonFixture.*` — gives every case a private working directory.
* `test/utils/CWD.*` — the working directory itself: `Create()` makes it,
  `NoCleanup()` keeps it after the case.
* `test/utils/ProbeCall.*` — writes the `LoaderConfig` to `config.json`, starts the
  **loader** with `--X-AppBox-ConfigFile`, which launches the test binary as a probe
  process with the sandbox DLL injected; the probe asks the test process for its task
  over a named pipe and reports the result back.
* `test/utils/HiveBuilder.*` — builds the artifacts of a test sandbox, so an
  end-to-end test owns what the sandbox mounts. The builder writes the hive
  file and the isolation file of an overlay directly and tracks the content of
  the hive and the isolation modes apart, so a mode can be listed for a key
  which the hive does not hold.
* `test/utils/RealHkcuKey.*` — RAII helper which owns a key below the real
  HKCU of the test process, so a case which needs a host entry leaves nothing
  behind.
* `test/utils/RegistryRootKey.*` — resolves the predefined handle of a registry
  root key, which is what a probe passes to the registry API.
* `test/utils/KnownFolder.*` — path of a known folder.
* `test/utils/ReadFileFull.*` / `test/utils/WriteFileFull.*` — file I/O
  helpers.
* `test/utils/Semaphore.*` — synchronization between the test process and the
  probe process.
* `test/probe/*` — the operations executed inside the sandbox (`CreateFileW`,
  `CreateDirectoryW`, `DeleteFileW`, `ListDir`, `ReadFileFull`), plus the
  probes of the remaining cases (`LaunchProcess`, `QueryAttributes`).
* `test/probe/RegWriteValue.cpp` / `RegReadValue.cpp` — the operations
  executed inside the sandbox.
* `test/probe/RegEnumKey.cpp` / `RegEnumValue.cpp` — sub key and value
  enumeration inside the sandbox.
* `test/probe/RegShadowRead.cpp` / `RegQueryKeyName.cpp` — shadow key value
  reads and kernel object name queries inside the sandbox.
* `test/probe/RegOpenWriteValue.cpp` — a write access open of an existing key
  inside the sandbox, followed by a value write and a read back.
* `test/probe/__init__.hpp` — the probe registry: a probe registers itself by
  name on start and `ProbeInit` registers the command which the loader starts.

## Tracer tests

* `test/unit/Unit_Tracer*.cpp` — the parser, the PE reader, the scope rules, the
  breakpoint plan, the report and the command line, all without a debugger.
* `test/unit/Unit_TracerIntegration.cpp` — a real run of `cmd.exe` below the real
  debugger, including a child process; it skips itself when `cdb.exe` is not
  installed.

## Related documentation

* [README.md](../README.md) — build, components and artifacts.
* [FilesystemIsolation.md](../docs/FilesystemIsolation.md) — filesystem
  isolation architecture.
* [RegistryIsolation.md](../docs/RegistryIsolation.md) — registry isolation
  architecture.
* [Tracer.md](../docs/Tracer.md) — API tracer: usage, mechanism and measured
  cost.
