# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.0] - 2026-11-20

AppBox: a Windows application sandbox system with resource isolation support.

### Added

- **Filesystem isolation**: every file operation of a sandboxed process is
  redirected onto a layered view — a writable overlay on top of read-only base
  filesystems and the host — and per-entry isolation modes (`Full`, `Write
  Copy`, `Merge`, `Whiteout`) decide what the process sees and where its
  modifications land; the mode of a folder is picked in the `Isolation` column
  of the filesystem workspace or through the isolation dialog of its tree
  ([Filesystem Isolation](docs/FilesystemIsolation.md)).
- **Registry isolation**: all five root keys are redirected onto a private hive
  file inside the overlay; isolation modes for a key (`Full`, `Merge`, `Write
  Copy`, `Hide`) and for a value (`Full`, `Hide`) decide what the sandboxed
  process sees and where its modifications land, `Merge` is the only mode which
  modifies the real registry, and the content of a hidden entry is kept in the
  hive instead of being dropped
  ([Registry Isolation](docs/RegistryIsolation.md)).
- **Network isolation**: the name resolution of the packaged application is
  answered from the redirections of the workspace, and its TCP and UDP traffic
  is optionally carried through a SOCKS5 proxy
  ([Network Isolation](docs/NetworkIsolation.md)).
- **Environment isolation**: the environment variables the packaged application
  sees are composed with the isolation mode and the merge mode of every
  variable and installed as the environment block of the process, so the
  environment of the host is never modified and the modifications of the
  application survive in the state directory of the sandbox
  ([Environment Isolation](docs/EnvironmentIsolation.md)).
- **Fonts isolation**: the fonts a filesystem layer carries are loaded into the
  font table of every process of a run, so the packaged application creates,
  enumerates and renders them as if they were installed while the font
  directory, the font table and the registry of the host stay untouched
  ([Fonts Isolation](docs/FontsIsolation.md)).
- **Packer**: the `Project Type` box of the packer writes either a
  self-contained archive, a self-extracting program or a patch package; the
  packages of the `patch` directory next to a standalone archive are merged
  into its resources in ascending name order, carrying the filesystem layers,
  the virtual registry, the network configuration and the environment variables
  of the package ([Patch Layers](docs/PatchLayer.md)); the capture records the
  change an installation made to a machine — the realtime capture and the
  snapshot capture — and writes the difference into the workspaces
  ([Capture](docs/Capture.md)); the tree of the filesystem view offers an
  expand menu, and the settings of the startup screen are part of the project.
- **Launcher**: a windowless program runs a packaged application inside the
  sandbox — it injects the sandbox DLLs the archive carries below `app`, starts
  the startup files of its configuration and keeps the read-only resources
  below `app` and the writable state below `data`; a sandbox may be started
  more than once, the runs of one archive sharing the master instance which
  prepared the sandbox, and the launcher itself is never isolated, not even
  when a sandboxed program starts one; its command line options and the log
  files a run writes only when it asks for them are described in
  [Launcher](docs/Launcher.md); while it prepares the sandbox it may show a
  startup screen whose image is embedded into the launcher as an executable
  resource ([Startup Screen](docs/StartupScreen.md)).
- **Variable expansion**: a value of the `Environment` or the `Registry`
  workspace may reference a known folder of the machine as `%APPBOX:<NAME>%`;
  the archive keeps the reference and the sandbox expands it while it runs with
  the folders of the machine it runs on and the bitness of the reader, so a
  packed application stays correct on a machine whose folders are somewhere
  else; the capture rewrites the absolute paths it records into the references
  of the folders they point into (see
  [Variable Expansion](README.md#variable-expansion)).
- **Version resources**: the version of the project is embedded into
  `AppBox.exe` and `AppBoxLauncher.exe` as `VS_VERSION_INFO` version
  information, so the Explorer properties of both programs show the version
  they were built with.
