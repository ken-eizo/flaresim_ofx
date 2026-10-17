// FlareSimOFX: host-neutral OpenFX Image Effect adapter.
// Optical equations and lens files: LocalStarlight/flaresim_nuke.
// CPU implementation and sampling optimisations: ken-eizo/flaresim_ae.
#include "ofxImageEffect.h"
#include "ofxParam.h"
#include "ofxProperty.h"
#include "FlareLensCore.hpp"
#include "FlareLensOptics.hpp"
#include "FlareLensCpuParallel.hpp"
#include "SourceDetection.hpp"
#include "starburst.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#else
#include <dlfcn.h>
#endif

namespace fs=std::filesystem;
namespace {
OfxHost* host=nullptr;
OfxImageEffectSuiteV1* fx=nullptr;
OfxPropertySuiteV1* prop=nullptr;
OfxParameterSuiteV1* params=nullptr;
constexpr const char* ID="dev.keneizo.FlareSimOFX";
constexpr const char* kSource=kOfxImageEffectSimpleSourceClipName;
constexpr const char* kOutput=kOfxImageEffectOutputClipName;
constexpr float pi=3.14159265358979323846f;

struct Instance {
  OfxImageClipHandle source=nullptr, output=nullptr;
  OfxParamSetHandle pset=nullptr;
};
struct Image {
  OfxPropertySetHandle props=nullptr;
  uint8_t* data=nullptr;
  OfxRectI bounds{0,0,0,0};
  int rowbytes=0, channels=0, bytes=0;
  bool isfloat=false;
  Image()=default;
  Image(const Image&)=delete;Image& operator=(const Image&)=delete;
  ~Image(){if(props && fx)fx->clipReleaseImage(props);}
  bool open(OfxImageClipHandle clip,double t) {
    if(fx->clipGetImage(clip,t,nullptr,&props)!=kOfxStatOK||!props)return false;
    void* ptr=nullptr;char* comp=nullptr;char* depth=nullptr;
    if(prop->propGetPointer(props,kOfxImagePropData,0,&ptr)!=kOfxStatOK||!ptr)return false;
    if(prop->propGetIntN(props,kOfxImagePropBounds,4,&bounds.x1)!=kOfxStatOK)return false;
    if(prop->propGetInt(props,kOfxImagePropRowBytes,0,&rowbytes)!=kOfxStatOK)return false;
    if(prop->propGetString(props,kOfxImageEffectPropComponents,0,&comp)!=kOfxStatOK)return false;
    if(prop->propGetString(props,kOfxImageEffectPropPixelDepth,0,&depth)!=kOfxStatOK)return false;
    channels=comp&&strcmp(comp,kOfxImageComponentRGBA)==0?4:
             comp&&strcmp(comp,kOfxImageComponentRGB)==0?3:0;
    if(depth&&strcmp(depth,kOfxBitDepthFloat)==0){bytes=4;isfloat=true;}
    else if(depth&&strcmp(depth,kOfxBitDepthShort)==0){bytes=2;}
    else if(depth&&strcmp(depth,kOfxBitDepthByte)==0){bytes=1;}
    data=(uint8_t*)ptr;
    return channels>0&&bytes>0&&bounds.x2>bounds.x1&&bounds.y2>bounds.y1;
  }
  bool contains(int x,int y)const {
    return x>=bounds.x1&&x<bounds.x2&&y>=bounds.y1&&y<bounds.y2;
  }
  const uint8_t* ptr_at(int x,int y)const {
    return data+ptrdiff_t(y-bounds.y1)*rowbytes+
      ptrdiff_t(x-bounds.x1)*channels*bytes;
  }
  uint8_t* ptr_at(int x,int y) {
    return data+ptrdiff_t(y-bounds.y1)*rowbytes+
      ptrdiff_t(x-bounds.x1)*channels*bytes;
  }
  float read(int x,int y,int c)const {
    if(!contains(x,y)||c>=channels)return c==3?1.f:0.f;
    const auto* p=ptr_at(x,y)+c*bytes;
    if(isfloat){float v;std::memcpy(&v,p,4);return std::isfinite(v)?v:0.f;}
    if(bytes==2){uint16_t v;std::memcpy(&v,p,2);return v/65535.f;}
    return *p/255.f;
  }
  void write(int x,int y,int c,float value) {
    auto* p=ptr_at(x,y)+c*bytes;
    if(!std::isfinite(value))value=0;
    if(isfloat){std::memcpy(p,&value,4);return;}
    value=std::clamp(value,0.f,1.f);
    if(bytes==2){const uint16_t v=uint16_t(std::lround(value*65535.f));std::memcpy(p,&v,2);return;}
    *p=uint8_t(std::lround(value*255.f));
  }
};

Instance* instance(OfxImageEffectHandle e) {
  OfxPropertySetHandle p=nullptr;void* v=nullptr;
  if(fx->getPropertySet(e,&p)!=kOfxStatOK||!p)return nullptr;
  prop->propGetPointer(p,kOfxPropInstanceData,0,&v);
  return static_cast<Instance*>(v);
}
double number(Instance* i,const char* name,double t,double fallback) {
  OfxParamHandle p=nullptr;
  if(params->paramGetHandle(i->pset,name,&p,nullptr)!=kOfxStatOK)return fallback;
  double v=fallback;
  if(params->paramGetValueAtTime(p,t,&v)!=kOfxStatOK || !std::isfinite(v))return fallback;
  return v;
}
int integer(Instance* i,const char* name,double t,int fallback) {
  OfxParamHandle p=nullptr;
  if(params->paramGetHandle(i->pset,name,&p,nullptr)!=kOfxStatOK)return fallback;
  int v=fallback;return params->paramGetValueAtTime(p,t,&v)==kOfxStatOK?v:fallback;
}
std::string string_param(Instance* i,const char* name,double t) {
  OfxParamHandle p=nullptr;char* v=nullptr;
  if(params->paramGetHandle(i->pset,name,&p,nullptr)!=kOfxStatOK)return {};
  if(params->paramGetValueAtTime(p,t,&v)!=kOfxStatOK || !v)return {};
  return v;
}
std::array<double,2> point_param(Instance* i,const char* name,double t) {
  std::array<double,2> v{.5,.5};OfxParamHandle p=nullptr;
  if(params->paramGetHandle(i->pset,name,&p,nullptr)==kOfxStatOK)
    params->paramGetValueAtTime(p,t,&v[0],&v[1]);
  return v;
}
fs::path module_location(){
#ifdef _WIN32
  HMODULE h=nullptr;
  // Address-based lookup avoids embedding local paths and does not pin the DLL.
  GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|
    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
    reinterpret_cast<LPCWSTR>(&module_location),&h);
  if(!h)return {};
  std::wstring path(32768,L'\0');
  const DWORD n=GetModuleFileNameW(h,path.data(),DWORD(path.size()));
  if(!n||n>=path.size())return {};
  path.resize(n);return fs::path(path);
#else
  Dl_info d{};
  if(dladdr(reinterpret_cast<const void*>(&module_location),&d)&&d.dli_fname)
    return fs::path(d.dli_fname);
  return {};
#endif
}
fs::path lens_directory(){
  if(const char* override_path=std::getenv("FLARESIM_LENS_DIR"))
    if(*override_path)return fs::u8path(override_path);
  // .../Contents/Win64/FlareSim.ofx -> .../Contents/Resources/lenses
  const auto mod=module_location();
  return mod.empty()?fs::path{}:mod.parent_path().parent_path()/"Resources"/"lenses";
}
std::shared_ptr<const LensSystem> load_lens(const fs::path& path){
  if(path.empty()||!fs::is_regular_file(path))return {};
  struct Entry{fs::path path;fs::file_time_type modified;std::shared_ptr<const LensSystem> lens;};
  static std::mutex m;static std::vector<Entry> cache;
  const auto modified=fs::last_write_time(path);
  {
    std::lock_guard<std::mutex> lock(m);
    for(const auto& e:cache)if(e.path==path&&e.modified==modified)return e.lens;
  }
  auto obj=std::make_shared<LensSystem>();
  // Original upstream .lens parser. Do not substitute another prescription on failure.
  if(!obj->load(path.string().c_str())||obj->surfaces.size()<2)return {};
  std::lock_guard<std::mutex> lock(m);
  if(cache.size()>=16)cache.erase(cache.begin());
  cache.push_back({path,modified,obj});
  return obj;
}
int abort_requested(void* ctx){
  return fx->abort(static_cast<OfxImageEffectHandle>(ctx));
}
struct ParamDef{const char* name;const char* label;double default_value;
                double min_value;double max_value;const char* hint;};
void define_double(OfxParamSetHandle set,const ParamDef& d){
  OfxPropertySetHandle p=nullptr;
  if(params->paramDefine(set,kOfxParamTypeDouble,d.name,&p)!=kOfxStatOK)throw std::runtime_error("paramDefine");
  prop->propSetString(p,kOfxPropLabel,0,d.label);
  prop->propSetDouble(p,kOfxParamPropDefault,0,d.default_value);
  prop->propSetDouble(p,kOfxParamPropMin,0,d.min_value);
  prop->propSetDouble(p,kOfxParamPropMax,0,d.max_value);
  prop->propSetDouble(p,kOfxParamPropDisplayMin,0,d.min_value);
  prop->propSetDouble(p,kOfxParamPropDisplayMax,0,d.max_value);
  prop->propSetString(p,kOfxParamPropHint,0,d.hint);
}
void define_int(OfxParamSetHandle set,const char* name,const char* label,int def,int min,int max,const char* hint){
  OfxPropertySetHandle p=nullptr;
  if(params->paramDefine(set,kOfxParamTypeInteger,name,&p)!=kOfxStatOK)throw std::runtime_error("paramDefine");
  prop->propSetString(p,kOfxPropLabel,0,label);
  prop->propSetInt(p,kOfxParamPropDefault,0,def);
  prop->propSetInt(p,kOfxParamPropMin,0,min);
  prop->propSetInt(p,kOfxParamPropMax,0,max);
  prop->propSetString(p,kOfxParamPropHint,0,hint);
}
void define_boolean(OfxParamSetHandle set,const char* name,const char* label,int def,const char* hint){
  OfxPropertySetHandle p=nullptr;
  params->paramDefine(set,kOfxParamTypeBoolean,name,&p);
  prop->propSetString(p,kOfxPropLabel,0,label);
  prop->propSetInt(p,kOfxParamPropDefault,0,def);
  prop->propSetString(p,kOfxParamPropHint,0,hint);
}
void define_choice(OfxParamSetHandle set,const char* name,const char* label,
                   int def,std::initializer_list<const char*> entries){
  OfxPropertySetHandle p=nullptr;
  params->paramDefine(set,kOfxParamTypeChoice,name,&p);
  prop->propSetString(p,kOfxPropLabel,0,label);
  prop->propSetInt(p,kOfxParamPropDefault,0,def);
  int x=0;for(const auto* e:entries)prop->propSetString(p,kOfxParamPropChoiceOption,x++,e);
}
void define_string(OfxParamSetHandle set,const char* name,const char* label,const char* default_value,const char* hint){
  OfxPropertySetHandle p=nullptr;
  params->paramDefine(set,kOfxParamTypeString,name,&p);
  prop->propSetString(p,kOfxPropLabel,0,label);
  prop->propSetString(p,kOfxParamPropDefault,0,default_value);
  prop->propSetString(p,kOfxParamPropHint,0,hint);
}
OfxStatus describe(OfxImageEffectHandle e){
  OfxPropertySetHandle p=nullptr; if(fx->getPropertySet(e,&p)!=kOfxStatOK)return kOfxStatErrBadHandle;
  prop->propSetString(p,kOfxPropLabel,0,"FlareSim");
  prop->propSetString(p,kOfxImageEffectPluginPropGrouping,0,"FlareSim");
  prop->propSetString(p,kOfxImageEffectPropSupportedContexts,0,kOfxImageEffectContextFilter);
  prop->propSetString(p,kOfxImageEffectPropSupportedContexts,1,kOfxImageEffectContextGeneral);
  prop->propSetString(p,kOfxImageEffectPropSupportedPixelDepths,0,kOfxBitDepthFloat);
  prop->propSetString(p,kOfxImageEffectPropSupportedPixelDepths,1,kOfxBitDepthShort);
  prop->propSetString(p,kOfxImageEffectPropSupportedPixelDepths,2,kOfxBitDepthByte);
  prop->propSetInt(p,kOfxImageEffectPropSupportsTiles,0,0);
  prop->propSetInt(p,kOfxImageEffectPropSupportsMultiResolution,0,1);
  prop->propSetString(p,kOfxImageEffectPluginRenderThreadSafety,0,kOfxImageEffectRenderInstanceSafe);
  prop->propSetInt(p,kOfxImageEffectPluginPropHostFrameThreading,0,0);
  return kOfxStatOK;
}
OfxStatus describe_context(OfxImageEffectHandle e){
  OfxPropertySetHandle p=nullptr;
  for(const auto* clip_name:{kSource,kOutput}){
    if(fx->clipDefine(e,clip_name,&p)!=kOfxStatOK)return kOfxStatFailed;
    prop->propSetString(p,kOfxImageEffectPropSupportedComponents,0,kOfxImageComponentRGBA);
    prop->propSetString(p,kOfxImageEffectPropSupportedComponents,1,kOfxImageComponentRGB);
  }
  OfxParamSetHandle s=nullptr;
  if(fx->getParamSet(e,&s)!=kOfxStatOK)return kOfxStatFailed;
  define_string(s,"lens_name","Lens Prescription","AI_Nikkor_50mm_F1.2S.lens",
                "Exact filename from the 1370 original Nuke lens presets in Resources/lenses.");
  define_string(s,"lens_file","Custom Lens File","",
                "Optional absolute .lens path. Overrides bundled Lens Prescription.");
  define_choice(s,"source_mode","Light Sources",0,{"Detect Highlights","Manual Position"});
  OfxPropertySetHandle q=nullptr;
  params->paramDefine(s,kOfxParamTypeDouble2D,"light_position",&q);
  prop->propSetString(q,kOfxPropLabel,0,"Light Position (0..1)");
  prop->propSetDouble(q,kOfxParamPropDefault,0,.5);
  prop->propSetDouble(q,kOfxParamPropDefault,1,.5);
  define_double(s,{"light_radiance","Manual Radiance",10,0,1e6,"Intensity for manual light source in linear RGB."});
  define_double(s,{"fov_h","FOV Horizontal",40,1,150,"Field of view in degrees."});
  define_double(s,{"fov_v","FOV Vertical (0=Auto)",0,0,150,"0 derives vertical FOV from aspect ratio."});
  define_double(s,{"threshold","Threshold",2,0,1e6,"Source detection luminance threshold in scene-linear RGB."});
  define_double(s,{"source_cap","Source Cap",0,0,1e6,"0 disables cap; see original Nuke."});
  define_int(s,"downsample","Downsample",4,1,32,"Block size of Nuke highlight extraction.");
  define_int(s,"max_sources","Max Sources",512,1,8192,"Highest luminance samples kept.");
  define_int(s,"cluster_radius","Cluster Radius (px)",0,0,256,"Optional Nuke light clustering.");
  define_double(s,{"gain","Flare Gain",1000,0,1e7,"Original Nuke-style ghost multiplier."});
  define_int(s,"ray_grid","Ray Grid",32,2,2048,"Samples per pupil axis; quadratic render cost." );
  define_int(s,"max_pairs","Max Ghost Pairs",40,1,2000,"Limit optical reflection pairs." );
  define_int(s,"spectral_samples","Spectral Samples",3,3,11,"Wavelengths sampled per optical path." );
  define_int(s,"aperture_blades","Aperture Blades",0,0,16,"0 is round, 3..16 polygonal." );
  define_double(s,{"aperture_rotation","Aperture Rotation",0,-360,360,"Orientation in degrees."});
  define_choice(s,"pupil_jitter","Pupil Jitter",0,{"Off","Stratified","Halton","Vogel"});
  define_int(s,"jitter_seed","Jitter Seed",0,0,2147483647,"Seed for stratified sampling.");
  define_boolean(s,"auto_seed","Auto Seed",1,"Use frame time as sampling seed.");
  define_choice(s,"pair_mode","Ghost Pair Selection",0,{"All","Only Selected","Exclude Selected"});
  define_int(s,"surface_a","Pair Surface A",0,0,256,"First zero-based reflecting optical surface.");
  define_int(s,"surface_b","Pair Surface B",1,0,256,"Second zero-based reflecting optical surface.");
  define_double(s,{"ghost_blur","Ghost Blur",0,0,.25,"Image-diagonal fraction as in Nuke."});
  define_int(s,"ghost_blur_passes","Ghost Blur Passes",3,0,8,"Separable box blur passes.");
  define_double(s,{"haze_gain","Haze Gain",0,0,1e5,"Scattered-light layer brightness."});
  define_double(s,{"haze_radius","Haze Radius",.15,0,.5,"Image-diagonal fraction."});
  define_int(s,"haze_passes","Haze Blur Passes",3,0,8,"Haze blur passes.");
  define_double(s,{"starburst_gain","Starburst Gain",0,0,1e5,"Physical Fraunhofer diffraction gain."});
  define_double(s,{"starburst_scale","Starburst Scale",.15,0,1,"Image-diagonal fraction."});
  define_choice(s,"output_mode","Output Mode",0,
    {"Composite","Flare Only","Ghost Only","Haze Only","Starburst Only","Sources Only"});
  define_boolean(s,"preview","Preview Mode",0,"Use reduced preview sample settings.");
  define_int(s,"preview_grid","Preview Ray Grid",16,2,2048,"Fast interactive quality.");
  define_int(s,"preview_max_sources","Preview Max Sources",100,1,8192,"Preview workload limit.");
  define_int(s,"preview_downsample","Preview Downsample",8,1,32,"Preview source scan resolution.");
  define_int(s,"preview_spectral","Preview Spectral Samples",3,3,11,"Preview wavelength count.");
  return kOfxStatOK;
}
OfxStatus create_instance(OfxImageEffectHandle e){
  auto p=std::make_unique<Instance>();
  if(fx->clipGetHandle(e,kSource,&p->source,nullptr)!=kOfxStatOK||
     fx->clipGetHandle(e,kOutput,&p->output,nullptr)!=kOfxStatOK||
     fx->getParamSet(e,&p->pset)!=kOfxStatOK)return kOfxStatFailed;
  OfxPropertySetHandle effect=nullptr;fx->getPropertySet(e,&effect);
  prop->propSetPointer(effect,kOfxPropInstanceData,0,p.get());p.release();
  return kOfxStatOK;
}
OfxStatus destroy_instance(OfxImageEffectHandle e){
  OfxPropertySetHandle ep=nullptr;fx->getPropertySet(e,&ep);
  delete instance(e);prop->propSetPointer(ep,kOfxPropInstanceData,0,nullptr);
  return kOfxStatOK;
}
std::vector<float> haze_layer(const std::vector<float>& rgb,int w,int h,
                             const fl::ofx::DetectionOptions& options,
                             float gain,float radius,int passes){
  std::vector<float> haze(size_t(w)*h*3,0);
  if(gain<=0)return haze;
  const int ds=std::clamp(options.downsample,1,32);
  for(int by=0;by<h;by+=ds)for(int bx=0;bx<w;bx+=ds) {
    double r=0,g=0,b=0;int count=0;
    for(int y=by;y<std::min(h,by+ds);++y)for(int x=bx;x<std::min(w,bx+ds);++x){
      size_t i=(size_t(y)*w+x)*3;r+=rgb[i];g+=rgb[i+1];b+=rgb[i+2];++count;
    }
    if(!count)continue;
    r/=count;g/=count;b/=count;
    double lum=.2126*r+.7152*g+.0722*b;
    if(options.source_cap>0 && lum>options.source_cap){
      double k=options.source_cap/lum;r*=k;g*=k;b*=k;lum=options.source_cap;
    }
    if(lum<options.threshold)continue;
    for(int y=by;y<std::min(h,by+ds);++y)for(int x=bx;x<std::min(w,bx+ds);++x){
      size_t i=(size_t(y)*w+x)*3;haze[i]=float(r);haze[i+1]=float(g);haze[i+2]=float(b);
    }
  }
  const int radius_px=int(std::round(radius*std::sqrt(double(w)*w+double(h)*h)));
  if(radius_px>0 && passes>0)fl::box_blur_rgb(haze,w,h,radius_px,passes);
  for(float& c:haze)c*=gain;
  return haze;
}
std::vector<float> starburst_layer(const std::vector<fl::Source>& sources,
                       int w,int h,int blades,float rotation,float gain,float scale,float fov_h,float fov_v){
  std::vector<float> rgb(size_t(w)*h*3,0);
  if(sources.empty()||gain<=0)return rgb;
  StarburstConfig cfg;cfg.gain=gain;cfg.scale=scale;cfg.aperture_blades=blades;
  cfg.aperture_rotation_deg=rotation;
  StarburstPSF psf; compute_starburst_psf(cfg,psf,512);
  if(psf.empty())return rgb;
  const float th=std::tan(fov_h*pi/360.f),tv=std::tan(fov_v*pi/360.f);
  std::vector<BrightPixel> converted;converted.reserve(sources.size());
  for(const auto& s:sources) {
    BrightPixel b{};
    b.angle_x=std::atan((s.x*2.f-1)*th);
    b.angle_y=std::atan((s.y*2.f-1)*tv);
    b.r=s.r;b.g=s.g;b.b=s.b;converted.push_back(b);
  }
  std::vector<float> r(size_t(w)*h),g(r.size()),b(r.size());
  render_starburst(psf,cfg,converted,th,tv,r.data(),g.data(),b.data(),w,h,w,h,0,0);
  for(size_t i=0;i<r.size();++i){rgb[3*i]=r[i];rgb[3*i+1]=g[i];rgb[3*i+2]=b[i];}
  return rgb;
}
OfxStatus render(OfxImageEffectHandle e,OfxPropertySetHandle in){
  auto* d=instance(e);if(!d)return kOfxStatErrBadHandle;
  OfxTime time=0;OfxRectI window{0,0,0,0};
  if(prop->propGetDouble(in,kOfxPropTime,0,&time)!=kOfxStatOK ||
     prop->propGetIntN(in,kOfxImageEffectPropRenderWindow,4,&window.x1)!=kOfxStatOK)return kOfxStatFailed;
  Image input,output;
  if(!input.open(d->source,time)||!output.open(d->output,time))return kOfxStatErrImageFormat;
  const int w=input.bounds.x2-input.bounds.x1,h=input.bounds.y2-input.bounds.y1;
  if(w<1||h<1||w>16384||h>16384||size_t(w)*h>size_t(16384)*16384)return kOfxStatErrImageFormat;
  if(fx->abort(e))return kOfxStatReplyDefault;
  const int mode=integer(d,"output_mode",time,0);
  const bool preview=integer(d,"preview",time,0)!=0;
  const int grid=preview?integer(d,"preview_grid",time,16):integer(d,"ray_grid",time,32);
  const int spectral=preview?integer(d,"preview_spectral",time,3):integer(d,"spectral_samples",time,3);
  fl::ofx::DetectionOptions detect;
  detect.threshold=float(number(d,"threshold",time,2));
  detect.source_cap=float(number(d,"source_cap",time,0));
  detect.downsample=preview?integer(d,"preview_downsample",time,8):integer(d,"downsample",time,4);
  detect.max_sources=preview?integer(d,"preview_max_sources",time,100):integer(d,"max_sources",time,512);
  detect.cluster_radius=integer(d,"cluster_radius",time,0);
  std::vector<float> src_rgb(size_t(w)*h*3,0);
  for(int y=0;y<h;++y){
    if((y&63)==0&&fx->abort(e))return kOfxStatReplyDefault;
    for(int x=0;x<w;++x){
      const size_t i=(size_t(y)*w+x)*3;
      for(int c=0;c<3;++c)src_rgb[i+c]=input.read(x+input.bounds.x1,y+input.bounds.y1,c);
    }
  }
  std::vector<fl::Source> sources;
  if(integer(d,"source_mode",time,0)==1){
    const auto point=point_param(d,"light_position",time);
    const float v=float(number(d,"light_radiance",time,10));
    sources.push_back({float(point[0]),float(point[1]),v,v,v});
  }else{
    sources=fl::ofx::detect_sources(src_rgb,w,h,detect);
  }
  fl::Settings settings{};
  settings.fov_h_deg=float(number(d,"fov_h",time,40));
  settings.fov_v_deg=float(number(d,"fov_v",time,0));
  settings.grid=grid;settings.spectral_samples=spectral;
  settings.max_pairs=integer(d,"max_pairs",time,40);
  settings.gain=float(number(d,"gain",time,1000));
  if(preview && integer(d,"source_mode",time,0)==0) {
    const float ds=float(std::max(1,detect.downsample));
    const float final_ds=float(std::max(1,integer(d,"downsample",time,4)));
    settings.gain*= (ds/final_ds)*(ds/final_ds);
  }
  settings.aperture_blades=integer(d,"aperture_blades",time,0);
  settings.aperture_rotation_deg=float(number(d,"aperture_rotation",time,0));
  settings.pupil_jitter=integer(d,"pupil_jitter",time,0);
  settings.jitter_seed=integer(d,"auto_seed",time,1)?
        int(std::fmod(std::abs(time),2147483647.)):integer(d,"jitter_seed",time,0);
  settings.pair_mode=integer(d,"pair_mode",time,0);
  settings.pair_surface_a=integer(d,"surface_a",time,0);
  settings.pair_surface_b=integer(d,"surface_b",time,1);
  const float fov_v=settings.fov_v_deg>0?settings.fov_v_deg:
    2.f*180.f/pi*std::atan(std::tan(settings.fov_h_deg*pi/360.f)*h/float(w));
  const bool need_ghost=mode==0||mode==1||mode==2;
  const bool need_haze=mode==0||mode==1||mode==3;
  const bool need_star=mode==0||mode==1||mode==4;
  std::vector<float> emitted(size_t(w)*h*3,0.f);
  if(need_ghost && !sources.empty()){
    fs::path path;
    const std::string custom=string_param(d,"lens_file",time);
    if(!custom.empty())path=fs::u8path(custom);
    else {
      const std::string name=string_param(d,"lens_name",time);
      // No path separators in bundled preset names. Custom files need explicit path.
      if(name.find('/')!=std::string::npos||name.find('\\')!=std::string::npos||
         name.empty()||name=="."||name=="..")return kOfxStatErrValue;
      path=lens_directory()/fs::u8path(name);
    }
    auto lens=load_lens(path);
    if(!lens)return kOfxStatFailed;
    const auto ghost=fl::render_sources_parallel(*lens,sources,settings,w,h,
                     {&abort_requested,e},4);
    if(ghost.abort_error)return kOfxStatReplyDefault;
    if(ghost.rgb.size()==emitted.size())emitted=ghost.rgb;
    const float blur=float(number(d,"ghost_blur",time,0));
    const int passes=integer(d,"ghost_blur_passes",time,3);
    if(blur>0 && passes>0)
      fl::box_blur_rgb(emitted,w,h,int(std::round(blur*std::hypot(float(w),float(h)))),passes);
  }
  if(fx->abort(e))return kOfxStatReplyDefault;
  if(need_haze && number(d,"haze_gain",time,0)>0 && integer(d,"source_mode",time,0)==0) {
    auto haze=haze_layer(src_rgb,w,h,detect,float(number(d,"haze_gain",time,0)),
        float(number(d,"haze_radius",time,.15)),integer(d,"haze_passes",time,3));
    for(size_t i=0;i<emitted.size();++i)emitted[i]+=haze[i];
  }
  if(fx->abort(e))return kOfxStatReplyDefault;
  if(need_star && number(d,"starburst_gain",time,0)>0) {
    auto star=starburst_layer(sources,w,h,settings.aperture_blades,
        settings.aperture_rotation_deg,float(number(d,"starburst_gain",time,0)),
        float(number(d,"starburst_scale",time,.15)),settings.fov_h_deg,fov_v);
    for(size_t i=0;i<emitted.size();++i)emitted[i]+=star[i];
  }
  if(mode==5) {
    std::fill(emitted.begin(),emitted.end(),0.f);
    for(const auto& s:sources){
      const int x=std::clamp(int(s.x*w),0,w-1),y=std::clamp(int(s.y*h),0,h-1);
      const size_t i=(size_t(y)*w+x)*3;
      emitted[i]+=s.r;emitted[i+1]+=s.g;emitted[i+2]+=s.b;
    }
  }
  // Non-tile effect: image dimensions are the full source image.
  // Write *only* the requested render window, respecting signed row strides
  // and potentially different input/output bounds.
  const int x1=std::max(window.x1,output.bounds.x1);
  const int y1=std::max(window.y1,output.bounds.y1);
  const int x2=std::min(window.x2,output.bounds.x2);
  const int y2=std::min(window.y2,output.bounds.y2);
  for(int y=y1;y<y2;++y){
    if((y&63)==0&&fx->abort(e))return kOfxStatReplyDefault;
    for(int x=x1;x<x2;++x){
      const int ix=x-input.bounds.x1,iy=y-input.bounds.y1;
      if(ix<0||iy<0||ix>=w||iy>=h)continue;
      const size_t index=(size_t(iy)*w+ix)*3;
      for(int c=0;c<3;++c){
        const float base=mode==0?input.read(x,y,c):0;
        output.write(x,y,c,base+emitted[index+c]);
      }
      if(output.channels==4){
        const float base_alpha=std::clamp(input.read(x,y,3),0.f,1.f);
        const float peak=std::max({0.f,emitted[index],emitted[index+1],emitted[index+2]});
        const float alpha=base_alpha+(1.f-base_alpha)*std::clamp(peak,0.f,1.f);
        output.write(x,y,3,alpha);
      }
    }
  }
  return kOfxStatOK;
}
OfxStatus load(){
  if(!host||!host->fetchSuite)return kOfxStatFailed;
  fx=(OfxImageEffectSuiteV1*)host->fetchSuite(host->host,kOfxImageEffectSuite,1);
  prop=(OfxPropertySuiteV1*)host->fetchSuite(host->host,kOfxPropertySuite,1);
  params=(OfxParameterSuiteV1*)host->fetchSuite(host->host,kOfxParameterSuite,1);
  return fx&&prop&&params?kOfxStatOK:kOfxStatFailed;
}
OfxStatus main_entry(const char* action,const void* raw,
                     OfxPropertySetHandle in,OfxPropertySetHandle){
  try{
    auto e=(OfxImageEffectHandle)raw;
    if(strcmp(action,kOfxActionLoad)==0)return load();
    if(strcmp(action,kOfxActionUnload)==0){fx=nullptr;prop=nullptr;params=nullptr;return kOfxStatOK;}
    if(!fx||!prop||!params)return kOfxStatFailed;
    if(strcmp(action,kOfxActionDescribe)==0)return describe(e);
    if(strcmp(action,kOfxImageEffectActionDescribeInContext)==0)return describe_context(e);
    if(strcmp(action,kOfxActionCreateInstance)==0)return create_instance(e);
    if(strcmp(action,kOfxActionDestroyInstance)==0)return destroy_instance(e);
    if(strcmp(action,kOfxImageEffectActionRender)==0)return render(e,in);
    return kOfxStatReplyDefault;
  }catch(const std::bad_alloc&){return kOfxStatErrMemory;}
   catch(const std::exception&){return kOfxStatFailed;}
   catch(...){return kOfxStatFailed;}
}
void set_host(OfxHost* h){host=h;}
OfxPlugin plugin={kOfxImageEffectPluginApi,1,ID,1,0,&set_host,&main_entry};
}
extern "C" {
#ifdef _WIN32
__declspec(dllexport)
#else
__attribute__((visibility("default")))
#endif
int OfxGetNumberOfPlugins(){return 1;}
#ifdef _WIN32
__declspec(dllexport)
#else
__attribute__((visibility("default")))
#endif
OfxPlugin* OfxGetPlugin(int i){return i==0?&plugin:nullptr;}
}
