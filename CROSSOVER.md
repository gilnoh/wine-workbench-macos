# Fixes for CrossOver users

This project builds CodeWeavers' published CrossOver Wine source with a few patches of its own. Some of those patches fix problems that also exist in CrossOver itself. For those, we provide **drop-in files**: replacements for a few files inside CrossOver.app, plus a script that installs them and one that puts the originals back.

This is unofficial. It is not made, endorsed or supported by CodeWeavers. If something goes wrong after installing it, run the restore script first, and do not report problems to CodeWeavers while these files are installed.

**Download:** https://github.com/gilnoh/wine-workbench-macos/releases/latest

## CrossOver 26.3.0 — Sea Power

**What it fixes.** In *Sea Power: Naval Combat in the Missile Age*, missions start with ships missing and then end at once as "completed". In the mission *Gauntlet*, for example, the Knox- and Perry-class frigates never appear. With these files every ship is there as expected, and the missions play normally: carriers launch their aircraft, submarines fire their torpedoes, and the frame rate is good. Tested on several missions. It is the same fix this project's own Wine build has run Sea Power with since October 2026.

**Why it happens.** Wine on macOS does not keep a copy of the "last error" value where the game's runtime (Unity's Mono) reads it. The game then takes data files that do not exist for files that do, and loads nothing from them.

**Also included:** a fix for a Wine leak in long sessions, where the game eventually freezes with all CPU cores busy (seen in *Fantasy General II*; the Wine log then says `msync: warn: node memory pool exhausted`). It applies when the bottle has CrossOver's MSync setting on. Tested inside CrossOver 26.3 with a stress test of the waiting pattern that leaks: with CrossOver's own files the pool ran out after about a minute; with these files 40 million rounds ran to the end without it.

**Requirements.** CrossOver 26.3.0 exactly. The install script checks the exact build and stops without changing anything if it does not match. Tested on an Apple Silicon Mac.

**Files replaced**, all inside `CrossOver.app/Contents/SharedSupport/CrossOver/lib/wine/`: `x86_64-unix/ntdll.so`, `x86_64-windows/ntdll.dll`, `x86_64-windows/kernelbase.dll`.

### Install

1. Quit every program running in your bottles, then quit CrossOver (Cmd-Q).
2. Unzip the download.
3. Open Terminal (Applications → Utilities → Terminal). Type `bash ` (with a space after it), drag `install.command` from the unzipped folder into the Terminal window, and press Return.
4. If the script says macOS did not allow changing CrossOver.app: open System Settings → Privacy & Security → App Management, switch on **Terminal**, quit and reopen Terminal, and repeat step 3. The script opens that settings page for you.
5. Start CrossOver and play.

The script backs up CrossOver's original files to `~/Library/Application Support/wine-workbench/crossover-dropin-backup/` before replacing them, and checks every file by checksum before and after.

You can also double-click `install.command`. macOS usually refuses to open a script downloaded from the internet; then use System Settings → Privacy & Security → "Open Anyway", or the Terminal way above.

### Undo

Quit CrossOver as in step 1, then run `restore.command` the same way as step 3. It puts the backed-up originals back and checks them.

A CrossOver update also replaces these files with CrossOver's own, so after an update the fix is gone and Sea Power behaves as before. A new drop-in is needed for each CrossOver version.

## How it is built

The files come from this repository's runtime: CodeWeavers' CrossOver source ([winecx-sources](https://github.com/gilnoh/winecx-sources), tag `winecx-26.3.0`, unmodified) plus the patches in [`patches/`](https://github.com/gilnoh/wine-workbench-macos/tree/master/patches), built by `bin/build-runtime` and packaged by `bin/build-crossover-dropin`. The findings behind them, including why `kernelbase.dll` and an extra library search path in `ntdll.so` are needed, are in [`docs/NOTEBOOK.md`](https://github.com/gilnoh/wine-workbench-macos/blob/master/docs/NOTEBOOK.md). The files are LGPL-2.1-or-later, like Wine; `NOTICE.txt` in the download lists the exact sources.

Problems and questions: https://github.com/gilnoh/wine-workbench-macos/issues
