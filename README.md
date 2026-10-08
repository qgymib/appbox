# appbox

A Windows application sandbox system with resource isolation support.

## Overview

appbox provides runtime isolation for Windows applications, enabling controlled execution of untrusted programs with filesystem, registry, network and environment isolation capabilities.

## Features

- **Filesystem Isolation**: Redirects every file operation onto a layered view — a writable overlay on top of read-only base filesystems and the host — and enforces per-entry isolation modes (`Full`, `Write Copy`, `Merge`, `Whiteout`) that decide what the sandboxed process sees and where its modifications land; the mode of a folder is picked in the `Isolation` column of the filesystem workspace or through the isolation dialog of its tree, which also carries the mode of the root of the view (see [Filesystem Isolation](docs/FilesystemIsolation.md)).
- **Registry Isolation**: Redirects all five root keys onto a private hive file inside the overlay and enforces three isolation modes (`Full`, `WriteCopy`, `Hide`); the host registry is never modified (see [Registry Isolation](docs/RegistryIsolation.md)).
- **Network Isolation**: Answers the name resolution of the packaged application from the redirections of the workspace, and optionally carries its TCP and UDP traffic through a SOCKS5 proxy (see [Network Isolation](docs/NetworkIsolation.md)).
- **Environment Isolation**: Collects the environment variables the packaged application sees inside the sandbox with the isolation mode and the merge mode of every variable; the composed environment lives in a private table of the sandbox, so the environment of the host is never modified and the modifications of the application survive in the state directory of the sandbox (see [Environment Isolation](docs/EnvironmentIsolation.md)).
- **Fonts Isolation**: Loads the fonts a layer of the filesystem carries into the font table of every process of a run, so the packaged application creates, enumerates and renders them as if they were installed, while the font directory, the font table and the registry of the host stay untouched (see [Fonts Isolation](docs/FontsIsolation.md)).
- **Patch Packages**: The `Project Type` box of the packer writes either a self-contained archive or a patch package which holds the resources of the packaged application without the launcher and without the sandbox injection modules; the packages of the `patch` directory next to a standalone archive are merged into its resources in ascending name order — the filesystem layers, the virtual registry, the network configuration and the environment variables of a package included (see [Patch Layers](docs/PatchLayer.md)).

## Build

### Prerequisites

1. Install CMake 3.25 or later.
2. Install a C++17 compatible compiler (MSVC, GCC, or Clang).
3. Clone the repository with submodules:
   ```bash
   git clone --recursive https://github.com/qgymib/appbox.git
   ```

### Build Steps

The `Release` preset of `CMakePresets.json` configures the tree below
`build/Release`, builds the release products, runs their tests and packs them
into a zip archive. A single workflow command runs all of the steps at once:

```bash
cmake --workflow --preset Release
```

See [test/README.md](test/README.md) for the test suites and the way to run them.

### Build Artifacts

The main product is written below the build directory the preset names, inside
the configuration subdirectory the generator adds below it:
`build/Release/Release/AppBox.exe`.

## Project Components

### AppBox

The main product of the repository: a wxWidgets-based GUI application which
packages an installed application into a portable zip archive and edits the
isolation the packaged application runs with. Its workspaces edit the four
isolation domains of the sandbox, hold the destination archive and the project
type of the `Build` command, and may reference a known folder of the machine
(see [Variable Expansion](#variable-expansion)).

### Launcher

Windowless application which runs a packaged application inside the sandbox: it
injects the sandbox DLLs the archive carries below `app`, starts the startup
files of its configuration, and keeps the read-only resources below `app` and
the writable state of the sandbox below `data`, both beside the launcher
program, so deleting `data` resets the sandbox to the state the archive was
packed with.

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

| Name | Example path |
| --- | --- |
| `ProgramFiles` | `C:\Program Files` |
| `ProgramFilesCommon` | `C:\Program Files\Common Files` |
| `USERPROFILE` | `C:\Users\Alice` |
| `Documents` | `C:\Users\Alice\Documents` |
| `Desktop` | `C:\Users\Alice\Desktop` |
| `AppData` | `C:\Users\Alice\AppData\Roaming` |
| `LocalAppData` | `C:\Users\Alice\AppData\Local` |
| `LocalAppDataLow` | `C:\Users\Alice\AppData\LocalLow` |
| `Downloads` | `C:\Users\Alice\Downloads` |
| `Favorites` | `C:\Users\Alice\Favorites` |
| `StartMenu` | `C:\Users\Alice\AppData\Roaming\Microsoft\Windows\Start Menu` |
| `Programs` | `C:\Users\Alice\AppData\Roaming\Microsoft\Windows\Start Menu\Programs` |
| `Startup` | `C:\Users\Alice\AppData\Roaming\Microsoft\Windows\Start Menu\Programs\Startup` |
| `ProgramData` | `C:\ProgramData` |
| `Windows` | `C:\Windows` |
| `System32` | `C:\Windows\System32` |
| `Fonts` | `C:\Windows\Fonts` |

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
- [Tracer](docs/Tracer.md) - Tracer workspace: usage, mechanism and measured cost
- [Tests](test/README.md) - Unit tests and end-to-end tests of the sandbox

## License

See [LICENSE](LICENSE) file for details.
