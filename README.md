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
  progress dialog. While the run is going on it shows the number of packed
  files and offers `Cancel`; when the run finished the same dialog keeps its
  progress bar and presents the result instead - the outcome of the run
  (success, cancellation or failure) is never reported by a second dialog.
  `Cancel` becomes `Close` then and the dialog stays until it is dismissed

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
