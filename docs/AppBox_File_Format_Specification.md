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
  | Metadata Length   | # 8 bytes.
  |-------------------|
  | Metadata          | # JSON data.
  |-------------------|
  | Filesystem Length | # 8 bytes.
  |-------------------|
  | Filesystem        | # Compressed data
  |-------------------|
  | Registry Length   | # 8 bytes.
  |-------------------|
  | Registry          | # Compressed data
  |-------------------|
  | MAGIC             | # 8 bytes binary data.
  |-------------------|
  | OFFSET            | # 8 bytes.
  |-------------------|
  | CRC32             | # 4 bytes. CRC32(MAGIC+OFFSET)
n -===================-
```

### Loader

A 32-bit loader to start actual program. It is OK if the actual program is 64-bit.

### MAGIC

Defined as:

```text
0x24 0x41 0x50 0x50 0x42 0x4F 0x58 0x3A | $ A P P B O X :
```

### Filesystem Length

8 bytes number, defines the length of the following filesystem data.

### Filesystem

Compressed filesystem data.

### Registry Length

8 bytes number, defines the length of the following registry data.

### Registry

Compressed registry data.

### Metadata Length

8 bytes number, defines the length of the following metadata.

### Metadata

See [Metadata Details](#metadata-details)

### OFFSET

The offset of the first [MAGIC](#magic) from the beginning of the file.

### CRC32

A CRC32 code for [n-20, n-4] bytes, ensures the [OFFSET](#offset) value is correct.

## Metadata Details

Metadata is a JSON encoding in UTF-8:

```json
{
  "filesystem": "[List]",
  "registry": "{Object}",
  "settings": "{Object}",
  "network": "{Object}"
}
```

### filesystem

Filesystem is a JSON list, each element is a JSON object contains the following fields:

```json
{
  "path": "<path>",
  "isolation": "full | merge | write_copy | hide",
  "attribute": [
    "readonly"
  ]
}
```

+ `path`: File or folder path in sandbox. It must exist.
+ `isolation`: File and folder isolation mode.
    + `full`: Completely isolated from the host device. Only files in the virtual filesystem are visible to the
      application even if a corresponding directory exists on the host device. Any writes to files are redirected to the
      sandbox data area.
    + `merge`: Files will be visible from both the virtual filesystem and the host device. Any writes to files which are
      new or already exist on the host device will pass through and be written to the host device. Any writes to files
      that are in the virtual filesystem will be written to the sandbox data area.
    + `write_copy`: Write Copy mode is used when the directory is required to have visibility to files on the host
      device but cannot change them. Files will be visible from both the virtual filesystem and the host device. Any
      writes to files are redirected to the sandbox data area.
    + `hide`: Hide mode is used when a directory on the host device could interfere with the application's ability to
      run properly. The application will receive a `File Not Found` error code whenever an attempt is made to read from
      or write to files in the directory even if the files exist on the host device.
+ `attribute`: File and folder attribute.
    + `readonly`: Flagging files and folders as read-only prevents the application from modifying the file or folder
      contents.

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
