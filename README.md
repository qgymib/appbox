# appbox

A Windows application sandbox system with resource isolation support.

## Overview

appbox provides runtime isolation for Windows applications, enabling controlled execution of untrusted programs with filesystem, registry, and network isolation capabilities.

## Features

- **Filesystem Isolation**: Redirects every file operation onto a layered view — a writable overlay on top of read-only base filesystems and the host — and enforces per-entry isolation modes (`Full`, `Write Copy`, `Whiteout`) that decide what the sandboxed process sees and where its modifications land (see [Filesystem Isolation](docs/FilesystemIsolation.md)).
- **Registry Isolation**: Redirects all five root keys onto a private hive file inside the overlay and enforces three isolation modes (`Full`, `WriteCopy`, `Hide`); the host registry is never modified (see [Registry Isolation](docs/RegistryIsolation.md)).
- **Network Isolation**: Answers the name resolution of the packaged application from the redirections of the workspace, and optionally carries its TCP and UDP traffic through a SOCKS5 proxy (see [Network Isolation](docs/NetworkIsolation.md)).
- **Environment Isolation**: Collects the environment variables the packaged application sees inside the sandbox with the isolation mode and the merge mode of every variable; the composed environment lives in a private table of the sandbox, so the environment of the host is never modified and the modifications of the application survive in the state directory of the sandbox (see [Environment Isolation](docs/EnvironmentIsolation.md)).

## Requirements

- CMake 3.15+
- C++17 compatible compiler
- wxWidgets 3.x (for AppBox and the loader)
- Windows SDK

## Build

### Prerequisites

1. Install CMake 3.15 or later
2. Install a C++17 compatible compiler (MSVC, GCC, or Clang)
3. Clone the repository with submodules:
   ```bash
   git clone --recursive <repository-url>
   ```

### Build Steps

```bash
# Create build directory
mkdir build && cd build

# Configure with CMake
cmake .. -G "Visual Studio 17 2022" -A x64
# Or for Ninja:
# cmake .. -G Ninja

# Build
cmake --build . --config Release
```

See [test/README.md](test/README.md) for the test suites and the way to run them.

### Build Artifacts

Every product is written below the build directory, inside a configuration
subdirectory (`Debug` or `Release`):

| Product | Path |
| --- | --- |
| `AppBox.exe` (main product) | `build/<config>/<config>/AppBox.exe` |
| `AppBoxLoader.exe` | `build/<config>/loader/<config>/AppBoxLoader.exe` |
| `AppBoxTracer.exe` (API tracer) | `build/<config>/tracer/<config>/AppBoxTracer.exe` |
| `AppBoxTests.exe` (unit and end-to-end tests) | `build/<config>/test/<config>/AppBoxTests.exe` |

## Project Components

### AppBox

The main product of the repository: a wxWidgets-based GUI application which
packages an installed application into a portable zip archive. The window
follows the three part layout of a packaging tool: a ribbon toolbar on top, a
vertical icon navigation on the left and the workspace on the right.

The navigation offers the Filesystem, Registry, Network and Environment
workspaces, which edit the isolation the packaged application runs with, and the
Settings page, which is an empty state. The table of the Environment workspace
holds one row per variable with its name, its value, its isolation mode, its
merge mode and the text which joins the value with the value of the host; the
name of the search path variable is filled in with the merge mode `Prepend` and
the separator `;`, and the two values can be changed afterwards. A value of the
Environment and of the Registry workspace may reference a known folder of the
machine which runs the sandbox (see [Variable Expansion](#variable-expansion)). The tables
explain themselves while the mouse rests on them: the header of the `Isolation`
column of the three workspaces lists the modes it offers with their meaning, and
the mode columns of the Environment workspace describe the mode of the row below
the cursor. The directory
tree of the Filesystem workspace starts
at the `Sandbox Filesystem` container and holds the preset directories below
it: `Program Files` and `Current User Directory` at the top level, with
`Documents` and `Desktop` below the profile of the user. Every preset directory
owns a layer of the archive and accepts imported folders and files, so
`Documents` and `Desktop` are packed into `app/filesystem/#Documents#` and
`app/filesystem/#Desktop#` and are mapped back to the real folders of the user
when the sandbox runs; both are resolved from their own known folder id, which
keeps them correct when the shell redirects them.

The tree names the host folder behind a node while the mouse rests on it: a
preset directory shows its real host directory, an imported folder shows the
folder it was imported from, and a folder below an import shows that folder
extended by the relative path of the folder. The Home page of the ribbon builds
the archive to the path of the `Output File` box; the configuration of a
session — imported folders, startup files and the three workspaces — travels
with the JSON project file of `File -> Export Configuration...` and is restored
by `File -> Import Configuration...`, which replaces the whole configuration
after a confirmation. Imported folders and files do not have to exist on the
machine which imports the project, so a project can be exchanged before the
packaged application is installed.

### Loader

wxWidgets-based GUI application for managing sandboxed processes:

- Extracts the packed archive, injects the sandbox DLL and starts the
  sandboxed processes.
- Keeps the read-only resources of the packed application below `app` and the
  state of the sandbox below `data`, both beside the loader program: the state
  directory is created at run time and carries the writable overlay of the
  filesystem, the registry hive the sandbox mounts (seeded from
  `app/registry/user.hiv` on the first run) and the injected sandbox DLLs.
  Deleting it resets the sandbox to the state the archive was packed with.
- Starts every startup file of its configuration which is marked for auto
  start; `--X-AppBox-Startup <trigger>` starts the single startup file with
  that trigger instead and suppresses the auto start of the other files. An
  unknown trigger starts nothing and turns into a non zero exit code.
- Starts the startup files without a console window when the configuration
  sets `hide_console`: the loader is a GUI program without a console, so a
  console program it starts would otherwise open a console window of its own.
  The window is created hidden instead of being shown, which is what an
  unattended run needs; the program keeps its console and therefore its
  standard streams. The switch is off by default and does not affect a GUI
  program, which never owns a console window.
- Offers a read-only sandbox registry browser in its admin UI, which mounts
  the hive of the overlay directly and never touches the host registry
  (see [Registry Isolation](docs/RegistryIsolation.md)).

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

Windows DLL providing runtime isolation: filesystem, registry and network
redirection via API hooks, an overlay filesystem for non-destructive testing,
and named pipe communication with the loader.

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

The names are resolved by the loader (`loader/utils/KnownFolder.*`) and handed
to the sandbox through the injected configuration; the expansion itself is the
pure function `appbox::ExpandVariables()` of
`sandbox/utils/VariableExpansion.*`.

## Documentation

- [Filesystem Isolation](docs/FilesystemIsolation.md) - Filesystem isolation architecture
- [Registry Isolation](docs/RegistryIsolation.md) - Registry isolation architecture
- [Network Isolation](docs/NetworkIsolation.md) - Network isolation architecture
- [Environment Isolation](docs/EnvironmentIsolation.md) - Environment isolation architecture
- [Tracer](docs/Tracer.md) - API tracer: usage, mechanism and measured cost
- [Tests](test/README.md) - Unit tests and end-to-end tests of the sandbox

## License

See [LICENSE](LICENSE) file for details.
