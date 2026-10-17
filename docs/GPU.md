# GPU backends and portability

## Implemented

- **Windows NVIDIA CUDA:** native ray tracing from FlareSimAE, including GPU ghost splatting and optional ghost blur. Compiles with NVIDIA CUDA 13.0 (validated on an RTX 2070 SUPER). Uses a host-independent, private CUDA allocation plus synchronous readback of float4 BGRA output to the OFX CPU buffer.
- **macOS Apple Metal:** native ray tracing and ghost blur from the AE Metal implementation. Uses an owned MTLDevice / MTLCommandQueue, private scratch Metal buffers, a CPU-readable output Metal buffer, a bundled `FlareLensMetal.metallib`, and synchronous readback to OFX CPU image memory. **Source integration is implemented but native macOS compilation and hardware testing are not yet verified.**
- **Other systems or hosts:** CPU ray tracing fallback. A missing GPU/driver/kernel/unsupported lens configuration never substitutes a different lens.

## Design and ownership

The baseline OFX plugin **does not advertise CUDA or Metal GPU memory through OFX 1.5 properties**. The OpenFX host always supplies ordinary CPU-addressable images, including integer/float variants. The plugin chooses its private GPU for **optical ghost work only**, transferring back to host memory for compositing. This is not zero-copy. Direct host-managed GPU streams and Metal buffers require careful host-side capability negotiation and non-blocking render semantics; those are reserved for a separate implementation, not misrepresented as complete.

The Nuke optical model, lens prescriptions, pair selection and spectral sampling are unchanged by the accelerated adapters. Ghost ray splatting is re-used from AE, which itself is derived from the original Nuke algorithms. Automatic source detection, Haze and FFT diffraction remain CPU-based at this stage.

The `Optical Renderer` control offers:
- `Auto (GPU preferred)`: use available CUDA/Metal, otherwise CPU.
- `CPU`: reproducible CPU baseline.
- `GPU (fallback CPU)`: try GPU first; guaranteed safe CPU fallback if unavailable.

## Building

Windows (CUDA 13.0 or compatible Toolkit installed):
```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DFLARESIM_GPU=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Windows or Linux CPU:
```bash
cmake -S . -B build-cpu -DFLARESIM_GPU=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build-cpu --config Release
ctest --test-dir build-cpu -C Release --output-on-failure
```

macOS with Metal SDK (requires macOS Xcode, `xcrun metal` and `xcrun metallib`):
```bash
cmake -S . -B build-metal -DFLARESIM_GPU=ON -DCMAKE_BUILD_TYPE=Release \
  '-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64'
cmake --build build-metal --config Release --parallel
ctest --test-dir build-metal -C Release --output-on-failure
```

A Metal physics header is generated from the shared CUDA device equations; `FlareLensMetalKernels.metal` is then compiled to AIR and the metallib placed inside the OFX bundle. The GPU shader will not be found if the whole bundle is not installed.

## Known limitations

- Full-frame image transfer for each OFX render, especially at high resolutions. This can offset GPU speedups for small ray grids.
- No direct GPU OFX 1.5 render contract yet; intentionally does not advertise unsupported CUDA/Metal pointer semantics. This is a portability choice.
- Optical starburst and haze and source detection still use CPU.
- CPU/GPU floating-point differences must be measured against the original Nuke renderer and documented. A simulated OFX host with RTX 2070 SUPER measured 1446.75 vs 1432.88 for one 64×32/64-grid ghost fixture (RGB L1 values), a difference in total energy of about 0.96%, and spatial L1 difference 46.55. This is **not** a general image-quality guarantee.
- The reference CPU path and GPU paths share the same library data but are not bit-identical.
- macOS universal native build and host-specific testing are still outstanding if GitHub Actions runners are unavailable.
