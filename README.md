# FlareSim OFX

**Portable OpenFX image-effect port of [FlareSim for Nuke](https://github.com/LocalStarlight/flaresim_nuke).**  
Original physical lens prescriptions, ghost ray tracing, source thresholding, and Fraunhofer starburst take precedence over host-specific effects.

This is **an early CPU implementation (v0.1.0)**, not a verified cross-host production release. The Windows x64 OFX binary builds and passes the initial standalone tests; no real OFX host has been tested yet.

## Architecture

- **Nuke is the reference** for optics, lens data, ghost reflection, source detection, haze, and starburst.
- **FlareSimAE is a secondary source** for independent CPU rendering, performance improvements, pupil sampling, and thread-safe caches.
- **OpenFX is the delivery format**, not a DaVinci Resolve-specific integration. It uses only standard OFX Image Effect API 1, with no Nuke NDK or Adobe SDK dependency.
- A pure CPU render path is always available. **CUDA and Metal are not yet implemented in this OFX port.** Cross-host GPU support will be optional, host-capability based.

## Build

Requirements: CMake 3.24+, C++20 compiler. The necessary OpenFX C API headers and original Nuke lens files are included.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The bundle is generated at:

`build/FlareSim.ofx.bundle`

On Windows, copy **the entire** `FlareSim.ofx.bundle` folder (not only the `.ofx` binary) to `C:\Program Files\Common Files\OFX\Plugins\`. Restart the OFX host. This is the standard system-wide folder; hosts can have additional search paths.

For macOS / Linux use a suitable generator and copy the entire built bundle to a supported OFX plugin directory (usually `/Library/OFX/Plugins` on macOS or `/usr/OFX/Plugins` on Linux).

## Controls

**Lens Prescription:** Exact name of an upstream Nuke `.lens` file, such as `AI_Nikkor_50mm_F1.2S.lens`. The `Resources/lenses` folder contains the original prescriptions and `Resources/LensCatalog.txt` lists the 1,370 canonical names. **Custom Lens File** can override the preset with an absolute path.

**Light Sources:** Detect Highlights (upstream default concept) or Manual Position in normalised 0..1 coordinates.

**Optical:** FOV, Flare Gain, Ray Grid, Max Ghost Pairs, Aperture, Pupil Jitter, Spectral Samples, selected reflection pair.

**Highlight extraction:** Threshold, Source Cap, Downsample, Max Sources, Cluster Radius. The image is assumed to be **scene-linear**; threshold is not applied to display-referred sRGB.

**Output Mode:** Composite / Flare Only / Ghost Only / Haze Only / Starburst Only / Sources Only. Portable RGBA output replaces Nuke's arbitrary named-channel groups (which the baseline OFX API does not guarantee across hosts).

**Preview Mode:** Overrides grid, downsample, spectral samples, and max sources. Like the Nuke preview, it compensates source downsample energy approximately; capped sources may still differ.

## Status and caveats

- Windows x64: CMake Release build and basic C ABI + CPU optical tests pass.
- macOS/Linux: source/CMake portability targeted, not yet built or tested.
- Hosts: DaVinci Resolve, Fusion, Natron, Nuke, and others **must be verified individually**. An OFX API plugin is not guaranteed to work in every host/version.
- GPU: CPU-only. This can be very slow with many light sources, reflection pairs, or high Ray Grid. Set Preview Mode and limit sources for now.
- Fidelity: the ghost core uses source-derived CPU code; a complete pixel-level parity study against the original Nuke CUDA kernel is still outstanding.
- Portable OFX 1.x lacks a universal, feature-equivalent mapping for Nuke custom image channels and per-pair checkboxes. Those remain documented follow-up items.
- Output images support RGBA / RGB with 8-, 16-bit integer and 32-bit floating-point data; full-frame image input is required for optical evaluation, and render window writes respect OFX image bounds.
- No verified production host UI/overlay. An optional overlay must use standard OFX capability checks.

See [Architecture and compatibility](docs/ARCHITECTURE.md).

## Attribution and licensing

Original [FlareSim for Nuke](https://github.com/LocalStarlight/flaresim_nuke) by Steve Watts Kennedy; underlying optical renderer [blackhole-rt](https://github.com/space55/blackhole-rt) by Eamonn Nugent. After Effects adaptation and rendering improvements from [FlareSimAE](https://github.com/ken-eizo/flaresim_ae). License: MIT. OpenFX headers are BSD-3-Clause licensed by the OpenFX contributors (their license is provided under `vendor/openfx/LICENSE.md`). See [LICENSE](LICENSE) and [NOTICE.md](NOTICE.md).

**Catalog note:** All 1,370 canonical Nuke prescriptions are present; 2 upstream files contain only a single optical surface and cannot form a two-bounce ghost. They are preserved unchanged and must not silently fall back to a different lens.
