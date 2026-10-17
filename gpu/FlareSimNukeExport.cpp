#include "ghost_cuda.h"
#include "ghost.h"
#include "lens.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
#ifdef _WIN32
#define NUKE_REF_API extern "C" __declspec(dllexport)
#else
#define NUKE_REF_API extern "C" __attribute__((visibility("default")))
#endif
// A compiled copy of original Nuke CUDA kernel, with no shared objects or
// mutable C++ state crossing the dynamic library boundary.
NUKE_REF_API int nuke_reference_render(const char* lens_path,
    int width,int height,int grid,float fov_h,float gain,
    float red,float green,float blue,float* rgb_out,int* out_pairs){
  try {
    if(!lens_path||!rgb_out||width<1||height<1)return -1;
    LensSystem lens;
    if(!lens.load(lens_path))return -2;
    const float sx=lens.focal_length*std::tan(fov_h*.00872664625997165f);
    const float sy=sx*float(height)/width;
    GhostConfig cfg{};
    cfg.ray_grid=grid;cfg.gain=gain;cfg.pupil_jitter=0;
    std::vector<GhostPair> pairs;
    std::vector<float> boosts;
    filter_ghost_pairs(lens,sx,sy,cfg,pairs,boosts);
    if(out_pairs)*out_pairs=int(pairs.size());
    if(pairs.empty())return -3;
    std::vector<BrightPixel> src{{0.f,0.f,red,green,blue}};
    std::vector<float> r(size_t(width)*height,0),g(r.size(),0),b(r.size(),0);
    GpuBufferCache cache;
    std::string error;
    launch_ghost_cuda(lens,pairs,boosts,src,sx,sy,
      r.data(),g.data(),b.data(),width,height,
      width,height,0,0,cfg,cache,&error);
    if(!error.empty())return -4;
    for(size_t i=0;i<r.size();i++){
      rgb_out[i*3]=r[i];rgb_out[i*3+1]=g[i];rgb_out[i*3+2]=b[i];
    }
    return 0;
  } catch(...) {return -5;}
}
#include "FlareSimNukeABI.hpp"


#include "FlareSimNukeABI.hpp"
// Original upstream ghost_cuda.cu, ghost.cpp, trace.cpp and lens.cpp are
// compiled into this independent reference DLL without changing their math.
NUKE_REF_API int nuke_reference_render_multi(
 const char* path,const FlareSimNukeSource* lights,int count,
 const FlareSimNukeParams* config,int width,int height,float* output){
 try{
  if(!path||!lights||!config||!output||count<0||
     width<1||height<1||width>16384||height>16384)return -1;
  LensSystem lens;
  if(!lens.load(path))return -2;
  const float pi=3.14159265358979323846f;
  const float fh=config->fov_h_deg*pi/180.f;
  const float fv=config->fov_v_deg>0?
       config->fov_v_deg*pi/180.f:
       2.f*std::atan(std::tan(fh*.5f)*height/float(width));
  const float sx=lens.focal_length*std::tan(fh*.5f);
  const float sy=lens.focal_length*std::tan(fv*.5f);
  if(!std::isfinite(sx+sy)||sx<=0||sy<=0)return -3;
  GhostConfig cfg{};
  cfg.ray_grid=std::clamp(config->grid,2,2048);
  cfg.gain=config->gain;
  cfg.aperture_blades=config->blades;
  cfg.aperture_rotation_deg=config->aperture_rotation;
  cfg.spectral_samples=config->spectral;
  cfg.pupil_jitter=config->jitter;
  cfg.pupil_jitter_seed=config->jitter_seed;
  std::vector<GhostPair> pairs;
  std::vector<float> boosts;
  filter_ghost_pairs(lens,sx,sy,cfg,pairs,boosts);
  std::vector<GhostPair> selected;
  std::vector<float> selectedBoost;
  for(size_t i=0;i<pairs.size()&&int(i)<config->max_pairs;i++){
    const bool match=pairs[i].surf_a==config->pair_a&&
       pairs[i].surf_b==config->pair_b;
    if(config->pair_mode==1&&!match)continue;
    if(config->pair_mode==2&&match)continue;
    selected.push_back(pairs[i]);
    selectedBoost.push_back(boosts[i]);
  }
  std::vector<BrightPixel> src;
  src.reserve(size_t(count));
  const float tanH=std::tan(fh*.5f),tanV=std::tan(fv*.5f);
  for(int i=0;i<count;i++){
    const auto& s=lights[i];
    src.push_back({
      std::atan((s.x*2.f-1.f)*tanH),
      std::atan((s.y*2.f-1.f)*tanV),s.r,s.g,s.b});
  }
  const size_t pixels=size_t(width)*height;
  std::vector<float> r(pixels,0),g(pixels,0),b(pixels,0);
  if(!selected.empty()&&!src.empty()){
    GpuBufferCache cache;
    std::string error;
    launch_ghost_cuda(lens,selected,selectedBoost,src,sx,sy,
      r.data(),g.data(),b.data(),width,height,
      width,height,0,0,cfg,cache,&error);
    if(!error.empty())return -4;
  }
  for(size_t i=0;i<pixels;i++){
    output[i*3]=r[i];output[i*3+1]=g[i];output[i*3+2]=b[i];
  }
  return 0;
 }catch(...){return -5;}
}
