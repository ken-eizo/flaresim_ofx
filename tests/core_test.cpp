#include "FlareLensCore.hpp"
#include "FlareLensOptics.hpp"
#include "SourceDetection.hpp"
#include <cassert>
#include <cmath>
#include <filesystem>
#include <iostream>
int main(){
  const auto root=std::filesystem::path(FLARESIM_SOURCE_ROOT);
  const auto path=root/"resources/lenses/AI_Nikkor_50mm_F1.2S.lens";
  LensSystem lens;
  if(!lens.load(path.string().c_str())||lens.surfaces.empty()){std::cerr<<"Lens load failed: "<<path<<"\n";return 1;}
  std::vector<float> image(64*32*3);
  image[(size_t(15)*64+32)*3]=80;
  image[(size_t(15)*64+32)*3+1]=90;
  image[(size_t(15)*64+32)*3+2]=100;
  fl::ofx::DetectionOptions options;options.downsample=1;options.threshold=2;
  auto sources=fl::ofx::detect_sources(image,64,32,options);
  if(sources.size()!=1 || std::abs(sources[0].x-(32.5f/64))>0.0001f){std::cerr<<"Incorrect source coordinates\n";return 2;}
  std::vector<float> mask(64*32,0.f);
  if(!fl::ofx::detect_sources(image,64,32,options,&mask).empty())return 6;
  mask[15*64+32]=1.f;
  if(fl::ofx::detect_sources(image,64,32,options,&mask).size()!=1)return 7;
  options.downsample=4;
  sources=fl::ofx::detect_sources(image,64,32,options);
  if(sources.size()!=1){std::cerr<<"Incorrect block-average detection\n";return 3;}
  // Original Nuke truncates downsample grid dimensions and merges the
  // remainder into its last source block rather than creating a new block.
  std::vector<float> odd(10*7*3,0.f);
  for(int y=0;y<7;y++)for(int x=0;x<10;x++)
     odd[(size_t(y)*10+x)*3]=10.f;
  fl::ofx::DetectionOptions edges;
  edges.downsample=4;edges.threshold=1.f;
  const auto odd_src=fl::ofx::detect_sources(odd,10,7,edges);
  if(odd_src.size()!=2)return 8;
  if(std::abs(odd_src[1].x-.6f)>1e-5f||
     std::abs(odd_src[1].y-2.f/7.f)>1e-5f)return 9;
  fl::Settings vogel;vogel.grid=32;vogel.pupil_jitter=3;
  const auto golden=fl::make_pupil(vogel);
  if(golden.size()!=1024)return 10;
  for(const auto& sample:golden)
    if(!std::isfinite(sample.u+sample.v)||
       sample.u*sample.u+sample.v*sample.v>1.f+1e-5f)return 11;
  fl::Settings settings;settings.grid=4;settings.max_pairs=2;settings.gain=1000;
  const auto rendered=fl::render_cpu(lens,sources,settings,64,32);
  if(rendered.rgb.size()!=64u*32u*3u||rendered.abort_error){std::cerr<<"CPU render failed\n";return 4;}
  for(float v:rendered.rgb){if(!std::isfinite(v)||v<0){std::cerr<<"Nonfinite output\n";return 5;}}
  std::cout<<"Core optical test OK; surfaces="<<lens.surfaces.size()<<"\n";
  return 0;
}
