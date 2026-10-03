# appbox

A Windows application sandbox system with resource isolation support.

## Overview

appbox provides runtime isolation for Windows applications, enabling controlled execution of untrusted programs with filesystem, registry, and network isolation capabilities.

## Features

- **Filesystem Isolation**: Redirects every file operation onto a layered view — a writable overlay on top of read-only base filesystems and the host — and enforces per-entry isolation modes (`Full`, `Write Copy`, `Merge`, `Whiteout`) that decide what the sandboxed process sees and where its modifications land; the mode of a folder is picked in the `Isolation` column of the filesystem workspace or through the isolation dialog of its tree, which also carries the mode of the root of the view (see [Filesystem Isolation](docs/FilesystemIsolation.md)).
- **Registry Isolation**: Redirects all five root keys onto a private hive file inside the overlay and enforces three isolation modes (`Full`, `WriteCopy`, `Hide`); the host registry is never modified (see [Registry Isolation](docs/RegistryIsolation.md)).
- **Network Isolation**: Answers the name resolution of the packaged application from the redirections of the workspace, and optionally carries its TCP and UDP traffic through a SOCKS5 proxy (see [Network Isolation](docs/NetworkIsolation.md)).
- **Environment Isolation**: Collects the environment variables the packaged application sees inside the sandbox with the isolation mode and the merge mode of every variable; the composed environment lives in a private table of the sandbox, so the environment of the host is never modified and the modifications of the application survive in the state directory of the sandbox (see [Environment Isolation](docs/EnvironmentIsolation.md)).
- **Patch Packages**: The `Project Type` box of the packer writes either a self-contained archive or a patch package which holds the resources of the packaged application without the launcher and without the sandbox injection modules; the packages of the `patch` directory next to a standalone archive are merged into its resources in ascending name order — the filesystem layers, the virtual registry, the network configuration and the environment variables of a package included (see [Patch Layers](docs/PatchLayer.md)).

## Requirements

- CMake 3.25+
- C++17 compatible compiler
- Windows SDK

## Build

### Prerequisites

1. Install CMake 3.25 or later — the first release which accepts the schema
   version 6 of `CMakePresets.json`. The `cmake_minimum_required` of
   `CMakeLists.txt` is a separate bound and stays at 3.15.
2. Install a C++17 compatible compiler (MSVC, GCC, or Clang)
3. Clone the repository with submodules:
   ```bash
   git clone --recursive <repository-url>
   ```

### Build Steps

Every configuration is built through the presets of `CMakePresets.json`. The
`Release` preset configures the tree below `build/Release` and builds the
release products:

```bash
# Configure and build the Release configuration
cmake --preset Release
cmake --build --preset Release
```

`cmake --workflow --preset Release` runs the configure, build, test and package
steps of the configuration in one go, and `ctest --preset Release` runs its
tests alone. The `Debug` preset wraps the same steps for development:

```bash
cmake --preset Debug
cmake --build --preset Debug
ctest --preset Debug
```

See [test/README.md](test/README.md) for the test suites and the way to run them.

### Build Artifacts

Every product is written below the build directory the preset names
(`build/Debug` or `build/Release`), inside the configuration subdirectory the
generator adds below it:

| Product | Path |
| --- | --- |
| `AppBox.exe` (main product) | `build/<config>/<config>/AppBox.exe` |
| `AppBoxLauncher.exe` | `build/<config>/launcher/<config>/AppBoxLauncher.exe` |
| `AppBoxTracer.exe` (API tracer) | `build/<config>/tracer/<config>/AppBoxTracer.exe` |
| `AppBoxTests.exe` (unit and end-to-end tests) | `build/<config>/test/<config>/AppBoxTests.exe` |

## Project Components

### AppBox

The main product of the repository: a wxWidgets-based GUI application which
packages an installed application into a portable zip archive and edits the
isolation the packaged application runs with. The window follows the three part
layout of a packaging tool: a toolbar on top, a vertical icon navigation on the
left and the workspace on the right.

The navigation offers the Filesystem, Registry, Network and Environment
workspaces, which edit the four isolation domains of the sandbox, and the
Settings workspace, whose `Output` tab holds the destination archive of the
`Build` command and the project type; a value of the Environment and of the
Registry workspace may reference a known folder of the machine which runs the
sandbox (see [Variable Expansion](#variable-expansion)). The configuration of a
session — imported folders, startup files, the project type and the four
workspaces — travels with the JSON project file of `File -> Export
Configuration...` and is restored by `File -> Import Configuration...`.

The `Project Type` box of the `Output` tab selects the product of the `Build`
command. `Standalone (ZIP)` writes a self-contained archive which carries the
launcher, its configuration, the two sandbox injection modules and the resources
of the application below `app`; `Patch (ZIP)` writes a patch package which holds
the same resources rooted at the archive root, so it can be dropped into the
`patch` directory next to a standalone archive, where the launcher merges it on
top of the resources of `app` (see [Patch Layers](docs/PatchLayer.md)).

### Launcher

Windowless application which runs a packaged application inside the sandbox: it
injects the sandbox DLLs the archive carries below `app` (`sandbox32.dll` and
`sandbox64.dll`) and starts the startup files of its configuration. The launcher
keeps the read-only resources of the packed application below `app` and the
state of the sandbox below `data`, both beside the launcher program: the state
directory is created at run time and carries the writable overlay of the
filesystem and the registry hive the sandbox mounts. Deleting it resets the
sandbox to the state the archive was packed with.

`--X-AppBox-Shell` runs the `cmd.exe` of the machine which runs the sandbox
inside the isolation instead of the application of the configuration: without a
command the shell runs interactively, with a command it runs
`cmd /c <command>`.

### Tracer

Console tool which reports the functions a program uses: it drives `cdb.exe`
to arm one-shot breakpoints on `ntdll`, `kernel32`, `kernelbase`, `ws2_32`
and `dnsapi` and collects the functions which are actually called, including
the ones of the child processes.

```
AppBoxTracer --output cmd.txt cmd.exe /c cmd.exe /c echo child
```

See [Tracer](docs/Tracer.md) for the usage, the mechanism and the measured cost.

### Sandbox

Windows DLL providing runtime isolation: filesystem, registry, network and
environment redirection via API hooks, the fonts of the packaged application
loaded into the font table of a sandboxed process, an overlay filesystem for
non-destructive testing, and named pipe communication with the launcher.

## Variable Expansion

A value of the `Environment` or the `Registry` workspace may reference a known
folder of the machine which runs the sandbox instead of spelling its path out.
The archive keeps the reference and the sandbox replaces it while it runs, so a
packed application stays correct on a machine whose folders are somewhere else —
the `Documents` and `Desktop` folder of a user are redirected into OneDrive on
many installations, for example.

A reference is spelled `%APPBOX:<NAME>%`. The prefix `APPBOX:` and the name are
compared ignoring the case, so `%appbox:documents%` names the same folder as
`%APPBOX:Documents%` does.

| Name | Known folder | Example path |
| --- | --- | --- |
| `ProgramFiles` | `FOLDERID_ProgramFiles` | `C:\Program Files` |
| `USERPROFILE` | `FOLDERID_Profile` | `C:\Users\Alice` |
| `Documents` | `FOLDERID_Documents` | `C:\Users\Alice\Documents` |
| `Desktop` | `FOLDERID_Desktop` | `C:\Users\Alice\Desktop` |
| `Windows` | `FOLDERID_Windows` | `C:\Windows` |
| `System32` | `FOLDERID_System` | `C:\Windows\System32` |
| `Fonts` | `FOLDERID_Fonts` | `C:\Windows\Fonts` |

The list follows the preset directories of the Filesystem workspace: the name of
a variable is the layer key of a preset directory without its `#` delimiters, so
a preset directory which is added to the packer later brings its variable with
it. The example paths above are the ones of a machine whose user profile is not
redirected; the sandbox always reports the real path of the machine it runs on.

Where a reference may be used:

* **Environment workspace** — the value of a variable, for example
  `PATH=%APPBOX:ProgramFiles%\Foo\Bar`, which the packaged application reads as
  `PATH=C:\Program Files\Foo\Bar`. The reference is replaced before the value is
  composed with the value of the host, so the merge modes `Prepend` and `Append`
  join the expanded text.
* **Registry workspace** — the value of a key, for a string type only:
  `REG_SZ`, `REG_EXPAND_SZ` and `REG_MULTI_SZ`, whose every item is expanded on
  its own. `REG_DWORD`, `REG_BINARY` and every other type keep their bytes, even
  when they happen to spell a reference.

The rules of the expansion:

* A reference whose name is not listed, a reference without the `APPBOX:` prefix
  (a `%PATH%` reference belongs to the shell), a reference without a closing `%`
  and a lone `%` keep their own spelling.
* An expansion is never resolved again: the text a reference was replaced with is
  copied as it is.
* The expansion belongs to the values the archive carries. The environment of
  the sandbox is composed while the sandbox starts and the hive of the registry
  is expanded while it is mounted, so a value the packaged application stores
  while it runs is reported as the application spelled it for the rest of that
  run.

The names are resolved by the launcher (`launcher/utils/KnownFolder.*`) and handed
to the sandbox through the injected configuration; the expansion itself is the
pure function `appbox::ExpandVariables()` of
`sandbox/utils/VariableExpansion.*`.

## Documentation

- [Filesystem Isolation](docs/FilesystemIsolation.md) - Filesystem isolation architecture
- [Registry Isolation](docs/RegistryIsolation.md) - Registry isolation architecture
- [Network Isolation](docs/NetworkIsolation.md) - Network isolation architecture
- [Environment Isolation](docs/EnvironmentIsolation.md) - Environment isolation architecture
- [Fonts Isolation](docs/FontsIsolation.md) - Fonts isolation architecture
- [Patch Layers](docs/PatchLayer.md) - Patch packages: layout, merge rules and the launcher side
- [Tracer](docs/Tracer.md) - API tracer: usage, mechanism and measured cost
- [Tests](test/README.md) - Unit tests and end-to-end tests of the sandbox

## License

See [LICENSE](LICENSE) file for details.
