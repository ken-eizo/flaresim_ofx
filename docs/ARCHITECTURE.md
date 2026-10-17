# Architecture and cross-host contract

## Reference priorities

1. **Nuke original**: ray tracing, optical prescriptions, Fresnel physics, ghost pairs, chromatic response, aperture, Fraunhofer starburst, scene-linear source sampling, haze, and output semantics.
2. **After Effects adaptation**: host-independent CPU tracing, deterministic accumulations, optical/pupil caches, sampling and multithread improvements. Improvements must not silently change reference optics.
3. **OpenFX contract**: canonical/pixel coordinates, positive or negative row strides, depth conversions, frame-time values, render windows, image lifetime, cancellation, property descriptors. No hardcoded host names.

The same physical lens file is always selected explicitly. A bad file MUST fail rather than silently defaulting to an unrelated preset.

## Components

| Component | Source | Notes |
|---|---|---|
| `vendor/nuke/src/{lens,trace,ghost,starburst}.*` | Original Nuke | Preserved ray optics, FFT PSF and data types |
| `resources/lenses/*.lens` | Original Nuke | Source-of-truth prescriptions |
| `engine/FlareLensCore.*` | AE | CPU optical renderer with Nuke-oriented splat |
| `engine/FlareLensOptics.*` | AE | Shared sampling and post-process utilities |
| `engine/FlareLensCpuParallel.*` | AE | Bounded worker count and cancellation |
| `src/SourceDetection.*` | New | Host-neutral, Nuke-style block luminance detection |
| `src/FlareSimOFX.cpp` | New | Standard OFX C ABI, host mapping and pipeline |
| `vendor/openfx/include` | AcademySoftwareFoundation | Official OpenFX C API |
| `tests/*` | New | Standalone core and ABI smoke tests |

No `Nuke NDK`, `AE SDK`, Qt, proprietary Resolve APIs, or a specific host binary is linked by the OFX target.

## Coordinate and full-frame processing

The optical algorithm is full-frame / non-local. The initial OFX adapter declares **no tiled input support** and requests a full input image. The render action writes only its actual `renderWindow`, respecting the separate source/output bounds and image row bytes. Host-adjusted render scale is expected to be reflected in input image bounds; verify project-window and pixel-aspect edge cases before production release.

The renderer is deterministic for fixed frame, parameters, lens and source image. The host owns frame scheduling and caching.

## CPU, GPU and precision

CPU is mandatory baseline; plugin does not claim or require any OFX GPU extension. Future optional accelerated paths must provide a fallback when suites/extensions or GPU devices are absent. GPU buffers/streams from After Effects are NOT directly portable to OFX.

Image inputs are read in the host-provided pixel depth; float paths preserve HDR values and negative RGB (before highlight threshold), while integer outputs are clamped as required. Threshold, flare gain and source colour use **scene-linear radiometric RGB**, not automatic host-specific LUTs.

## Baseline parity checklist

- Validate all 1,370 canonical prescriptions load and retain checksums.
- Compare lens geometry, sample positions, pair selection and per-wavelength throughput to original Nuke.
- Compare source extraction, capped energy, clustering, downsample and preview compensation.
- Compare Ghost Only / Haze Only / Starburst Only on known HDR fixtures.
- Compare varying resolution, pixel aspect, render scale, non-origin bounds, negative row stride and partial render windows.
- Check UTF-8 and Unicode .lens filenames across OSes.
- Confirm host capability negotiation, multiple instances, cancellation, and long running renders.

## Host validation matrix

| Host | Load/render smoke test | CPU parity | GPU |
|---|---|---|---|
| Natron | Not tested | Not tested | Not implemented |
| DaVinci Resolve / Fusion | Not tested | Not tested | Not implemented |
| Nuke OpenFX host | Not tested | Not tested | Not implemented |
| Other OFX hosts | Not tested | Not tested | Not implemented |

Avoid stating universal compatibility before these checks. OFX is a common API specification; hosts vary in which contexts, image layouts, image depths, overlays, and optional suites they implement.

## Future development priorities

1. Real-host validation and OFX contract fixes.
2. Complete Nuke optical + output parity tests and all-pair controls.
3. Optional GPU acceleration with strict host feature detection and CPU fallback.
4. Native standard OFX overlay for manual light position, when supported.
5. macOS and Linux builds, CI, versioned releases, installation guides.
