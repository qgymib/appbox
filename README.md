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
- Visual Studio and GCC/Clang compiler support
- Resource embedding via CMakeRC

## Requirements

- CMake 3.15+
- C++17 compatible compiler
- wxWidgets 3.x (for loader)
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
- Read-only sandbox registry browser (admin UI): a registry editor style key
  tree and value list which mounts `<overlay_fs>\registry\user.hiv` directly,
  never touching the host registry (see
  [Registry Isolation](docs/RegistryIsolation.md))

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
