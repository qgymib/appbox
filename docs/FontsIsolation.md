# Fonts isolation

appbox isolates the fonts of a packaged application: the `Fonts` preset directory
of the `Filesystem` workspace maps its layer to the font directory of the system
(`FOLDERID_Fonts`, which is `%windir%\Fonts` on every installation), and the fonts
a layer carries are loaded into the font table of every process of the run. A
sandboxed application therefore creates, enumerates and renders the packaged fonts
as if they were installed, while the font directory, the font table and the
registry of the host stay untouched: nothing of a run survives its processes.

The workspace which imports the fonts is documented in
[README.md](../README.md#variable-expansion).

## The `Fonts` preset directory

| Property | Value |
| --- | --- |
| Identifier | `fonts` |
| Label of the tree item | `Fonts` |
| Layer key | `#Fonts#` |
| Known folder | `FOLDERID_Fonts` (`%windir%\Fonts`) |
| Variable | `%APPBOX:Fonts%` |
| Position in the tree | below `Windows`, next to `System32` |

The layer is an ordinary layer of the filesystem view: the content travels in the
archive as `app/filesystem/#Fonts#/<import>/...`, the folder merges its layers like
every other folder, and a file of a patch package overrides the file of the same
name of the archive (see [Filesystem Isolation](FilesystemIsolation.md)).

## How a packaged font becomes usable

The module `sandbox/fonts/Isolation` runs with the other modules of the sandbox,
before the hooks are attached, and loads the fonts of the view into the font table
of the process:

1. It builds the view path of the font directory (`%windir%\Fonts` in NT form) and
   resolves it with the resolver of the filesystem (`ResolveFull` with
   `bStopOnFirstFound` off), so the layers which hold the folder come back in the
   precedence order of the view: the upper layer first, then the layers of the
   patch packages in ascending name order, then the layer of the archive, then the
   host.
2. It walks the font directory of every layer but the host one and collects the
   font files, deduplicated by their path below the font directory: the first layer
   which holds a name is the layer the view shows for that name.
3. Every collected name is resolved in the view on its own, so a whiteout, an
   opaque marker and the isolation mode of a file are honoured like they are for
   every other read of the sandbox.
4. The file of the layer which won is loaded with
   `AddFontResourceExW(<DOS path of the file>, FR_PRIVATE, nullptr)`.

`FR_PRIVATE` is what keeps the isolation complete: the font is added to the font
table of **the calling process only**, no registry value is written, another
process of the host never sees it, and the kernel drops the table when the process
exits. The flag is never zero, which would add the font to the font table of the
session and leak it to every process of the host, and `FR_NOT_ENUM` is never set,
which would keep the application from enumerating the font it may use.

Only the fonts of a layer are loaded: a font the host filesystem carries is
installed already and is left to the host. A font which cannot be loaded is logged
and skipped, and a view without a font directory is not an error: a broken font
never fails the start of an application.

Every process of a run does this for itself, because the sandbox DLL is injected
into the processes a sandboxed application starts as well, and the fonts are loaded
before the application reaches its entry point, so its first font call already sees
them.

## The path the kernel opens

The font driver of the system reads the file of a font resource itself: it opens
the path in kernel mode (or in the process of the font driver), so the file hooks
of the sandbox never see it. A path of the view therefore never reaches the file of
a layer, which is why

* the module loads the fonts from the **path of the file inside its layer** and
* the sandbox rewrites the path of a call the application makes itself.

The call which the implementation of `gdi32!AddFontResourceExW` makes was measured
on this build instead of being assumed, because `win32u!NtGdiAddFontResourceW` is
not documented:

| Property | Measured value |
| --- | --- |
| Implementation | `gdi32!AddFontResourceExW` is a thunk into `gdi32full.dll` through the API set `ext-ms-win-gdi-internal-desktop-l1-1-0`; the implementation calls `win32u!NtGdiAddFontResourceW`, which is a plain syscall stub (syscall `0x1180` on this build). |
| Path | NT form with the namespace of the object manager, for example `\??\C:\Windows\Fonts\consola.ttf`. A DOS path is refused. |
| `cwc` | Number of characters of the buffer **including** the terminating NUL of the last path; a count without it is refused. |
| `cFiles` | Number of files of the buffer. Every call of the two documented entry points of this build passes `1`, and a buffer which carries several files is refused by this build (see the known gaps). |
| `flags` | The flags of the caller (`FR_PRIVATE` and friends) plus an internal bit `1` of the implementation. |
| Parameters 5 and 6 | Passed through unchanged; the hook never looks at them. |

The hooks of the domain are:

| Hook | Behaviour |
| --- | --- |
| `win32u!NtGdiAddFontResourceW` | A call which names one file is resolved in the view: a path a sandbox layer holds is replaced with the path of that file and the buffer is rebuilt with the count of the caller plus the difference of the two path lengths. Every other call is forwarded unchanged. |
| `win32u!NtGdiRemoveFontResourceW` | The very same rewrite, so a caller which adds a font of the view and removes it again with the same path reaches the same file. |

The rewrite keeps the convention of the caller: only the difference of the two path
lengths is added to the count, so the buffer stays consistent whatever the caller
counted. A buffer whose declared length does not carry a terminator, a path which
only the host filesystem holds, a path the isolation hides and a path no layer holds
are forwarded unchanged: the kernel then reports what the view reports.

`win32u.dll` and `gdi32.dll` are loaded on demand, so the sandbox loads both before
it resolves the entry points of these hooks: a hook which cannot be resolved is
fatal in isolation mode, and the sandbox would otherwise silently stop rewriting the
font paths. The modules are loaded in isolation mode only, so a process which is not
sandboxed does not load the graphics modules because of the sandbox.

The module of the fonts reads the view with the resolver of the filesystem, whose
entry points are resolved together with the hooks. It resolves the two entry points
it needs (`RtlInitUnicodeString` and `NtQueryAttributesFile`) itself, without
attaching a hook, so the module order of the sandbox — every module runs before the
hooks are attached — stays in place.

## Tests

The cases run the real launcher with a probe process inside the sandbox:

* `test/e2e/Font_PackedFontIsUsable.cpp` — the font table of the sandboxed process
  carries the family of the packed font, a font which is created for that family is
  the family itself, and the file of the view carries the content of the layer.
* `test/e2e/Font_AddFontResourceEx_ViewPath.cpp` — a call of the application which
  names the path of the view adds the font, so the hook resolved the path to the
  file of the layer.
* `test/e2e/Font_HostIsNotModified.cpp` — the font folder of the host carries no
  file of the layer, the font registry of the host carries no entry of the font,
  and the family is gone once the sandboxed process ended.
* `test/e2e/Patch_FontOfTheLastLayerWins.cpp` — a patch package overrides the font
  of the archive which is stored under the same name.

The cases need a family the host does not carry, which `test/utils/TestFont.*`
builds from an installed font: it rewrites the family strings of the `name` table
in place — the replacement has the same length, so no offset of the file moves —
and recomputes the two checksums which cover the table.

## Known gaps and limitations

* **DirectWrite is not covered.** The system font collection of DirectWrite
  (`IDWriteFactory::GetSystemFontCollection`) is built from the font registry and
  the font cache of the system, and `AddFontResourceEx` does not reach it: the
  documented equivalent of the GDI call is the custom font collection of the API,
  which an application has to ask for itself. An application which renders with
  DirectWrite (Chromium and Electron, WPF, WinUI) therefore keeps the fonts of the
  host. The GDI and GDI+ path (Win32, MFC, wxWidgets, Windows Forms) is covered.
* **A family the host carries is not overridden.** The font table of the session is
  built before the process starts, so GDI keeps the installed face next to the
  private one and prefers the installed one. A packaged font whose family is
  installed already is usable, but it is not the face the system chooses.
* **A buffer which carries several font files is not rewritten.** This build refuses
  such a buffer for every spelling which was tried, so its layout is unknown and the
  hook forwards it unchanged. Every caller which was observed passes one file.
* **The registry is not presented.**
  `HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Fonts` of the sandbox does not
  list the packaged fonts, because the font table does not read it: an application
  which reads the key to decide whether a font is installed does not see them. The
  values can be written by hand in the `Registry` workspace of the packer.
* **The fonts are loaded once, when a process starts.** A font the application
  writes into the font folder of the view while it runs becomes part of the font
  table of the next start of the sandbox, not of the process which wrote it.
* **A font file has to stay reachable while the process runs.** The file is loaded
  from the path of its layer, which the sandbox never removes, because the resources
  of the archive are read-only.
