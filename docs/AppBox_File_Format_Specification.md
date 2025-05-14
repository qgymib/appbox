# AppBox File Format Specification

A program and its dependencies is packed in AppBox file format.

## Layout

The AppBox file layout is defined as following:

```
0 -===================-
  | Loader            | # Program loader (i386).
  |-------------------|
  | MAGIC             | # 8 bytes binary data.
  |-------------------|
  | Metadata Length   | # 8 bytes. Big-Endian.
  |-------------------|
  | Metadata          | # Compressed data
  |-------------------|
  | Filesystem Length | # 8 bytes. Big-Endian.
  |-------------------|
  | Filesystem        | # Compressed data
  |-------------------|
  | Registry Length   | # 8 bytes. Big-Endian.
  |-------------------|
  | Registry          | # Compressed data
  |-------------------|
  | MAGIC             | # 8 bytes binary data.
  |-------------------|
  | OFFSET            | # 8 bytes. Big-Endian.
  |-------------------|
  | CRC32             | # 4 bytes. Big-Endian. CRC32(MAGIC+OFFSET)
n -===================-
```

### Loader

A 32-bit loader to start actual program. It is OK if the actual program is 64-bit.

### MAGIC

Defined as:

```text
0x24 0x41 0x50 0x50 0x42 0x4F 0x58 0x3A | $ A P P B O X :
```

### Metadata Length

8 bytes big-ending number, defines the length of following metadata.

### Metadata

See [Metadata Details](#metadata-details)

### Filesystem Length

8 bytes big-ending number, defines the length of following filesystem data.

### Filesystem

Compressed filesystem data.

### Registry Length

8 bytes big-ending number, defines the length of following registry data.

### Registry

Compressed registry data.

### OFFSET

The offset of the first [MAGIC](#magic) from the begin of file.

### CRC32

A CRC32 code for [n-20, n-4] bytes, ensure the [OFFSET](#offset) value is correct.

## Metadata Details

Metadata is a compressed json:

```json
{
  "filesystem": "<Object>",
  "registry": "<Object>",
  "settings": "<Object>",
  "network": "<Object>"
}
```

### filesystem

> // TODO

### registry

> // TODO

### settings

```json
{
  "process": "<Object>",
  "sandbox": "<Object>",
  "startup": "<Object>"
}
```

#### settings.process

```json
{
  "cwd": "<Object>",
  "spawn_child_within_sandbox": "<Object>",
  "shutdown_process_tree_on_root_exit": true
}
```

##### settings.process.cwd

Defines the current working directory.

+ `0`: Use current directory.
+ `1`: Use startup file directory.
+ _String_: Use the specific path. Support [variable expansion](#variable-expansion).

##### settings.process.spawn_child_within_sandbox

Spawn child processes within the virtualized environment.

```json
{
  "mode": true,
  "list": [
    "name1"
  ]
}
```

+ `mode`: `true` to treat as whitelist, `false` to treat as blacklist.
+ `list`: Executable name list. If `mode` is `true`, only executables in the list run outside sandbox. If `mode` is
  `false`, only executables in the list run inside sandbox.

##### settings.process.shutdown_process_tree_on_root_exit

+ `true`: If root process exit, terminate all child processes.
+ `false`: Do not terminate child processes.

#### settings.sandbox

```json
{
  "location": "PATH",
  "reset": false
}
```

##### settings.sandbox.location

The location of modified files.

##### settings.sandbox.reset

Reset sandbox on application shutdown.

#### settings.startup

```json
{
  "splash_image": "PATH",
  "splash_until": 0
}
```

##### settings.startup.splash_image

The startup image, must in `bmp` format, encoding in base64.

##### settings.startup.splash_until

The number of seconds the splash would show.

If set to 0, show until the fist application window appears.

### network

> // TODO

## Variable expansion

Any key word occur will be replaced with the corresponding value. The key is **case sensitivity**.

| Variable            | Example Value                |
|---------------------|------------------------------|
| %AppData%           | C:\Users\xxx\AppData\Roaming |
| %LocalAppData%      | C:\Users\xxx\AppData\Local   |
| %ProgramFiles%      | C:\Program Files             |
| %ProgramFiles(x86)% | C:\Program Files (x86)       |
| %SysWOW64%          | C:\Windows\SysWOW64          |
| %WinDir%            | C:\Windows                   |
