# fooyin Managed Library

A drop-in replacement for fooyin's **File Operations** plugin. It keeps your music folder organized for you: when you add tracks or edit their tags, files are moved into `Artist/Album/` folders based on a pattern you choose. When you delete an artist's or album's last tracks, its leftover folders are cleaned up too. The original manual copy, move, rename and delete actions are still there.

- **fooyin:** 0.14.1
- **Platforms:** Linux (fooyin Flatpak) and Windows x64
- **Version:** 0.1.7

This is an independent GPL extension, not an official fooyin release.

## Install

### Linux (Flatpak)

You need the fooyin Flatpak (`org.fooyin.fooyin`), the KDE SDK (`flatpak install org.kde.Sdk//6.11`) and the fooyin source for the headers:

```bash
git clone --branch v0.14.1 --depth 1 https://github.com/fooyin/fooyin.git ../fooyin
./build.sh      # builds inside the KDE SDK and runs the tests
./install.sh    # copies the plugin into the Flatpak's user plugin folder
```

If the fooyin source is somewhere other than `../fooyin`, run `FOOYIN_SOURCE=/path/to/fooyin ./build.sh`.

The plugin is installed to `~/.var/app/org.fooyin.fooyin/.local/lib/fooyin/plugins/`, and the previous version is kept as `fyplugin_fileops.so.bak`. Restart fooyin with **File → Quit** and reopen it.

If **Settings → Plugins** shows two "File Operations" entries, untick **Load** on the stock one (author: fooyin).

### Windows

There's no prebuilt release yet. Build the plugin (see [Building on Windows](#building-on-windows)), run `package.ps1` to create a ZIP, then:

1. Extract the ZIP to a permanent folder. It holds the install receipt and rollback backups.
2. Quit fooyin with **File → Quit**.
3. In an administrator PowerShell, run `.\install.ps1` from that folder. For a non-default fooyin location, add `-FooyinDirectory 'C:\path\to\fooyin'`.

The installer checks that your fooyin runtime matches the tested build, backs up the current plugin plus your fooyin config and database, then installs. `.\rollback.ps1` puts the previous plugin back. It does **not** undo moved files.

## Setup

Open **Settings → Plugins → File Operations → Configure**.

1. **Managed folder:** defaults to your system Music folder (`~/Music` or `C:\Users\<you>\Music`). Change it if your music is elsewhere. It must also be added as a library in fooyin.
2. **Organization pattern:** leave the default or write your own (see below). The dialog shows a live preview.
3. Tick **Automatically organize new additions and tag changes** and Save.
4. In fooyin's library settings, keep **Monitor libraries** and **Monitor track files** on so changes made by other programs are picked up.

Enabling it does **not** touch your existing collection. To organize what's already there, click **Preview existing library…**, review the proposed moves, then click **Organize these files**.

## Organization pattern

Default:

```
%albumartist%/%album%/$if(%disc%,%disc%-,)$num(%track%,2) %title%
```

This gives `Daft Punk/Discovery/01 One More Time.flac`, or `Daft Punk/Alive 2007/2-03 Around the World.flac` for multi-disc albums. The extension always stays the same, and `/` separates folders.

| Tag | Value |
|---|---|
| `%albumartist%` | Album Artist, or Artist if that's empty |
| `%artist%`, `%album%`, `%title%` | The tag as-is |
| `%disc%`, `%track%` | The number only (`3/12` → `3`) |

| Function | Does |
|---|---|
| `$if(x,then,else)` | `then` if `x` isn't empty, otherwise `else` (optional) |
| `$if2(x,fallback)` | `x`, or `fallback` if `x` is empty |
| `$num(x,n)` | Zero-pads a number to `n` digits |
| `$lower(x)`, `$upper(x)`, `$trim(x)` | Changes case or trims spaces |
| `$replace(x,from,to)` | Replaces text |

Characters that aren't allowed in file names are replaced. If a folder or file name would come out empty (for example, a missing album tag), the track is skipped and logged.

## What happens to files

- **Tracks inside the managed folder** are moved. fooyin's library and playlists follow the move, because the track keeps its ID.
- **Tracks added from outside** (another drive, a network share) are **copied** in, and the original stays where it was. You can turn this off with **Copy newly added files from outside this folder**. Tracks that belong to another fooyin library are never pulled in.
- **Existing files are never overwritten.** If the destination already exists, both files stay put and the conflict is logged.
- **Playing or paused tracks** wait until playback moves on. Moves also wait while a library scan is running.

## Folder cleanup

After a track is moved away or deleted, its old folder is removed if it's now empty. The cleanup then works upward and stops at the managed folder, so deleting an artist's last album removes the album folder and then the artist folder.

Leftover extras often keep a folder from being empty. Two optional settings send them to the trash:

- **Cover artwork:** image files (`jpg`, `png`, `webp`, `gif`, `bmp`, `tif`) named `cover`, `folder`, `front`, `back`, `fanart`, `artist`, `poster`, `banner`, `logo`, `disc`, `cdart`, `clearart`, `landscape` or `thumb`.
- **NFO files:** any `.nfo`.

A folder is only cleaned when nothing else is left in it: no music, subfolders, links or unrecognized files. Files go to the trash and are never deleted permanently. If moving one to the trash fails, it stays. Every action is logged.

"Remove from library" without deleting the files doesn't trigger cleanup. To clean folders left behind by earlier versions, use **Clean up old folders from activity log**.

## Not handled

These are skipped: cue sheets, files inside archives, subsongs, database-only tags, symbolic links and junctions, and destination paths of 250 characters or more.

Other things to know:
- Playlists in other applications aren't updated.
- Don't run manual file operations on tracks while the organizer is still processing them.
- On Windows, a file that's locked by another program is retried up to 10 times, waiting longer each time (3–30 s).

## Logs and recovery

The configure dialog shows the last 200 log messages. The full log is `activity.log` in:

| Setup | Folder |
|---|---|
| Linux Flatpak | `~/.var/app/org.fooyin.fooyin/data/fooyin/managed-fileops/` |
| Linux native | `~/.local/share/fooyin/managed-fileops/` |
| Windows | `%LOCALAPPDATA%\fooyin\managed-fileops\` |

Before each move, the plugin writes a `pending.json` to the same folder. If fooyin closes in the middle of a move, the plugin uses it on the next start to finish updating the library. If the situation is unclear, the organizer pauses instead of guessing, and the log explains why.

## Troubleshooting

- **Nothing happens after enabling:** only new additions and tag changes are organized. Use **Preview existing library…** for everything else.
- **One file didn't move:** check the log. It's usually playback, a tag mismatch or a conflict. To retry, select the track and choose **Tagging → Reload tags from files**.
- **A folder was left behind:** something in it isn't recognized, or artwork/NFO cleanup is off. Look in the folder, or check the log.
- **Wrong artist folder:** Album Artist takes priority over Artist.

When reporting a bug, include the plugin version and the relevant log lines, and remove any personal paths first.

## Building on Windows

Requirements: Visual Studio 2022 C++ Build Tools, Qt 6.12.0 MSVC2022 x64 (with Qt SVG), Python 3, CMake 3.25+ and Ninja, plus the installed fooyin 0.14.1 and its source at the same version:

```powershell
git clone --branch v0.14.1 https://github.com/fooyin/fooyin.git ..\fooyin
```

From an **x64 Native Tools Command Prompt for VS 2022**, run:

```powershell
.\build.ps1   -QtDirectory 'C:\Qt\6.12.0\msvc2022_64' -FooyinSource '..\fooyin'
.\package.ps1 -QtDirectory 'C:\Qt\6.12.0\msvc2022_64' -FooyinSource '..\fooyin'
```

Add `-FooyinDirectory` if fooyin isn't installed in the default location. `make_imports.py` generates import libraries from the installed fooyin DLLs, so you don't need a full fooyin build.

## Tests

`ctest` runs three tests:
- unit tests for path planning, the pattern engine and folder cleanup
- a plugin-load test
- an integration test against the real fooyin libraries, using a temporary database and sample MP3s

The integration test covers moves, kept track IDs and playlist paths, tag-change monitoring, waiting for playback, external copies, conflicts and recovery from an interrupted move. It doesn't cover cleanup after a delete. Nothing in your real library is touched.

## Credits and license

Managed-library extension by Gene Dupré ([genedupre](https://github.com/genedupre)). Based on fooyin's File Operations plugin by Luke Taylor and the fooyin contributors. Licensed under **GPL-3.0-or-later**, see [LICENSE](LICENSE).
