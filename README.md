# FlareSim OFX

**Portable OpenFX image-effect port of [FlareSim for Nuke](https://github.com/LocalStarlight/flaresim_nuke).**  
Original physical lens prescriptions, ghost ray tracing, source thresholding, and Fraunhofer starburst take precedence over host-specific effects.

This is experimental source code. Windows CUDA builds and directly compares the original Nuke GPU renderer on RTX 2070 SUPER. Previous GitHub Releases do not include these latest source changes. macOS Metal has not been natively verified.

## Architecture

- **Nuke is the reference** for optics, lens data, ghost reflection, source detection, haze, and starburst.
- **FlareSimAE is a secondary source** for independent CPU rendering, performance improvements, pupil sampling, and thread-safe caches.
- **OpenFX is the delivery format**, not a DaVinci Resolve-specific integration. It uses only standard OFX Image Effect API 1, with no Nuke NDK or Adobe SDK dependency.
- Nuke-fidelity Auto: original Nuke CUDA code in a separate Windows companion DLL, or the closer Nuke-derived CPU path. Golden Angle and explicit Fast GPU mode use AE-derived CUDA/Metal. CPU fallback remains available.

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

**Lens File:** The first Nuke control. The original default is EMPTY: no lens is silently substituted. All 1,370 prescriptions are bundled, and a separate OFX lens preset can be chosen explicitly.

**Light Sources:** Detect Highlights (Nuke) is the initial mode. Optional Manual Position and overlay are in OFX Extensions.

**Controls:** Nuke ordering and defaults are kept: Ray Grid 64, Ghost Blur 0.003, Highlight Detection, auto FOV V. Pupil Jitter adds Golden Angle (Vogel) as the fourth option after Off, Stratified, Halton.

**Highlight extraction:** Threshold, Source Cap, Downsample, Max Sources, Cluster Radius. The image is assumed to be **scene-linear**; threshold is not applied to display-referred sRGB.

**Output Mode:** Composite / Flare Only / Ghost Only / Haze Only / Starburst Only / Sources Only. Portable RGBA output replaces Nuke's arbitrary named-channel groups (which the baseline OFX API does not guarantee across hosts).

**Preview Mode:** Overrides grid, downsample, spectral samples, and max sources. Like the Nuke preview, it compensates source downsample energy approximately; capped sources may still differ.

## Remaining Nuke-specific gaps

Generic OFX outputs do not guarantee Nuke's arbitrary named channels (flare.rgb, source.rgb, haze.rgb and starburst.rgb), so portable Output Mode is used. Nuke's complete individual 500-pair toggles are not yet implemented; the OFX single-pair selection is only a partial substitute. An optional second Mask clip now gates source detection like the Nuke original. Pixel-level reference parity and cross-host testing remain pending.

## Status and caveats

- Windows x64: CUDA 13.0, RTX 2070 SUPER. All six CTest cases pass: original Nuke CUDA pixel parity, original Nuke blur exact parity, core, all 1,370 lens files, OFX host and C ABI. Include FlareSimNukeReference.dll next to FlareSim.ofx.
- macOS: Metal shader generation and native Objective-C++ adapter implemented; **build and GPU test still unverified**. Linux remains CPU-only.
- Hosts: DaVinci Resolve, Fusion, Natron, Nuke, and others **must be verified individually**. An OFX API plugin is not guaranteed to work in every host/version.
- GPU: CUDA/Metal only accelerate Ghost ray tracing and its blur currently. Automatic source detection, Haze and Starburst still run on CPU. GPU requires host-to-device staging / readback, not a zero-copy OFX host integration.
- Fidelity: original Nuke GPU vs OFX upstream bridge 0.000353% relative L1 pixel error; original vs CPU 0.0043%; original vs optional AE optimized GPU 3.93%, on a single Nikkor fixture. Full Nuke host output parity is not yet certified.
- Portable OFX 1.x lacks a universal, feature-equivalent mapping for Nuke custom image channels and per-pair checkboxes. Those remain documented follow-up items.
- Output images support RGBA / RGB with 8-, 16-bit integer and 32-bit floating-point data; full-frame image input is required for optical evaluation, and render window writes respect OFX image bounds.
- No verified production host UI/overlay. An optional overlay must use standard OFX capability checks.

See [Nuke parity test report (Japanese)](docs/NUKE_PARITY_JA.md), [architecture](docs/ARCHITECTURE.md) and [GPU implementation](docs/GPU.md).

## Attribution and licensing

Original [FlareSim for Nuke](https://github.com/LocalStarlight/flaresim_nuke) by Steve Watts Kennedy; underlying optical renderer [blackhole-rt](https://github.com/space55/blackhole-rt) by Eamonn Nugent. After Effects adaptation and rendering improvements from [FlareSimAE](https://github.com/ken-eizo/flaresim_ae). License: MIT. OpenFX headers are BSD-3-Clause licensed by the OpenFX contributors (their license is provided under `vendor/openfx/LICENSE.md`). See [LICENSE](LICENSE) and [NOTICE.md](NOTICE.md).

**Catalog note:** All 1,370 canonical Nuke prescriptions are present; 2 upstream files contain only a single optical surface and cannot form a two-bounce ghost. They are preserved unchanged and must not silently fall back to a different lens.
