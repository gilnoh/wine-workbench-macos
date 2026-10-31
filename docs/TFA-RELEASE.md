# Task Force Admiral playable preview: using the binary

This is an experimental Wine + Mesa/Zink + KosmicKrisp runtime for Apple Silicon.
It reports OpenGL 4.6; this is not a claim of complete GL conformance or support
for every OpenGL application. It is independent of an installed CrossOver.

## Requirements and tested scope

Apple Silicon, Rosetta 2, Metal-capable macOS, internet access, and a Steam account
owning Task Force Admiral. The actual validation host was an M1 Max on macOS
27.0.1 (26A434). Other Macs/macOS versions are not yet qualified. No Homebrew,
Xcode, compiler or Python is intended to be required to use the binary package.
Build tools are required only for rebuilding it.

The runtime passed relocated/isolation tests, and the user confirmed museum and
mission play with clear audio and normal loading transitions. First entry into
3D can take time. The new first-run setup wrapper is a separate qualification
item; see the release notes for its exact validation status.

## Install and play

1. Download the binary archive and SHA256SUMS from the private release. Verify
   with `shasum -a 256 -c SHA256SUMS` (download all listed assets, or check the
   selected archive's line). Extract the archive to a permanent, writable folder.
2. Install Rosetta if macOS requests it. This preview is ad-hoc signed, not Apple
   notarized. Review the sources/checksums before allowing it in macOS security
   settings. Do not disable Gatekeeper globally.
3. Open Terminal in the extracted folder and run `./tfa setup`. This creates a
   fresh bottle and downloads Microsoft prerequisites, fonts, and Steam. Their
   respective license terms apply. No Steam credentials or game files ship here.
4. Run `./tfa steam`, log in, and install TFA in Steam's default **C:** library.
   Do not run another Windows Steam client at the same time.
5. Leave Steam open. In a second Terminal in the package folder, run `./tfa play`.
   Use windowed mode in the game if fullscreen clips the menu.

The default bottle is `~/Library/Application Support/TFA Wine Preview/prefix`.
It survives replacement of the runtime folder. For an existing compatible test
bottle, set `TFA_PREFIX=/absolute/path` for both commands. Back it up first; do not
point this at an unrelated CrossOver bottle. Custom Steam libraries are supported
by the source workbench launcher; this preview's simple launcher expects C:.

Exit TFA and Steam before replacing or moving runtime files. Keep the previous
runtime to roll back. Do not include your bottle in bug reports: it contains
Steam session state and private data. For diagnostics, capture command output
locally and review it before sharing.

If Steam's installer checksum changes, setup deliberately stops. Obtain an
updated reviewed release; do not remove the verification. Source build and
packaging instructions: [TFA-BUILD.md](TFA-BUILD.md). The package includes the
same guide and source snapshots/provenance alongside its release assets.
