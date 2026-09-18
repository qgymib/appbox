# appbox

A Windows application sandbox system with resource isolation support.

## Overview

appbox provides runtime isolation for Windows applications, enabling controlled execution of untrusted programs with filesystem, registry, and network isolation capabilities.

## Features

### Resource Isolation

- **Filesystem Isolation**: Three-layer filesystem architecture
- **Registry Isolation**: Registry key isolation (documented)
- **Network Isolation**: Network access control (documented)

### Build System

- CMake-based build system
- 32-bit and 64-bit sandbox DLL support
- Every third-party dependency is built as a static library and linked into
  the executables; the sandbox injection modules
  (`AppBoxSandbox32.dll` / `AppBoxSandbox64.dll`) are the only dynamic
  artifacts
- Visual Studio and GCC/Clang compiler support
- Resource embedding via CMakeRC

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

# Run tests
ctest -C Release --output-on-failure
```

### Build Artifacts

Every product is written below the build directory, inside a configuration
subdirectory (`Debug` or `Release`):

| Product | Path |
| --- | --- |
| `AppBox.exe` (main product) | `build/<config>/<config>/AppBox.exe` |
| `AppBoxLoader.exe` | `build/<config>/loader/<config>/AppBoxLoader.exe` |
| `AppBoxUnitTests.exe` | `build/<config>/test/<config>/AppBoxUnitTests.exe` |
| `AppBoxTests.exe` (end-to-end) | `build/<config>/test/<config>/AppBoxTests.exe` |

`AppBox.exe` is described directly in the top level `CMakeLists.txt`, so its
target directory is the top of the build tree; the loader and the test
executables keep their own subdirectory scripts.

### Architecture-Specific Build

**MSVC**: Use `-A Win32` or `-A x64` to select architecture.

**GCC/Clang**: Requires multilib support (`gcc-multilib`, `g++-multilib`).

## Project Components

### Common Module

Shared utilities used across the project:
- `BuildCommandLine`: Build command line parsing
- `CRC32`: CRC32 checksum computation
- `Random`: Random number generation
- `RemoteClient/RemoteServer/RemoteSession`: RPC communication
- `SetLogLevel`: Logging configuration
- `WString`: Wide string utilities

### Loader

wxWidgets-based GUI application for managing sandboxed processes:
- Pipe-based RPC communication with sandbox
- Configuration management
- Process injection
- Window icon taken from the icon resource of the loader executable, so a
  packed loader keeps the icon of the loader in its title bar and on its
  taskbar button even when the executable carries the icon of a packaged
  application as well
- Read-only sandbox registry browser (admin UI): a registry editor style key
  tree and value list which mounts `<overlay_fs>\registry\user.hiv` directly,
  never touching the host registry (see
  [Registry Isolation](docs/RegistryIsolation.md))

### AppBox

The main product of the repository: a wxWidgets-based GUI application which
packages an installed application into a portable zip archive. Its sources
live in `src/` and the build product is `AppBox.exe`. The window follows the
three part layout of a packaging tool: a ribbon toolbar on top, a vertical
icon navigation on the left and the workspace on the right.

- **Ribbon toolbar**: the `Home` page carries the `Capture`, `Snapshot`,
  `Build`, `Startup`, `Output` and `Publish` groups; the `Advanced` page
  holds reserved groups. `Build` writes the archive to the path of the
  `Output File` box, `Build and Run` additionally extracts it to a temporary
  folder and starts the packaged loader, and `Startup Files` opens the startup
  file tree: a tree table of the preset directories and their imported folders
  with the `Name`, `Type` and `Startup` columns, whose `Startup` checkbox
  column marks the executable the packaged application starts. Exactly one
  executable is the startup file, so checking one unchecks the previous one.
  The file name of that executable names the loader program and its launch
  configuration inside the archive. Groups without a counterpart in the packer
  are shown disabled.
- **Navigation**: Filesystem / Registry / Network / Settings; only the
  filesystem workspace is implemented, the other pages are empty states.
- **Filesystem workspace**: the tree lists the preset directories (Program
  Files, Current User Directory) with their imported folders, and expands
  into the host subfolders of an import on demand. The toolbar row above the
  file list offers `Add Files`, `Add Folder`, `New Folder` (reserved),
  `Remove`, `Up Dir` and a filename search box.
- **File list**: the columns `Filename`, `Isolation`, `Hidden`, `No Sync`,
  `Read Only`, `No Upgrade`, `Size` and `Source Path`. The isolation
  attributes are read only: the packer always isolates fully, so they only
  document the runtime behaviour. `Source Path` shows the virtual path of the
  entry inside the sandbox view, e.g. `#ProgramFiles#\MyApp\app.exe`.
- **Imports**: `Add Folder` imports a host folder which becomes a
  subdirectory of a preset directory; `Add Files` imports individual host
  files into a folder of an already imported tree. Both are recorded in
  `PackModel` and participate in packing.
- The archive contains the embedded loader (compiled in via CMakeRC), the
  generated launch configuration, the imported folders below
  `filesystem/<layer key>/<import name>` and the individually imported files
  below `filesystem/<layer key>/<target directory>/<file name>`. The loader
  program and its configuration carry the file name of the startup file, so a
  startup file named `foo.exe` is packed as the archive entries `foo.exe` and
  `foo.exe.json`; extracting the archive and running `foo.exe` starts the
  sandboxed application. The packed loader also carries the file icon of the
  startup file: the icon group which the shell shows for that program is
  appended to the loader image, so Explorer shows the icon of the packaged
  application for `foo.exe` while the icon resources of the loader - and with
  them its window icon - stay untouched. A startup file without an icon leaves
  the icon of the loader in place
- **Build progress**: `Build` and `Build and Run` report through a single
  progress dialog. While the run is going on its first line shows the number of
  handled files and the elapsed time in `mm:ss`, e.g.
  `Packing files: 12 / 340 - Elapsed 00:12`, and the file it is working on
  follows on a line of its own, which the dialog shows as a detail line in a
  smaller font; `Cancel` is offered throughout. The file is shown as the path
  below the import root, so it stays readable for deeply nested host folders.
  The dialog carries no collapsible details area: the elapsed time is part of
  the message, because the timing flags of wxWidgets would hide it behind a
  `Show details` button. The extracting stage of `Build and Run` reports the
  same way and continues the progress bar where the packing stage ended. The
  native dialog keeps the size it was created with, so it is re-fitted whenever
  a message needs more room than every message before it; without this the
  buttons at its bottom would be cut off as soon as a file path makes the
  message longer. When the run finished the same dialog keeps its progress bar
  and presents the result instead - the outcome of the run (success,
  cancellation or failure) is never reported by a second dialog, and a
  successful run names the archive it wrote on a `Saved to: <path>` line.
  `Cancel` becomes `Close` then and the dialog stays until it is dismissed
- **Configuration import and export**: `File -> Export Configuration...` writes
  the current configuration (imported folders, imported files, main program and
  the `Output File` path) into a JSON project file, and
  `File -> Import Configuration...` restores a configuration from such a file.
  Importing replaces the whole configuration: an existing one is only dropped
  after the user confirmed the replacement, and a file which cannot be read
  leaves the current configuration untouched. Imported folders and files
  recorded in a project file do not have to exist on the machine which imports
  it, so a project can be exchanged before the packaged application is
  installed; a missing source is reported by the pack run instead

#### Configuration Project File

A project file only records the configuration - it never copies the imported
content - and is JSON text encoded as strict UTF-8 without a byte order mark.
Paths are stored the way they exist on the machine which exported them, so a
project file is not portable between machines with different install locations.

```json
{
  "version": 1,
  "output_path": "D:\\out\\MyApp.zip",
  "folders": [
    { "preset": "program_files", "name": "MyApp", "source": "C:\\Program Files\\MyApp" }
  ],
  "files": [
    { "preset": "user_profile", "target_dir": "MyApp\\data",
      "name": "settings.ini", "source": "C:\\tmp\\settings.ini" }
  ],
  "main_program": { "preset": "program_files", "folder": "MyApp", "path": "bin\\app.exe" }
}
```

| Member | Meaning |
| --- | --- |
| `version` | Format version of the file; the current format is version 1 and a different version is rejected |
| `output_path` | Path of the `Output File` box, empty when none was chosen |
| `folders` | Imported folders; `preset` is the preset directory, `name` the subdirectory below it and `source` the imported host folder |
| `files` | Individually imported files; `target_dir` is relative to the preset directory and starts with the name of an imported folder |
| `main_program` | Startup file; omitted while no main program is selected |

The file is written and read as strict UTF-8: a UTF-16 or UTF-32 byte order
mark and malformed UTF-8 bytes are rejected with an encoding error instead of
being decoded with replacement characters, while a leading UTF-8 byte order
mark is accepted. This project file is not the launch configuration of the
loader inside a packed archive (`<entry name>.json`, see the archive layout
above), which uses its own schema.

### Sandbox

Windows DLL providing runtime isolation:
- Filesystem redirection via API hooks
- Overlay filesystem for non-destructive testing
- Named pipe communication with loader

## Documentation

- [Filesystem Isolation](docs/FilesystemIsolation.md) - Filesystem isolation architecture
- [Registry Isolation](docs/RegistryIsolation.md) - Registry isolation architecture
- [Network Isolation](docs/NetworkIsolation.md) - Network isolation architecture

## License

See [LICENSE](LICENSE) file for details.
