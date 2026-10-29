# fooyin Managed Library

Automatically organize music by Album Artist and Album when tracks are added or their tags change. This extension replaces fooyin's File Operations plugin and retains its manual copy, move, rename and delete actions.

**Supported release target: Windows x64, fooyin 0.14.1, Qt 6.12.0.** The installer verifies the exact tested runtime DLL hashes; other builds of the same version may be rejected. Linux and macOS are not currently supported or tested. This is an independent GPL extension, not an official fooyin release.

## Use

Keep **File Operations** enabled. Open **Settings → Plugins → File Operations → Configure**.

1. Set the managed folder to `D:\music`. That folder must itself be configured as a fooyin library.
2. Enable automatic organization and Save. Existing files are not immediately bulk-organized.
3. New additions and subsequent tag changes are organized automatically. Keep **Library → Monitor libraries** and **Monitor track files** enabled so edits from other programs are detected.
4. For the existing collection, reopen Configure, choose **Preview existing library**, inspect the proposed paths, then **Organize these files**.

Folder layout: `Album Artist\Album\Disc-Track Title.extension`. Artist is the fallback when Album Artist is absent. Disc is unpadded; track is padded to at least two digits. For example:

`D:\music\Game Soundtracks\Tekken\6-63 Tekken 7 - Siren's Call.mp3`

Imports from outside the managed folder (including UNC shares) are **copied**; their original source files remain. Files already in another configured fooyin library are not imported automatically. The destination adopts the original track ID, preserving fooyin playlist references.

## Behavior and limits

- Disabled by default. Enabling handles additions and changes, not the entire historical playlist collection. Preview explicitly queues existing library files.
- Waits for playing/paused files, active library scans and outstanding differences between disk and fooyin path-driving tags.
- Preparation and network copying run on a background thread. Final renames and library updates run on the application thread. Network reads can delay orderly shutdown while an in-flight copy finishes.
- Never intentionally overwrites another file. Conflicts and incomplete tags appear in the activity log. No duplicate merging or automatic suffixes.
- Removes genuinely empty old folders under the managed root, including checking hidden/system entries.
- Two independent, default-off settings can also recycle leftover **cover artwork** and **NFO files**. Cleanup runs only in old folders containing no music, child folders, links or unknown files. Artwork means common filenames such as Cover, folder, front, back, fanart, artist, poster, banner, logo, disc, cdart, clearart, landscape and thumb, with JPG/JPEG/PNG/WebP/GIF/BMP/TIF/TIFF extensions. NFO means any `.nfo` file. Disabled categories stay in place. If recycling fails the file stays; there is no permanent-delete fallback. Every recycling action/failure is logged.
- Embedded artwork is not checked. Enable artwork cleanup only when external cover files are no longer needed. Music files and their embedded tags/artwork are never deleted by this cleanup.
- **Clean up old folders from activity log** saves the dialog settings and applies the same rules to source folders from earlier successful moves. This addresses folders left by the previous plugin version without scanning or reorganizing the entire library. Ordinary Save affects future moves only.
- Skips cue sheets, archives, subsongs, database-only tags, symbolic links and junctions. Destinations of 250 characters or longer are skipped.
- Five retries for metadata mismatches or changing files, then a logged skip; a subsequent refresh/edit or preview retries the track.
- Windows sharing/locking/access errors during a move retry automatically, with increasing delays of 3–30 seconds and a maximum of ten attempts. Other queued tracks can continue while a retry waits. Every failed attempt and eventual success are logged. Same-folder-tree moves use the Windows rename API without a copy/delete fallback, so a locked file never produces a temporary destination copy.
- A recovery journal records each operation before its rename. On startup it repairs an interrupted database path update when source/destination identity checks agree; ambiguous cases pause organization for review. This is not a full undo history.
- External applications' playlists are not rewritten. fooyin updates its own library and playlists. Concurrent filesystem changes by unrelated programs cannot be made transactional with fooyin.
- Do not use manual file operations on the same tracks while automatic organization is processing them.

Activity: `%LOCALAPPDATA%\fooyin\managed-fileops\activity.log`. Recovery journal in the same folder, `pending.json` (normally absent). The dialog shows this session's most recent 200 messages. The log also records source and destination paths for manual review.

## Install on Windows

**Current publication: source only.** No prebuilt DLL is included yet. Build and test the source using the instructions below, then run `package.ps1` with the same Qt/source/runtime parameters to create an installable ZIP. The public metadata/build-script revision still needs a fresh completed build; the preceding local runtime implementation passed the validation described below.

Use a packaged release containing `bin/fyplugin_fileops.dll`, `manifest.json`, `install.ps1` and `rollback.ps1`. A source checkout alone is not an installable binary release.

1. Extract the release into a permanent, writable folder. Keep this folder: it holds the installation receipt and rollback backups.
2. Quit fooyin using **File → Quit**.
3. Open PowerShell as administrator under your usual Windows account and change to the extracted folder.
4. Run:

   ```powershell
   .\install.ps1
   ```

   For a custom installation location:

   ```powershell
   .\install.ps1 -FooyinDirectory 'D:\Apps\fooyin'
   ```

5. Reopen fooyin, keep **File Operations** enabled, and follow **Use** above.

The installer checks compatibility and checksums, backs up the previously installed plugin and the current Windows user's standard fooyin configuration/database, then installs the replacement. It does not move music or change organizer settings. Automatic organization is disabled on first installation; upgrades retain existing settings. Portable/custom profile locations are not included in the profile backup.

If Windows blocks downloaded scripts, review them and use `Unblock-File .\install.ps1` (and similarly for `rollback.ps1`) before running. Organization policy may still restrict script execution.

### Roll back

Quit fooyin and run ` .\rollback.ps1` from the same package folder in administrator PowerShell. It restores the plugin saved by the most recent installation from that folder. That might be an earlier version of this extension. It does **not** undo music moves or restore the database. Keep your usual music backups.

A fooyin update may replace this plugin or change its compatibility. Do not bypass the installer's runtime checks: a new runtime needs a matching build and tests.

## Build from source

Requirements:

- Windows x64 and Visual Studio 2022 C++ Build Tools with a Windows SDK.
- Qt **6.12.0 MSVC2022 x64** SDK, including Qt SVG.
- Python 3, CMake 3.25 or newer, and Ninja on PATH.
- The matching installed fooyin 0.14.1 runtime.
- Upstream fooyin source at commit `5a373aa9763d6f8d86cc749c68971a0f3b1d8918` (v0.14.1).

Obtain upstream source in a separate directory:

```powershell
git clone --branch v0.14.1 https://github.com/fooyin/fooyin.git ..\fooyin
git -C ..\fooyin checkout 5a373aa9763d6f8d86cc749c68971a0f3b1d8918
```

From an **x64 Native Tools Command Prompt for VS 2022**, launch PowerShell and run from this repository:

```powershell
.\build.ps1 -QtDirectory 'C:\Qt\6.12.0\msvc2022_64' -FooyinSource '..\fooyin'
```

Use `-FooyinDirectory` for a non-default runtime location. The script generates import libraries, builds the plugin and runs CTest. After successful tests, run `.\package.ps1 -QtDirectory 'C:\Qt\6.12.0\msvc2022_64' -FooyinSource '..\fooyin'` (adding `-FooyinDirectory` for a custom runtime); it rebuilds and tests before packaging, and its ZIP contains the matching source, license, binary and installer. Output is `build/fyplugin_fileops.dll`. `make_imports.py` reconstructs import libraries from the installed DLL export tables, distinguishing data exports from functions.

The current CMake project uses MSVC flags, Windows DLL imports and Windows runtime paths. Some file-management code has non-Windows branches; that does not make the delivered plugin a tested cross-platform build.

## Troubleshooting

- **Nothing happens after enabling:** enabling does not reorganize existing tracks. Use the preview to review a bulk move, or reload tags for a selected track to queue it.
- **One file stays behind:** stop playback if it is playing or paused. Temporary Windows move failures retry up to ten attempts with delays. After a logged skip, select only that track and choose **Tagging → Reload tags from files** to retry.
- **Old folders remain:** cleanup preserves music, unknown files, child folders and disabled sidecar categories. Enable the appropriate optional cleanup settings if desired; existing leftovers can be checked with **Clean up old folders from activity log**.
- **Wrong artist folder:** check **Album Artist**, which takes priority over Artist.
- **Collision:** the organizer retains the source instead of overwriting another file. Resolve the conflicting destination before retrying.

When reporting problems, include the plugin/fooyin version and relevant log lines. Review logs for personal paths before sharing. Do not upload your library database, configuration, installation backups or recovery journal.

## Credits and license

Managed-library extension by **Dupre Gene (genedupre)**. Based on fooyin's File Operations plugin by Luke Taylor and fooyin contributors. Original copyright notices are retained. Source is licensed under **GPL-3.0-or-later**; see [LICENSE](LICENSE).

## Validation

The preceding local Windows build passed all three CTest targets. The public packaging revision has not yet completed a fresh build; do not treat these prior results as release certification.

CTest includes path/file-operation tests, a DLL-load test and an integration test using the installed fooyin core/gui DLLs, a temporary SQLite library and copied upstream MP3 fixtures. It checks disabled defaults, settings saves, Album Artist moves, preserved track IDs and playlist paths, real external tag-file monitoring, pending tag writes, playing-file deferral, external copies, collisions and interrupted-move recovery. Cleanup tests cover independent artwork/NFO toggles, protecting music/unknown files/subfolders/root, and retaining files when recycling fails. No real library files are modified by the tests.

The integration test's configuration dialog is also rendered to `managed-settings.png`. This does not simulate every codec, network failure or power-loss condition.
