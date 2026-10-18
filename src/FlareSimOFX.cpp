// FlareSimOFX: host-neutral OpenFX Image Effect adapter.
// Optical equations and lens files: LocalStarlight/flaresim_nuke.
// CPU implementation and sampling optimisations: ken-eizo/flaresim_ae.
#include "ofxImageEffect.h"
#include "ofxParam.h"
#include "ofxProperty.h"
#include "FlareLensCore.hpp"
#include "FlareLensOptics.hpp"
#include "FlareLensCpuParallel.hpp"
#include "FlareSimGpu.hpp"
#if defined(FLARESIM_HAVE_OVERLAY)
#include "FlareSimOverlay.hpp"
#endif
#include "SourceDetection.hpp"
#include "FlareSimLensOptions.hpp"
#include "starburst.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
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
constexpr const char* kMask="Mask";
constexpr float pi=3.14159265358979323846f;
constexpr const char* kLensDefault="AI_Nikkor_50mm_F1.2S.lens";
constexpr const char* kPresetFiles[]{
  "AI_Nikkor_50mm_F1.2S.lens",
  "Canon_50mm_F1.2_(Canon_FD50mm_F1.2L).lens",
  "Leica_50mm_F1.4_(Leica_Summilux_50mm_F1.4).lens",
  "Zeiss_50mm_F1.3_(Arri_Zeiss_Master_Prime_T1.3_50mm).lens",
  "Sony_50mm_F1.45_IF_(Sony_Planar_T_FE_50mm_F1.4_ZA).lens",
  "Cosina_50mm_f1_(Cosina_Voigtlander_Nokton_50mm_F1.0_Aspherical).lens"
};

struct Instance {
  OfxImageClipHandle source=nullptr, output=nullptr, mask=nullptr;
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
             comp&&strcmp(comp,kOfxImageComponentRGB)==0?3:
             comp&&strcmp(comp,kOfxImageComponentAlpha)==0?1:0;
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
const std::vector<std::string>& all_lens_files(){
  static const std::vector<std::string> entries=[]{
    std::vector<std::string> result;
    std::error_code ec;
    const auto directory=lens_directory();
    for(fs::directory_iterator it(directory,ec),end;!ec&&it!=end;it.increment(ec)){
      if(!it->is_regular_file(ec)||ec)continue;
      if(it->path().extension()==".lens")
        result.push_back(it->path().filename().string());
    }
    std::sort(result.begin(),result.end());
    return result;
  }();
  return entries;
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
OfxPropertySetHandle define_double(OfxParamSetHandle set,const ParamDef& d){
  OfxPropertySetHandle p=nullptr;
  if(params->paramDefine(set,kOfxParamTypeDouble,d.name,&p)!=kOfxStatOK)throw std::runtime_error("paramDefine");
  prop->propSetString(p,kOfxPropLabel,0,d.label);
  prop->propSetDouble(p,kOfxParamPropDefault,0,d.default_value);
  prop->propSetDouble(p,kOfxParamPropMin,0,d.min_value);
  prop->propSetDouble(p,kOfxParamPropMax,0,d.max_value);
  prop->propSetDouble(p,kOfxParamPropDisplayMin,0,d.min_value);
  prop->propSetDouble(p,kOfxParamPropDisplayMax,0,d.max_value);
  prop->propSetString(p,kOfxParamPropHint,0,d.hint);
  return p;
}
OfxPropertySetHandle define_int(OfxParamSetHandle set,const char* name,const char* label,int def,int min,int max,const char* hint){
  OfxPropertySetHandle p=nullptr;
  if(params->paramDefine(set,kOfxParamTypeInteger,name,&p)!=kOfxStatOK)throw std::runtime_error("paramDefine");
  prop->propSetString(p,kOfxPropLabel,0,label);
  prop->propSetInt(p,kOfxParamPropDefault,0,def);
  prop->propSetInt(p,kOfxParamPropMin,0,min);
  prop->propSetInt(p,kOfxParamPropMax,0,max);
  prop->propSetString(p,kOfxParamPropHint,0,hint);
  return p;
}
OfxPropertySetHandle define_boolean(OfxParamSetHandle set,const char* name,const char* label,int def,const char* hint){
  OfxPropertySetHandle p=nullptr;
  params->paramDefine(set,kOfxParamTypeBoolean,name,&p);
  prop->propSetString(p,kOfxPropLabel,0,label);
  prop->propSetInt(p,kOfxParamPropDefault,0,def);
  prop->propSetString(p,kOfxParamPropHint,0,hint);
  return p;
}
OfxPropertySetHandle define_choice(OfxParamSetHandle set,const char* name,const char* label,
                   int def,std::initializer_list<const char*> entries){
  OfxPropertySetHandle p=nullptr;
  params->paramDefine(set,kOfxParamTypeChoice,name,&p);
  prop->propSetString(p,kOfxPropLabel,0,label);
  prop->propSetInt(p,kOfxParamPropDefault,0,def);
  int x=0;for(const auto* e:entries)prop->propSetString(p,kOfxParamPropChoiceOption,x++,e);
  return p;
}
OfxPropertySetHandle define_string(OfxParamSetHandle set,const char* name,const char* label,const char* default_value,const char* hint){
  OfxPropertySetHandle p=nullptr;
  params->paramDefine(set,kOfxParamTypeString,name,&p);
  prop->propSetString(p,kOfxPropLabel,0,label);
  prop->propSetString(p,kOfxParamPropDefault,0,default_value);
  prop->propSetString(p,kOfxParamPropHint,0,hint);
  return p;
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
#if defined(FLARESIM_HAVE_OVERLAY)
  // OFX V1 standard overlay. Only advertise when the host supplies its
  // interact suite; unsupported hosts retain numeric Light Position controls.
  if(fl::overlay::supported())
    prop->propSetPointer(p,kOfxImageEffectPluginPropOverlayInteractV1,
                        0,reinterpret_cast<void*>(&fl::overlay::entry));
  if(fl::overlay::supports_v2())
    prop->propSetPointer(p,kOfxImageEffectPluginPropOverlayInteractV2,
                        0,reinterpret_cast<void*>(&fl::overlay::entry_v2));
#endif
  return kOfxStatOK;
}
OfxStatus describe_context(OfxImageEffectHandle e){
  OfxPropertySetHandle p=nullptr;
  for(const auto* clip_name:{kSource,kOutput}){
    if(fx->clipDefine(e,clip_name,&p)!=kOfxStatOK)return kOfxStatFailed;
    prop->propSetString(p,kOfxImageEffectPropSupportedComponents,0,kOfxImageComponentRGBA);
    prop->propSetString(p,kOfxImageEffectPropSupportedComponents,1,kOfxImageComponentRGB);
  }
  if(fx->clipDefine(e,kMask,&p)==kOfxStatOK&&p){
    prop->propSetString(p,kOfxImageEffectPropSupportedComponents,0,kOfxImageComponentAlpha);
    prop->propSetInt(p,kOfxImageClipPropOptional,0,1);
  }
  OfxParamSetHandle s=nullptr;
  if(fx->getParamSet(e,&s)!=kOfxStatOK)return kOfxStatFailed;

  // A single visible control, implemented with the standard OpenFX Choice.
  // Enumerate the bundled lens directory at plugin description time. OFX
  // choices are static for the lifetime of a host descriptor, so restart
  // the host after adding another .lens file.
  auto* preset_props=define_choice(s,"lens_preset","Lens File",0,{
    "None","AI_Nikkor_50mm_F1.2S.lens",
    "Canon_50mm_F1.2_(Canon_FD50mm_F1.2L).lens",
    "Leica_50mm_F1.4_(Leica_Summilux_50mm_F1.4).lens",
    "Zeiss_50mm_F1.3_(Arri_Zeiss_Master_Prime_T1.3_50mm).lens",
    "Sony_50mm_F1.45_IF_(Sony_Planar_T_FE_50mm_F1.4_ZA).lens",
    "Cosina_50mm_f1_(Cosina_Voigtlander_Nokton_50mm_F1.0_Aspherical).lens"
  });
  // The first six positions preserve existing preview projects.
  const auto& lenses=all_lens_files();
  int catalog_index=7;
  for(const auto& filename:lenses)
    if(std::find(std::begin(kPresetFiles),std::end(kPresetFiles),filename)==std::end(kPresetFiles))
      prop->propSetString(preset_props,kOfxParamPropChoiceOption,catalog_index++,filename.c_str());

  // Old file-path parameter remains *hidden* solely for saved project
  // compatibility. There is only one visible lens selector.
  auto* old_file=define_string(s,"lens_file","Legacy Lens Path","",
       "Legacy project compatibility");
  prop->propSetInt(old_file,kOfxParamPropSecret,0,1);
  OfxParamHandle unused_handle=nullptr;

  // Nuke knob sections, defined in the same order as FlareSim.cpp.
  auto group=[&](const char* name,const char* label,bool opened){
    OfxPropertySetHandle g=nullptr;
    if(params->paramDefine(s,kOfxParamTypeGroup,name,&g)==kOfxStatOK&&g){
      prop->propSetString(g,kOfxPropLabel,0,label);
      prop->propSetInt(g,kOfxParamPropGroupOpen,0,opened?1:0);
    }
  };
  auto grouped=[&](OfxPropertySetHandle descriptor,const char* groupName){
    if(descriptor)
      prop->propSetString(descriptor,kOfxParamPropParent,0,groupName);
  };

  group("nuke_ghost","Ghost",true);
  grouped(define_double(s,{"gain","Flare Gain",1000,0,1e7,"Nuke ghost intensity multiplier."}),"nuke_ghost");
  grouped(define_int(s,"ray_grid","Ray Grid (NxN)",64,2,2048,"Nuke entrance-pupil sample grid."),"nuke_ghost");
  // Golden Angle (Vogel) is the optional AE extension, appended after the
  // original Nuke modes; original mode indices and default Off remain intact.
  grouped(define_choice(s,"pupil_jitter","Pupil Jitter",0,
                {"Off","Stratified","Halton","Golden Angle (Vogel)"}),"nuke_ghost");
  grouped(define_int(s,"jitter_seed","Jitter Seed",0,0,2147483647,"Nuke fixed stratified jitter seed."),"nuke_ghost");
  grouped(define_boolean(s,"auto_seed","Auto Seed",1,"Seed from the current frame."),"nuke_ghost");
  grouped(define_double(s,{"threshold","Threshold",2,0,1e6,"Minimum source luminance in scene-linear RGB."}),"nuke_ghost");
  grouped(define_double(s,{"source_cap","Source Cap",0,0,1e6,"0 means no cap."}),"nuke_ghost");
  grouped(define_int(s,"max_sources","Max Sources",512,0,8192,"0 means unlimited up to a safety cap."),"nuke_ghost");
  grouped(define_int(s,"downsample","Downsample",4,1,32,"Nuke block source detection stride."),"nuke_ghost");
  grouped(define_int(s,"cluster_radius","Cluster Radius",0,0,256,"Merge nearby bright sources in pixels."),"nuke_ghost");

  group("nuke_preview","Preview Mode",false);
  grouped(define_boolean(s,"preview","Enable Preview Mode",0,"Temporarily override quality controls."),"nuke_preview");
  grouped(define_int(s,"preview_grid","Ray Grid (NxN)",16,2,2048,"Preview ray grid."),"nuke_preview");
  grouped(define_int(s,"preview_max_sources","Max Sources",100,0,8192,"Preview source count."),"nuke_preview");
  grouped(define_int(s,"preview_downsample","Downsample",8,1,32,"Preview source stride."),"nuke_preview");
  grouped(define_choice(s,"preview_spectral","Spectral Samples",0,{"3 (R/G/B)","5","7","9","11"}),"nuke_preview");

  group("nuke_camera","Camera",false);
  grouped(define_boolean(s,"fov_use_sensor","Use Sensor Size",0,"Compute FOV using focal length and sensor size."),"nuke_camera");
  grouped(define_choice(s,"sensor_preset","Sensor Preset",0,{"Custom","Full Frame","Super 35","APS-C Canon","APS-C Nikon/Sony","Micro Four Thirds"}),"nuke_camera");
  grouped(define_double(s,{"fov_h","FOV H (deg)",40,1,180,"Nuke horizontal field of view."}),"nuke_camera");
  grouped(define_boolean(s,"fov_auto_v","Auto FOV V",1,"Derive vertical FOV from horizontal FOV and aspect ratio."),"nuke_camera");
  grouped(define_double(s,{"fov_v","FOV V (deg)",24,1,180,"Manual vertical FOV, used when Auto FOV V is disabled."}),"nuke_camera");
  grouped(define_double(s,{"sensor_w","Sensor Width (mm)",36,1,100,"Sensor width."}),"nuke_camera");
  grouped(define_double(s,{"sensor_h","Sensor Height (mm)",24,1,100,"Sensor height."}),"nuke_camera");
  grouped(define_double(s,{"focal_length","Focal Length (mm)",50,1,2000,"Lens focal length."}),"nuke_camera");

  group("nuke_aperture","Aperture",false);
  grouped(define_int(s,"aperture_blades","Aperture Blades",0,0,16,"0 round; 3..16 polygon."),"nuke_aperture");
  grouped(define_double(s,{"aperture_rotation","Aperture Rotation",0,-180,180,"Degrees."}),"nuke_aperture");

  group("nuke_spectral","Spectral",false);
  grouped(define_choice(s,"nuke_spectral_quality","Spectral Samples",0,{"3 (R/G/B)","5","7","9","11"}),"nuke_spectral");

  group("nuke_post","Post-process",false);
  grouped(define_double(s,{"ghost_blur","Ghost Blur",.003,0,.25,"Nuke blur as a fraction of frame diagonal."}),"nuke_post");
  grouped(define_int(s,"ghost_blur_passes","Ghost Blur Passes",3,0,8,"Nuke separable box blur passes."),"nuke_post");
  grouped(define_double(s,{"haze_gain","Haze Gain",0,0,1e5,"Veiling glare intensity."}),"nuke_post");
  grouped(define_double(s,{"haze_radius","Haze Radius",.15,0,.5,"Diagonal fraction."}),"nuke_post");
  grouped(define_int(s,"haze_passes","Haze Blur Passes",3,0,8,"Nuke box blur passes."),"nuke_post");

  group("nuke_star","Starburst",false);
  grouped(define_double(s,{"starburst_gain","Starburst Gain",0,0,1e5,"Fraunhofer diffraction intensity."}),"nuke_star");
  grouped(define_double(s,{"starburst_scale","Starburst Scale",.15,0,1,"Diffraction radius relative to diagonal."}),"nuke_star");

  group("nuke_diagnostics","Diagnostics",false);
  // Source diagnostics are selected using Output Mode > Sources Only.

  group("nuke_output","Output",true);
  // Nuke writes flare/source/haze/starburst to arbitrary named channels.
  // Standard OFX cannot promise this across every host; expose the mapped
  // RGB outputs explicitly, and keep v0.1 preview values stable.
  // Portable output does not offer arbitrary Nuke named image channels.
  // Use original Nuke labels where the effect can actually deliver them.
  grouped(define_choice(s,"output_format","Output Mode",0,{
    "Flare as RGB","Sources Only","Composite"
  }),"nuke_output");
  // Keep old preview output mode hidden to preserve saved parameter IDs.
  auto* old_output=define_choice(s,"output_mode","Legacy Output Mode",0,{
    "Composite","Flare Only","Ghost Only","Haze Only","Starburst Only","Sources Only"
  });
  prop->propSetInt(old_output,kOfxParamPropSecret,0,1);

  group("nuke_pairs","Pairs",false);
  // The existing OFX single-pair selection remains optional; Nuke's full
  // list of on/off toggles requires a separate host-independent port.
  grouped(define_choice(s,"pair_mode","Pair Selection",0,{"All","Only Selected","Exclude Selected"}),"nuke_pairs");
  grouped(define_int(s,"surface_a","Pair Surface A",0,0,256,"Zero-based first optical surface."),"nuke_pairs");
  grouped(define_int(s,"surface_b","Pair Surface B",1,0,256,"Zero-based second optical surface."),"nuke_pairs");

  group("additional","Additional Controls",false);

  grouped(define_choice(s, "source_mode","Light Sources",0,{"Detect Highlights","Manual Position"}),"additional");
  OfxPropertySetHandle q=nullptr;
  params->paramDefine(s,kOfxParamTypeDouble2D,"light_position",&q);
  prop->propSetString(q,kOfxPropLabel,0,"Light Position");
  prop->propSetDouble(q,kOfxParamPropDefault,0,.5);
  prop->propSetDouble(q,kOfxParamPropDefault,1,.5);
  prop->propSetString(q,kOfxParamPropParent,0,"additional");
  grouped(define_double(s,{"light_radiance","Manual Radiance",10,0,1e6,"Intensity of the OFX-only manual source."}),"additional");
  grouped(define_choice(s,"render_backend","Optical Renderer",0,{"Auto","CPU","GPU"}),"additional");
  grouped(define_int(s,"max_pairs","Max Ghost Pairs",65535,1,65535,"OFX resource/safety limit; unlike Nuke's individually enabled pair checkboxes."),"additional");

  // Preserve historical preview parameter identifiers, but keep the two
  // synonymous, obsolete fields out of the canonical Nuke UI.
  auto* legacy=define_string(s,"lens_name","Legacy Lens Name",kLensDefault,
       "Old preset text retained for previous preview projects.");
  prop->propSetInt(legacy,kOfxParamPropSecret,0,1);
  legacy=define_int(s,"spectral_samples","Legacy Spectral Samples",3,3,11,"Legacy.");
  prop->propSetInt(legacy,kOfxParamPropSecret,0,1);
  legacy=define_int(s,"preview_spectral_legacy","Legacy Preview Spectral",3,3,11,"Legacy.");
  prop->propSetInt(legacy,kOfxParamPropSecret,0,1);

  return kOfxStatOK;
}
OfxStatus create_instance(OfxImageEffectHandle e){
  auto p=std::make_unique<Instance>();
  if(fx->clipGetHandle(e,kSource,&p->source,nullptr)!=kOfxStatOK||
     fx->clipGetHandle(e,kOutput,&p->output,nullptr)!=kOfxStatOK||
     fx->getParamSet(e,&p->pset)!=kOfxStatOK)return kOfxStatFailed;
  // A host may decline to define the optional mask clip; that is not fatal.
  fx->clipGetHandle(e,kMask,&p->mask,nullptr);
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
                             float gain,float radius,int passes,
                             const std::vector<float>* mask){
  std::vector<float> haze(size_t(w)*h*3,0);
  if(gain<=0)return haze;
  const int ds=std::clamp(options.downsample,1,32);
  const int dw=std::max(1,w/ds),dh=std::max(1,h/ds);
  for(int byidx=0;byidx<dh;++byidx)for(int bxidx=0;bxidx<dw;++bxidx) {
    const int by=byidx*ds,bx=bxidx*ds;
    const int readY=byidx==dh-1?h:std::min(h,by+ds);
    const int readX=bxidx==dw-1?w:std::min(w,bx+ds);
    double r=0,g=0,b=0,mask_sum=0;int count=0;
    for(int y=by;y<readY;++y)for(int x=bx;x<readX;++x){
      size_t i=(size_t(y)*w+x)*3;r+=rgb[i];g+=rgb[i+1];b+=rgb[i+2];
      mask_sum+=mask?std::clamp((*mask)[size_t(y)*w+x],0.f,1.f):1.0f;
      ++count;
    }
    if(!count)continue;
    r/=count;g/=count;b/=count;
    double lum=.2126*r+.7152*g+.0722*b;
    if(options.source_cap>0 && lum>options.source_cap){
      double k=options.source_cap/lum;r*=k;g*=k;b*=k;lum=options.source_cap;
    }
    const double mask_weight=mask_sum/count;
    r*=mask_weight;g*=mask_weight;b*=mask_weight;lum*=mask_weight;
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
  const int mode=integer(d,"output_format",time,0);
  // 0 = Flare as RGB (Nuke), 1 = Sources Only (Nuke), 2 = Composite
  const bool sources_only=mode==1;
  const bool composite=mode==2;
  // At the Nuke-default initial state no optical prescription has been
  // selected. Short circuit before image reads, large scratch allocations,
  // source scanning or CUDA initialization.
  const bool no_lens=integer(d,"lens_preset",time,0)==0 &&
       string_param(d,"lens_file",time).empty() &&
       string_param(d,"lens_name",time)==kLensDefault;
  if(!sources_only && no_lens &&
     number(d,"haze_gain",time,0)==0 &&
     number(d,"starburst_gain",time,0)==0){
    const int x1=std::max(window.x1,output.bounds.x1);
    const int y1=std::max(window.y1,output.bounds.y1);
    const int x2=std::min(window.x2,output.bounds.x2);
    const int y2=std::min(window.y2,output.bounds.y2);
    for(int y=y1;y<y2;++y){
      if((y&63)==0&&fx->abort(e))return kOfxStatReplyDefault;
      for(int x=x1;x<x2;++x){
        for(int c=0;c<std::min(3,output.channels);++c)
          output.write(x,y,c,composite?input.read(x,y,c):0.f);
        if(output.channels==4)
          output.write(x,y,3,input.read(x,y,3));
      }
    }
    return kOfxStatOK;
  }
  // Sources Only is diagnostic: no lens, CUDA, PSF, 3-channel frame
  // allocation or ray tracing should ever run here. Match the original
  // Nuke block-average source threshold (including optional alpha mask).
  if(sources_only) {
    const bool manual=integer(d,"source_mode",time,0)==1;
    const int ds=std::clamp(integer(d,"preview",time,0)!=0?
      integer(d,"preview_downsample",time,8):integer(d,"downsample",time,4),1,32);
    const float threshold=float(number(d,"threshold",time,2));
    const float cap=float(number(d,"source_cap",time,0));
    const int max_sources=integer(d,"preview",time,0)!=0?
      integer(d,"preview_max_sources",time,100):integer(d,"max_sources",time,512);
    const int dw=std::max(1,w/ds),dh=std::max(1,h/ds);
    struct Block {int x0,y0;float r,g,b,lum;};
    std::vector<Block> blocks;
    if(manual){
      const auto point=point_param(d,"light_position",time);
      const float rad=float(number(d,"light_radiance",time,10));
      blocks.push_back({std::clamp(int(point[0]*w),0,w-1),
                        std::clamp(int(point[1]*h),0,h-1),rad,rad,rad,rad});
    }else{
      Image mask;
      bool use_mask=false;
      if(d->mask && fx->clipGetPropertySet){
        OfxPropertySetHandle mp=nullptr;int connected=0;
        if(fx->clipGetPropertySet(d->mask,&mp)==kOfxStatOK&&mp &&
           prop->propGetInt(mp,kOfxImageClipPropConnected,0,&connected)==kOfxStatOK&&connected)
          use_mask=mask.open(d->mask,time);
      }
      blocks.reserve(size_t(std::min(dw*dh,std::max(1,max_sources))));
      for(int by=0;by<dh;++by){
        if((by&15)==0&&fx->abort(e))return kOfxStatReplyDefault;
        for(int bx=0;bx<dw;++bx){
          const int x0=bx*ds,y0=by*ds;
          const int xend=bx==dw-1?w:std::min(w,x0+ds);
          const int yend=by==dh-1?h:std::min(h,y0+ds);
          double rr=0,gg=0,bb=0,aa=0;int count=0;
          for(int y=y0;y<yend;++y)for(int x=x0;x<xend;++x){
            const int px=x+input.bounds.x1,py=y+input.bounds.y1;
            rr+=input.read(px,py,0);
            gg+=input.read(px,py,1);
            bb+=input.read(px,py,2);
            aa+=use_mask?mask.read(px,py,mask.channels==4?3:0):1.;
            ++count;
          }
          if(!count)continue;
          float r=float(rr/count),g=float(gg/count),b=float(bb/count);
          float lum=.2126f*r+.7152f*g+.0722f*b;
          if(cap>0&&lum>cap){const float k=cap/lum;r*=k;g*=k;b*=k;lum=cap;}
          const float opacity=float(aa/count);
          r*=opacity;g*=opacity;b*=opacity;lum*=opacity;
          if(lum<threshold)continue;
          blocks.push_back({x0,y0,r,g,b,lum});
        }
      }
      const int limit=max_sources>0?max_sources:8192;
      if(blocks.size()>size_t(limit)){
        std::partial_sort(blocks.begin(),blocks.begin()+limit,blocks.end(),
          [](const Block&a,const Block&b){return a.lum>b.lum;});
        blocks.resize(limit);
      }
    }
    // Only write the requested render window; avoid frame-sized scratch
    // buffers. The source map is rendered as nominal ds-by-ds rectangles.
    const int x1=std::max(window.x1,output.bounds.x1);
    const int y1=std::max(window.y1,output.bounds.y1);
    const int x2=std::min(window.x2,output.bounds.x2);
    const int y2=std::min(window.y2,output.bounds.y2);
    for(int y=y1;y<y2;++y){
      if((y&63)==0&&fx->abort(e))return kOfxStatReplyDefault;
      for(int x=x1;x<x2;++x){
        output.write(x,y,0,0);output.write(x,y,1,0);output.write(x,y,2,0);
        if(output.channels==4)output.write(x,y,3,input.read(x,y,3));
      }
    }
    for(const auto& block:blocks){
      const int bx0=block.x0+input.bounds.x1,by0=block.y0+input.bounds.y1;
      for(int y=std::max(y1,by0);y<std::min(y2,by0+ds);++y)
        for(int x=std::max(x1,bx0);x<std::min(x2,bx0+ds);++x){
          output.write(x,y,0,block.r);
          output.write(x,y,1,block.g);
          output.write(x,y,2,block.b);
        }
    }    return kOfxStatOK;
  }
  const bool preview=integer(d,"preview",time,0)!=0;
  const int grid=preview?integer(d,"preview_grid",time,16):integer(d,"ray_grid",time,64);
  const int spectral=3+2*std::clamp(preview?integer(d,"preview_spectral",time,0):integer(d,"nuke_spectral_quality",time,0),0,4);
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
  std::vector<float> mask_values;
  if(d->mask && fx->clipGetPropertySet) {
    OfxPropertySetHandle mp=nullptr;
    if(fx->clipGetPropertySet(d->mask,&mp)==kOfxStatOK&&mp){
      int connected=0;
      if(prop->propGetInt(mp,kOfxImageClipPropConnected,0,&connected)==kOfxStatOK&&connected){
        Image mask;
        if(mask.open(d->mask,time)){
          mask_values.resize(size_t(w)*h);
          const int chan=mask.channels==4?3:0;
          for(int y=0;y<h;++y)for(int x=0;x<w;++x)
            mask_values[size_t(y)*w+x]=mask.read(x+input.bounds.x1,y+input.bounds.y1,chan);
        }
      }
    }
  }
  fl::Settings settings{};
  settings.fov_h_deg=float(number(d,"fov_h",time,40));
  settings.fov_v_deg=float(number(d,"fov_v",time,24));
  // Nuke's sensor mode computes optical FOV from physical sensor dimensions.
  const int preset=integer(d,"sensor_preset",time,0);
  const float sensorW[]={0.f,36.f,24.89f,22.3f,23.5f,17.3f};
  const float sensorH[]={0.f,24.f,18.66f,14.9f,15.6f,13.f};
  const float sw=preset>=1&&preset<=5?sensorW[preset]:
                    float(number(d,"sensor_w",time,36));
  const float sh=preset>=1&&preset<=5?sensorH[preset]:
                    float(number(d,"sensor_h",time,24));
  if(integer(d,"fov_use_sensor",time,0)!=0){
    const float f=std::max(.1f,float(number(d,"focal_length",time,50)));
    settings.fov_h_deg=2.f*180.f/pi*std::atan(sw/(2*f));
    settings.fov_v_deg=2.f*180.f/pi*std::atan(sh/(2*f));
  }else if(integer(d,"fov_auto_v",time,1)!=0){
    settings.fov_v_deg=2.f*180.f/pi*std::atan(
      std::tan(settings.fov_h_deg*pi/360.f)*float(h)/float(w));
  }
  settings.grid=grid;settings.spectral_samples=spectral;
  settings.max_pairs=integer(d,"max_pairs",time,65535);
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
        int(std::clamp(time,double(std::numeric_limits<int>::min()),
                             double(std::numeric_limits<int>::max()))):
        integer(d,"jitter_seed",time,0);
  settings.pair_mode=integer(d,"pair_mode",time,0);
  settings.pair_surface_a=integer(d,"surface_a",time,0);
  settings.pair_surface_b=integer(d,"surface_b",time,1);
  detect.fov_h_deg=settings.fov_h_deg;
  detect.fov_v_deg=settings.fov_v_deg;
  std::vector<fl::Source> sources;
  if(integer(d,"source_mode",time,0)==1){
    const auto point=point_param(d,"light_position",time);
    const float v=float(number(d,"light_radiance",time,10));
    sources.push_back({float(point[0]),float(point[1]),v,v,v});
  }else{
    sources=fl::ofx::detect_sources(src_rgb,w,h,detect,
           mask_values.empty()?nullptr:&mask_values);
  }
  const float fov_v=settings.fov_v_deg>0?settings.fov_v_deg:
    2.f*180.f/pi*std::atan(std::tan(settings.fov_h_deg*pi/360.f)*h/float(w));
  const bool need_ghost=!sources_only;
  const bool need_haze=!sources_only;
  const bool need_star=!sources_only;
  std::vector<float> emitted(size_t(w)*h*3,0.f);
  if(need_ghost && !sources.empty()){
    fs::path path;
    const std::string custom=string_param(d,"lens_file",time);
    if(!custom.empty()){
      path=fs::u8path(custom);
      // Standard file-string widgets are not guaranteed to expose a browse
      // button. Accept the exact bundled filename as a convenient fallback,
      // without guessing a different lens for failed explicit paths.
      if(!path.has_parent_path() && path.filename()==path &&
         path!=".." && path!="." && path.extension()==".lens")
        path=lens_directory()/path;
    }
    else {
      const int preset=integer(d,"lens_preset",time,0);
      const std::string legacy=string_param(d,"lens_name",time);
      // Preserve previous preview projects that manually entered an exact name.
      // No prescription selected is Nuke's true initial state. This is
      // distinct from a missing or corrupt explicit lens file.
      if(preset==0 && legacy==kLensDefault) {
        path.clear();
      }else{
        std::string name=legacy!=kLensDefault && preset==0?
          legacy:preset>=1&&preset<=int(std::size(kPresetFiles))?
          kPresetFiles[preset-1]:"";
        if(name.empty()&&preset>=7){
          int index=7;
          for(const auto& candidate:all_lens_files()){
            if(std::find(std::begin(kPresetFiles),std::end(kPresetFiles),candidate)!=std::end(kPresetFiles))
              continue;
            if(index++==preset){name=candidate;break;}
          }
        }
        if(name.find('/')!=std::string::npos||name.find('\\')!=std::string::npos||
           name.empty()||name=="."||name=="..")return kOfxStatErrValue;
        path=lens_directory()/fs::u8path(name);
      }
    }
    auto lens=path.empty()?std::shared_ptr<const LensSystem>{}:load_lens(path);
    if(!lens && !path.empty())
      std::fprintf(stderr,"FlareSim: failed to load lens: %s\n",path.string().c_str());
    if(lens){
    const float blur=float(number(d,"ghost_blur",time,.003));
    const int passes=integer(d,"ghost_blur_passes",time,3);
    const int backend=integer(d,"render_backend",time,0);
    bool gpu_rendered=false;
    bool nuke_reference_used=false;
    if(backend!=1 && fl::ofxgpu::available()) {
      // Faithful default: compile and call the UNMODIFIED upstream Nuke
      // ghost_cuda.cu from an isolated DLL on CUDA hosts. AE Golden Angle
      // (method 3) deliberately uses the extended GPU sampler instead.
      if(settings.pupil_jitter<=2)
        gpu_rendered=nuke_reference_used=fl::ofxgpu::render_nuke_reference(
           path.string(),sources,settings,w,h,blur,passes,emitted);
      // Golden Angle (AE extension) deliberately uses the fast GPU kernel.
      // For Nuke Off/Stratified/Halton, Auto falls back to the closer CPU
      // optical path if the EXACT original Nuke CUDA reference is absent.
      // This preserves predictable fidelity on macOS/CPU-only OFX hosts;
      // users may select GPU (Fast) explicitly to prefer CUDA/Metal speed.
      if(!gpu_rendered && (backend==2 || settings.pupil_jitter==3))
        gpu_rendered=fl::ofxgpu::render_ghosts(*lens,sources,settings,w,h,blur,passes,emitted);
    }
    if(std::getenv("FLARESIM_GPU_TRACE"))
      std::fprintf(stderr,"FlareSimOFX optical renderer: %s (%zu sources, grid %d)\n",
                   nuke_reference_used?"Nuke original CUDA":
                   gpu_rendered?"AE extended GPU":"CPU",
                   sources.size(),settings.grid);
    if(!gpu_rendered){
      const auto ghost=fl::render_sources_parallel(*lens,sources,settings,w,h,
                       {&abort_requested,e},4);
      if(ghost.abort_error)return kOfxStatReplyDefault;
      if(ghost.rgb.size()==emitted.size())emitted=ghost.rgb;
      if(blur>0 && passes>0)
        fl::box_blur_rgb(emitted,w,h,int(std::round(blur*std::hypot(float(w),float(h)))),passes);
    }
    } // lens selected
  }
  if(fx->abort(e))return kOfxStatReplyDefault;
  if(need_haze && number(d,"haze_gain",time,0)>0 && integer(d,"source_mode",time,0)==0) {
    auto haze=haze_layer(src_rgb,w,h,detect,float(number(d,"haze_gain",time,0)),
        float(number(d,"haze_radius",time,.15)),integer(d,"haze_passes",time,3),
        mask_values.empty()?nullptr:&mask_values);
    for(size_t i=0;i<emitted.size();++i)emitted[i]+=haze[i];
  }
  if(fx->abort(e))return kOfxStatReplyDefault;
  if(need_star && number(d,"starburst_gain",time,0)>0) {
    auto star=starburst_layer(sources,w,h,settings.aperture_blades,
        settings.aperture_rotation_deg,float(number(d,"starburst_gain",time,0)),
        float(number(d,"starburst_scale",time,.15)),settings.fov_h_deg,fov_v);
    for(size_t i=0;i<emitted.size();++i)emitted[i]+=star[i];
  }
  if(sources_only) {
    std::fill(emitted.begin(),emitted.end(),0.f);
    const int ds=std::clamp(detect.downsample,1,32);
    for(const auto& s:sources){
      const int cx=int(s.x*w),cy=int(s.y*h);
      for(int y=std::max(0,cy-ds/2);y<std::min(h,cy-ds/2+ds);++y)
        for(int x=std::max(0,cx-ds/2);x<std::min(w,cx-ds/2+ds);++x){
          const size_t i=(size_t(y)*w+x)*3;
          emitted[i]=s.r;emitted[i+1]=s.g;emitted[i+2]=s.b;
        }
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
        const float base=composite?input.read(x,y,c):0;
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
#if defined(FLARESIM_HAVE_OVERLAY)
  auto* isuite=(OfxInteractSuiteV1*)host->fetchSuite(host->host,kOfxInteractSuite,1);
  auto* dsuite=(OfxDrawSuiteV1*)host->fetchSuite(host->host,kOfxDrawSuite,1);
  fl::overlay::initialize(fx,prop,params,isuite,dsuite);
#endif
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
OfxPlugin plugin={kOfxImageEffectPluginApi,1,ID,1,4,&set_host,&main_entry};
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
