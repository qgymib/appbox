# Patch Layers

A patch package carries a part of the resources of a packaged application and is
merged into a standalone archive when the sandbox starts. It lets a published
application be updated without repacking and redistributing the whole archive:
the resources of `app` stay the base image of the sandbox, and every patch of
the `patch` directory is applied on top of them in ascending name order.

> **Implementation status**: the `Patch (ZIP)` project type of the packer and
> the patch package it writes are implemented, and the launcher consumes all four
> domains of the packages of the `patch` directory: it validates every package
> against the `cache` directory, mounts the filesystem layers of a package on
> top of the layers of `app`, merges the hive of a package into the hive the
> sandbox mounts and hands the isolation files of the four domains to the
> sandbox, which merges them in layer order.

## Patch package

A patch package is a zip archive written by the `Patch (ZIP)` project type of
the `Project Type` box of the packer. It holds the very same resource tree a
standalone archive keeps below `app`, rooted at the archive root instead: the
launcher program, its configuration, the two sandbox injection modules and the
`app` directory itself do not travel, because a patch is applied by the launcher
of a standalone archive and not started on its own.

```
<patch>.zip
├── filesystem/isolation.json           isolation modes of the filesystem
├── filesystem/<layer key>/<import>/... imported content (read-only layers)
├── registry/user.hiv                   virtual registry of the workspace
├── registry/isolation.json             isolation modes of the registry
├── network/isolation.json              DNS redirections and proxy
└── environment/isolation.json          environment variables of the workspace
```

The entry names are the resource relative names of
[`common/SandboxLayout.hpp`](../common/SandboxLayout.hpp): the archive relative
name of a standalone archive is the name of `app`, a slash and the resource
relative name, which is what the packer and the launcher share.

A patch project needs no startup file: a patch package carries no launcher, so it
cannot start a program. The `Build and Run` command is offered for a standalone
project only.

## Directory layout

```
.
├── <startup>.exe            launcher payload of the standalone archive
├── <startup>.exe.json       launcher configuration of the standalone archive
├── app/                     read-only resources, the base image of the sandbox
├── cache/                   extracted patches, created by the launcher
│   ├── 00-foo/
│   │   ├── filesystem/...   extracted resources of the package
│   │   └── md5.txt          digest of the package they were extracted from
│   └── 01-bar/
│       └── md5.txt
├── data/                    writable state of the sandbox, created by the launcher
└── patch/                   patch packages, created by the user
    ├── 00-foo.zip
    └── 01-bar.zip
```

- `patch` is created by the user. A package only takes effect while it is
  inside that directory, next to the launcher of the standalone archive.
- `cache` is created by the launcher as soon as the `patch` directory holds at
  least one package. Neither directory travels in the archive: the packer writes
  the resources only.
- The patches of the directory are applied in ascending name order of their file
  name (an ASCII comparison), so `00-foo.zip` is applied before `01-bar.zip`.
  The file names are the order of the layers and nothing else: a package may be
  replaced as long as its name keeps its place in that order.
- The cache entry of a package is its file name without the extension, so
  `00-foo.zip` is extracted into `cache/00-foo`. Two packages which only differ
  in the case of their extension would share an entry, so the launcher logs the
  second one and skips it.
- The digest of the package an entry was extracted from is recorded in
  `cache/<name>/md5.txt`, and the file is the last file the launcher writes into
  the entry: an entry whose digest is missing or does not match the package is
  dropped and extracted again, so a run which was interrupted half way cannot
  look like a complete extraction.
- Deleting `cache` only costs the extraction of the next run. The launcher does
  not delete the entry of a package which the user removed from `patch`: the
  directory holds what the runs so far extracted, and deleting it resets that.
- `app` carries the injection modules the launcher injects (`app/sandbox32.dll`
  and `app/sandbox64.dll`) beside the four domains, so the modules belong to the
  archive which is started. A patch package carries none of them: a package
  never replaces the modules of the archive it is applied to.

## Merge rules

`app` is the base image of the sandbox and every patch overrides the layer below
it: the content of `app` is overridden by `00-foo.zip`, which is overridden by
`01-bar.zip`. Overriding is per resource, not per package: a patch which holds a
single isolation file keeps every other resource of the layers below it.

### Filesystem

The launcher mounts the layers of the packages **before** the layers of `app`,
the last package of the ascending order first, because the resolution of the
sandbox prefers the layer which was mounted first: a file which exists in a
package and in `app` is the file of the package, and the file of `01-bar.zip`
wins over the same file of `00-foo.zip`, which wins over the file of `app`. A
resource which no package carries stays the resource of `app`. Mounting the
packages in front of the archive leaves the resolution of the sandbox untouched;
within one resource root the precedence of the more specific preset directory
(`#Documents#` before `#USERPROFILE#`) is unchanged.

The isolation modes are merged the same way. Every layer contributes the
isolation file of its filesystem domain, and the sandbox applies the files in
layer order: the file of `app` first, then the file of every package in
ascending order. An entry of a later file replaces the mode of the same path, so
the mode of the last layer which names a path is the mode the sandboxed process
observes, and a path which no file names follows the closest entry above it and
the default of the view. A folder which `app` isolates as `Write Copy` and
`01-bar.zip` isolates as `Full` is `Full` for the sandbox.

The merge is per path and not per subtree: a package which names a folder leaves
the modes of the entries below it in place when the layers below named them,
because the sandbox resolves the mode of a path through the closest listed
entry.

### Registry

The virtual registry of `app` is the hive the launcher seeds into the state
directory of the sandbox, and the sandbox mounts exactly that hive: a package
does not become a layer of its own, its hive is **merged into** the hive the
sandbox mounts. The launcher applies the hives of the packages in ascending name
order, so the keys and the values of the last package which names an entry are
the ones the sandboxed process observes, while an entry no package names keeps
the content of `app` and of the earlier runs. The merge works per key and per
value, so a package which lists a single value keeps every other entry of the
layers below it, and a package which carries no `registry/user.hiv` keeps the
hive of those layers.

The merge runs **at every start** and it writes into the hive of the state
directory, so the packages are the top of the registry of a run:

- An entry a package names is the entry of the package at every start. A
  modification or a deletion of such an entry inside the sandbox is therefore
  reset by the next start — a deletion included, because the entry is written
  into the hive again while the whiteout marker of the deletion only hides an
  entry the hive does not hold.
- An entry no package names keeps the state of the sandbox: a value the
  sandboxed application wrote, and a key or a value it created, survive the
  next run exactly like they do without a patch directory.

The isolation modes of the registry are merged the same way as the modes of the
filesystem: every layer contributes the isolation file of its registry domain
and the sandbox applies the files in layer order, so the mode of the last file
which names a key or a value is the mode the sandboxed process observes and an
entry no file names follows the closest entry above it and the default mode
`Write Copy`.

### Environment

The environment is composed layer by layer, starting at the environment of the
host and continuing with the isolation file of `app` and the isolation files of
the patches in ascending order. Every layer applies its own rule to the result
of the layers below it, which is what `ComposeEnvironmentValue()` of
[`common/EnvironmentIsolation.hpp`](../common/EnvironmentIsolation.hpp)
expresses: `Host` passes the value below it through, `Replace` drops it, and
`Prepend`/`Append` join the value of the layer with the value below it through
the merge string of the layer. `Full` drops the value below the layer as well,
because the value the layers below composed is not part of the environment of a
layer which isolates the variable from it.

A host `PATH` of `vx`, a `00-foo.zip` which sets `PATH` to `v0` with `Prepend`
and a `01-bar.zip` which sets `PATH` to `v1` with `Append` therefore compose to
`v0;vx;v1` for the sandboxed process.

The composition is per variable: a variable no later layer names keeps the value
the layers below it composed, so a package overrides the variables it names and
not the environment of the layers below it. The state file of the sandbox
(`data/environment/state.json`) is applied after the last layer, so a value the
application stored wins over every layer, exactly like a write of the
application wins over the lower layers of the filesystem.

### Network

The DNS redirections and the proxy of the network isolation files are merged
layer by layer:

- The redirections are merged per hostname: a redirection of `01-bar.zip`
  overrides a redirection of the same hostname of `00-foo.zip`, while a hostname
  no later file lists keeps the redirection below it. A file which lists no
  redirection therefore keeps every redirection below it.
- The proxy is merged per file: the proxy of the last file which names a proxy
  that can be used is the proxy of the run, and a file which names none keeps the
  proxy below it. A package whose `Network` workspace holds no proxy therefore
  does not remove the proxy of the archive.

A file which is not a network isolation file of the supported version is skipped
with everything it carries, so neither its redirections nor its proxy take part
in the run.

## Launcher side

The launcher consumes the `patch` directory of the standalone archive it starts. A
run

1. enumerates `patch/*.zip` next to its configuration file, sorted by name in
   ascending ASCII order, and ignores a directory which does not exist or holds
   no package;
2. computes the MD5 digest of a package and compares it with the digest
   recorded in `cache/<name>/md5.txt`, so a package which was replaced is
   extracted again and an unchanged package is reused;
3. extracts a package whose cache entry is missing or stale, mounts the layers
   of its `filesystem` directory before the layers of `app` and merges its
   `registry/user.hiv` into the hive the sandbox mounts;
4. hands the isolation file of every layer of the four domains to the sandbox,
   which applies the files of a domain in layer order: the filesystem and the
   registry domain merge their isolation modes, the network domain merges its
   redirections and picks the proxy of the last layer which names one, and the
   environment domain composes its variables layer by layer;
5. logs and skips a package which cannot be read, which keeps a broken package
   from failing the run. A package which carries no `filesystem` directory
   contributes no layer at all, a layer whose directory is named after a key the
   launcher does not know is skipped with the layers of that package, a hive which
   cannot be mounted is skipped with the registry of that package, and a
   malformed isolation file is skipped by the sandbox for that domain and that
   layer alone.

The four domains travel through the same injected configuration as one ordered
list of paths per domain, so the launcher itself carries no merge logic: the list
of a domain holds the file of the archive first and the file of every package
after it, and the sandbox applies the list in that order.

### Notes on the implementation

- **MD5 through CNG.** `bcrypt.dll` computes the digest without a session:
  `BCryptOpenAlgorithmProvider(BCRYPT_MD5_ALGORITHM)`, `BCryptHashData` and
  `BCryptFinishHash`. It avoids the legacy CryptoAPI of `advapi32`, which the
  launcher and the sandbox do not link, and it needs no `CryptAcquireContext`.
  MD5 is a change detector here, not a security boundary: a package is replaced
  by the user of the application, so a fast digest of the archive is enough.
  The file is read in blocks, so a package of any size is hashed without being
  held in memory.
- **No size and time fast path.** The digest of every package is computed on
  every run instead of recording its size and its last write time next to it.
  The digest of a package of a few megabytes costs milliseconds, and it cannot
  go stale when a package is replaced by one of the same size and the same write
  time.
- **Atomic cache entries.** A package is extracted into `cache/<name>.tmp` and
  the directory is renamed into `cache/<name>` only when the extraction
  finished, so a run which is interrupted half way cannot leave a directory
  which looks complete. The digest is written as the last file of the staging
  directory, which makes it the second marker of a complete extraction.
- **Layer order.** The packages are mounted in front of the layers of the
  archive instead of being merged into a new layer, which keeps the resolution
  of the sandbox unchanged: the existing rule "the layer which was mounted first
  wins" already implements "the last patch wins".
- **The registry is merged and not mounted.** The sandbox mounts exactly one
  hive: its root handle carries the open and the create policy, the merged
  enumeration and the merged counts, the whiteout store and the save snapshot.
  Making the packages further layers would mean to rewrite that model (a merged
  enumeration across several mounts, the recognition of the handles of every
  mount, a copy-up into the writable one). Merging the hives into the hive the
  sandbox mounts keeps the whole model as it is and needs no change inside the
  sandbox at all.
- **The merge mounts the hive of the state directory.** The merge needs the
  hive mounted for writing, which fails while another process holds it: a
  sandbox of an earlier run which is still alive mounts the same file. A run
  whose merge cannot mount the hive logs the failure and continues with the
  hive as it is, which is the state of the runs before it — a package then
  takes effect at the next start.
- **A broken package is not fatal.** A package which cannot be read, a package
  which is not an archive and a malformed isolation file of a package are
  logged and skipped, exactly like a malformed isolation file of the archive
  is: the resources of the layers below them stay in place.
- **The launcher reads the packages of the archive.** A patch package is written
  by the `ZipWriter` of the packer, so the launcher extracts it with the
  `ExtractArchive` of `src/core/ZipReader.cpp`, which is the read half of the
  same layout: the entry names of a package are sanitized by the very rules the
  packer writes them with.
- **The sandbox merges the four domains, not the launcher.** A domain travels as
  the ordered list of the isolation files of the layers of the run, exactly like
  the filesystem and the registry domain do, so the launcher carries no merge
  logic and a malformed document is skipped for the layer which carries it
  without touching the layers below and above it. Composing the four domains
  into one document in the launcher would move the rules of a domain out of the
  module which owns them.
- **The state of the sandbox is applied after the layers.** The modifications
  the packaged application made to its environment (`data/environment/state.json`)
  are applied after the last isolation file, and the hive of the sandbox carries
  the content of the runs before the merge runs, so a package is the top of the
  environment and of the registry of a run which the application did not change
  itself. An entry a package names is therefore the entry of the package at
  every start, while an entry no package names keeps the state of the sandbox.
- **The registry value-level isolation modes need the key inside the hive.** The
  mode of a value is applied while the sandbox reads the value from the hive: a
  key the hive does not hold is opened in the real registry, and the read of a
  value below a handle which points there is forwarded unchanged. A value-level
  mode which names a key only the host holds is therefore not applied. This is a
  gap of the registry domain and not of the patch layer — it holds for the
  isolation file of `app` as well, and the patch layer only adds further files
  which name modes — so it is documented as a known gap of
  [RegistryIsolation.md](RegistryIsolation.md) instead of being closed here:
  closing it would need the handle of a real key to be traced back to the view
  path it was opened from, which the registry layer does not track today.

## Related documentation

- [Filesystem isolation](FilesystemIsolation.md), [Registry
  isolation](RegistryIsolation.md), [Network isolation](NetworkIsolation.md) and
  [Environment isolation](EnvironmentIsolation.md) describe the isolation the
  merged resources of `app` and the patches configure.
- [`README.md`](../README.md) describes the packer and the `Project Type` box.
- [`test/README.md`](../test/README.md) lists the test suites, including the
  unit tests of the patch packer and the end-to-end cases of the patch layers.
