# GPU backends and portability

## Implemented

- **Windows CUDA:** Nuke reference methods use an isolated original ghost_cuda.cu compiled as FlareSimNukeReference.dll. Golden Angle and explicitly Fast GPU mode retain the optional AE optimized CUDA renderer.
- **macOS Apple Metal:** native ray tracing and ghost blur from the AE Metal implementation. Uses an owned MTLDevice / MTLCommandQueue, private scratch Metal buffers, a CPU-readable output Metal buffer, a bundled `FlareLensMetal.metallib`, and synchronous readback to OFX CPU image memory. **Source integration is implemented but native macOS compilation and hardware testing are not yet verified.**
- **Other systems or hosts:** CPU ray tracing fallback. A missing GPU/driver/kernel/unsupported lens configuration never substitutes a different lens.

## Design and ownership

The baseline OFX plugin **does not advertise CUDA or Metal GPU memory through OFX 1.5 properties**. The OpenFX host always supplies ordinary CPU-addressable images, including integer/float variants. The plugin chooses its private GPU for **optical ghost work only**, transferring back to host memory for compositing. This is not zero-copy. Direct host-managed GPU streams and Metal buffers require careful host-side capability negotiation and non-blocking render semantics; those are reserved for a separate implementation, not misrepresented as complete.

The default Windows CUDA optical output uses the original Nuke GPU implementation, not the AE kernel. Source extraction, haze and FFT diffraction still run on CPU. Direct host-managed OpenFX CUDA/Metal GPU buffers are not required.

Optical Renderer:
- Auto (Nuke fidelity): Windows uses original Nuke CUDA when available, then CPU. Other platforms use CPU for original sampling modes.
- CPU (Reference): Nuke-derived CPU renderer.
- GPU (Fast): Original CUDA if available, otherwise optional optimized CUDA/Metal; CPU fallback on failure.
- Golden Angle (Vogel): optional AE sampling, with accelerated GPU when supported.

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
- A fixture with the original Nuke CUDA kernel measured 0.000353% OFX reference error, 0.0043% CPU error and 3.93% optional AE GPU difference (relative L1). These are not universal image-quality guarantees.
- The reference CPU path and GPU paths share the same library data but are not bit-identical.
- macOS universal native build and host-specific testing are still outstanding if GitHub Actions runners are unavailable.
