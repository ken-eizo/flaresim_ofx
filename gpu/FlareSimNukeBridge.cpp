#include "FlareSimGpu.hpp"
#include "FlareSimNukeABI.hpp"
#include "FlareLensOptics.hpp"
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#if defined(_WIN32) && defined(FLARESIM_HAVE_CUDA)
#define NOMINMAX
#include <Windows.h>
#include <filesystem>
#include <mutex>
namespace fl::ofxgpu {
bool render_nuke_reference(const std::string& lens_path,
     const std::vector<fl::Source>& lights,const fl::Settings& settings,
     int w,int h,float blur,int blur_passes,std::vector<float>& output){
 if(lens_path.empty()||lights.empty()||!available())return false;
 using Fn=FlareSimNukeRenderFn;
 static std::once_flag once;
 static Fn fn=nullptr;
 std::call_once(once,[]{
   HMODULE own=nullptr;
   if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|
       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
       reinterpret_cast<LPCWSTR>(&render_nuke_reference),&own)||!own)return;
   wchar_t filename[32768]{};
   const auto chars=GetModuleFileNameW(own,filename,32768);
   if(!chars||chars>=32768)return;
   const auto dll=std::filesystem::path(filename).parent_path()/
                  L"FlareSimNukeReference.dll";
   HMODULE ref=LoadLibraryW(dll.c_str());
   if(ref)fn=reinterpret_cast<Fn>(GetProcAddress(ref,"nuke_reference_render_multi"));
 });
 if(!fn)return false;
 std::vector<FlareSimNukeSource> sources;
 sources.reserve(lights.size());
 for(const auto& s:lights)sources.push_back({s.x,s.y,s.r,s.g,s.b});
 FlareSimNukeParams args{};
 args.fov_h_deg=settings.fov_h_deg;
 args.fov_v_deg=settings.fov_v_deg;
 args.gain=settings.gain;
 args.aperture_rotation=settings.aperture_rotation_deg;
 args.grid=settings.grid;
 args.spectral=settings.spectral_samples;
 args.blades=settings.aperture_blades;
 args.jitter=settings.pupil_jitter;
 args.jitter_seed=settings.jitter_seed;
 args.pair_mode=settings.pair_mode;
 args.pair_a=settings.pair_surface_a;
 args.pair_b=settings.pair_surface_b;
 args.max_pairs=settings.max_pairs;
 output.resize(size_t(w)*h*3);
 const int code=fn(lens_path.c_str(),sources.data(),int(sources.size()),
                   &args,w,h,output.data());
 if(code!=0){output.clear();return false;}
 if(blur>0&&blur_passes>0)
   fl::box_blur_rgb(output,w,h,int(std::round(blur*std::hypot(float(w),float(h)))),
                    blur_passes);
 return true;
}
}
#else
namespace fl::ofxgpu {
bool render_nuke_reference(const std::string&,
  const std::vector<fl::Source>&,const fl::Settings&,
  int,int,float,int,std::vector<float>&){return false;}
}
#endif
