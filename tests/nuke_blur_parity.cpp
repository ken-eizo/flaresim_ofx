#include "FlareLensOptics.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>
// This function below is copied unmodified from
// LocalStarlight/flaresim_nuke/src/FlareSim.cpp, without needing Nuke NDK.
static void box_blur(float* buf, int w, int h, int radius, int passes,
                     std::vector<float>& tmp)
{
    if (radius < 1 || passes < 1) return;
    const size_t npx = (size_t)w * h;
    tmp.resize(npx);

    const int maxdim = std::max(w, h);
    std::vector<double> ps(maxdim + 1);

    for (int p = 0; p < passes; ++p)
    {
        // ---- Horizontal pass: buf -> tmp ----
        for (int y = 0; y < h; ++y)
        {
            const float* row = buf + (size_t)y * w;
            float*       out = tmp.data() + (size_t)y * w;
            ps[0] = 0.0;
            for (int x = 0; x < w; ++x)
                ps[x + 1] = ps[x] + row[x];
            for (int x = 0; x < w; ++x)
            {
                int lo = std::max(0,     x - radius);
                int hi = std::min(w - 1, x + radius);
                out[x] = (float)((ps[hi + 1] - ps[lo]) / (hi - lo + 1));
            }
        }
        std::memcpy(buf, tmp.data(), npx * sizeof(float));

        // ---- Vertical pass: buf -> tmp ----
        for (int x = 0; x < w; ++x)
        {
            ps[0] = 0.0;
            for (int y = 0; y < h; ++y)
                ps[y + 1] = ps[y] + buf[(size_t)y * w + x];
            for (int y = 0; y < h; ++y)
            {
                int lo = std::max(0,     y - radius);
                int hi = std::min(h - 1, y + radius);
                tmp[(size_t)y * w + x] = (float)((ps[hi + 1] - ps[lo]) / (hi - lo + 1));
            }
        }
        std::memcpy(buf, tmp.data(), npx * sizeof(float));
    }
}


int main(){
 constexpr int W=113,H=69;
 std::vector<float> input(size_t(W)*H*3);
 for(size_t i=0;i<input.size();i++)
   input[i]=float(int((i*137u+17u)%311u)-72)*.017f;
 auto port=input;
 std::vector<float> planar(size_t(W)*H);
 std::vector<float> reference(input.size());
 for(int c=0;c<3;c++){
   for(size_t i=0;i<planar.size();i++)planar[i]=input[i*3+c];
   std::vector<float> scratch;
   box_blur(planar.data(),W,H,4,3,scratch);
   for(size_t i=0;i<planar.size();i++)reference[i*3+c]=planar[i];
 }
 fl::box_blur_rgb(port,W,H,4,3);
 double max_diff=0,total=0,diff=0;
 for(size_t i=0;i<input.size();i++){
   if(!std::isfinite(port[i])||!std::isfinite(reference[i]))return 1;
   const double delta=std::abs(double(port[i])-reference[i]);
   max_diff=std::max(max_diff,delta);
   diff+=delta;total+=std::abs(reference[i]);
 }
 const double relative=diff/std::max(total,1e-20);
 std::cout<<"Nuke original post blur vs OFX interleaved RGB"
   <<" MaxPixel "<<max_diff<<" RelativeL1 "<<relative<<"\n";
 return relative<=1.e-6&&max_diff<=1.e-6?0:2;
}
