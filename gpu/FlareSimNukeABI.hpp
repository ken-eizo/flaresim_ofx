#pragma once
// Stable POD boundary between OFX adapter and unmodified upstream Nuke CUDA DLL.
// No STL containers, host SDK handles, or CUDA device pointers cross this ABI.
struct FlareSimNukeSource{
 float x,y,r,g,b; // normalized full-image coordinates + scene-linear radiance
};
struct FlareSimNukeParams{
 float fov_h_deg,fov_v_deg,gain,aperture_rotation;
 int grid,spectral,blades,jitter,jitter_seed;
 int pair_mode,pair_a,pair_b,max_pairs;
};
using FlareSimNukeRenderFn=int(*)(
 const char* lens_path,const FlareSimNukeSource* sources,int source_count,
 const FlareSimNukeParams* config,int w,int h,float* out_rgb);
