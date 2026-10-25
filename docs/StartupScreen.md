# Startup Screen

A packaged application takes a moment to start: the launcher of a standalone
archive loads and extracts the patch packages of the user, mounts the filesystem
layers of the sandbox, mounts the virtual registry and only then creates the
processes of the application. The startup screen covers that moment with a small
window which presents the name of the packaged application, the startup image of
the session and the progress of the run, so a user sees that the program is
being prepared instead of a launcher which seems to do nothing.

## Settings workspace

The `Startup` tab of the Settings workspace holds the startup screen of the
session:

| Control | Meaning |
| --- | --- |
| `Show the startup screen while the launcher starts the application` | Whether the launcher of the archive opens a window of its own. The flag travels in the launcher configuration and in the project file. |
| `Startup Image` | The host path of the image the screen presents, chosen with `Browse...`. The box is read only, so a hand typed path can never name a file the pack run cannot read. |
| `Browse...` | Opens a file dialog which offers the formats the launcher decodes (`*.png`, `*.jpg`, `*.jpeg` and `*.bmp`, see `StartupImageExtensions()` of `src/core/StartupScreen.*`). |
| `Clear` | Drops the image of the session, which leaves the name of the application and the progress of the run. |

A patch project cannot set a startup screen: a patch package carries no launcher
which could show one, so the tab is disabled and says so, exactly like the
`Metadata` tab.

## Launcher resources

The image does not travel as a file of the archive: the pack run reads the bytes
of the file the session names and embeds them as an RCDATA resource of the
launcher payload itself (see `src/core/LauncherResources.cpp`). The launcher
carries two resources of its own, both of them declared in
[`launcher/EmbeddedResourceIds.h`](../launcher/EmbeddedResourceIds.h):

| Resource | Content |
| --- | --- |
| `IDR_APPBOX_LAUNCHER_CONFIG` | The UTF-8 JSON document of the run configuration, written by every pack run. |
| `IDR_APPBOX_STARTUP_IMAGE` | The bytes of the startup image, written when the session sets one. |

A session without an image writes no image resource at all: the payload the
packer starts from is the launcher as it was built, which never carries one, so
there is nothing to replace or drop. The archive itself carries no `app/startup`
entry.

The launcher configuration carries the screen of the run:

```json
{
  "hide_console": false,
  "startup": {
    "screen": true
  },
  "startups": []
}
```

- `screen` decides whether a window is opened at all. A configuration which does
  not name the member shows the screen, which is what an archive packed before
  the member existed describes.
- The image is not part of the document: the resource is the only signal of a
  session which presents one, so a payload without
  `IDR_APPBOX_STARTUP_IMAGE` shows the name of the application and the progress
  of the run only. A document which still names the removed `image` member of an
  earlier version parses as well, because unknown members are ignored.

## Behavior

- **Name**: the launcher reads the `ProductName` of its own version resource,
  which the packer fills with the file properties of the packaged application
  (see `src/core/ApplicationMetadata.*`); a program without a product name falls
  back to its `FileDescription` and finally to its own file name without the
  extension.
- **Image**: the resource is decoded once with the imaging component of Windows
  (WIC, a component of the operating system) into premultiplied BGRA pixels, so
  a logo with transparency keeps it. The decoder reads the bytes from memory
  instead of a file and sniffs their container, so the format of the selected
  file does not have to be spelled anywhere. A payload without the resource is
  the normal session without an image; an image resource which cannot be
  decoded is reported in the log of the run and leaves the screen without an
  image. Neither case ever refuses the run.
- **Layout**: the name is centered on top, the image below it and the progress
  bar at the bottom. The image keeps its aspect ratio and is scaled down while
  it does not fit into 60 percent of the work area of the monitor, so a large
  logo never leaves the screen. The window is a borderless card which is
  centered on the work area of the monitor of the foreground window, and it is
  created without activating itself, so it never takes the focus from the
  program the run starts.
- **Progress**: the bar follows the stages of the run - preparing, loading the
  patch packages, mounting the sandbox and starting the application - and
  approaches the reported value instead of jumping. The launcher pumps the
  message queue while it works, and the extraction of a patch package reports
  every file it writes, so the bar keeps moving while a large package is
  unpacked.
- **End**: the screen is destroyed as soon as every process of the run was
  created, which is the moment the packaged application is on its own. A run
  which fails before that closes the screen on its way out as well, so the
  window never stays behind.

## Known gaps and limitations

1. A patch package cannot change a launcher resource: the configuration and the
   startup image live in the payload of the standalone archive, so a package
   which is dropped into its `patch` directory cannot replace either of them.
   The packer offers no way to do that.
2. The progress inside one patch package is reported per extracted file, but the
   packages which are already in the cache of a run are counted as a single
   step: the bar advances without a report of their own while they are read.
3. The image is scaled down only: an image which is smaller than the room the
   screen gives it is drawn with its own size, so a small logo stays small.
4. The launcher carries no manifest which would make it aware of the resolution
   of the monitor, so the system scales the window of the screen on a machine
   whose resolution differs from 96 DPI. The layout is spelled in the units the
   system scales, which keeps the screen in proportion, but a screenshot of it
   is not pixel exact on such a machine.
5. The screen has no rounded corners, no shadow and no fade out: it is a plain
   borderless window, which is what the Win32 API of the launcher offers without
   the desktop window manager composition APIs.
