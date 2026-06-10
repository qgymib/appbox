# Resource isolation

The sandbox supports isolation of the filesystem, registry, and network.

## Filesystem

Filesystem isolation consists of three kind of layers:
1. `RawFS` — the raw filesystem, content is immutable.
2. `LowerFS` — the sandbox filesystem, mapped to a specific path within `RawFS`. Content is immutable. One sandbox can have multiple `LowerFS`.
2. `UpperFS` — the working filesystem, used to store all file changes. Only one `UpperFS` is allowed.

The structure of `LowerFS` is as follows:

```
.
|- %APPDATA%
|- %Profile%
|- %ProgramData%
|- %ProgramFiles%
|- <DRIVER>
|- registry.reg
```

The structure of `UpperFS` is as follows:

```
.
|- <DRIVER>
|- registry.hive
```

All content is optional.

The file search order is as follows:
1. `UpperFS`
2. `LowerFS`
3. `RawFS`
