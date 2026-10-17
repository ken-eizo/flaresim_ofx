#include "SourceDetection.hpp"
#include <algorithm>
#include <cmath>
#include <vector>
namespace fl::ofx {
namespace {
float luma(const fl::Source& s){return .2126f*s.r+.7152f*s.g+.0722f*s.b;}
}
std::vector<fl::Source> detect_sources(const std::vector<float>& rgb,int w,int h,
                                      const DetectionOptions& o,
                                      const std::vector<float>* mask){
 std::vector<fl::Source> src;
 if(w<=0||h<=0||rgb.size()!=size_t(w)*size_t(h)*3)return src;
 if(mask&&mask->size()!=size_t(w)*size_t(h))return src;
 const int ds=std::clamp(o.downsample,1,32);
 // Nuke's integer-divided grid: leftover pixels are accumulated in the
 // last block, rather than creating an additional partial block.
 const int dw=std::max(1,w/ds),dh=std::max(1,h/ds);
 src.reserve(size_t(dw)*dh);
 for(int by=0;by<dh;by++)for(int bx=0;bx<dw;bx++){
   const int x0=bx*ds,y0=by*ds;
   const int xEnd=bx==dw-1?w:std::min(w,x0+ds);
   const int yEnd=by==dh-1?h:std::min(h,y0+ds);
   float r=0,g=0,b=0,mask_sum=0;int count=0;
   // Original Nuke FlareSim.cpp scans rows first.
   for(int y=y0;y<yEnd;y++)for(int x=x0;x<xEnd;x++){
     const size_t index=(size_t(y)*w+x)*3;
     r+=rgb[index];g+=rgb[index+1];b+=rgb[index+2];
     mask_sum+=mask?(*mask)[size_t(y)*w+x]:1.f;
     ++count;
   }
   if(count==0)continue;
   r/=count;g/=count;b/=count;
   float brightness=.2126f*r+.7152f*g+.0722f*b;
   if(o.source_cap>0.f&&brightness>o.source_cap){
     const float ratio=o.source_cap/brightness;
     r*=ratio;g*=ratio;b*=ratio;brightness=o.source_cap;
   }
   // Original Nuke applies the block-averaged alpha after source capping.
   const float opacity=mask_sum/count;
   brightness*=opacity;r*=opacity;g*=opacity;b*=opacity;
   if(brightness<o.threshold)continue;
   // Nuke's centroid uses the nominal NxN block (not the extra absorbed
   // remainder pixels) relative to the complete image format.
   const int nominalXEnd=std::min(w,x0+ds);
   const int nominalYEnd=std::min(h,y0+ds);
   const float px=.5f*(x0+nominalXEnd)/w;
   const float py=.5f*(y0+nominalYEnd)/h;
   src.push_back({px,py,r,g,b});
 }
 if(o.cluster_radius>0&&!src.empty()){
   const float pi=3.14159265358979323846f;
   const float tanH=std::tan(o.fov_h_deg*pi/360.f);
   const float tanV=std::tan(o.fov_v_deg*pi/360.f);
   const float threshold=o.cluster_radius*2.f*tanH/w;
   const float thresholdSq=threshold*threshold;
   std::sort(src.begin(),src.end(),[](const auto&a,const auto&b){
     return luma(a)>luma(b);
   });
   std::vector<char> consumed(src.size(),false);
   std::vector<fl::Source> merged;merged.reserve(src.size());
   for(size_t i=0;i<src.size();++i){
     if(consumed[i])continue;
     auto seed=src[i];
     const float ax=std::atan((seed.x*2.f-1.f)*tanH);
     const float ay=std::atan((seed.y*2.f-1.f)*tanV);
     float weight=luma(seed);
     float sumX=ax*weight,sumY=ay*weight;
     for(size_t j=i+1;j<src.size();++j){
       if(consumed[j])continue;
       const auto& target=src[j];
       const float bx=std::atan((target.x*2.f-1.f)*tanH);
       const float by=std::atan((target.y*2.f-1.f)*tanV);
       const float dx=ax-bx,dy=ay-by;
       if(dx*dx+dy*dy>thresholdSq)continue;
       consumed[j]=1;
       const float extra=luma(target);
       sumX+=bx*extra;sumY+=by*extra;weight+=extra;
       seed.r+=target.r;seed.g+=target.g;seed.b+=target.b;
     }
     if(weight>0.f){
       seed.x=(std::tan(sumX/weight)/tanH+1.f)*.5f;
       seed.y=(std::tan(sumY/weight)/tanV+1.f)*.5f;
     }
     merged.push_back(seed);
   }
   src.swap(merged);
 }
 // Nuke treats 0 as unlimited, bounded here only for OFX host safety.
 const int cap=o.max_sources>0?o.max_sources:8192;
 if(src.size()>size_t(cap)){
   std::partial_sort(src.begin(),src.begin()+cap,src.end(),
       [](const auto&a,const auto&b){return luma(a)>luma(b);});
   src.resize(cap);
 }
 return src;
}
}
