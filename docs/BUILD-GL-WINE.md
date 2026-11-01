# Build an OpenGL Wine gaming environment on Apple Silicon

This guide connects [wine-workbench-macos](https://github.com/gilnoh/wine-workbench-macos) and [mesa-wine-gl](https://github.com/gilnoh/mesa-wine-gl). Wine's macOS OpenGL driver loads our Mesa bridge, which uses Zink → Vulkan → KosmicKrisp → Metal. Wine and the graphics libraries run as x86_64 under Rosetta. CrossOver.app is not required: Wine is built from CodeWeavers' published sources. This is an independent experimental project, with credit to Wine, CodeWeavers, Mesa and their contributors.

The first target is Task Force Admiral, not a claim that every OpenGL game works. Consult [the notebook](NOTEBOOK.md) for measured compatibility and unresolved issues. A reported GL 4.6 version is not conformance certification. An Apple Developer membership is not needed to build locally; signing and notarization are separate distribution work.

## 1. Prepare the build host

Use an Apple Silicon Mac with Rosetta, full Xcode selected by `xcode-select`, its command-line tools and accepted license, Git, and both Homebrew installations: arm64 at `/opt/homebrew`, x86_64 at `/usr/local`. Follow the official [Homebrew installation instructions](https://docs.brew.sh/Installation) and install the Intel instance from a Rosetta shell. Do not substitute ARM libraries into the x86 dependency tree.

The recorded successful build used an M1 Max, macOS 27.0.1, Xcode 26.6/macOS 26.5 SDK and x86 LLVM 21.1.8. The graphics scripts select deployment target 26.0; this does not qualify every macOS version from 26 onward. Host package versions are not fully locked: this is a source-pinned functional rebuild, not a bit-for-bit compiler bootstrap. See the [recorded host inventory](https://github.com/gilnoh/mesa-wine-gl/blob/main/docs/evidence/2026-10-31/clean-rebuild/host-toolchain.json) when investigating toolchain differences.

Use a new workspace, not an existing personal bottle or archived graphics tree. Wine and Mesa builds use six jobs (GStreamer uses the host CPU count) and can keep the machine busy for tens of minutes; allow ample disk space for Wine, LLVM and two Mesa build trees.

```sh
mkdir -p "$HOME/tfa-source-build"
cd "$HOME/tfa-source-build"
git clone https://github.com/gilnoh/wine-workbench-macos.git
git clone https://github.com/gilnoh/mesa-wine-gl.git
cd wine-workbench-macos
bin/provision-host --gl
bin/provision-host --check --gl
```

Provisioning installs missing Homebrew packages and adjusts the x86 MoltenVK ICD manifest for Steam device discovery. It does not install Xcode, Homebrew or Rosetta itself. The sibling names above are required by the build scripts. For an exact project snapshot, check out the same `tfa-source-preview-1` tag in **both** repositories before provisioning. This tag pins our sources; dependency packages may still change. To select it, run `git checkout tfa-source-preview-1` inside each clone.

## 2. Fetch and build

```sh
bin/fetch-tfa-sources
bin/rebuild-tfa-local
```

The fetcher checks the WineCX 26.3.0 tarball SHA-256, GStreamer 1.28.7 revision and the Mesa revision in the sister repository's `sources.lock.json`. The builder creates pristine vendor Git trees, applies only declared patches, builds GStreamer, KosmicKrisp, the Mesa bridge and Wine, bundles graphics dependencies, then runs a visible Windows swap probe with failure controls. No game installation is needed for this step.

Outputs include `runtimes/winecx-26.3.0-mesa-gl-x86_64`, the sister repository's `build/graphics-bundle-swap-fix`, and `../logs/` plus `../build-steps.json`. The clean-build driver deliberately refuses an existing Wine destination. If it fails, read the named log and the notebooks before changing anything; resume the individual recorded command or start a new workspace. Do not delete a working runtime or repeatedly rerun the clean driver over a partial tree.

## 3. Create a bottle and install Steam

Close other Windows Steam clients first. From the workbench directory:

```sh
bin/mkprefix steam-winecx winecx-26.3.0-mesa-gl-x86_64
curl --fail --location --output ../downloads/SteamSetup.exe https://cdn.akamai.steamstatic.com/client/installer/SteamSetup.exe
WINEPREFIX="$PWD/prefixes/steam-winecx" runtimes/winecx-26.3.0-mesa-gl-x86_64/bin/wine ../downloads/SteamSetup.exe
```

`mkprefix` installs fonts, d3dcompiler_47 and native VC++ prerequisites using the pinned Winetricks script. Their licenses apply. Obtain Steam only from Valve; the current installer may differ from the historical release checksum. At the end of the installer, deselect “Run Steam”, or exit its automatically started client normally. Then launch Steam with the workbench environment:

```sh
RUNTIME=winecx-26.3.0-mesa-gl-x86_64 bin/gamerun steam
```

Log in yourself and install TFA into the default **C:** library. The optional S: mount is not required. This source workflow uses the named prefix `steam-winecx`; the packaged launcher uses a different default bottle. Never copy Steam credentials into a repository or start two prefixes' Steam clients together.

## 4. Play and validate

Leave Steam open and run this from a second Terminal in the workbench:

```sh
bin/gamerun tfa
```

The profile selects the Mesa bridge and bundled KosmicKrisp, verifies graphics hashes, finds the game through Steam library manifests, and uses direct OpenAL WASAPI for music. Do not replace game DLLs, disable LUT processing or inject an old project's libraries. Use the game's windowed mode if menus are clipped.

Check loading transitions, music, museum ships/textures and a mission including the 3D view. A working menu alone is insufficient. First entry into 3D may take time; record the delay rather than assuming a hang. The more detailed LUT probe requires your legally installed game assets and the sister project's isolated Python probe environment; see its README. No game assets, Steam account or proprietary CrossOver binaries are supplied here.

## For Codex, Claude Code, OpenCode or another assistant

Give the assistant both sibling directories and this request:

> Read wine-workbench-macos/AGENT_START_HERE.md, both repositories' docs/NOTEBOOK.md, and docs/BUILD-GL-WINE.md. Check the prerequisites and source pins, then build in this fresh workspace. Preserve the declared patch series and keep all downloads, prefixes and build output out of Git. Record exact commits, tool versions and command exit statuses. Prove negative controls can fail. Stop for my Steam login and ask me to judge graphics/audio; do not infer gameplay success from logs. Never run two Windows Steam clients or kill unrelated Wine processes.

Do not treat an AI agent's completion message as validation. Retain logs and ask it to distinguish measured probe results, your gameplay observations and untested assumptions. See [GL-SUPPORT-WINE-BUILD.md](GL-SUPPORT-WINE-BUILD.md) for portable packaging and source/notice requirements. Signing/notarization is not part of this source-build procedure.
