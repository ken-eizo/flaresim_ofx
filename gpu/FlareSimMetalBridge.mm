#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include "FlareSimGpu.hpp"
#include "FlareLensMetal.hpp"
#include <algorithm>
#include <vector>
#include <cstdint>
#include <limits>

namespace fl::ofxgpu {
bool available() noexcept {
  @autoreleasepool {
    id<MTLDevice> dev=MTLCreateSystemDefaultDevice();
    return dev!=nil;
  }
}
bool render_ghosts(const LensSystem& lens,const std::vector<fl::Source>& sources,
                   const fl::Settings& settings,int w,int h,
                   float blur,int passes,std::vector<float>& result){
  @autoreleasepool {
    if(sources.empty()||w<1||h<1||w>16384||h>16384||
       sources.size()>2048||lens.surfaces.size()>64)return false;
    const size_t npx=size_t(w)*h;
    if(npx>size_t(16384)*16384 || npx>std::numeric_limits<size_t>::max()/16)return false;
    id<MTLDevice> device=MTLCreateSystemDefaultDevice();
    if(!device)return false;
    id<MTLCommandQueue> queue=[device newCommandQueue];
    if(!queue)return false;
    const bool needs_blur=blur>0.f&&passes>0;
    const size_t need=fl::gpu::metal_scratch_bytes(lens,int(sources.size()),
                          settings.grid,w,h,4,needs_blur,false,false);
    if(need==0||need>std::numeric_limits<NSUInteger>::max()||npx*16>std::numeric_limits<NSUInteger>::max())
      return false;
    id<MTLBuffer> output=[device newBufferWithLength:npx*16 options:MTLResourceStorageModeShared];
    id<MTLBuffer> scratch=[device newBufferWithLength:need options:MTLResourceStorageModePrivate];
    if(!output||!scratch)return false;
    fl::gpu::View view{};
    view.output=(__bridge void*)output;
    view.width=w;view.height=h;
    view.output_pitch_bytes=w*16;
    view.output_mode=4; // Internal ghost-only BGRA128
    view.ghost_blur=std::max(0.f,blur);
    view.ghost_blur_passes=std::clamp(passes,0,6);
    view.command_stream=(__bridge void*)queue;
    if(!fl::gpu::metal_compose(lens,sources,settings,view,(__bridge void*)scratch,need))return false;
    const float* pixel=(const float*)output.contents;
    if(!pixel)return false;
    result.resize(npx*3);
    for(size_t i=0;i<npx;i++){
      result[3*i]=pixel[4*i+2];result[3*i+1]=pixel[4*i+1];result[3*i+2]=pixel[4*i];
    }
    return true;
  }
}
}
