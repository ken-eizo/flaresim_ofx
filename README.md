# FlareSim OFX

A portable [OpenFX](https://openeffects.org/) port of [FlareSim for Nuke](https://github.com/LocalStarlight/flaresim_nuke). FlareSim simulates physically based lens flares by ray tracing real optical prescriptions instead of using painted elements or lookup textures.

**Version:** 1.0.0. **Windows:** x64 (CUDA / CPU fallback). **macOS:** Apple Metal build workflow provided; binary and runtime verification pending.

## Credits

The core optical work originated with **Eamonn Nugent** ([blackhole-rt](https://github.com/space55/blackhole-rt)) and **Steve Watts Kennedy** ([FlareSim for Nuke](https://github.com/LocalStarlight/flaresim_nuke)). Selected rendering improvements originate from [FlareSimAE](https://github.com/ken-eizo/flaresim_ae). This is an unofficial OpenFX adaptation.

Original Nuke tutorial: https://youtu.be/yEsBOQNG16Y

## How it works

FlareSim detects bright input pixels as light sources. Rays pass through the selected lens surfaces, and two-surface reflection pairs generate optical ghosts at the camera sensor. The original optical model reproduces chromatic aberration, Fresnel weighting and aperture-dependent shapes. Optional haze and diffraction starburst are also available.

Windows includes the original Nuke CUDA reference renderer, alongside an independently implemented CPU fallback and optional extended GPU sampling.

## Installation

Get the package from the [Releases page](https://github.com/ken-eizo/flaresim_ofx/releases).

**Windows (x64):** Extract the entire `FlareSim.ofx.bundle` folder into:

`C:\Program Files\Common Files\OFX\Plugins\`

Restart the OFX host. The DLL, lens prescriptions and third-party notices must remain inside the bundle.

**macOS:** Once its GitHub Actions build has passed, install the entire bundle under `/Library/OFX/Plugins/` and restart the host. A macOS release file is not provided until that build succeeds.

OpenFX compatibility is host dependent. Houdini 21 Copernicus successfully rendered a diagnostic six-choice build, but this does not prove compatibility with every OFX host, GPU or image format.

## Quick start

1. Connect an input image to **Source**.
2. Select a prescription from **Lens File**. The stable version lists six curated choices to avoid crashing hosts that cannot construct a dropdown of 1,370 entries. All 1,370 original prescriptions remain bundled.
3. Set **FOV H (deg)** to match the camera's field of view.
4. Adjust **Threshold**. The original Nuke default is 2 for scene-linear HDR; for images whose pixel values range from 0 to 1, try 0.1, or choose **Manual Position**.
5. Adjust **Flare Gain** to taste.
6. Use **Output Mode → Sources Only** to inspect source detection without the expensive ghost render.

## Output channels

Nuke uses separate `flare.rgb`, `source.rgb`, `haze.rgb` and `starburst.rgb` image planes. Named planes cannot be guaranteed by the baseline OpenFX API in every host, so the portable port offers:

| Output Mode | Result |
|---|---|
| **Flare as RGB** | Flare only, written to RGB |
| **Sources Only** | Detected source map, without optical ray tracing |
| **Composite** | Flare combined with the input RGB (OFX addition) |

Nuke's **Separate channels** output and original default are not yet reproduced, and are intentionally not mislabeled as supported.

## Parameters

The Nuke control names and order are retained where functions match.

### Ghost

**Flare Gain** scales intensity. **Ray Grid (NxN)** determines pupil samples. **Pupil Jitter** supports the original Off, Stratified and Halton options plus the optional Golden Angle (Vogel) extension. **Jitter Seed** and **Auto Seed** control stratified patterns. **Threshold**, **Source Cap**, **Max Sources**, **Downsample**, and **Cluster Radius** control source detection and density.

### Preview Mode

**Enable Preview Mode** switches to its own **Ray Grid**, **Max Sources**, **Downsample** and **Spectral Samples** values to make look development faster. Downsample brightness compensation follows the Nuke design.

### Camera

**Use Sensor Size**, **Sensor Preset**, **FOV H (deg)**, **Auto FOV V**, **FOV V (deg)**, **Sensor Width (mm)**, **Sensor Height (mm)** and **Focal Length (mm)** specify field of view.

### Aperture

**Aperture Blades** and **Aperture Rotation** describe the entrance pupil.

### Spectral

**Spectral Samples** controls the number of wavelength samples (3, 5, 7, 9 or 11).

### Post-process

**Ghost Blur**, **Ghost Blur Passes**, **Haze Gain**, **Haze Radius** and **Haze Blur Passes** produce blur and veiling glare.

### Starburst

**Starburst Gain** and **Starburst Scale** control the diffraction pattern.

### Output

**Output Mode** controls the host-compatible RGB result, including **Sources Only** diagnostics. The redundant Show Sources checkbox was removed.

### Additional Controls

OFX-only additions are kept separate: **Light Sources**, **Light Position**, **Manual Radiance**, **Optical Renderer** and **Max Ghost Pairs**. **Pair Selection** and **Pair Surface A/B** are a basic pair filter, not the original Nuke Pairs UI. A standard OFX overlay is used where the host supports it.

## Pairs tab

The original Nuke plugin dynamically exposes individual enable/disable controls for reflection pairs. Full per-pair toggle parity has not yet been achieved in this OFX port. The simplified pair controls are under **Additional Controls** instead of pretending to reproduce the Nuke interface.

## Lens files

All **1,370 original `.lens` files** are bundled under `FlareSim.ofx.bundle/Contents/Resources/lenses`. Each describes optical surface geometry, materials and coatings.

For maximum current host compatibility, the v1.0.0 **Lens File** menu exposes only six built-in selections; adding a file to the folder does **not** make it appear in the stable six-choice dropdown. Developers can opt into the full menu with `-DFLARESIM_EXPERIMENTAL_FULL_LENS_CHOICES=ON`, but some hosts may crash while displaying the resulting 1,370-choice UI.

## Render behaviour and performance tips

The original Nuke default has no lens selected, allowing startup to skip expensive optical work. **Sources Only** bypasses ray tracing. Use **Preview Mode**, fewer **Max Sources**, lower **Ray Grid** and a higher **Threshold** to reduce render times.

Numeric parameter legal bounds are separate from their narrower interactive slider display bounds, so large values remain available through typing without making small drags jump by enormous amounts. Individual OFX hosts may interpret the display bounds differently.

## Building from source

Requires CMake 3.24+, C++20, the included OpenFX headers and bundled original Nuke sources.

Windows / Visual Studio 2022:

~~~powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON
cmake --build build --config Release --parallel 6
ctest --test-dir build -C Release --output-on-failure
~~~

macOS / Apple Metal: see `.github/workflows/release-macos.yml` and `.github/workflows/build.yml`. The pipeline must complete successfully before a macOS ZIP is advertised.

The final bundle is under `build/FlareSim.ofx.bundle`.

## License and bug reports

MIT license; see [LICENSE](LICENSE) and [NOTICE.md](NOTICE.md) for upstream attribution and third-party notices.

For bugs, contact [@keneizo](https://x.com/keneizo) or [keneizo1013@gmail.com](mailto:keneizo1013@gmail.com).
