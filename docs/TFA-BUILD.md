# Rebuild and package the TFA preview

The release assets include exact workbench and mesa-wine-gl Git bundles and upstream source archives. `release-manifest.json` records their commit IDs, upstream revisions, and file hashes. For the public source workflow, start with [BUILD-GL-WINE.md](BUILD-GL-WINE.md). Source snapshots allow inspection without relying on mutable branch names.

## Clean source build

On an Apple Silicon build host, place clean clones side by side:

```sh
git clone https://github.com/gilnoh/wine-workbench-macos.git
git clone https://github.com/gilnoh/mesa-wine-gl.git
# Check out the exact commits from release-manifest.json in each clone.
cd wine-workbench-macos
bin/provision-host --gl
bin/fetch-tfa-sources
bin/rebuild-tfa-local
```

For the supplied source bundles, use `git clone wine-workbench-macos.bundle wine-workbench-macos` and `git clone mesa-wine-gl.bundle mesa-wine-gl`, then check out the manifest commits. This also works when the workbench release commit has not been pushed to the public remote. The two directories must be named `wine-workbench-macos` and `mesa-wine-gl` and share a parent. The build driver expects a fresh workspace and logs each step under `../logs/`. It also runs a visible graphics probe at the end. Do not run it while someone expects the machine to remain idle. It uses six build jobs.

The recorded build uses Xcode 26.6, its macOS26.5 SDK, x86 Homebrew LLVM21.1.8, MinGW, pinned Python requirements, WineCX26.3.0, Mesa27.0.0-devel at the locked commit, and GStreamer1.28.7. See host-toolchain.json for the actual inventory. Host provisioning is not a hermetic compiler/dependency lock. A successful rebuild is a functional reproduction, not a promise of byte-identical rebuilds across host/toolchain versions. The binary archive contains the tested build's bytes, rather than a subsequent independently compiled copy.

Build the portable variant after the clean build:

```sh
bin/build-runtime --winecx 26.3.0 --pe-gcc --portable \
  --name winecx-26.3.0-tfa-portable-x86_64 \
  --src ../mesa-wine-gl/src/winecx-bridge/sources/wine \
  --extra-series ../mesa-wine-gl/patches/wine/series --jobs 6
```

`--portable` seeds module-relative dlopen names, rewrites media-library links, removes the build-time GStreamer rpath and re-signs changed binaries. All vendor changes must remain in the declared patch series. Never repair deployment by adding DYLD_FALLBACK_LIBRARY_PATH.

## Package and verify

The current packager is a release-maintainer tool tied to the recorded host inventory, not a generic fresh-host installer. It expects `../host-toolchain.json`, `../build-steps.json` and `../checkpoints.json`, plus the exact dependency kegs named in `tools/release/collect-sources.py`. Capture the new build's actual inventory/checkpoints; never copy historical provenance to claim a different build. Adapting and verifying these inputs is required on another host. Local gameplay through the guide above does not require packaging.

Run `bin/package-tfa-release OUTPUT_DIRECTORY`. It copies only the named runtime, graphics bundle, launcher, setup tools, documentation and provenance. It must never copy prefixes, Steam caches, credentials, games or downloaded proprietary installers. Output is a candidate until tested after extraction at another path.

Audit Mach-O dependencies, rpaths, symlinks, dynamic dlopen names and launcher paths. Run with Homebrew and original build trees denied; prove the denial with a failing read control. Validate fresh wineboot, swap/LUT positive and negative controls, Media Foundation decoding, HTTPS, then Steam and TFA music/museum/mission. Library-load success alone is not playback or TLS success. Save results and limitations in docs/NOTEBOOK.md and retain the exact tested artifact hashes.

Review bundled license texts and dependency source records before wider release. Include corresponding upstream sources/patches and dependency build recipes as required by their licenses. Re-sign before hashing, never after. Archive runtime and source assets, publish their SHA256 checksums, and test a fresh extraction. A second Mac and a cold first-run setup are distinct checks from relocation on the build host; report their status separately.

## Recreate the release archives

The package command also runs `tools/release/collect-sources.py` and `tools/release/inventory.py`. It collects checksum-verified recipe sources/patches and pinned Git-resource archives into `OUTPUT_DIRECTORY/../supplemental-sources`, retains upstream notices, and rejects unmapped payload libraries. The component inventory distinguishes shipped payload from compiler/build inputs. Existing downloaded sources are reused only after checksum checks where an expected digest is available. Git-resource archives retain their exact revision and downloaded SHA-256.

```sh
mkdir -p ../artifacts
bin/package-tfa-release ../artifacts/TFA-Playable-Preview-1
COPYFILE_DISABLE=1 tar -czf ../artifacts/TFA-Playable-Preview-1-rc2-macos-apple-silicon.tar.gz -C ../artifacts TFA-Playable-Preview-1
COPYFILE_DISABLE=1 tar -czf ../artifacts/TFA-Preview-1-supplemental-sources.tar.gz -C ../artifacts supplemental-sources
git bundle create ../artifacts/wine-workbench-macos.bundle HEAD
git -C ../mesa-wine-gl bundle create ../artifacts/mesa-wine-gl.bundle HEAD
```

Retain the clean-build Wine/Mesa/GStreamer source archive asset as well as these supplemental sources. Generate `SHA256SUMS` over the final archives, bundles and standalone guides. Verify all manifest entries after extraction and compare the runtime/graphics hashes against the gameplay-tested copy. For a new build, repeat the functional checks; matching a previous guide is not evidence that a new binary passed.
