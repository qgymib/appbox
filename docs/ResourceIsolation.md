# Resource isolation

The sandbox supports isolation of the filesystem, registry, and network.

## Filesystem

Filesystem isolation consists of three layers:
1. `BaseFS` — the sandbox filesystem, mapped to a specific path within `RawFS`. Content is immutable.
2. `OverlayFS` — the working filesystem, used to store all file changes.
3. `RawFS` — the raw filesystem, content is immutable.

The structure of `BaseFS` is as follows:

```
.
|- filesystem
|- registry
|- meta.json
```

The structure of `OverlayFS` is as follows:

```
.
|- filesystem
    |- deleted
        |- <SHA256>
    |- 
```

When a sandboxed program attempts to open a file, the file lookup logic proceeds as follows:
1. Compute the SHA256 hash of the absolute file path.
2. Search for a file matching that SHA256 hash under `OverlayFS` / `filesystem` / `deleted`.
3. If found, the file is considered deleted; return `FILE_NOT_FOUND` and end the process.
4. If the file exists under `OverlayFS` / `filesystem` / `<PATH>`, open that file and end the process.
5. If the file exists under `BaseFS` / `filesystem` / `<PATH>` and is opened in read-only mode, open that file and end the process.
6. If the file exists under `BaseFS` / `filesystem` / `<PATH>`, copy it to `OverlayFS` / `filesystem` / `<PATH>`, open it, and end the process.
7. If the file exists under `RawFS` and is opened in read-only mode, open that file and end the process.
8. If the file exists under `RawFS`, copy it to `OverlayFS` / `filesystem` / `<PATH>`, open it, and end the process.

## Registry


## Network
