# Third-party notices — FlareSim OFX

## Original FlareSim for Nuke

This project ports source code and lens prescriptions from [LocalStarlight/flaresim_nuke](https://github.com/LocalStarlight/flaresim_nuke) by Steve Watts Kennedy. The physical ray tracing work is based on [space55/blackhole-rt](https://github.com/space55/blackhole-rt) by Eamonn Nugent. Original authorship, copyright, attribution and licenses remain acknowledged. The OpenFX adaptation is unofficial.

The 1,370 original lens prescription files are retained inside the OFX bundle. Lens brands and names identify optical prescriptions; they do not imply manufacturer endorsement.

## FlareSimAE contributions

Selected CPU rendering, performance and GPU sampling adaptations originate from [ken-eizo/flaresim_ae](https://github.com/ken-eizo/flaresim_ae). These modifications are not claimed to be unchanged Nuke source.

## OpenFX headers

OpenFX C API headers were contributed by the OpenFX project and are distributed under their own BSD-3-Clause licensing terms, preserved in vendor/openfx.

## CUDA and Apple Metal

The NVIDIA CUDA Toolkit, NVIDIA GPU driver, Apple frameworks and proprietary compiler tooling are not bundled as source dependencies; respective licenses and SDK terms apply. The Windows package includes binaries compiled using CUDA but not the CUDA Toolkit itself.

This repository is neither officially affiliated with Nuke/Foundry, Blackmagic Design, SideFX, Adobe, Apple, NVIDIA, nor the original FlareSim authors.

See LICENSE and individual vendor directories for complete license text.
