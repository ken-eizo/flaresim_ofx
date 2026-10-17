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
| `tests/*` | New | Standalone core, catalog, ABI, and synthetic OFX host render tests |

No `Nuke NDK`, `AE SDK`, Qt, proprietary Resolve APIs, or a specific host binary is linked by the OFX target.

## Coordinate and full-frame processing

The optical algorithm is full-frame / non-local. The initial OFX adapter declares **no tiled input support** and requests a full input image. The render action writes only its actual `renderWindow`, respecting the separate source/output bounds and image row bytes. Host-adjusted render scale is expected to be reflected in input image bounds; verify project-window and pixel-aspect edge cases before production release.

The renderer is deterministic for fixed frame, parameters, lens and source image. The host owns frame scheduling and caching.

## CPU, GPU and precision

CPU is mandatory baseline; plugin does not claim or require any OFX GPU extension. CUDA/Metal ray tracing is invoked in private backend contexts on standard OFX CPU images and copied back to OFX host memory. See [GPU](GPU.md). Future optional accelerated paths must provide a fallback when suites/extensions or GPU devices are absent. GPU buffers/streams from After Effects are NOT directly portable to OFX.

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
| Natron | Not tested | Not tested | Windows CUDA engine tested outside host, Metal unverified |
| DaVinci Resolve / Fusion | Not tested | Not tested | Windows CUDA engine tested outside host, Metal unverified |
| Nuke OpenFX host | Not tested | Not tested | Windows CUDA engine tested outside host, Metal unverified |
| Other OFX hosts | Not tested | Not tested | Windows CUDA engine tested outside host, Metal unverified |

Avoid stating universal compatibility before these checks. OFX is a common API specification; hosts vary in which contexts, image layouts, image depths, overlays, and optional suites they implement.

## Future development priorities

1. Real-host validation and OFX contract fixes.
2. Complete Nuke optical + output parity tests and all-pair controls.
3. Direct OFX host-managed GPU buffers/streams where supported; current private CUDA/Metal implementations always retain CPU fallback.
4. Native standard OFX overlay for manual light position, when supported.
5. macOS and Linux builds, CI, versioned releases, installation guides.

## OFX interaction and initial defaults

- Bundled lens selection: curated choice menu; all 1,370 Nuke .lens files are also accessible with a standard OFX file-path browser.
- Custom file path overrides a curated preset; missing lens errors no longer replace the input by black in Composite mode.
- Nuke highlight detection is the initial mode. Optional manual source is in OFX Extensions.
- Light Position is a normalized, animatable XY parameter. In overlay-capable hosts, the OpenFX OverlayInteractV1 uses a 1px white crosshair with a 3px black outline for visibility, drawn in canonical RoD coordinates and draggable with the mouse.
- Hosts without overlay support continue to use the numeric controls.
- The synthetic OFX host tests check file selection descriptors, overlay event lifecycle, source coordinates, and nonzero flare output on a black plate.
- Actual DaVinci Resolve overlay and file-picker behavior must be confirmed by viewing the host UI, not assumed from host capability negotiation.

## Nuke reference parity

Parameter groups follow original Nuke order; OFX Extensions is last. Restored original Ray Grid=64, Ghost Blur=0.003, manual FOV V=24, auto FOV V, sensor/focal controls, and Detect Highlights default. The optional Mask clip is applied via block-average alpha after source capping, matching Nuke.

Unfinished compatibility items: full dynamic 500-checkbox ghost-pair UI, arbitrary named Nuke image channels, Nuke-vs-OFX reference-frame parity and real-host integration tests.
