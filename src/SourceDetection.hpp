#pragma once
#include "FlareLensCore.hpp"
#include <vector>
namespace fl::ofx {
struct DetectionOptions {
  float threshold=2.f, source_cap=0.f;
  int downsample=4, max_sources=512, cluster_radius=0;
  float fov_h_deg=40.f,fov_v_deg=24.f;
};
// Input is scene-linear, full-frame RGB (not display-referred).
// Preserves upstream Nuke per-block averaging and luma weighting.
std::vector<fl::Source> detect_sources(const std::vector<float>& rgb,int w,int h,
                                       const DetectionOptions& options,
                                       const std::vector<float>* mask=nullptr);
}
