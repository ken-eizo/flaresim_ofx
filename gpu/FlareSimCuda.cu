#include "FlareSimGpu.hpp"
#include "FlareLensGpuBridge.hpp"
#include <cuda_runtime.h>
#include <algorithm>
#include <limits>
#include <vector>

namespace fl::ofxgpu {
bool available() noexcept {
  int count=0;
  return cudaGetDeviceCount(&count)==cudaSuccess && count>0;
}
namespace {
struct Allocation{
  void* ptr=nullptr;
  ~Allocation(){if(ptr)cudaFree(ptr);}
  bool allocate(size_t bytes){return cudaMalloc(&ptr,bytes)==cudaSuccess;}
};
}
bool render_ghosts(const LensSystem& lens,const std::vector<fl::Source>& sources,
                   const fl::Settings& settings,int w,int h,
                   float blur,int passes,std::vector<float>& result){
  if(!available()||sources.empty()||w<1||h<1||w>16384||h>16384||
     sources.size()>2048||lens.surfaces.size()>64)return false;
  const size_t npx=size_t(w)*h;
  if(npx>size_t(16384)*16384 || npx>std::numeric_limits<size_t>::max()/16)return false;
  const bool needs_blur=blur>0.f&&passes>0;
  const size_t scratch_bytes=fl::gpu::scratch_bytes(
      lens,int(sources.size()),settings.grid,w,h,4,needs_blur,false,false);
  if(scratch_bytes==0)return false;
  Allocation out,scratch;
  if(!out.allocate(npx*16)||!scratch.allocate(scratch_bytes))return false;
  fl::gpu::View view{};
  view.input=nullptr;
  view.output=out.ptr;
  view.width=w;view.height=h;
  view.input_pitch_bytes=0;view.output_pitch_bytes=w*16;
  view.output_mode=4; // AE internal mode 4: Ghost Only (BGRA128).
  view.ghost_blur=std::max(blur,0.f);
  view.ghost_blur_passes=std::clamp(passes,0,6);
  view.command_stream=nullptr; // Default CUDA stream; synchronization is performed in cuda_compose.
  if(!fl::gpu::cuda_compose(lens,sources,settings,view,scratch.ptr,scratch_bytes))return false;
  std::vector<float4> pixels(npx);
  if(cudaMemcpy(pixels.data(),out.ptr,npx*sizeof(float4),cudaMemcpyDeviceToHost)!=cudaSuccess)
    return false;
  result.resize(npx*3);
  for(size_t i=0;i<npx;i++){
    // Source AE GPU renderer stores BGRA. OFX CPU adapter uses RGB.
    result[i*3+0]=pixels[i].z;
    result[i*3+1]=pixels[i].y;
    result[i*3+2]=pixels[i].x;
  }
  return true;
}
}
