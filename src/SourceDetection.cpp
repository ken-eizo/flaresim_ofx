#include "SourceDetection.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
namespace fl::ofx {
namespace {
float luma(const fl::Source& s) {return .2126f*s.r+.7152f*s.g+.0722f*s.b;}
}
std::vector<fl::Source> detect_sources(const std::vector<float>& rgb,int w,int h,
                                       const DetectionOptions& opts) {
  std::vector<fl::Source> sources;
  if(w<=0||h<=0||rgb.size()!=size_t(w)*size_t(h)*3)return sources;
  const int ds=std::clamp(opts.downsample,1,32);
  // Nuke calculates an average RGB over each downsample block before threshold.
  for(int by=0;by<h;by+=ds) {
    for(int bx=0;bx<w;bx+=ds) {
      double cr=0,cg=0,cb=0; int count=0;
      for(int y=by;y<std::min(h,by+ds);++y)
        for(int x=bx;x<std::min(w,bx+ds);++x) {
          const size_t i=(size_t(y)*w+x)*3;
          const float r=rgb[i],g=rgb[i+1],b=rgb[i+2];
          if(!std::isfinite(r)||!std::isfinite(g)||!std::isfinite(b))continue;
          cr+=r;cg+=g;cb+=b;++count;
        }
      if(count==0)continue;
      fl::Source s{};
      s.r=float(cr/count);s.g=float(cg/count);s.b=float(cb/count);
      float lum=luma(s);
      if(opts.source_cap>0&&lum>opts.source_cap){
        const float k=opts.source_cap/lum;s.r*=k;s.g*=k;s.b*=k;lum=opts.source_cap;
      }
      if(lum<opts.threshold)continue;
      s.x=(bx+.5f*std::min(ds,w-bx))/w;
      s.y=(by+.5f*std::min(ds,h-by))/h;
      sources.push_back(s);
    }
  }
  if(opts.cluster_radius>0 && sources.size()>1) {
    std::sort(sources.begin(),sources.end(),[](auto&a,auto&b){return luma(a)>luma(b);});
    std::vector<char> consumed(sources.size());
    std::vector<fl::Source> merged;merged.reserve(sources.size());
    const float d2=float(opts.cluster_radius)*opts.cluster_radius;
    for(size_t i=0;i<sources.size();++i){
      if(consumed[i])continue;
      auto s=sources[i];
      float total=luma(s);
      double wx=s.x*total,wy=s.y*total;
      for(size_t j=i+1;j<sources.size();++j){
        if(consumed[j])continue;
        const auto& t=sources[j];
        const float dx=(t.x-s.x)*w,dy=(t.y-s.y)*h;
        if(dx*dx+dy*dy>d2)continue;
        consumed[j]=1;
        const float wt=luma(t);
        wx+=t.x*wt;wy+=t.y*wt;total+=wt;
        s.r+=t.r;s.g+=t.g;s.b+=t.b;
      }
      if(total>0){s.x=float(wx/total);s.y=float(wy/total);}
      merged.push_back(s);
    }
    sources.swap(merged);
  }
  if(opts.max_sources>0&&sources.size()>size_t(opts.max_sources)){
    std::partial_sort(sources.begin(),sources.begin()+opts.max_sources,sources.end(),
      [](const auto& a,const auto& b){return luma(a)>luma(b);});
    sources.resize(opts.max_sources);
  }
  return sources;
}
} // namespace fl::ofx
