#pragma once
#include "FlareLensCore.hpp"
#include <vector>
#include <string>

// Platform-specific GPU execution behind a host-neutral contract.
// Source images and result buffers are CPU-owned at the OFX 1.0 boundary;
// only optical ray tracing is dispatched to CUDA/Metal.
namespace fl::ofxgpu {
bool available() noexcept;
bool render_nuke_reference(const std::string& lens_path,
    const std::vector<fl::Source>&,const fl::Settings&,int,int,
    float blur,int blur_passes,std::vector<float>&);
bool render_ghosts(const LensSystem& lens,const std::vector<fl::Source>& sources,
                   const fl::Settings& settings,int w,int h,
                   float ghost_blur,int blur_passes,
                   std::vector<float>& output);
}
