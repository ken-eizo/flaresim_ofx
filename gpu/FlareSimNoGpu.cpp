#include "FlareSimGpu.hpp"
namespace fl::ofxgpu {
bool available() noexcept{return false;}
bool render_ghosts(const LensSystem&,const std::vector<fl::Source>&,
                   const fl::Settings&,int,int,float,int,std::vector<float>&){
  return false;
}
}
