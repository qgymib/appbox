# appbox

Windows inline-hook sandbox (similar to ThinApp). OverlayFS-based file + registry isolation via DLL injection and Detours hooking.

## Project

- **Stack**: C++17, CMake 3.15+, wxWidgets (loader GUI), Detours (API hooking), spdlog (logging), nlohmann_json, CLI11, googletest
- **Entry point**: `loader/main.cpp` — wxApp `AppBoxLoader`
- **Injected DLL**: `sandbox/Sandbox.cpp` — DllMain → `OnDllAttach` → init hooks
- **Config format**: JSON (`LoaderConfig`, `SandboxConfig`) — see `loader/Config.hpp`, `sandbox/Config.hpp`

## Commands

```bash
# Full build + test + package (preferred)
cmake --workflow --preset Release

# Or step by step
cmake -S . -B build/Release --preset Release
cmake --build build/Release --preset Release
ctest --preset Release
cpack --preset Release

# Run sandbox manually
build/Release/loader/AppBoxLoader --config sandbox.json

# Run tests only
build/Release/test/AppBoxTests --loader=build/Release/loader/AppBoxLoader
```

## Architecture

| Module | Role |
|--------|------|
| `loader/` | Host EXE: reads config, sets up overlay FS, starts RPC pipe, injects sandbox DLL into target process, embeds sandbox DLLs via cmrc |
| `sandbox/` | DLL injected into target process: hooks NtCreateFile/NtOpenKey/etc. via Detours, resolves overlay paths, manages handle tracking |
| `common/` | Shared between loader+sandbox: `WString` (UTF8↔wide), `RemoteServer`/`RemoteClient` (named-pipe RPC), `Random` |
| `test/` | GTest integration tests: builds virtual FS trees with `FsBuilder`, exercises sandbox via RPC probes, verifies isolation |
| `third_party/` | CLI11, Detours, asio, base64, cmrc, expected, googletest, nlohmann_json, spdlog, wxWidgets |

**Layers**:
- `RawFS` — immutable host filesystem
- `LowerFS` — read-only overlay(s), each is a directory tree + optional `registry.reg`
- `UpperFS` — writable overlay, stores changes + `registry.hive`

See `docs/ResourceIsolation.md` and `docs/RegistryIsolation.md`.

## Conventions

- **Formatting**: Microsoft style, 120 col, `clang-format` in repo root. Run on all C++ before commit.
- **Warnings-as-errors**: `/W4 /WX` (MSVC) / `-Wall -Wextra -Werror` (GCC/Clang).
- **Namespaces**: `appbox`, sub-namespaces per module (`appbox::filesystem`, `appbox::test`, etc.).
- **Naming**: PascalCase for types, functions, files; `snake_case` for variables/members; `s_` prefix for static const; `g_` for globals.
- **Headers**: `#ifndef APPBOX_{MODULE}_{NAME}_HPP` guard, `/*! */` Doxygen-style `@brief`/`@param`/`@return`.
- **Strings**: UTF-8 `std::string` internally, `std::wstring` for Win32 API. Convert via `UTF8ToWide`/`WideToUTF8` (common/WString.hpp).
- **Error handling**: NTSTATUS with `NT_SUCCESS()` check; `throw std::runtime_error` for unrecoverable; JSON error via `tl::expected<nlohmann::json, RemoteError>` in RPC.
- **Logging**: spdlog macros (`LOG_D`, `LOG_I`, `LOG_E` wrappers from `utils/Log.hpp`), compiled at TRACE level.
- **JSON serialization**: `NLOHMANN_DEFINE_TYPE_INTRUSIVE` / `NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT` for config structs.
- **Handle management**: `HandleInfo` class tracks sandbox handles; never use fake handles — all returned handles are real kernel handles.
- **Hook pattern**: Each `NtXxx` has a `.hpp` (type def + `HookRecord` extern) + a `.cpp` (loader, hook fn, `sys_NtXxx` function pointer, `HookRecord` definition). Hook fns log params then delegate to real syscall.
- **Testing**: GTest, `TEST_F(Fs, Name)` with `CommonFixture`, build virtual FS with `FsBuilder` tree DSL, verify via RPC probes.
- **Windows-only**: The CI only runs on `windows-latest`.

## Notes
