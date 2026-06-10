# Registry Isolation

`AppBox` is a sandbox that runs on Windows 7 and later.

Like the filesystem overlay (RawFS → LowerFS → UpperFS), the registry also supports a multi-layer isolation model.

Each `LowerFS` layer may optionally contain a file named `registry.reg` in its root directory — this is the read-only registry information authored by the sandbox packager. There can be multiple LowerFS layers, each with its own `registry.reg`; they are merged in LowerFS priority order (see §1). In `UpperFS`, a file named `registry.hive` contains all the registry information needed for application runtime, including compiled and merged LowerFS data and all application modifications.

---

## 1. Layer Model

| Layer | Storage | Persistence | Description |
|-------|---------|------------|-------------|
| **Host Registry** | Windows real registry | Read-only (from sandbox view) | The system registry, treated as the bottom layer |
| **LowerFS `registry.reg`** (multiple layers) | `.reg` text file per LowerFS layer | Read-only, user-editable | Optional per layer. Each LowerFS may contain a `registry.reg`; layers are merged in priority order (top-down) |
| **UpperFS `registry.hive`** | Binary registry hive file in UpperFS | Read-write | The writable sandbox layer; stores merged LowerFS data + all app modifications + sandbox metadata |

Search order (highest priority first):
1. `DATA` subtree of `registry.hive` (app modifications + compiled LowerFS content)
2. Host registry (fallback for keys not present in `registry.hive`)

---

## 2. `registry.hive` File Structure

`registry.hive` is a standard binary registry hive file (the same format used by the Windows registry). It can be loaded with `RegLoadAppKeyW` at sandbox startup.

```
registry.hive (loaded hive root)
├── DATA                          ← Application-visible registry data
│   ├── Machine
│   │   ├── SOFTWARE
│   │   │   └── WOW6432Node       ← 32-bit view of SOFTWARE (only on 64-bit Windows)
│   │   │       └── Foo           ← 32-bit app data under HKLM\SOFTWARE\Foo
│   │   ├── SYSTEM
│   │   ├── HARDWARE
│   │   └── ...
│   └── User
│       └── <SID>                 ← Current user SID
│           ├── Software
│           │   └── Classes
│           │       └── WOW6432Node ← 32-bit view of HKCU\SOFTWARE\Classes
│           └── ...
└── __sbx__                       ← Sandbox internal metadata (hidden from app)
    ├── Meta
    │   ├── LowerFSHashes          ← Per-layer hashes (see below)
    │   │   ├── 0                  ← REG_BINARY, SHA-256 of LowerFS[0]'s registry.reg
    │   │   ├── 1                  ← REG_BINARY, SHA-256 of LowerFS[1]'s registry.reg
    │   │   └── ...                ← One entry per LowerFS layer
    │   ├── Version                ← REG_DWORD, incrementing compile version
    │   └── CompileTime            ← REG_QWORD, FILETIME of last compile
    ├── Modified                   ← Key paths that the app has modified (physical paths)
    │   └── Machine\SOFTWARE\Foo   ← REG_NONE marker key (64-bit view)
    │   └── Machine\SOFTWARE\WOW6432Node\Foo ← REG_NONE marker key (32-bit view)
    ├── Deleted                    ← Key paths that the app has deleted (physical paths)
    │   └── Machine\SOFTWARE\Bar   ← REG_NONE marker key
    ├── Whiteout                   ← Value-level whiteouts (physical key paths)
    │   └── Machine\SOFTWARE\Foo  ← Subkey for each key path
    │       ├── "OldFlag"          ← REG_NONE marker for each whited-out value
    │       └── "Version"          ← REG_NONE marker for each whited-out value
    └── Opaque                     ← Paths marked opaque (physical paths)
        └── Machine\SOFTWARE\Baz   ← REG_NONE marker key
```

### 2.1 `DATA` subtree

The `DATA` subtree mirrors the standard Windows registry namespace that the sandboxed application will see. Path translation must account for WoW64 registry redirection (see §6.8 for full details):

| App View | Hive Path (Native 64-bit) | Hive Path (WoW64 32-bit) |
|----------|---------------------------|--------------------------|
| `HKLM\SOFTWARE\Foo` | `DATA\Machine\SOFTWARE\Foo` | `DATA\Machine\SOFTWARE\WOW6432Node\Foo` |
| `HKLM\SYSTEM\Foo` | `DATA\Machine\System\Foo` | `DATA\Machine\System\Foo` (not redirected) |
| `HKCU\...\Foo` | `DATA\User\<SID>\Foo` | `DATA\User\<SID>\Foo` (not redirected, except `SOFTWARE\Classes`) |
| `HKCU\SOFTWARE\Classes\Foo` | `DATA\User\<SID>\SOFTWARE\Classes\Foo` | `DATA\User\<SID>\SOFTWARE\Classes\WOW6432Node\Foo`* |
| `HKCR\Foo` | `DATA\Machine\SOFTWARE\Classes\Foo` | `DATA\Machine\SOFTWARE\WOW6432Node\Classes\Foo`* |
| `HKU\<SID>\...` | `DATA\User\<SID>\...` | `DATA\User\<SID>\...` |

> **Note on HKCR**: `HKEY_CLASSES_ROOT` is a merged view of `HKLM\SOFTWARE\Classes` and `HKCU\SOFTWARE\Classes`. The sandbox maps HKCR to `DATA\Machine\SOFTWARE\Classes\...` (native 64-bit). For WoW64 processes, the shared/redirected subkey logic of `HKLM\SOFTWARE\Classes` applies (see §6.8.3). User-specific COM class registrations (normally visible via `HKCU\SOFTWARE\Classes`) are not separately mapped under HKCR. Applications that rely on per-user class registrations through HKCR may need the packager to include those entries in `registry.reg` under the `HKLM\SOFTWARE\Classes` path.

> **Note on WOW6432Node**: The `WOW6432Node` subdirectory in the hive `DATA` subtree is a physical representation of the 32-bit registry view. It is only present when the sandbox is running on 64-bit Windows and a 32-bit (WoW64) process has been sandboxed. See §6.8 for the complete WoW64 redirection rules, including shared-key exceptions and `KEY_WOW64_*` flag handling.

> **\* Shared-key exception for HKCU\SOFTWARE\Classes and HKCR**: The WoW64 hive paths shown for `HKCU\SOFTWARE\Classes\Foo` and `HKCR\Foo` (with `WOW6432Node`) only apply when `Foo` is a **redirected subkey** (e.g., `CLSID`, `Interface`, `DirectShow`, `Media Type`, `MediaFoundation`). For **shared subkeys** (e.g., `.txt`, `Appid`, `HCP`), the WoW64 process accesses the same physical path as the 64-bit process — no `WOW6432Node` is inserted. This is determined by the "most specific match" rule (see §6.8.3).

### 2.2 `__sbx__` subtree (hidden from application)

The `__sbx__` subtree is a reserved key under the hive root. It is **invisible to the sandboxed application** — all hooks filter it out during open, create, and enumerate operations.

#### `__sbx__\Meta`
Stores metadata about the LowerFS compilation state for change detection:

| Value | Type | Description |
|-------|------|-------------|
| `LowerFSHashes\0` | `REG_BINARY` (32 bytes) | SHA-256 hash of LowerFS layer 0's `registry.reg` content at last compile. If that layer has no `registry.reg`, all zeros. |
| `LowerFSHashes\1` | `REG_BINARY` (32 bytes) | SHA-256 hash of LowerFS layer 1's `registry.reg` content. And so on for each layer. |
| `Version` | `REG_DWORD` | Monotonically increasing version counter, incremented on each successful recompile. |
| `CompileTime` | `REG_QWORD` | FILETIME timestamp of the last compile. |

The number of subkeys under `LowerFSHashes` equals the number of LowerFS layers at compile time. Layer indices correspond to the LowerFS priority order (0 = lowest priority, N-1 = highest priority). This per-layer hashing allows the update flow (§5) to detect exactly which layers have changed and recompile only the affected entries.

#### `__sbx__\Modified`
Contains marker keys (REG_NONE, zero-length value) that track which registry paths the application has written to (physical paths, including `WOW6432Node` for redirected keys — see §6.8.9). When the app modifies a key (via NtSetValueKey, NtCreateKey, NtDeleteKey, NtDeleteValueKey, NtRenameKey, etc.), the corresponding path is recorded here. Additionally, when the app creates a key under a deleted ancestor, the ancestor path is also recorded here (implicitly "undeleting" it — see §6.2 step 5b, step 6, and §6.4 step 3a).

Example: After `NtSetValueKey(HKLM\SOFTWARE\Foo, "Version", 0x1234)`:
- The value is written to:
  - `DATA\Machine\SOFTWARE\Foo\Version` (64-bit process)
  - `DATA\Machine\SOFTWARE\WOW6432Node\Foo\Version` (32-bit WoW64 process)
- A marker key is created under `__sbx__\Modified`:
  - `Machine\SOFTWARE\Foo` (64-bit process)
  - `Machine\SOFTWARE\WOW6432Node\Foo` (32-bit WoW64 process)

This marker is used during the LowerFS update flow to preserve app changes (see §5).

#### `__sbx__\Deleted`
Contains marker keys (REG_NONE, zero-length value) for paths that are deleted — either by the application (via NtDeleteKey) or by path-whiteout directives in `registry.reg` (see §3.2). Paths are physical paths including `WOW6432Node` where applicable (see §6.8.9). When a path is in this set:
- Opening the key returns `STATUS_OBJECT_NAME_NOT_FOUND` (unless the key has been recreated in `DATA` by the app). For OPEN operations, ancestor deletion markers are also checked — if any ancestor is deleted, the entire subtree is invisible from the host registry fallback.
- The key is excluded from enumeration results merged from the host registry.

#### `__sbx__\Whiteout`
Contains subkeys for key paths that have specific value-level whiteouts. Each subkey represents a key path (e.g., `Machine\SOFTWARE\Foo` for 64-bit, or `Machine\SOFTWARE\WOW6432Node\Foo` for 32-bit WoW64 — see §6.8.9), and within it, each whited-out value name is stored as a `REG_NONE` zero-length value.

**Semantics**: Value whiteout **only blocks fallback to the host registry**. If a value exists in the hive `DATA` subtree (either compiled from LowerFS or written by the app), it is visible regardless of the whiteout marker. The whiteout prevents the sandbox from returning a host registry value for the specified name when the value is absent from `DATA`.

Example: If `registry.reg` contains `"OldFlag"=".$APPBOX_WHITEOUT$"` under `[HKEY_LOCAL_MACHINE\SOFTWARE\Foo]`:
- A subkey is created under `__sbx__\Whiteout`:
  - `Machine\SOFTWARE\Foo` (64-bit target)
  - `Machine\SOFTWARE\WOW6432Node\Foo` (32-bit WoW64 target)
- A value `"OldFlag"` (REG_NONE, zero-length) is created under that subkey.
- During `NtQueryValueKey` for `OldFlag` at `HKLM\SOFTWARE\Foo`:
  - If `OldFlag` exists in DATA → return it (app modification or compiled data takes precedence).
  - If `OldFlag` does NOT exist in DATA → check `__sbx__\Whiteout` → marker found → return `STATUS_OBJECT_NAME_NOT_FOUND` (host fallback blocked).

#### `__sbx__\Opaque`
Contains marker keys (REG_NONE, zero-length value) for paths marked opaque in `registry.reg` (the `.$APPBOX_OPAQUE$` directive). Paths are physical paths including `WOW6432Node` where applicable (see §6.8.9). When a path is opaque, during enumeration the sandbox does **not** merge results from the host registry — only keys in the hive `DATA` are visible.

---

## 3. Whiteout and Opaque in `registry.reg`

The `registry.reg` file in LowerFS uses special value markers for whiteout/opaque semantics, identical in spirit to the filesystem overlay whiteout.

### 3.1 Value Whiteout

Hides a specific value in this and all lower layers:

```reg
[HKEY_LOCAL_MACHINE\SOFTWARE\Foo]
"OldFlag"=".$APPBOX_WHITEOUT$"
```

**Compile behavior**: The named value is **not** added to `DATA`. Instead, a marker is recorded in `__sbx__\Whiteout` so that during runtime, if the value is absent from `DATA`, the host registry fallback is blocked and `STATUS_OBJECT_NAME_NOT_FOUND` is returned.

**Runtime behavior**: When querying a value, the sandbox first checks `DATA`. If the value is found there, it is returned regardless of any whiteout marker (the app's modification or compiled data takes precedence). Only when the value is absent from `DATA` does the sandbox check `__sbx__\Whiteout` — if a marker exists, the host fallback is suppressed and `STATUS_OBJECT_NAME_NOT_FOUND` is returned.

### 3.2 Path Whiteout

Hides an entire key path in this and all lower layers:

```reg
[HKEY_LOCAL_MACHINE\SOFTWARE\Foo]
".$APPBOX_WHITEOUT$"=dword:00000001
```

**Compile behavior**: The key path is NOT written to `DATA`. Instead, a marker is stored in `__sbx__\Deleted`, indicating this path is deleted. During runtime, any attempt to open this key returns `STATUS_OBJECT_NAME_NOT_FOUND`, and enumeration skips it — unless the app has subsequently created the key in `DATA` (in which case the key in `DATA` takes precedence, per the same principle as value whiteout).

### 3.3 Path Opaque

Hides all content under this path from lower layers:

```reg
[HKEY_LOCAL_MACHINE\SOFTWARE\Foo]
".$APPBOX_OPAQUE$"=dword:00000001
```

**Compile behavior**: A marker is stored in `__sbx__\Opaque\[path]`. During runtime, when enumerating under this path, the host registry is not consulted — only keys present in `DATA` are returned.

### 3.4 Precedence Rules

1. If both `.$APPBOX_WHITEOUT$` and `.$APPBOX_OPAQUE$` exist under the same path, that path is treated as deleted (whiteout takes precedence).
2. Whiteout/opaque markers from `registry.reg` only affect the compiled state and host fallback behavior. If the application later writes data to the same path, the data in `DATA` takes precedence — the whiteout marker does not hide values that exist in `DATA`.
3. During enumeration, value whiteout markers filter out matching value names from the host merge results. Keys/values present in `DATA` are always visible.

> **WoW64 Note**: The compile flow applies WoW64 redirection to the paths in `registry.reg` when writing to the hive (see §4.1 step 3a and §6.8.7). For a 32-bit target, a path like `HKLM\SOFTWARE\Foo` is compiled to `DATA\Machine\SOFTWARE\WOW6432Node\Foo`, and the corresponding `__sbx__` marker is at `Machine\SOFTWARE\WOW6432Node\Foo`. For a 64-bit target, no WOW6432Node is inserted.

---

## 4. Compile Flow: `registry.reg` → `registry.hive`

The compile step runs during sandbox initialization (first startup, or when any `registry.reg` has changed). It transforms the user-editable `.reg` files from all LowerFS layers into the binary hive.

### 4.1 Algorithm

```
Input:  registry.reg files (optional, one per LowerFS layer)
Output: registry.hive (populated DATA + __sbx__)

1. Initialize
   a. If registry.hive does not exist, create an empty hive file with RegCreateKeyEx/NtCreateKey
      (or create programmatically via the registry hive API).
   b. Ensure the skeleton structure exists: DATA, DATA\Machine, DATA\User, __sbx__, __sbx__\Meta,
      __sbx__\Meta\LowerFSHashes, __sbx__\Modified, __sbx__\Deleted, __sbx__\Whiteout, __sbx__\Opaque.

2. For each LowerFS layer in priority order (lowest priority first → highest priority last):
   a. If the layer has no registry.reg → skip this layer.
   b. Parse the layer's registry.reg:
      - Parse each [HKEY_...] section and its values.
      - For each value entry:
        - If value data is ".$APPBOX_WHITEOUT$" (value whiteout):
          Record in a temporary value-whiteout set for this layer (not added to DATA).
        - If value name is ".$APPBOX_WHITEOUT$" (path whiteout):
          Record path in a temporary path-whiteout set for this layer (not added to DATA).
        - If value name is ".$APPBOX_OPAQUE$" (opaque):
          Record path in a temporary opaque set for this layer (not added to DATA).
        - Otherwise: add the key+value to a temporary compiled data map for this layer.
   c. Merge this layer's results into the global merged data:
      - Data map: overwrite lower-priority entries with this layer's entries.
      - Path-whiteout set: add to global path-whiteout set (a higher layer can re-delete
        a path that a lower layer defined).
      - Value-whiteout set: add to global value-whiteout set (a higher layer can whiteout
        additional values).
      - Opaque set: add to global opaque set.
      - A higher-priority layer may "undelete" a path by including actual data for it
        (the data takes precedence: the path is removed from the global path-whiteout
        set, and the data will be written to DATA in step 3). Similarly, if a
        higher-priority layer provides data for a specific value that was value-whited
        out by a lower layer, the (key_path, value_name) entry is removed from the
        global value-whiteout set.
   d. After all layers are merged, apply precedence rules:
      - For each path in the merged data map: remove the path from the merged
        path-whiteout set (data takes precedence over path whiteout).
      - For each (key_path, value_name) in the merged data map: remove the
        corresponding entry from the merged value-whiteout set (data takes
        precedence over value whiteout).

3. After all layers are merged, write to hive DATA:
   a. For each key+value in the merged compiled data map:
      - Apply WoW64 path redirection to the key path (see §6.8), respecting shared-key
        exceptions and redirected subkeys (§6.8.3):
        If the path falls under a redirection scope root (e.g., HKLM\SOFTWARE)
        and the path does NOT already contain WOW6432Node, apply the "most
        specific match" rule against the shared/redirected key table (§6.8.3).
        If the most specific matching entry is "Redirected" → insert WOW6432Node
        after the redirected root component. If "Shared" → no WOW6432Node.
        The target bitness is determined from the sandbox configuration
        or auto-detected from the target executable's PE header.
        - For a 32-bit target: HKLM\SOFTWARE\Foo → DATA\Machine\SOFTWARE\WOW6432Node\Foo
        - For a 32-bit target, shared key: HKLM\SOFTWARE\Classes\.txt → DATA\Machine\SOFTWARE\Classes\.txt
        - For a 32-bit target, redirected subkey: HKLM\SOFTWARE\Classes\CLSID\{xxx} → DATA\Machine\SOFTWARE\WOW6432Node\Classes\CLSID\{xxx}
        - For a 64-bit target: HKLM\SOFTWARE\Foo → DATA\Machine\SOFTWARE\Foo
        - If the path explicitly contains WOW6432Node, use it as-is (no further redirection).
      - Create the key path under DATA\ and write the value.
   b. Paths that are whited out (in the merged path-whiteout set) are NOT written to DATA.
   c. Values that are value-whited out (in the merged value-whiteout set) are NOT written to DATA.

4. Write to __sbx__
   a. For each value whiteout (key_path, value_name):
      Apply the same WoW64 path redirection as in step 3a to key_path.
      Create the subkey __sbx__\Whiteout\<key_path> if not exists,
      then create a REG_NONE zero-length value named <value_name> under it.
   b. For each whited-out path: apply WoW64 redirection, then record in __sbx__\Deleted\[path].
   c. For each opaque path: apply WoW64 redirection, then record in __sbx__\Opaque\[path].

5. Store compile metadata
   a. For each LowerFS layer (by index):
      Compute SHA-256 of that layer's registry.reg content (or all zeros if absent)
      → write to __sbx__\Meta\LowerFSHashes\<index>.
   b. Increment __sbx__\Meta\Version.
   c. Write current FILETIME to __sbx__\Meta\CompileTime.

6. Flush the hive to disk.
```

### 4.2 Handling No `registry.reg`

If no LowerFS layer contains a `registry.reg` file:
- No LowerFS data is compiled into `DATA`.
- All entries under `__sbx__\Meta\LowerFSHashes` are set to all zeros (indicating no source).
- The sandbox operates with only an empty writable layer on top of the host registry.

---

## 5. Update Flow: `registry.reg` Change Detection and Hive Update

When the user modifies `registry.reg` in any LowerFS layer, the application view must reflect the changes while preserving any application modifications.

### 5.1 Change Detection

At sandbox initialization (before any sandboxed process starts):

1. For each LowerFS layer (by index), compute SHA-256 of its `registry.reg` file (or all zeros if absent).
2. Compare each hash against the corresponding value under `__sbx__\Meta\LowerFSHashes` stored in the hive.
3. If ALL hashes match → skip update (no changes in any layer).
4. If ANY hash differs → proceed with the update.

### 5.2 Update Algorithm

The update is performed **at initialization time**, before any sandboxed process is launched. The hive is loaded, modified, flushed, and then re-loaded for the sandbox session.

```
Input:  New registry.reg content (from all LowerFS layers)
        Existing registry.hive (with app modifications tracked in __sbx__)
Output: Updated registry.hive

1. Re-parse all LowerFS layers (same as §4.1 step 2) and merge them in priority order
   to obtain the new global compiled view:
   newData      = { path → [key, values] }   from the merged .reg files
   newDeleted   = { whited-out paths }       from the merged path-whiteout sets
   newWhiteout  = { (key_path, value_name) } from the merged value-whiteout sets
   newOpaque    = { opaque paths }           from the merged opaque sets

   Note: All paths in these sets are physical paths (WoW64 redirection applied
   per §4.1 step 3a and §6.8.7 when constructing paths from registry.reg content).

2. Read existing state from hive:
   modifiedSet = keys under __sbx__\Modified (paths the app has changed)
   deletedSet  = keys under __sbx__\Deleted  (paths that are deleted / path-whiteout)
   oldWhiteout = value names under __sbx__\Whiteout subkeys (existing value whiteouts)
   oldOpaque   = keys under __sbx__\Opaque   (existing opaque markers)

3. For each entry (path, values) in newData:
   a. If path ∈ modifiedSet → SKIP (app has modified this key; keep the app's version).
   b. If path ∈ deletedSet  → SKIP (app has deleted this key; honor the deletion).
   c. Otherwise → UPSERT the key+values into DATA\[path] in the hive.
      (Create if not exists, overwrite value data if exists.)

4. For entries that existed in the old compiled data but NOT in newData:
   a. If path ∈ modifiedSet → keep the app's version in DATA.
   b. If path ∈ deletedSet  → keep the deletion marker (path already removed from DATA).
   c. If any descendant path ∈ modifiedSet → keep the key in DATA
      (removing it would also remove the app's modified subkeys/values).
   d. Otherwise → REMOVE the key from DATA (the user removed it from registry.reg).

5. Update __sbx__\Deleted (path-whiteout markers from registry.reg):
   a. For paths in newDeleted that are NOT in modifiedSet:
      UPSERT the deletion marker.
   b. For paths in old deleted set that are NOT in newDeleted and NOT in modifiedSet:
      REMOVE the deletion marker (user removed the path whiteout from registry.reg).
      Note: these are compile-time whiteouts only. App-deleted paths always have a
      corresponding Modified marker (§6.4 step 3c creates both Deleted and Modified
      markers for NtDeleteKey), so the "NOT in modifiedSet" condition ensures app
      deletions are preserved.
   c. Paths that the app has deleted (NtDeleteKey creating both Modified and Deleted
      markers) are NOT removed — app deletions are preserved.

6. Update __sbx__\Whiteout (value-level whiteouts from registry.reg):
   a. For each (key_path, value_name) in newWhiteout:
      - If key_path ∈ modifiedSet AND (the value exists in DATA\[key_path]
        OR a whiteout marker already exists for this value name under
        __sbx__\Whiteout\[key_path]):
        SKIP (the app has written or deleted this value; the app's version
        takes precedence).
      - Otherwise → UPSERT the whiteout marker under __sbx__\Whiteout\[key_path].
   b. For each (key_path, value_name) in oldWhiteout but NOT in newWhiteout:
      - If key_path ∈ modifiedSet: SKIP (preserve the app's value-level
        changes, including deletions, for modified keys — see §6.4 step 3d
        for how NtDeleteValueKey creates whiteout markers).
      - Otherwise → REMOVE the whiteout marker (user removed the value
        whiteout from registry.reg).
      - The value will now be visible from the host registry on subsequent queries.
   c. Remove empty __sbx__\Whiteout subkeys when all their value markers have been removed.

7. Update __sbx__\Opaque:
   a. For paths in newOpaque that are NOT in modifiedSet and NOT in deletedSet:
      UPSERT the opaque marker.
   b. For paths in old opaque set that are NOT in newOpaque and NOT in modifiedSet:
      REMOVE the opaque marker.

8. Update compile metadata:
   a. For each LowerFS layer (by index):
      Compute SHA-256 of that layer's registry.reg (or all zeros if absent)
      → write to __sbx__\Meta\LowerFSHashes\<index>.
      If the number of LowerFS layers has changed, add or remove entries
      under __sbx__\Meta\LowerFSHashes to match the current layer count.
   b. Increment __sbx__\Meta\Version.
   c. Update __sbx__\Meta\CompileTime.

9. Flush the hive to disk.
```

### 5.3 Key Design Rationale

- **App modifications are never silently overwritten**: The `__sbx__\Modified` and `__sbx__\Deleted` sets act as a "diff" layer that preserves user-visible application behavior across LowerFS updates.
- **Unmodified keys always reflect the latest merged LowerFS view**: If the user updates a version number in any layer's `registry.reg`, the app sees the new version immediately — unless the app has overwritten it.
- **Value whiteout respects app modifications**: If the app has written a value that is later whited out in an updated `registry.reg`, the app's value in `DATA` takes precedence. The whiteout marker is not applied.
- **Update is atomic from the application's perspective**: It happens before any sandboxed process runs.

---

## 6. `registry.hive` Mapping Flow (Runtime)

At runtime, all sandboxed processes have the sandbox DLL injected. The DLL hooks NtOpenKey, NtCreateKey, NtEnumerateKey, NtQueryValueKey, NtSetValueKey, NtDeleteKey, NtDeleteValueKey, and related registry APIs. Each sandboxed process loads `registry.hive` independently using `RegLoadAppKeyW`, giving a private hive root handle.

### 6.1 Path Translation

The sandbox translates the application's virtual registry path to a hive-relative path. The translation is WoW64-aware — redirected paths insert `WOW6432Node` for 32-bit (WoW64) processes (see §6.8 for the complete redirection rules).

```
Path Translation (simplified — see §6.8 for WoW64 redirection details)

App Virtual Path   →   Hive Relative Path (native 64-bit)     Hive Relative Path (WoW64 32-bit)
──────────────────────────────────────────────────────────────────────────────────────────────────
HKLM\SOFTWARE\X   →   DATA\Machine\SOFTWARE\X               DATA\Machine\SOFTWARE\WOW6432Node\X
HKLM\SYSTEM\X     →   DATA\Machine\System\X                 DATA\Machine\System\X (no redirect)
HKCU\...\X        →   DATA\User\<CurrentUserSID>\X          DATA\User\<CurrentUserSID>\X
HKCU\SOFTWARE\
  Classes\X       →   DATA\User\<SID>\SOFTWARE\Classes\X     DATA\User\<SID>\SOFTWARE\Classes\
                                                             WOW6432Node\X*
HKCR\X            →   DATA\Machine\SOFTWARE\Classes\X       DATA\Machine\SOFTWARE\WOW6432Node\
                                                                     Classes\X*
HKU\<SID>\X       →   DATA\User\<SID>\X                     DATA\User\<SID>\X
HKCC\X            →   DATA\Machine\System\CurrentControlSet\X (same for both)
```

> **\* WoW64 shared-key exception**: The `WOW6432Node` insertion shown for `HKCU\SOFTWARE\Classes` and `HKCR` only applies to **redirected subkeys** (e.g., `CLSID`, `Interface`, `DirectShow`, `Media Type`, `MediaFoundation`). For **shared subkeys** (e.g., `.txt`, `Appid`, `HCP`, `PROTOCOLS`, `Typelib`), no `WOW6432Node` is inserted — the WoW64 process accesses the same physical key as the 64-bit process. See §6.8.3 for the complete shared/redirected key table and the "most specific match" rule.

**Path translation algorithm** (applied in §6.2 Open/Create Flow):

```
TranslateVirtualPathToHive(virtualPath, accessMask):
  1. Convert the app virtual path to the base hive-relative path:
     - HKLM\...           → DATA\Machine\...
     - HKCU\...           → DATA\User\<CurrentUserSID>\...
     - HKCR\...           → DATA\Machine\SOFTWARE\Classes\...
     - HKU\<SID>\...      → DATA\User\<SID>\...
     - HKCC\...           → DATA\Machine\System\CurrentControlSet\...

  2. Apply WoW64 redirection (see §6.8 for full rules):
     a. Determine the effective registry view:
        - isWow64 = result of IsWow64Process() at DLL init
        - If isWow64 AND NOT (accessMask & KEY_WOW64_64KEY):
          view = WoW64 (32-bit)
        - Else if NOT isWow64 AND (accessMask & KEY_WOW64_32KEY):
          view = WoW64 (32-bit)
        - Else:
          view = Native (64-bit)
     b. If view == WoW64 AND the path falls under a redirection
        scope root (HKLM\SOFTWARE or HKCU\SOFTWARE\Classes) AND
        the path does NOT already contain WOW6432Node:
        - Note: "redirection scope root" means a root under which WoW64
          redirection logic applies — the root itself may be Redirected
          (e.g., HKLM\SOFTWARE) or Shared (e.g., HKCU\SOFTWARE\Classes),
          but both contain subkeys with mixed Shared/Redirected status.
        - Determine whether the path should be redirected using the
          "most specific match" rule against the shared/redirected
          key table (§6.8.3):
          Find the longest (most specific) entry in the table that
          matches the path. If that entry is "Shared" → do NOT insert
          WOW6432Node. If that entry is "Redirected" → insert
          WOW6432Node after the redirected root component.
          If no entry matches, the path inherits the parent's status:
          under a redirection scope root, the default is Redirected.
        - Example (redirected): DATA\Machine\SOFTWARE\Foo → DATA\Machine\SOFTWARE\WOW6432Node\Foo
        - Example (shared subkey): DATA\Machine\SOFTWARE\Classes\.txt → no WOW6432Node
        - Example (redirected subkey under shared): DATA\Machine\SOFTWARE\Classes\CLSID\{xxx} → DATA\Machine\SOFTWARE\WOW6432Node\Classes\CLSID\{xxx}
     c. If view == Native: no redirection applied.

  3. Return the final hive-relative path.
```

The current user SID is retrieved at injection time and stored in a global accessible to the hooks. The WoW64 status of the process is also determined at DLL init (`IsWow64Process`) and stored globally.

> **HKCR Note**: `HKEY_CLASSES_ROOT` is a merged view of `HKLM\SOFTWARE\Classes` and `HKCU\SOFTWARE\Classes`. The sandbox maps HKCR to `DATA\Machine\SOFTWARE\Classes\...` (native 64-bit). For WoW64 processes, the WoW64 redirection applies the same shared/redirected logic as `HKLM\SOFTWARE\Classes` (see §6.8.3): redirected subkeys (e.g., CLSID) map to `DATA\Machine\SOFTWARE\WOW6432Node\Classes\...`, while shared subkeys (e.g., `.txt`, `Appid`) map to `DATA\Machine\SOFTWARE\Classes\...` with no WOW6432Node. If per-user COM registrations (from `HKCU\SOFTWARE\Classes`) need to be visible under HKCR, they must be included in `registry.reg` under the `HKLM\SOFTWARE\Classes` path.

### 6.2 Open / Create Flow

```
NtOpenKey / NtCreateKey / NtOpenKeyEx / NtCreateKeyTransacted (hooked)

1. Translate the app's virtual path to the hive-relative path using the path translation
   algorithm in §6.1 (includes WoW64 redirection based on process bitness and KEY_WOW64_* flags).

2. If the translated path falls under "__sbx__" → return STATUS_OBJECT_NAME_NOT_FOUND.
   (The __sbx__ subtree is never exposed to the application.)

3. Try to open the translated path under the loaded hive root:
   a. Construct OBJECT_ATTRIBUTES with RootDirectory = hive root handle,
      ObjectName = translated relative path (e.g., "DATA\Machine\SOFTWARE\Foo").
   b. Call the real NtOpenKey/NtCreateKey with these attributes.
   c. On SUCCESS:
      - Record the returned handle in the sandbox handle set.
      - Return the handle to the caller.
   d. On NOT_FOUND / NAME_NOT_FOUND → proceed to step 4.

4. Check __sbx__\Deleted for the key path and all ancestor paths:
   If the key path or any ancestor path has a deletion marker:
   a. For OPEN operations → return STATUS_OBJECT_NAME_NOT_FOUND.
      (The key or an ancestor was deleted by the app or path-whiteout from
       registry.reg. Ancestor deletion must be checked because deleting a key
       should make its entire subtree invisible from the host registry fallback.
       Note: if the key was recreated in DATA by the app, step 3 would have
       found it, so reaching this step means the key is truly absent from DATA.)
   b. For CREATE operations → proceed to step 6.
      (The key will be created in the hive, and ancestor deletion markers
       will be removed — see step 6. A CREATE operation implicitly "undeletes"
       the key and its ancestors.)

5. Check __sbx__\Opaque on the key path itself and all ancestor paths:
   If the path itself or any ancestor is opaque:
   a. For OPEN operations → skip host fallback; return STATUS_OBJECT_NAME_NOT_FOUND.
      (The opaque marker means host registry content is not visible under this path.)
   b. For CREATE operations → skip host fallback; create the key directly in
      DATA\[path] in the hive (do not copy from host). Record the key path in
      __sbx__\Modified, remove any __sbx__\Deleted\[path] marker if one exists.
      Also remove any __sbx__\Deleted markers for ancestor paths and record each
      such ancestor path in __sbx__\Modified (creating a key under a deleted
      ancestor implicitly undeletes it). Return the hive handle.
      (Opaque only blocks host fallback; the app may still create new keys in the hive.)

6. Check if sandbox-managed access is required:
   If any of the following conditions is true, the key MUST be accessed through
   the hive (not through a raw host handle):
   a. The operation is a create (NtCreateKey).
   b. The requested access includes write capability (KEY_SET_VALUE,
      KEY_CREATE_SUB_KEY, etc.).
   c. The key path itself has a value-whiteout marker under
      __sbx__\Whiteout (value whiteout filtering cannot be applied to a
      raw host handle).
      Note: ancestor whiteout markers do NOT require sandbox-managed access
      for the current key. Value whiteouts are key-specific — a whiteout on
      an ancestor only blocks host fallback for values under that ancestor
      key, not for values under descendant keys. A raw host handle for the
      current key is safe because the ancestor's whiteout does not affect it.
   d. The key path or any ancestor has an opaque marker under __sbx__\Opaque
      (already handled in step 5, but listed here for completeness).

   If sandbox-managed access IS required (any of a–d):
   - Open the path from the host registry.
   - If the host key exists:
     Copy the key (and its entire subkey tree, recursively) from the host
     into DATA\... in the hive. During the copy, skip any values that have
     whiteout markers in __sbx__\Whiteout.
     Record the key path in __sbx__\Modified.
     Remove the __sbx__\Deleted\[path] marker if one exists (the key now
     exists in DATA, so the deletion is effectively undone).
     Remove any __sbx__\Deleted markers for ancestor paths and record each
     such ancestor path in __sbx__\Modified. Creating a key under a deleted
     ancestor implicitly undeletes the ancestor (it now exists in DATA as a
     parent key), so its deletion marker is stale and must be removed.
     Return a handle to the newly created hive path.
   - If the host key does NOT exist:
     For create operations: create the key in the hive under DATA\...,
     record in __sbx__\Modified, remove any __sbx__\Deleted\[path] marker,
     also remove any __sbx__\Deleted markers for ancestor paths and record
     each such ancestor path in __sbx__\Modified,
     return the hive handle.
     For open operations: return STATUS_OBJECT_NAME_NOT_FOUND.

   If sandbox-managed access is NOT required (read-only, no whiteout markers,
   no opaque markers on the path):
   - Call the real system API directly on the host registry.
   - Return the real host handle (no sandbox tracking needed).
   - IMPORTANT: The returned host handle is a "passthrough" handle — operations
     on it are not filtered by the sandbox. This is acceptable because:
     (a) the key has no whiteout/opaque markers that need enforcement, and
     (b) the handle is read-only (kernel enforces access mask).
     See §6.7 for risks and mitigations.
```

### 6.3 Read / Query Flow

```
NtQueryValueKey / NtEnumerateKey / NtEnumerateValueKey / NtQueryKey / NtQueryMultipleValueKey (hooked)

1. Look up the handle in the sandbox handle set.
2. If the handle is NOT in the sandbox handle set:
   - It is a host registry handle (returned from step 6.b above).
   - Pass through to the real system call.

3. If the handle IS in the sandbox handle set:
   - It points to a key in the loaded hive.

   For NtQueryValueKey (query a specific value by name):
   a. Call the real NtQueryValueKey on the hive handle.
   b. If the value is found in DATA → return it (app modification or compiled
      data takes precedence over any whiteout marker).
   c. If the value is NOT found in DATA:
      - Resolve the key path from the handle.
      - Check __sbx__\Whiteout\[key_path] for the requested value name.
        If a marker exists → return STATUS_OBJECT_NAME_NOT_FOUND
        (host fallback is blocked for this specific value).
      - Check __sbx__\Deleted and __sbx__\Opaque for the key path and all
        ancestor paths. If the key path or any ancestor is deleted, or the
        path itself or any ancestor is opaque → return
        STATUS_OBJECT_NAME_NOT_FOUND.
        (Ancestor deletion must be checked because deleting a key should
        make its entire subtree invisible from the host registry fallback.
        If the key or an ancestor exists in DATA, the value would have been
        found in step a above — reaching this step means the value is absent
        from DATA, so host fallback is the only source, and it must be
        blocked for deleted subtrees.)
      - Otherwise, fall through to the host registry: open the corresponding
        host key, query the value, and return the result.
        (The host handle is opened and closed within this call; it is not
        returned to the application.)

   For NtEnumerateKey (enumerate subkeys):
   a. Enumerate subkeys from the hive under the DATA\... path.
      Keys that exist in DATA are always included — they are not filtered by
      __sbx__\Deleted (DATA content takes precedence over deletion markers,
      since a key that exists in DATA may have been recreated after deletion).
   b. **WoW64: WOW6432Node visibility**: For WoW64 processes, the path translation
      in §6.1 already redirected the key handle to the WOW6432Node subtree. When
      enumerating, the process sees subkeys under WOW6432Node, not WOW6432Node
      itself. This naturally matches the host registry behavior — a 32-bit process
      enumerating HKLM\SOFTWARE sees the 32-bit application subkeys, not WOW6432Node.
      For native 64-bit processes enumerating HKLM\SOFTWARE, WOW6432Node IS a
      visible subkey (if it exists in DATA or host), matching the host behavior.
   c. Check __sbx__\Opaque for the current key path or any ancestor:
      - If opaque → return ONLY the hive enumeration results (step a).
      - If not opaque → check if the current key path or any ancestor is in
        __sbx__\Deleted. If so, suppress the host merge and return ONLY the
        hive enumeration results (a deleted key's entire subtree should be
        invisible from the host registry; DATA entries are still visible per
        the key principle above).
      - If not opaque and not deleted (or only deleted in an ancestor that
        has been recreated in DATA): enumerate from the corresponding host
        registry path. Merge host results with hive results, skipping entries
        already present in the hive results. Also skip host entries that are
        present in __sbx__\Deleted (these keys are deleted or whiteout-deleted
        from the sandbox view; they should not appear from the host fallback).
        Note: for WoW64 processes, the host enumeration is performed by calling
        the real NtEnumerateKey from the WoW64 process context — the kernel
        automatically applies WoW64 redirection, so the host results reflect the
        32-bit view. No additional WoW64 handling is needed for host enumeration.
   d. Return the merged and filtered results.
      Index translation: since NtEnumerateKey uses a zero-based index, the sandbox
      must maintain a stable merged view for the duration of the enumeration. When the
      app requests index N, the sandbox:
      - Collects all subkey names from hive (unconditionally included).
      - Collects all subkey names from host (if not opaque, excluding duplicates
        and entries in __sbx__\Deleted).
      - Sorts or preserves insertion order (hive entries first, then host-only entries).
      - Returns the subkey at position N from the merged list.
      The sandbox should cache the merged list per enumeration session to ensure
      consistent indexing across calls. If the key is modified between calls,
      the cache is invalidated and the app may see inconsistent results — this
      matches Windows behavior (NtEnumerateKey is not atomic).

   For NtEnumerateValueKey (enumerate values under a key):
   a. Enumerate values from the hive.
   b. Resolve the key path from the handle.
   c. Check __sbx__\Whiteout\[key_path]: filter out any value names that have
      a whiteout marker from the enumeration results.
      (Whited-out values were not compiled into DATA, so they won't appear in
       hive results. This filter mainly ensures consistency if the hive somehow
       contains a whited-out value.)
   d. Check __sbx__\Opaque for the current key path or any ancestor:
      - If opaque → return ONLY the hive enumeration results (after whiteout filtering).
      - If not opaque → enumerate values from the corresponding host registry path.
        Merge host results with hive results, skipping entries already present
        in the hive results OR present in __sbx__\Whiteout\[key_path].
   e. Return the merged and filtered results.
      Index translation: same approach as NtEnumerateKey above — the sandbox
      maintains a stable merged list of value names and maps the app's index
      to the correct entry.

   For NtQueryKey (query key metadata):
   a. If InformationClass is KeyNameInformation or KeyHandleTagsInformation:
      - Call the real NtQueryKey on the hive handle to get the key path.
      - The returned path will be relative to the hive root, e.g.,
        "\DATA\Machine\SOFTWARE\Foo" or "\DATA\Machine\SOFTWARE\WOW6432Node\Foo".
      - **Rewrite the path**: strip the "DATA\" prefix, reverse WoW64 redirection
        (if applicable), and translate back to the standard Windows registry namespace:
        1. Strip the "DATA\" prefix.
        2. If the process is WoW64 AND the path contains "WOW6432Node\" after
           a redirected root (e.g., "Machine\SOFTWARE\WOW6432Node\Foo"):
           Remove the "WOW6432Node\" component → "Machine\SOFTWARE\Foo".
           This reverses the WoW64 redirection so the app sees its virtual path.
           Note: WOW6432Node in the path is only removed for the WoW64 view —
           a native 64-bit process that explicitly opens WOW6432Node should see it.
        3. Translate the registry namespace:
           - "Machine\..." → "\REGISTRY\MACHINE\..."
           - "User\<SID>\..." → "\REGISTRY\USER\<SID>\..."
           - "Machine\SOFTWARE\Classes\..." → "\REGISTRY\MACHINE\SOFTWARE\Classes\..."
      - Return the rewritten path to the application.
      - If the path does not match a known translation pattern (e.g., it starts
        with "__sbx__"), return STATUS_OBJECT_NAME_NOT_FOUND (this should never
        happen since __sbx__ is filtered at open time, but serves as a safety net).
   b. If InformationClass is KeyBasicInformation, KeyNodeInformation, KeyFullInformation,
      KeyCachedInformation, etc.:
      - Pass through to the real system call (these return metadata like timestamps
        and subkey counts that do not leak hive-internal structure).
      - Note: subkey counts returned for hive keys may differ from the host registry
        because the hive does not include WOW6432Node as a visible subkey for WoW64
        processes. This is generally acceptable — applications rarely depend on exact
        subkey counts.

   For NtQueryMultipleValueKey:
   - Pass through to the real system call (the handle already refers to the
     correct key in the hive — no additional path translation needed).
```

### 6.4 Write Flow

```
NtSetValueKey / NtCreateKey / NtDeleteKey / NtDeleteValueKey / NtRenameKey (hooked)

1. Look up the handle in the sandbox handle set.
2. If the handle is NOT in the sandbox handle set:
   - It is a host registry handle (returned from a read-only open).
   - IMPORTANT: A read-only host handle can only be used for read operations.
     The Windows kernel enforces access checks — calling NtSetValueKey on a
     handle opened with KEY_READ will fail with STATUS_ACCESS_DENIED.
     Therefore, this case should not normally occur. However, it is possible
     through NtDuplicateObject with altered access rights (see §6.6).
   - If the handle was obtained via NtDuplicateObject elevation (tracked in the
     handle redirect map — see §6.6), use the redirect map to find the
     corresponding hive handle and proceed with the write on the hive handle.
   - Otherwise, pass through to the real system call (the kernel will reject
     the write with STATUS_ACCESS_DENIED, which is the expected behavior for
     read-only handles).

3. If the handle IS in the sandbox handle set:
   - Perform the operation directly (the handle points to the hive).
   - Note on WoW64: the handle's key path in the hive uses physical paths (including
     WOW6432Node for redirected keys). All __sbx__ marker paths below use the same
     physical path convention (see §6.8.9).
   - After the operation:
     a. If creating/opening a key (NtCreateKey) that was previously deleted:
        - Remove the __sbx__\Deleted\[path] marker. The key now exists in DATA,
          so the deletion marker is stale and would incorrectly filter the key
          during host merge in enumeration.
        - Also remove any __sbx__\Deleted markers for ancestor paths and record
          each such ancestor path in __sbx__\Modified. Creating a key under a
          deleted ancestor implicitly undeletes the ancestor (it now exists in
          DATA as a parent key).
     b. If writing/deleting values: record the key path in __sbx__\Modified.
     c. If deleting a key (NtDeleteKey):
        - Call the real NtDeleteKey on the hive handle.
          (Windows kernel handles deletion semantics: the key is marked for
          deletion and actually removed when the last handle is closed.)
        - Create the __sbx__\Deleted\[path] marker. Since NtDeleteKey marks
          the key for deferred deletion (removed when the last handle closes),
          there is a window between the API call and the actual key removal
          where another thread could open the same path. The Deleted marker
          prevents the sandbox from falling through to the host registry during
          this window. If the key still exists in DATA (not yet removed by the
          kernel), the enumeration logic (§6.3) correctly shows it (DATA entries
          are not filtered by Deleted markers).
        - Record the key path in __sbx__\Modified. Although the key is being
          deleted (not written to), the Modified marker is necessary so that
          the update flow (§5.2 step 5) can distinguish app-deleted paths from
          compile-time path-whiteouts. Without it, an app-deleted key that
          was never explicitly modified would lack a Modified marker, causing
          the update flow to incorrectly remove its Deleted marker when the
          user changes an unrelated LowerFS entry (see §5.2 step 5b).
        - Remove any __sbx__\Whiteout subkey for this path (the key is fully
          deleted; whiteout markers are no longer meaningful).
     d. If deleting a value (NtDeleteValueKey):
        - Remove the value from DATA.
        - Record the key path in __sbx__\Modified.
        - Create a whiteout marker for this value name under
          __sbx__\Whiteout\[key_path] (if one does not already exist).
          This blocks host registry fallback for the deleted value.
          Without this marker, the host registry value would "reappear"
          after the app explicitly deleted it — since the value is now
          absent from DATA, only the whiteout marker prevents the host
          fallback from returning it. (If a compile-time whiteout marker
          already exists for this value, keep it — the semantics are
          identical.)
     e. If setting a value (NtSetValueKey):
        - Write the value to DATA.
        - Record the key path in __sbx__\Modified.
        - If a whiteout marker exists for this value name under
          __sbx__\Whiteout\[key_path], remove it (the app's write takes
          precedence over the whiteout; the value now exists in DATA and
          should be visible).
     f. If renaming a key (NtRenameKey):
        - Call the real NtRenameKey on the hive handle.
        - After successful rename, update __sbx__ markers:
          - All markers under __sbx__ that reference the old path or any
            descendant path must be moved to the corresponding new path.
            Since markers are stored with absolute physical paths under __sbx__
            (e.g., __sbx__\Modified\Machine\SOFTWARE\Foo\Bar), renaming
            Machine\SOFTWARE\Foo to Machine\SOFTWARE\Foo2 requires:
            - Enumerate all entries under __sbx__\Modified, __sbx__\Deleted,
              __sbx__\Whiteout, and __sbx__\Opaque whose path starts with the
              old path prefix.
            - For each matching entry, move it from the old path to the new path.
            - Example: __sbx__\Modified\Machine\SOFTWARE\Foo → __sbx__\Modified\Machine\SOFTWARE\Foo2
            - Example: __sbx__\Modified\Machine\SOFTWARE\Foo\Bar → __sbx__\Modified\Machine\SOFTWARE\Foo2\Bar
            - Similarly for Deleted, Whiteout, and Opaque markers.
        - Record the new path in __sbx__\Modified.
```

### 6.5 Enumeration with Opaque Handling

The enumeration logic is integrated into §6.3 above. For reference, the key behaviors are:

1. **Key enumeration (NtEnumerateKey)**: Enumerate from the hive first (DATA entries are always included, regardless of Deleted markers), then optionally merge with host results (unless opaque), filtering out host entries present in `__sbx__\Deleted`.
2. **Value enumeration (NtEnumerateValueKey)**: Enumerate from the hive first, filter out whiteout-marked values, then optionally merge with host results (unless opaque), also skipping values in the whiteout set.

**Key principle — DATA entries are never filtered by `__sbx__\Deleted`**: If a key exists in DATA, it was either compiled from LowerFS or written by the app. A stale `__sbx__\Deleted` marker (e.g., from a rolled-back transaction, or from a deletion followed by recreation) must not hide a key that actually exists in DATA. The Deleted marker only affects the host merge — it prevents the sandbox from adding host registry entries for paths that the sandbox considers deleted.

**Opaque handling**: If the current key path or any ancestor path has an opaque marker in `__sbx__\Opaque`, the sandbox returns ONLY results from the hive `DATA`. No host merge is performed.

**WoW64 and enumeration**: For WoW64 processes, the path translation (§6.1) has already redirected the handle to the WOW6432Node subtree. Enumeration operates on the WOW6432Node path, so the process naturally sees the 32-bit view. Host enumeration is also WoW64-aware because the sandbox calls the real `NtEnumerateKey` from the WoW64 process context (the kernel applies redirection automatically). See §6.8.6 for details.

### 6.6 Handle Tracking

The sandbox maintains two data structures for handle management:

#### Handle Set
A **handle set** (a concurrent `std::unordered_set<HANDLE>`) that tracks all handles returned by sandbox-managed open/create operations. This set is checked on every hooked API call that takes a handle parameter.

**Important**: All handles in this set are **real kernel handles** obtained from `NtOpenKey`/`NtCreateKey` on the loaded hive. No fake handles are synthesized. This satisfies constraint #3 (minimize hooks, no fake handles).

Handles are removed from the set when `NtClose` is called on them (the hook on `NtClose` checks the set).

#### Handle Redirect Map
A **handle redirect map** (a concurrent `std::unordered_map<HANDLE, HANDLE>`) that maps host handles to corresponding hive handles. This map is used when a read-only host handle is elevated to write access via `NtDuplicateObject` (see below).

Entries in this map are cleaned up when `NtClose` is called on the host handle (the corresponding hive handle is also closed).

#### NtDuplicateObject Handling

`NtDuplicateObject` can duplicate a handle from one process to another, optionally changing its access mask. This poses an isolation risk:

1. A sandboxed process opens a key with `KEY_READ` → gets a host handle (not tracked in sandbox set).
2. The process calls `NtDuplicateObject` on the host handle with `DUPLICATE_SAME_ACCESS` or with elevated access (e.g., `KEY_SET_VALUE`).
3. The duplicated handle is a host registry handle with potentially elevated access.
4. Writes through this handle bypass the sandbox.

To prevent this, `NtDuplicateObject` is hooked with the following logic:

```
NtDuplicateObject (hooked)

1. Call the real NtDuplicateObject to duplicate the handle.
2. If the source handle is in the sandbox handle set:
   - The duplicated handle is also a hive handle.
   - Add the duplicated handle to the sandbox handle set.
3. If the source handle is NOT in the sandbox handle set:
   - The duplicated handle is a host handle.
   - If the requested access includes any write capability:
     - Perform copy-before-write: open/create the key in the hive, copying
       values and subkeys from the host.
     - Add the duplicated host handle to the handle redirect map,
       mapping it to the new hive handle.
     - Record the path in __sbx__\Modified.
   - Otherwise (read-only duplicate): do not track (same as the original).
4. Return the duplicated handle.
```

### 6.7 Pre-Existing Handles and Pass-through Handles

#### Handles Opened Before Injection

When the sandbox DLL is injected into a target process, the process may already have open registry handles. These pre-existing handles are not tracked by the sandbox and cannot be intercepted retroactively.

**Design choice**: The sandbox does not attempt to enumerate or redirect pre-existing handles. This is consistent with the filesystem sandbox's "forward-looking" approach — only operations that occur after hook installation are intercepted. Pre-existing handles are a known limitation.

**Mitigation**: In practice, the sandbox DLL is injected at process creation time (via `CreateProcess` with `CREATE_SUSPENDED` + DLL injection), before the process's main code runs. At this point, the process typically has very few registry handles open — only those inherited from the parent process or opened by the loader. Applications that cache registry handles from DLL initialization (before the sandbox hooks are active) may bypass the sandbox for those specific handles.

#### Pass-through Host Handles

As described in §6.2 step 6, the sandbox may return raw host registry handles for read-only access when there are no whiteout or opaque markers to enforce. These "pass-through handles" have the following properties:

- **Not tracked**: They are not in the sandbox handle set.
- **Read-only**: The Windows kernel enforces the requested access mask. A handle opened with `KEY_READ` cannot be used for writes.
- **Unfiltered**: Operations on pass-through handles go directly to the host registry without sandbox filtering. This is acceptable because:
  - The key has no sandbox-specific markers (whiteout/opaque/deleted) that need enforcement.
  - The handle is read-only, so the host registry cannot be modified through it.
- **Risk**: If the application later calls `NtDuplicateObject` to elevate the access mask of a pass-through handle, the `NtDuplicateObject` hook (§6.6) intercepts this and performs copy-before-write.

### 6.8 WoW64 Registry Redirection

On 64-bit Windows, 32-bit processes (WoW64 processes) are subject to registry redirection by the WoW64 subsystem. The sandbox must correctly handle this redirection to ensure that 32-bit and 64-bit processes see the appropriate registry view and that the two views do not conflict in the hive.

#### 6.8.1 WoW64 Redirection Overview

The Windows kernel transparently redirects certain registry paths for 32-bit WoW64 processes:

| Virtual Path (App View) | Physical Path (64-bit View) | Physical Path (32-bit WoW64 View) |
|--------------------------|------------------------------|-----------------------------------|
| `HKLM\SOFTWARE\X` | `\REGISTRY\MACHINE\SOFTWARE\X` | `\REGISTRY\MACHINE\SOFTWARE\WOW6432Node\X` |
| `HKCU\SOFTWARE\Classes\X` | `\REGISTRY\USER\<SID>\SOFTWARE\Classes\X` | `\REGISTRY\USER\<SID>\SOFTWARE\Classes\WOW6432Node\X` |

The redirection is performed by the kernel when a 32-bit process calls `NtOpenKey` or `NtCreateKey`. From the 32-bit app's perspective, it is accessing `HKLM\SOFTWARE\X`; the kernel opens the physical key at `WOW6432Node\X`.

#### 6.8.2 Hive Path Translation with WoW64 Redirection

The sandbox must apply WoW64 redirection when translating the app's virtual path to the hive path. This ensures that 32-bit and 64-bit registry views do not conflict in the hive `DATA` subtree.

**Redirected paths in the hive use `WOW6432Node`**: For a WoW64 process, the sandbox inserts `WOW6432Node` after the redirected root when constructing the hive path (see §6.1 for the complete path translation algorithm).

The critical design principle is: **the hive `DATA` subtree uses physical paths (including `WOW6432Node`) so that 32-bit and 64-bit views can coexist without conflict.** Without this separation, a 32-bit process writing to `HKLM\SOFTWARE\Foo` would store data at `DATA\Machine\SOFTWARE\Foo`, which is the same location used by a 64-bit process writing to the same virtual path — but the actual data should be different.

**KEY_WOW64_64KEY / KEY_WOW64_32KEY flags**: These access flags override the default WoW64 redirection:
- A WoW64 process with `KEY_WOW64_64KEY` accesses the 64-bit view (no WOW6432Node in hive path)
- A native 64-bit process with `KEY_WOW64_32KEY` accesses the 32-bit view (WOW6432Node is inserted in hive path)
- These flags are part of the `DesiredAccess` mask in `NtOpenKey`/`NtCreateKey`

**Process bitness detection**: At DLL initialization, the sandbox calls `IsWow64Process(GetCurrentProcess(), &isWow64)` to determine the process's WoW64 status. The result is stored in a global variable and used by all path translation logic.

#### 6.8.3 Shared registry keys

> The official documentation for WoW64 redirection is available at [Registry Keys Affected by Windows Installations That Include Windows on Windows (WOW) Support For Multiple Processor Architectures](https://learn.microsoft.com/en-us/windows/win32/winprog64/shared-registry-keys).

For WOW applications on affected Windows installations, the following table lists registry keys that are redirected or shared. Subkeys of the keys in this table inherit the parent key's behavior unless otherwise specified. If a key has no parent listed in this table, the key is shared.

| Key | Windows Server 2008 R2, Windows 7, and Newer |
| --- | --- |
| HKEY_LOCAL_MACHINE | Shared |
| HKEY_LOCAL_MACHINE\SOFTWARE | Redirected |
| HKEY_LOCAL_MACHINE\SOFTWARE\Classes | Shared |
| HKEY_LOCAL_MACHINE\SOFTWARE\Classes\Appid | Shared |
| HKEY_LOCAL_MACHINE\SOFTWARE\Classes\CLSID | Redirected |
| HKEY_LOCAL_MACHINE\SOFTWARE\Classes\DirectShow | Redirected |
| HKEY_LOCAL_MACHINE\SOFTWARE\Classes\HCP | Shared |
| HKEY_LOCAL_MACHINE\SOFTWARE\Classes\Interface | Redirected |
| HKEY_LOCAL_MACHINE\SOFTWARE\Classes\Media Type | Redirected |
| HKEY_LOCAL_MACHINE\SOFTWARE\Classes\MediaFoundation | Redirected |
| HKEY_LOCAL_MACHINE\SOFTWARE\Clients | Shared |
| HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\COM3 | Shared |
| HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Cryptography\Calais\Current | Shared |
| HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Cryptography\Calais\Readers | Shared |
| HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Cryptography\Services | Shared |
| HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\CTF\SystemShared | Shared |
| HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\CTF\TIP | Shared |
| HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\DFS | Shared |
| HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Driver Signing | Shared |
| HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\EnterpriseCertificates | Shared |
| HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\EventSystem | Shared |
| HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\MSMQ | Shared |
| HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Non-Driver Signing | Shared |
| HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Notepad\DefaultFonts | Shared |
| HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\OLE | Shared |
| HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\RAS | Shared |
| HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\RPC | Shared |
| HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\SOFTWARE\Microsoft\Shared Tools\MSInfo | Shared |
| HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\SystemCertificates | Shared |
| HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\TermServLicensing | Shared |
| HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\TransactionServer | Shared |
| HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths | Shared |
| HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\Control Panel\Cursors\Schemes | Shared |
| HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\Explorer\AutoplayHandlers | Shared |
| HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\Explorer\DriveIcons | Shared |
| HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\Explorer\KindMap | Shared |
| HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\Group Policy | Shared |
| HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\Policies | Shared |
| HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\PreviewHandlers | Shared |
| HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\Setup | Shared |
| HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\Telephony\Locations | Shared |
| HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Console | Shared |
| HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\FontDpi | Shared |
| HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\FontLink | Shared |
| HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\FontMapper | Shared |
| HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Fonts | Shared |
| HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\FontSubstitutes | Shared |
| HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Gre_Initialize | Shared |
| HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options | Shared |
| HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Language Pack | Shared |
| HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\NetworkCards | Shared |
| HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Perflib | Shared |
| HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Ports | Shared |
| HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Print | Shared |
| HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\ProfileList | Shared |
| HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Time Zones | Shared |
| HKEY_LOCAL_MACHINE\SOFTWARE\Policies | Shared |
| HKEY_LOCAL_MACHINE\SOFTWARE\RegisteredApplications | Shared |
| HKEY_CURRENT_USER | Shared |
| HKEY_CURRENT_USER\SOFTWARE | Shared |
| HKEY_CURRENT_USER\SOFTWARE\Classes | Shared |
| HKEY_CURRENT_USER\SOFTWARE\Classes\Appid | Shared |
| HKEY_CURRENT_USER\SOFTWARE\Classes\CLSID | Redirected |
| HKEY_CURRENT_USER\SOFTWARE\Classes\DirectShow | Redirected |
| HKEY_CURRENT_USER\SOFTWARE\Classes\Interface | Redirected |
| HKEY_CURRENT_USER\SOFTWARE\Classes\Media Type | Redirected |
| HKEY_CURRENT_USER\SOFTWARE\Classes\MediaFoundation | Redirected |

> **Most specific match rule**: When determining whether a path is Shared or Redirected, find the **longest (most specific) entry** in the table above that matches the path. Use that entry's status. If no entry matches, the path inherits its parent's status. Under a redirected root (e.g., `HKLM\SOFTWARE`), the default status for unmatched subkeys is Redirected.
>
> Examples:
> - `HKLM\SOFTWARE\Classes\.txt` → matches `HKLM\SOFTWARE\Classes` (Shared, most specific) → **Shared**, no WOW6432Node
> - `HKLM\SOFTWARE\Classes\CLSID\{xxx}` → matches `HKLM\SOFTWARE\Classes\CLSID` (Redirected, most specific) → **Redirected**, WOW6432Node inserted
> - `HKLM\SOFTWARE\Foo` → matches `HKLM\SOFTWARE` (Redirected, most specific) → **Redirected**, WOW6432Node inserted
> - `HKCU\SOFTWARE\Classes\Foo\Bar` → matches `HKCU\SOFTWARE\Classes` (Shared, most specific) → **Shared**, no WOW6432Node
> - `HKCU\SOFTWARE\Classes\Interface\{xxx}` → matches `HKCU\SOFTWARE\Classes\Interface` (Redirected, most specific) → **Redirected**, WOW6432Node inserted

#### 6.8.4 NtQueryKey Path Rewriting with WoW64

When a WoW64 process calls `NtQueryKey(KeyNameInformation)` on a hive handle, the sandbox must rewrite the returned path to reverse the WoW64 redirection (see §6.3 for the full rewriting algorithm):

- **Hive path**: `DATA\Machine\SOFTWARE\WOW6432Node\Foo`
- **App-expected path**: `\REGISTRY\MACHINE\SOFTWARE\Foo`

The rewriting removes the `WOW6432Node\` component from the path (for WoW64 processes) so the app sees its virtual path, matching the behavior of the host registry.

For native 64-bit processes (or WoW64 processes using `KEY_WOW64_64KEY`), the `WOW6432Node\` component is preserved in the returned path if the key is actually under `WOW6432Node` — since they are accessing the 64-bit view, `WOW6432Node` is a real subkey name that should be visible.

#### 6.8.5 WOW6432Node Visibility in Enumeration

The enumeration logic naturally handles WOW6432Node visibility because of the path translation:

- **WoW64 process enumerating `HKLM\SOFTWARE`**: The sandbox translates this to `DATA\Machine\SOFTWARE\WOW6432Node` (WoW64 redirection applied). The process enumerates subkeys under `WOW6432Node` — it does NOT see `WOW6432Node` itself as a subkey. This matches the host registry behavior where a 32-bit process enumerating `HKLM\SOFTWARE` sees the 32-bit application subkeys, not `WOW6432Node`.

- **Native 64-bit process enumerating `HKLM\SOFTWARE`**: The sandbox translates this to `DATA\Machine\SOFTWARE` (no redirection). The process enumerates subkeys under `SOFTWARE` — it DOES see `WOW6432Node` as a subkey (if it exists in DATA or host). This matches the host registry behavior where `WOW6432Node` is a real subkey visible to 64-bit processes.

#### 6.8.6 Host Fallback and WoW64

When the sandbox falls back to the host registry (e.g., for keys not in the hive), it calls the real `NtOpenKey`/`NtCreateKey` from the sandboxed process. Since the sandbox DLL runs in the same process as the target application:

- **For WoW64 processes**: the real `NtOpenKey` automatically applies WoW64 redirection — the kernel sees a 32-bit process and redirects accordingly (e.g., `HKLM\SOFTWARE\Foo` → `HKLM\SOFTWARE\WOW6432Node\Foo`).
- **For native 64-bit processes**: no redirection is applied.

This means **host fallback is naturally WoW64-aware** — the sandbox does not need to apply WoW64 redirection when accessing the host registry. The kernel handles it.

However, when copying data from the host to the hive (copy-before-write, §6.2 step 6), the sandbox must apply WoW64 redirection to the hive path (as described in §6.1). The data is read from the host (using the WoW64-aware kernel redirection) and written to the hive at the WoW64-corrected physical path.

#### 6.8.7 Compile Flow and WoW64

The compile flow (§4) must be WoW64-aware when translating `registry.reg` paths to hive paths.

**Target bitness**: The sandbox configuration specifies the target application's bitness (or it is auto-detected from the target executable's PE header). The compile flow uses this bitness to apply WoW64 redirection, respecting the shared-key and redirected-subkey table (§6.8.3):
- For a 32-bit target: `HKLM\SOFTWARE\Foo` in `registry.reg` → `DATA\Machine\SOFTWARE\WOW6432Node\Foo` in hive
- For a 32-bit target: `HKLM\SOFTWARE\Classes\.txt` in `registry.reg` → `DATA\Machine\SOFTWARE\Classes\.txt` in hive (shared key, no WOW6432Node)
- For a 32-bit target: `HKLM\SOFTWARE\Classes\CLSID\{xxx}` in `registry.reg` → `DATA\Machine\SOFTWARE\WOW6432Node\Classes\CLSID\{xxx}` in hive (redirected subkey)
- For a 64-bit target: `HKLM\SOFTWARE\Foo` in `registry.reg` → `DATA\Machine\SOFTWARE\Foo` in hive

**Explicit WOW6432Node paths**: If the packager explicitly writes a path containing `WOW6432Node` in `registry.reg`, the compile flow uses the path as-is (no further redirection is applied). This allows the packager to create entries for both 32-bit and 64-bit views in the same `registry.reg`:
```reg
[HKEY_LOCAL_MACHINE\SOFTWARE\Foo]
"Value1"="Data1"

[HKEY_LOCAL_MACHINE\SOFTWARE\WOW6432Node\Foo]
"Value2"="Data2"
```
The first entry targets the 64-bit view; the second explicitly targets the 32-bit view.

**Mixed-bitness scenarios**: If both 32-bit and 64-bit processes may use the same hive (e.g., a 64-bit parent spawning a 32-bit child), the packager should either:
- Include entries for both views in `registry.reg` (using explicit WOW6432Node paths for the 32-bit view)
- Or accept that the second process's view may fall through to the host registry for redirected paths (since the compile was done for the primary process's bitness only)

#### 6.8.8 Registry Reflection (Deprecated, Not Implemented)

Windows previously supported **registry reflection** — a mechanism that synchronized certain registry keys between the 32-bit and 64-bit views. When a key was "reflected," changes in one view were automatically copied to the other by the Windows kernel.

Registry reflection was deprecated in Windows Vista with Server 2008 and completely removed in Windows 8. The sandbox **does not implement registry reflection**. Applications that depend on reflection behavior (extremely rare on modern Windows) will not see cross-view synchronization in the sandbox.

#### 6.8.9 `__sbx__` Markers and WoW64

The `__sbx__` markers (Modified, Deleted, Whiteout, Opaque) use the same physical paths as the hive DATA subtree, including WOW6432Node where applicable. This means:
- A 32-bit process deleting `HKLM\SOFTWARE\Foo` creates `__sbx__\Deleted\Machine\SOFTWARE\WOW6432Node\Foo`
- A 64-bit process deleting `HKLM\SOFTWARE\Foo` creates `__sbx__\Deleted\Machine\SOFTWARE\Foo`

These are distinct markers for distinct views, which is correct — deleting a key in one view should not affect the other view. Similarly, Modified, Whiteout, and Opaque markers are view-specific for redirected paths.

When the sandbox checks `__sbx__` markers during open/query operations, it uses the same WoW64-aware path translation (§6.1) to construct the marker path, ensuring that markers are checked against the correct view.

### 6.9 Atomicity and Transaction Safety

Windows guarantees that individual registry API calls are atomic — for example, `NtSetValueKey` either completely writes a value or fails entirely. The sandbox hooks must preserve this guarantee where possible and document where they cannot.

#### 6.9.1 Single-Operation Atomicity

When a hooked registry API is called, the sandbox performs multiple steps:
1. Check the handle set / redirect map
2. Check `__sbx__` markers (Deleted, Whiteout, Opaque)
3. Perform the core registry operation on the hive
4. Update `__sbx__` markers (Modified, Deleted, Whiteout)

**Only step 3 is atomic** — it is performed by the real Windows API on the hive. Steps 1, 2, and 4 are additional sandbox bookkeeping that is NOT atomic with step 3.

**Impact**: If the process crashes or is terminated between steps 3 and 4, the core operation has been applied to the hive but the `__sbx__` markers may be stale:
- A `__sbx__\Modified` marker may be missing — the data exists in DATA but the update flow (§5.2) doesn't know to preserve it. A subsequent LowerFS update could overwrite the change.
- A `__sbx__\Deleted` marker may be missing — the key has been removed from DATA but the marker wasn't created. The sandbox would fall through to the host registry, making the key reappear.
- A `__sbx__\Whiteout` marker may not be removed after a value is written — the value exists in DATA (visible), and the stale whiteout marker is harmless (it only blocks host fallback for values absent from DATA).

**Mitigation**: The `__sbx__` markers are best-effort. The worst-case consequence of stale markers is:
- A lost `Modified` marker → LowerFS update may overwrite an app modification (only happens when the user changes `registry.reg`, which is rare).
- A lost `Deleted` marker → a deleted key temporarily reappears (from host fallback) until the next write operation refreshes the markers.

Both cases are self-healing: subsequent writes or the next sandbox restart will correct the markers.

#### 6.9.2 Transacted Registry API Atomicity

Windows supports transacted registry operations via KTM (Kernel Transaction Manager). The sandbox hooks the following transacted APIs:
- `NtCreateKeyTransacted` / `NtCreateKeyTransactedEx`
- `NtOpenKeyTransacted` / `NtOpenKeyTransactedEx`

These APIs take a transaction handle (obtained from `NtCreateTransaction` or `NtOpenTransaction`) and group multiple registry operations into an atomic unit — all operations are committed together or all are rolled back.

**How the sandbox handles transacted operations**:

The sandbox redirects transacted operations to the hive in the same way as non-transacted operations (§6.2). Since the hive is loaded as an application hive via `RegLoadAppKeyW`, transacted operations on the hive participate in the same KTM transaction as they would on the host registry. This is because KTM is a system-wide transaction manager — it operates on the registry key objects, not on the registry namespace.

**However, `__sbx__` marker updates are NOT part of the transaction.** This creates a consistency gap:

| Scenario | Core operation | `__sbx__` update | On rollback | Impact |
|----------|---------------|-------------------|-------------|--------|
| `NtCreateKeyTransacted` | Creates key in hive (transacted) | Writes `__sbx__\Modified` (non-transacted) | Key creation is rolled back in hive, but Modified marker persists | Minor: LowerFS update skips a non-existent modification |
| `NtDeleteKey` (within transaction) | Deletes key in hive (transacted) | Writes `__sbx__\Deleted` (non-transacted) | Key deletion is rolled back in hive (key reappears), but Deleted marker persists | **Moderate**: key exists in DATA but Deleted marker may incorrectly filter it from host merge during enumeration. Self-correcting after next write to the same path removes the marker (§6.4 step 3a). |
| `NtSetValueKey` (within transaction) | Writes value (transacted) | Writes `__sbx__\Modified` (non-transacted) | Value write is rolled back, but Modified marker persists | Minor: same as NtCreateKeyTransacted |

**Mitigation**: The `__sbx__` markers are always best-effort. The Deleted marker persistence issue is self-correcting because:
- §6.3 enumeration: keys that exist in DATA are not filtered by Deleted markers (only host merge results are filtered).
- §6.4 step 3a: any subsequent NtCreateKey on the same path removes the stale Deleted marker.
- §5.2 update flow: the Modified/Deleted sets are read from the hive and used for decision-making. If the app hasn't actually modified a key but the marker says it has, the update flow preserves the (non-existent) modification — which is harmless (the key just won't be updated from the new LowerFS data).

**The sandbox does NOT hook transaction management APIs** (`NtCommitComplete`, `NtRollbackComplete`, `NtRollbackTransaction`, `NtCreateTransaction`, `NtOpenTransaction`). These operate on the KTM transaction handle, not on registry keys. The sandbox has no need to intercept them — the transaction's commit or rollback applies to the hive operations automatically.

#### 6.9.3 Copy-Before-Write Atomicity

When the sandbox copies a key from the host registry to the hive (§6.2 step 6), it performs a recursive copy of all values and subkeys. This involves multiple registry API calls and is **not atomic**.

**Impact**: If the copy is interrupted (process crash, termination), the hive may contain a partially copied key. For example, the key and some values may exist in DATA, but subkeys may be missing.

**Mitigation**: Copy-before-write is idempotent — it can be safely retried. On the next open of the same key, the sandbox checks DATA and finds the partial copy. Since the key exists in DATA, it is returned to the app (even if incomplete). The missing subkeys would be visible from the host registry via the normal merge logic (§6.3), so the app's view is mostly correct.

If absolute consistency is required, the sandbox could use a two-phase copy:
1. Copy to a temporary path under DATA (e.g., `DATA\__tmp__\...`).
2. On completion, rename the temporary path to the final path.
3. On the next startup, check for and clean up any `__tmp__` entries.

This is a future optimization — the current design accepts the minor inconsistency window.

---

## 7. Handle Lifecycle

### 7.1 Initialization Sequence

The sandbox is a decentralized system — each Loader instance independently starts a target process with the sandbox DLL injected. There is no central coordinator.

1. Loader starts the target process with `CREATE_SUSPENDED` + DLL injection via Detours.
2. Loader copies the `SandboxConfig` (including UpperFS path, LowerFS paths, pipe path, etc.) into the target process via `DetourCopyPayloadToProcess`.
3. The sandbox DLL's `DllMain` → `OnDllAttach` runs (minimal — only operations safe under the loader lock):
   a. Load inject data from the Detours payload and store it in a process-global structure.
   b. Create a dedicated initialization thread via `CreateThread` and return immediately.
   > **Why not do everything in DllMain?** Microsoft explicitly forbids the following operations inside `DllMain` (which runs under the loader lock): loading/unloading DLLs (risk: `RegLoadAppKeyW` may load hive DLLs → deadlock), waiting on synchronization objects (risk: named pipe connection → deadlock), performing file I/O (risk: page fault during hive compile → deadlock), and calling Detours APIs that modify module lists. Performing these in a separate thread avoids all loader-lock hazards.
4. The initialization thread runs (after `DllMain` returns and the loader lock is released):
   a. Connect to the Loader's named pipe (for RPC communication).
   b. Check `registry.hive` — if it does not exist or if `registry.reg` hashes have changed, perform compile/update (§4, §5).
   c. Load `registry.hive` via `RegLoadAppKeyW` → store the hive root handle.
   d. Retrieve the current user SID and store it for path translation.
   e. Determine the process's WoW64 status via `IsWow64Process(GetCurrentProcess(), &isWow64)` and store the result for WoW64-aware path translation (§6.8).
   f. Install all NtXxx hooks.
   g. Signal the initialization-complete event.
5. Resume the target process's main thread.
   > **Race window**: Between step 5 (process resumes) and step 4f (hooks installed), the target process may call registry APIs that bypass the sandbox. In practice, this window is negligible because the process's first user-mode code (CRT initialization) typically does not perform registry operations that need isolation. If needed, the Loader can delay resumption until the initialization-complete event is signaled.

**Multi-instance behavior**: Each Loader instance generates a unique named pipe path (`\\.\pipe\appbox-{timestamp}-{random}`) but uses the same UpperFS path from the configuration. When two Loader instances use the same configuration, their sandboxed processes share the same UpperFS — including the same `registry.hive` file.

### 7.2 Per-Process Hive Loading

Each sandboxed process loads `registry.hive` independently via `RegLoadAppKeyW` during the initialization thread (see §7.1 step 4c). This is necessary because `RegLoadAppKeyW` returns a handle that is only valid in the calling process — the handle cannot be inherited or shared across processes.

The hive file path is derived from the UpperFS path in the `SandboxConfig`. The DLL:
1. Calls `RegLoadAppKeyW` with the hive file path to obtain a private hive root handle.
2. Stores the hive root handle in a process-global variable accessible to all hooks.
3. Retrieves the current user SID and stores it for path translation.
4. Determines the process's WoW64 status and stores it for WoW64-aware path translation (§6.8).

Although each process obtains its own handle, all processes loading the same hive file reference the same kernel hive object (see §7.3.1 for multi-instance implications). Changes are flushed to the hive file when `RegFlushKey` is called or when the last handle is closed.

> **Note on mixed-bitness hive access**: A 32-bit (WoW64) process and a 64-bit process may share the same `registry.hive` file (if they use the same UpperFS). The hive's `DATA` subtree accommodates both views via the `WOW6432Node` subdirectory — 32-bit entries are stored under `DATA\Machine\SOFTWARE\WOW6432Node\...` and 64-bit entries under `DATA\Machine\SOFTWARE\...`. Each process's path translation (§6.1) ensures it accesses the correct view. See §6.8 for details.

### 7.3 Multi-Instance Concurrency

The sandbox supports multiple instances of the same application running simultaneously. Since there is no central coordinator, all concurrency handling must be achieved through file-level and API-level mechanisms.

#### 7.3.1 Shared Kernel Hive Object

When multiple sandboxed processes call `RegLoadAppKeyW` on the same `registry.hive` file, the Windows kernel maps all calls to the **same hive object** in kernel memory. This is a critical property: all processes sharing the same hive file always see each other's changes immediately — there is no per-process stale-view problem.

**Implications**:
- Writes by one process are immediately visible to all other processes sharing the hive (no reload or flush is needed for cross-process visibility).
- `NtNotifyChangeKey` works across processes naturally — since all processes reference the same kernel hive object, change notifications triggered by one process are delivered to all registered waiters, matching the behavior of the real Windows registry.
- `__sbx__` markers (Modified, Deleted, Whiteout, Opaque) written by one process are immediately visible to all other processes sharing the hive.
- The kernel serializes individual registry operations on the shared hive object, so concurrent writes to the same key by different processes are safe at the individual-operation level (the kernel guarantees single-operation atomicity).

**Concurrent write serialization**: While individual registry operations are atomic, application-level read-modify-write patterns (read a value, compute a new value, write it back) are not atomic. The Windows kernel does not provide cross-process read-modify-write atomicity for the real registry either — applications that need this must provide their own synchronization (e.g., named mutexes, transactions). The sandbox does not add synchronization on behalf of the application. This is consistent with the design principle: if the application provides its own multi-process safety mechanism, the sandbox must preserve its semantics; if the application does not, the sandbox has no obligation to substitute.

**Application-owned named mutexes**: If the sandboxed application uses its own named mutexes to serialize registry access (e.g., a multi-instance application that uses a named mutex to ensure only one instance writes at a time), these mutexes continue to work correctly in the sandbox. The sandbox does not interfere with application-level synchronization — the application's named mutex operates in its own namespace, and the sandbox does not introduce a competing serialization mechanism.

#### 7.3.2 Initialization Race Condition

When multiple Loader instances start at the same time using the same configuration, each may independently detect that `registry.hive` does not exist and attempt to create it, or detect that `registry.reg` has changed and attempt to update it.

**Mitigation**: A file-level lock (`registry.hive.lock`) is used to serialize the compile/update flow (§4, §5):
1. Create `registry.hive.lock` with exclusive access before compile/update.
2. If the lock cannot be acquired → another process is performing compile/update → wait and retry.
3. After compile/update is complete → release the lock.

This ensures that only one process performs the compile/update at a time. After the hive file is created or updated, all processes load it via `RegLoadAppKeyW` and share the same kernel hive object (§7.3.1).

**Interaction with runtime operations**: The compile/update flow modifies the hive file on disk. If a sandboxed process already has the hive loaded (and therefore has a kernel hive object in memory), the on-disk file modification by the compile/update flow does not affect the in-memory hive object. The compile/update flow must ensure that no sandboxed process is actively using the hive before replacing the file. In practice, this is guaranteed by the Loader's lifecycle management — the compile/update runs before any sandboxed process is started, or the Loader terminates all sandboxed processes before performing an update.

#### 7.3.3 Subprocess Inheritance

When a sandboxed process creates a child process, the `CreateProcessInternalW` hook (installed by the sandbox DLL) intercepts the call and:
1. Creates the child process with `CREATE_SUSPENDED`.
2. Injects the sandbox DLL via `DetourUpdateProcessWithDll`.
3. Copies the same `SandboxConfig` payload to the child process via `DetourCopyPayloadToProcess`.
4. Resumes the child process.

The child process therefore:
- Shares the same UpperFS and `registry.hive` file.
- Connects to the **same** Loader's named pipe (the pipe path is inherited).
- Loads the same hive file via `RegLoadAppKeyW`, which maps to the same kernel hive object as the parent (§7.3.1).
- Sees the same `__sbx__` markers as the parent process immediately (no delay).

This ensures that a process tree started from a single Loader instance shares a consistent registry view.

#### 7.3.4 NtNotifyChangeKey and Cross-Process Change Notification

On the real Windows registry, `NtNotifyChangeKey` / `RegNotifyChangeKeyValue` allows a process to receive notifications when a registry key is modified — including modifications made by other processes. This is a common mechanism for multi-process applications to detect configuration changes.

**Sandbox behavior**: Since all sandboxed processes sharing the same hive file reference the same kernel hive object (§7.3.1), `NtNotifyChangeKey` works across processes naturally — change notifications triggered by one process's write are delivered to all processes that have registered for notifications on the same hive key. No additional sandbox mechanism (named events, signaling, etc.) is needed.

- **Sandbox-tracked handles**: When the application calls `NtNotifyChangeKey` on a sandbox-tracked hive handle, the sandbox passes the call through to the real `NtNotifyChangeKey` on the hive handle. The kernel delivers notifications across all processes sharing the hive object.
- **Host (non-sandbox) handles**: If the handle is a pass-through host handle (not in the sandbox handle set), the call is passed through to the real system API. No sandbox-specific handling is needed.
- **NtNotifyChangeMultipleKeys**: Handled identically — passed through to the real API on the hive handle.

#### 7.3.5 NtLockRegistryKey

`NtLockRegistryKey` acquires an exclusive lock on a registry key, preventing other processes from accessing it. This API is rarely used and is not officially documented by Microsoft (it is a native API with no Win32 equivalent).

**Sandbox behavior**: When the application calls `NtLockRegistryKey` on a sandbox-tracked handle, the call is passed through to the real `NtLockRegistryKey` on the hive handle. Since all processes sharing the hive reference the same kernel hive object (§7.3.1), the lock is effective across all such processes — matching the behavior of the real Windows registry where `NtLockRegistryKey` operates on the shared kernel registry namespace.

**Rationale**: Applications that depend on `NtLockRegistryKey` for cross-process mutual exclusion are extremely rare, and `NtLockRegistryKey` itself is unreliable even on the real Windows registry (it is reserved/unsupported). The sandbox passes it through without additional handling.

#### 7.3.6 KTM Transactions

When a sandboxed process uses KTM transacted registry APIs (§6.9.2), the transacted operations are redirected to the hive. Since all processes sharing the hive reference the same kernel hive object (§7.3.1), KTM transaction semantics are preserved:

- Transacted operations on the hive participate in the same KTM transaction as they would on the host registry, because KTM operates on the registry key objects (which are in the shared kernel hive), not on the registry namespace.
- Uncommitted transaction changes are not visible to other processes, matching the real Windows behavior.
- The `__sbx__` marker updates for transacted operations are not part of the transaction (§6.9.2), but since markers are stored in the shared hive object, they are immediately visible to all processes once written — there is no stale-view concern.

---

## 8. Hooked Registry APIs

### 8.1 Open / Create (path translation + sandbox handle tracking)
- `NtOpenKey` / `NtOpenKeyEx`
- `NtCreateKey` / `NtCreateKeyTransacted` / `NtCreateKeyTransactedEx`
- `NtOpenKeyTransacted` / `NtOpenKeyTransactedEx`

### 8.2 Write / Modify (track modifications in __sbx__)
- `NtSetValueKey`
- `NtDeleteKey`
- `NtDeleteValueKey`
- `NtRenameKey`
- `NtSetInformationKey`

### 8.3 Read / Enumerate (filter __sbx__, handle opaque + whiteout)
- `NtQueryValueKey`
- `NtQueryMultipleValueKey`
- `NtEnumerateKey`
- `NtEnumerateValueKey`
- `NtQueryKey`

### 8.4 Handle Management
- `NtClose` (remove handle from sandbox handle set; clean up handle redirect map)
- `NtDuplicateObject` (track duplicated handles; handle write-access elevation via redirect map)

### 8.5 Other (redirect to hive or pass through)
- `NtFlushKey`
- `NtSaveKey` / `NtSaveKeyEx` (redirect to hive to prevent host registry snapshot bypass)
- `NtReplaceKey` (redirect to hive)
- `NtRestoreKey` (redirect to hive)
- `NtNotifyChangeKey` / `NtNotifyChangeMultipleKeys` (use hive for change notifications)

---

## 9. Design Constraints Summary

| # | Constraint | How It Is Satisfied |
|---|-----------|---------------------|
| 1 | LowerFS optionally owns `registry.reg` | The compile flow checks each LowerFS for `registry.reg`; if absent, that layer contributes no data. Multiple LowerFS layers are merged in priority order (§4.1). |
| 2 | LowerFS `registry.reg` changes reflect in app view | Per-layer change detection via SHA-256 hashes stored in `__sbx__\Meta\LowerFSHashes` (§5.1); the update algorithm (§5.2) recompiles the merged view while preserving app modifications. |
| 3 | Minimize hooks, no fake handles | All handles returned to the app are real kernel handles. The sandbox only tracks them via a lightweight handle set and redirect map (§6.6). `NtDuplicateObject` is hooked to prevent handle elevation bypass. |
| 4 | Isolate all registry modifications | All write hooks redirect to the hive (§6.4). Copy-on-write (§6.2 step 6, §6.6 NtDuplicateObject) prevents host registry pollution. Shared kernel hive object ensures cross-process visibility without additional synchronization (§7.3.1). |
| 5 | Modified keys survive LowerFS updates | `__sbx__\Modified` and `__sbx__\Deleted` act as a preservation set during recompile (§5.2). Value-level whiteouts are also respected — if the app has written a whited-out value, the whiteout marker is not applied (§5.2 step 6). |
| 6 | `registry.hive` file structure documentation | §2 describes the complete hive structure including `DATA`, `__sbx__\Meta`, `__sbx__\Modified`, `__sbx__\Deleted`, `__sbx__\Whiteout`, and `__sbx__\Opaque`. |
| 7 | Compile flow from `registry.reg` to `registry.hive` | §4 describes the algorithm, including value whiteout, path whiteout, and opaque handling. |
| 8 | Update flow when `registry.reg` changes | §5 describes the change detection and update algorithm, including value whiteout and opaque marker synchronization. |
| 9 | `registry.hive` mapping flow at runtime | §6 describes the complete runtime flow: WoW64-aware path translation (§6.1), open/create with whiteout-aware host fallback (§6.2), read/query with whiteout/deleted/opaque handling + NtQueryKey path rewriting with WoW64 reversal + enumeration index translation + WOW6432Node visibility (§6.3), write with modification tracking + NtRenameKey marker updates (§6.4), enumeration merge logic (§6.5), handle tracking with NtDuplicateObject (§6.6), pre-existing and pass-through handles (§6.7), and complete WoW64 redirection handling including shared keys, compile flow, `__sbx__` markers, and host fallback (§6.8). |
| 10 | `__sbx__` design | §2.2 describes each `__sbx__` subtree: `Meta` (compile metadata), `Modified` (app-modified paths), `Deleted` (app-deleted and path-whiteout paths), `Whiteout` (value-level whiteouts), and `Opaque` (opaque paths). |
