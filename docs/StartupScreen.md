# Startup Screen

A packaged application takes a moment to start: the launcher of a standalone
archive loads and extracts the patch packages of the user, mounts the filesystem
layers of the sandbox, mounts the virtual registry and only then creates the
processes of the application. The startup screen covers that moment with a small
window which presents the name of the packaged application, the startup image of
the session and the progress of the run, so a user sees that the program is
being prepared instead of a launcher which seems to do nothing.

> **Implementation status**: the `Startup` tab of the Settings workspace, the
> archive entry of the startup image, the launcher configuration it is named in
> and the screen of the launcher are implemented, including the stages of the
> progress bar, the tolerance towards an image which is missing or cannot be
> decoded and the end-to-end cases of `test/e2e/Launcher_StartupScreen.cpp`.

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

## Archive layout

The image travels in the read-only resources of the archive, below `app`:

```
app/startup/splash.png       startup image of the session (optional)
```

The file name is the base name of the archive layout
([`common/SandboxLayout.hpp`](../common/SandboxLayout.hpp)) with the extension
of the file the user selected: a session which selects `logo.PNG` packs
`app/startup/splash.png`, so the launcher configuration names one entry whatever
format the user picked. A session without an image packs no file of that name.

The launcher configuration carries the screen of the run:

```json
{
  "hide_console": false,
  "startup": {
    "screen": true,
    "image": "startup/splash.png"
  },
  "startups": []
}
```

- `screen` decides whether a window is opened at all. A configuration which does
  not name the member shows the screen, which is what an archive packed before
  the member existed describes.
- `image` is the path of the image relative to the resource root, so the
  launcher resolves it below `app`; an empty value means that the screen
  presents the name of the application and the progress of the run only.

## Behavior

- **Name**: the launcher reads the `ProductName` of its own version resource,
  which the packer fills with the file properties of the packaged application
  (see `src/core/ApplicationMetadata.*`); a program without a product name falls
  back to its `FileDescription` and finally to its own file name without the
  extension.
- **Image**: the file is decoded once with the imaging component of Windows
  (WIC, a component of the operating system) into premultiplied BGRA pixels, so
  a logo with transparency keeps it. An image which is missing, which cannot be
  decoded or whose format is unknown leaves the screen without an image and is
  reported in the log of the run; it never refuses the run.
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

1. A patch package cannot carry a startup image of its own: the launcher
   configuration lives in the standalone archive, so a patch would have to
   override the image of the archive under the very same name. The packer
   offers no way to do that.
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
