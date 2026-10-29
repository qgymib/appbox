# appbox

A Windows application sandbox system with resource isolation support.

## Overview

appbox provides runtime isolation for Windows applications, enabling controlled execution of untrusted programs with filesystem, registry, network, environment and fonts isolation capabilities.

## Features

- **Filesystem Isolation**: Redirects every file operation onto a layered view — a writable overlay on top of read-only base filesystems and the host — and enforces per-entry isolation modes (`Full`, `Write Copy`, `Merge`, `Whiteout`) that decide what the sandboxed process sees and where its modifications land; the mode of a folder is picked in the `Isolation` column of the filesystem workspace or through the isolation dialog of its tree, which also carries the mode of the root of the view (see [Filesystem Isolation](docs/FilesystemIsolation.md)).
- **Registry Isolation**: Redirects all five root keys onto a private hive file inside the overlay and enforces isolation modes for a key (`Full`, `Merge`, `Write Copy`, `Hide`) and for a value (`Full`, `Hide`) that decide what the sandboxed process sees and where its modifications land; `Merge` is the only mode which modifies the real registry, the content of a hidden entry is kept in the hive instead of being dropped, a sandbox without a usable isolation file refuses to start, and the entry points which are not hooked are listed in the known gaps of the document (see [Registry Isolation](docs/RegistryIsolation.md)).
- **Network Isolation**: Answers the name resolution of the packaged application from the redirections of the workspace, and optionally carries its TCP and UDP traffic through a SOCKS5 proxy (see [Network Isolation](docs/NetworkIsolation.md)).
- **Environment Isolation**: Collects the environment variables the packaged application sees inside the sandbox with the isolation mode and the merge mode of every variable; the composed environment is installed as the environment block of the process, so every reader of the process sees it while the environment of the host is never modified, and the modifications of the application survive in the state directory of the sandbox (see [Environment Isolation](docs/EnvironmentIsolation.md)).
- **Fonts Isolation**: Loads the fonts a layer of the filesystem carries into the font table of every process of a run, so the packaged application creates, enumerates and renders them as if they were installed, while the font directory, the font table and the registry of the host stay untouched (see [Fonts Isolation](docs/FontsIsolation.md)).
- **Patch Packages**: The `Project Type` box of the packer writes either a self-contained archive, a self-extracting program or a patch package which holds the resources of the packaged application without the launcher; the packages of the `patch` directory next to a standalone archive are merged into its resources in ascending name order — the filesystem layers, the virtual registry, the network configuration and the environment variables of a package included (see [Patch Layers](docs/PatchLayer.md)).

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
packages an installed application into a portable zip archive or a
self-extracting program and edits the
isolation the packaged application runs with. Its workspaces edit the four
isolation domains of the sandbox, hold the destination archive and the project
type of the `Build` command, and may reference a known folder of the machine
(see [Variable Expansion](#variable-expansion)).

### Launcher

Application without a window of its own which runs a packaged application inside
the sandbox: it injects the sandbox DLLs the archive carries below `app`, starts
the startup files of its configuration, and keeps the read-only resources below
`app` and the writable state of the sandbox below `data`, both beside the
launcher program, so deleting `data` resets the sandbox to the state the archive
was packed with. While it prepares the sandbox it may show a startup screen,
which presents the name of the packaged application, its startup image and the
progress of the run (see [Startup Screen](docs/StartupScreen.md)). Its command
line options and the log files a run writes only when it asks for them are
described in [Launcher](docs/Launcher.md). A sandbox may be started more than
once: the runs of one archive share the master instance which prepared the
sandbox, and the requests of the other runs are served by it as well. The packer
writes the configuration of the sandbox into the image of every launcher it
writes with its hooks turned off, so the sandbox never isolates a launcher, not
even when a sandboxed program starts one (see
[Launcher](docs/Launcher.md#the-launcher-is-never-isolated)).

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

A reference is spelled `%APPBOX:<NAME>%`:

| Name | Example path |
| --- | --- |
| `%APPBOX:AppData%` | `C:\Users\Alice\AppData\Roaming` |
| `%APPBOX:Desktop%` | `C:\Users\Alice\Desktop` |
| `%APPBOX:Documents%` | `C:\Users\Alice\Documents` |
| `%APPBOX:Downloads%` | `C:\Users\Alice\Downloads` |
| `%APPBOX:Favorites%` | `C:\Users\Alice\Favorites` |
| `%APPBOX:Fonts%` | `C:\Windows\Fonts` |
| `%APPBOX:LocalAppData%` | `C:\Users\Alice\AppData\Local` |
| `%APPBOX:LocalAppDataLow%` | `C:\Users\Alice\AppData\LocalLow` |
| `%APPBOX:Music%` | `C:\Users\Alice\Music` |
| `%APPBOX:Pictures%` | `C:\Users\Alice\Pictures` |
| `%APPBOX:ProgramData%` | `C:\ProgramData` |
| `%APPBOX:ProgramFiles%` | `C:\Program Files` |
| `%APPBOX:ProgramFilesCommon%` | `C:\Program Files\Common Files` |
| `%APPBOX:ProgramFilesCommonX64%` | `C:\Program Files\Common Files` |
| `%APPBOX:ProgramFilesCommonX86%` | `C:\Program Files (x86)\Common Files` |
| `%APPBOX:ProgramFilesX64%` | `C:\Program Files` |
| `%APPBOX:ProgramFilesX86%` | `C:\Program Files (x86)` |
| `%APPBOX:Programs%` | `C:\Users\Alice\AppData\Roaming\Microsoft\Windows\Start Menu\Programs` |
| `%APPBOX:StartMenu%` | `C:\Users\Alice\AppData\Roaming\Microsoft\Windows\Start Menu` |
| `%APPBOX:Startup%` | `C:\Users\Alice\AppData\Roaming\Microsoft\Windows\Start Menu\Programs\Startup` |
| `%APPBOX:System32%` | `C:\Windows\System32` |
| `%APPBOX:SystemX86%` | `C:\Windows\SysWOW64` |
| `%APPBOX:USERPROFILE%` | `C:\Users\Alice` |
| `%APPBOX:Windows%` | `C:\Windows` |

The list follows the known folders of the machine: the name of a variable is the
layer key of a folder without its `#` delimiters, so a folder which is added to
the table later brings its variable with it. The example paths above are the
ones of a 64 bit machine whose user profile is not redirected; the sandbox
always reports the real path of the machine it runs on.

Three of the folders are only variables and no layer of the Filesystem
workspace: `ProgramFilesX64`, `ProgramFilesCommonX64` and `SystemX86` name the
64 bit folders unambiguously, which is what the capture needs to reference a
path that a 32 bit process may expand as well (`SystemX86` is also a layer of
the `SysWOW64` folder of the tree).

#### The folder of a 32 bit process

The operating system shows a different folder to a 32 bit process than to a 64
bit one, and the sandbox follows it: it picks the view which matches the bitness
of the process it is injected into, so a 32 bit application reads the folders a
32 bit process of the machine sees.

| Name | 64 bit process | 32 bit process |
| --- | --- | --- |
| `%APPBOX:ProgramFiles%` | `C:\Program Files` | `C:\Program Files (x86)` |
| `%APPBOX:ProgramFilesCommon%` | `C:\Program Files\Common Files` | `C:\Program Files (x86)\Common Files` |
| `%APPBOX:ProgramFilesX86%` | `C:\Program Files (x86)` | `C:\Program Files (x86)` |
| `%APPBOX:ProgramFilesCommonX86%` | `C:\Program Files (x86)\Common Files` | `C:\Program Files (x86)\Common Files` |
| `%APPBOX:ProgramFilesX64%` | `C:\Program Files` | not applicable |
| `%APPBOX:ProgramFilesCommonX64%` | `C:\Program Files\Common Files` | `C:\Program Files\Common Files` |
| `%APPBOX:System32%` | `C:\Windows\System32` | `C:\Windows\System32` |
| `%APPBOX:SystemX86%` | `C:\Windows\SysWOW64` | `C:\Windows\SysWOW64` |

A folder which does not exist for a 32 bit process (`ProgramFilesX64`) is the
`Not applicable` row of that table: the reference keeps its own spelling there,
exactly like a reference to a name the sandbox does not know. Every other folder
of the list is the same one in both views.

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

The capture of the packer fills the references in on its own: an absolute path of
a registry value or of an environment variable which points into a known folder
is rewritten to the reference of that folder while the difference of two
snapshots is written into the workspaces (see [Capture](docs/Capture.md)). The
capture only writes a reference whose meaning does not depend on the bitness of
the process which expands it, because it does not know that bitness:
`C:\Program Files` becomes `%APPBOX:ProgramFilesX64%` and `C:\Program
Files\Common Files` becomes `%APPBOX:ProgramFilesCommonX64%`, while
`%APPBOX:ProgramFiles%` and `%APPBOX:ProgramFilesCommon%` are never written by
the capture.

The rules of the expansion:

* A reference whose name is not listed, a reference without the `APPBOX:` prefix
  (a `%PATH%` reference belongs to the shell), a reference without a closing `%`
  and a lone `%` keep their own spelling.
* A reference which is spelled with another case keeps its own spelling as well,
  because the prefix and the name are compared by code unit. A value which names
  a variable the sandbox does not know is reported in the log, and a value which
  differs from the table in case alone is reported with the spelling of the
  table, so a mistyped reference is easy to find.
* An expansion is never resolved again: the text a reference was replaced with is
  copied as it is.
* The expansion belongs to the reader. The environment of the sandbox is
  composed while the sandbox starts, and a value of the registry is expanded
  while it is read: the hives keep the reference, so every process — a child of
  the other bit size, a later process of the run and a later run of the same
  state directory — expands the value with the folders of its own bitness. A
  value the packaged application stores while it runs is expanded the same way.
* A read of a registry value reports the expanded text. The expanded text can be
  longer than the stored one, so a caller which sizes its buffer from the size of
  the value retries with the size of the second answer, which is the
  `ERROR_MORE_DATA` retry of the registry API. The export of a key keeps the
  references, see
  [Registry Isolation](docs/RegistryIsolation.md#variable-references).

The names are resolved by `common/KnownFolder.hpp`, which the launcher, the
packer and the tests share, and handed to the sandbox through the injected
configuration; the expansion itself is the pure function
`appbox::ExpandVariables()` of `sandbox/utils/VariableExpansion.*`.

## Documentation

- [Filesystem Isolation](docs/FilesystemIsolation.md) - Filesystem isolation architecture
- [Registry Isolation](docs/RegistryIsolation.md) - Registry isolation architecture
- [Network Isolation](docs/NetworkIsolation.md) - Network isolation architecture
- [Environment Isolation](docs/EnvironmentIsolation.md) - Environment isolation architecture
- [Fonts Isolation](docs/FontsIsolation.md) - Fonts isolation architecture
- [Capture](docs/Capture.md) - Capturing the change an installation made to a machine: the realtime capture and the snapshot capture
- [Patch Layers](docs/PatchLayer.md) - Patch packages: layout, merge rules and the launcher side
- [Launcher](docs/Launcher.md) - Launcher command line options and the log files of a run
- [Startup Screen](docs/StartupScreen.md) - Startup screen of the launcher: settings, archive layout and behavior
- [Debug](docs/Debug.md) - Debug workspace: usage, mechanism and measured cost
- [Tests](test/README.md) - Unit tests and end-to-end tests of the sandbox

## License

See [LICENSE](LICENSE) file for details.
