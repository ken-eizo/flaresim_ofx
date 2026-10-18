// Minimal self-contained OpenFX host ABI integration test.
// Not a substitute for testing DaVinci Resolve, Natron, or Nuke.
#include "ofxImageEffect.h"
#include "ofxParam.h"
#include "ofxProperty.h"
#include "ofxInteract.h"
#include "ofxDrawSuite.h"
#include <algorithm>
#include <cstdarg>
#include <cmath>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif

struct Props {
  std::map<std::string,std::vector<std::string>> strings;
  std::map<std::string,std::vector<double>> doubles;
  std::map<std::string,std::vector<int>> ints;
  std::map<std::string,std::vector<void*>> pointers;
};
struct Param{std::string type;Props props;};
struct ParamSet{std::map<std::string,std::unique_ptr<Param>> params;};
struct Clip {std::string name;Props props;};
struct Effect{
 Props props;ParamSet parameters;
 std::map<std::string,std::unique_ptr<Clip>> clips;
};
struct MockImage {
 Props props;
 std::vector<float> pixels;
};
static bool defining_params=false;
static Effect effect;
static MockImage source,output;
static OfxImageEffectSuiteV1 fx{};
static OfxPropertySuiteV1 pr{};
static OfxParameterSuiteV1 pa{};
static OfxHost host{};
static OfxInteractSuiteV1 it{};
static OfxDrawSuiteV1 ds{};
static int draw_primitive_calls=0;
static OfxStatus mock_draw_color(OfxDrawContextHandle,const OfxRGBAColourF*){
 return kOfxStatOK;
}
static OfxStatus mock_draw_line_width(OfxDrawContextHandle,float){
 return kOfxStatOK;
}
static OfxStatus mock_draw_primitive(OfxDrawContextHandle,OfxDrawPrimitive,
                                    const OfxPointD*,int){
 ++draw_primitive_calls;
 return kOfxStatOK;
}

struct MockInteract {Props props;};
static OfxStatus mock_interact_get(OfxInteractHandle h,OfxPropertySetHandle* p){
  *p=reinterpret_cast<OfxPropertySetHandle>(&reinterpret_cast<MockInteract*>(h)->props);
  return kOfxStatOK;
}
static OfxStatus mock_interact_redraw(OfxInteractHandle){return kOfxStatOK;}
static OfxStatus mock_rod(OfxImageClipHandle,OfxTime,OfxRectD* r){
  r->x1=0;r->y1=0;r->x2=64;r->y2=32;return kOfxStatOK;
}
static OfxStatus mock_param_set_at(OfxParamHandle h,OfxTime t,...){
  auto* par=reinterpret_cast<Param*>(h);
  va_list a;va_start(a,t);
  if(par->type==kOfxParamTypeDouble2D){
    double x=va_arg(a,double),y=va_arg(a,double);
    par->props.doubles[kOfxParamPropDefault]={x,y};
  }else if(par->type==kOfxParamTypeChoice){
    const int mode=va_arg(a,int);
    par->props.ints[kOfxParamPropDefault]={mode};
  }else{
    va_end(a);
    return kOfxStatErrUnknown;
  }
  va_end(a);
  return kOfxStatOK;
}

template<typename T>
static void set_index(std::map<std::string,std::vector<T>>& values,
                      const char* name,int i,T x){
 if(i<0||i>8192)return;
 auto& v=values[name];
 if(v.size()<=size_t(i))v.resize(size_t(i)+1);
 v[i]=std::move(x);
}
template<typename T>
static bool get_index(const std::map<std::string,std::vector<T>>& values,
                      const char* name,int i,T& x){
 auto it=values.find(name);
 if(i<0||it==values.end()||size_t(i)>=it->second.size())return false;
 x=it->second[i];return true;
}
static OfxStatus set_string(OfxPropertySetHandle h,const char* n,int i,const char* v){
 set_index(reinterpret_cast<Props*>(h)->strings,n,i,std::string(v?v:""));return kOfxStatOK;
}
static OfxStatus set_int(OfxPropertySetHandle h,const char* n,int i,int v){
 set_index(reinterpret_cast<Props*>(h)->ints,n,i,v);return kOfxStatOK;
}
static OfxStatus set_double(OfxPropertySetHandle h,const char* n,int i,double v){
 set_index(reinterpret_cast<Props*>(h)->doubles,n,i,v);return kOfxStatOK;
}
static OfxStatus set_ptr(OfxPropertySetHandle h,const char* n,int i,void* v){
 set_index(reinterpret_cast<Props*>(h)->pointers,n,i,v);return kOfxStatOK;
}
static OfxStatus get_string(OfxPropertySetHandle h,const char* n,int i,char** v){
 auto& map=reinterpret_cast<Props*>(h)->strings;
 auto it=map.find(n);
 if(it==map.end()||size_t(i)>=it->second.size())return kOfxStatErrUnknown;
 *v=it->second[i].data();return kOfxStatOK;
}
static OfxStatus get_int(OfxPropertySetHandle h,const char* n,int i,int* v){
 return get_index(reinterpret_cast<Props*>(h)->ints,n,i,*v)?kOfxStatOK:kOfxStatErrUnknown;
}
static OfxStatus get_double(OfxPropertySetHandle h,const char* n,int i,double* v){
 return get_index(reinterpret_cast<Props*>(h)->doubles,n,i,*v)?kOfxStatOK:kOfxStatErrUnknown;
}
static OfxStatus get_ptr(OfxPropertySetHandle h,const char* n,int i,void** v){
 return get_index(reinterpret_cast<Props*>(h)->pointers,n,i,*v)?kOfxStatOK:kOfxStatErrUnknown;
}
static OfxStatus get_int_n(OfxPropertySetHandle h,const char* n,int count,int* v){
 for(int i=0;i<count;i++)if(get_int(h,n,i,v+i)!=kOfxStatOK)return kOfxStatErrUnknown;
 return kOfxStatOK;
}
static OfxStatus get_double_n(OfxPropertySetHandle h,const char* n,int count,double* v){
 for(int i=0;i<count;i++)if(get_double(h,n,i,v+i)!=kOfxStatOK)return kOfxStatErrUnknown;
 return kOfxStatOK;
}
static OfxStatus effect_props(OfxImageEffectHandle,OfxPropertySetHandle* out){
 *out=reinterpret_cast<OfxPropertySetHandle>(&effect.props);return kOfxStatOK;
}
static OfxStatus paramset(OfxImageEffectHandle,OfxParamSetHandle* out){
 *out=reinterpret_cast<OfxParamSetHandle>(&effect.parameters);return kOfxStatOK;
}
static OfxStatus clip_define(OfxImageEffectHandle,const char* name,OfxPropertySetHandle* out){
 auto& c=effect.clips[name];if(!c)c=std::make_unique<Clip>();
 c->name=name;*out=reinterpret_cast<OfxPropertySetHandle>(&c->props);return kOfxStatOK;
}
static OfxStatus clip_handle(OfxImageEffectHandle,const char* name,
                             OfxImageClipHandle* out,OfxPropertySetHandle* ps){
 auto it=effect.clips.find(name);if(it==effect.clips.end())return kOfxStatErrUnknown;
 *out=reinterpret_cast<OfxImageClipHandle>(it->second.get());
 if(ps)*ps=reinterpret_cast<OfxPropertySetHandle>(&it->second->props);
 return kOfxStatOK;
}
static OfxStatus clip_get_image(OfxImageClipHandle h,OfxTime,const OfxRectD*,OfxPropertySetHandle* out){
 auto* c=reinterpret_cast<Clip*>(h);
 *out=reinterpret_cast<OfxPropertySetHandle>(c->name=="Source"?&source.props:&output.props);
 return kOfxStatOK;
}
static OfxStatus clip_release(OfxPropertySetHandle){return kOfxStatOK;}
static int effect_abort(OfxImageEffectHandle){return 0;}
static OfxStatus param_define(OfxParamSetHandle,const char* type,const char* name,
                              OfxPropertySetHandle* out){
 auto p=std::make_unique<Param>();p->type=type;
 *out=reinterpret_cast<OfxPropertySetHandle>(&p->props);
 effect.parameters.params[name]=std::move(p);
 return kOfxStatOK;
}
static OfxStatus param_handle(OfxParamSetHandle,const char* name,
                              OfxParamHandle* out,OfxPropertySetHandle* props){
 if(defining_params)return kOfxStatErrBadHandle;
 auto it=effect.parameters.params.find(name);
 if(it==effect.parameters.params.end())return kOfxStatErrUnknown;
 *out=reinterpret_cast<OfxParamHandle>(it->second.get());
 if(props)*props=reinterpret_cast<OfxPropertySetHandle>(&it->second->props);
 return kOfxStatOK;
}
// Variadic arguments must start after the final named OfxTime parameter.
static OfxStatus param_value_at_time(OfxParamHandle handle,OfxTime t,...){
 auto* p=reinterpret_cast<Param*>(handle);
 va_list args;va_start(args,t);
 if(p->type==kOfxParamTypeString){
   auto** out=va_arg(args,char**);
   auto it=p->props.strings.find(kOfxParamPropDefault);
   *out=it==p->props.strings.end()?const_cast<char*>(""):it->second[0].data();
 }else if(p->type==kOfxParamTypeDouble2D){
   double* x=va_arg(args,double*),*y=va_arg(args,double*);
   *x=p->props.doubles[kOfxParamPropDefault][0];
   *y=p->props.doubles[kOfxParamPropDefault][1];
 }else if(p->type==kOfxParamTypeDouble){
   double* out=va_arg(args,double*);
   *out=p->props.doubles[kOfxParamPropDefault][0];
 }else{
   int* out=va_arg(args,int*);
   *out=p->props.ints[kOfxParamPropDefault][0];
 }
 va_end(args);return kOfxStatOK;
}
static const void* suite(OfxPropertySetHandle,const char* name,int ver){
 if(ver!=1)return nullptr;
 if(std::strcmp(name,kOfxImageEffectSuite)==0)return &fx;
 if(std::strcmp(name,kOfxPropertySuite)==0)return &pr;
 if(std::strcmp(name,kOfxParameterSuite)==0)return &pa;
 if(std::strcmp(name,kOfxInteractSuite)==0)return &it;
 if(std::strcmp(name,kOfxDrawSuite)==0)return &ds;
 return nullptr;
}
static void image_init(MockImage& image,int w,int h){
 image.pixels.assign(size_t(w)*h*4,0);
 set_ptr(reinterpret_cast<OfxPropertySetHandle>(&image.props),kOfxImagePropData,0,image.pixels.data());
 set_int(reinterpret_cast<OfxPropertySetHandle>(&image.props),kOfxImagePropRowBytes,0,w*4*sizeof(float));
 int v[4]={0,0,w,h};
 for(int i=0;i<4;i++)set_int(reinterpret_cast<OfxPropertySetHandle>(&image.props),kOfxImagePropBounds,i,v[i]);
 set_string(reinterpret_cast<OfxPropertySetHandle>(&image.props),kOfxImageEffectPropComponents,0,kOfxImageComponentRGBA);
 set_string(reinterpret_cast<OfxPropertySetHandle>(&image.props),kOfxImageEffectPropPixelDepth,0,kOfxBitDepthFloat);
}
int main(){
#ifdef _WIN32
 HMODULE module=LoadLibraryA(FLARESIM_BUNDLE_BINARY);
 if(!module)return 1;
 auto get=reinterpret_cast<OfxPlugin*(*)(int)>(GetProcAddress(module,"OfxGetPlugin"));
#else
 void* module=dlopen(FLARESIM_BUNDLE_BINARY,RTLD_NOW);
 if(!module){std::cerr<<dlerror()<<"\n";return 1;}
 auto get=reinterpret_cast<OfxPlugin*(*)(int)>(dlsym(module,"OfxGetPlugin"));
#endif
 if(!get||!get(0))return 2;
 auto* plugin=get(0);
 pr.propSetString=&set_string;pr.propSetInt=&set_int;pr.propSetDouble=&set_double;
 pr.propSetPointer=&set_ptr;pr.propGetString=&get_string;pr.propGetInt=&get_int;
 pr.propGetDouble=&get_double;pr.propGetPointer=&get_ptr;pr.propGetIntN=&get_int_n;
 pr.propGetDoubleN=&get_double_n;
 fx.getPropertySet=&effect_props;fx.getParamSet=&paramset;
 fx.clipDefine=&clip_define;fx.clipGetHandle=&clip_handle;
 fx.clipGetImage=&clip_get_image;fx.clipReleaseImage=&clip_release;
 fx.abort=&effect_abort;
 fx.clipGetRegionOfDefinition=&mock_rod;
 pa.paramDefine=&param_define;pa.paramGetHandle=&param_handle;
 pa.paramGetValueAtTime=&param_value_at_time;
 pa.paramSetValueAtTime=&mock_param_set_at;
 it.interactGetPropertySet=&mock_interact_get;
 it.interactRedraw=&mock_interact_redraw;
 ds.setColour=&mock_draw_color;
 ds.setLineWidth=&mock_draw_line_width;
 ds.draw=&mock_draw_primitive;
 host.fetchSuite=&suite;
 plugin->setHost(&host);
 auto call=[&](const char* action,OfxPropertySetHandle in=nullptr){
   return plugin->mainEntry(action,&effect,in,nullptr);
 };
 if(call(kOfxActionLoad)!=kOfxStatOK){std::cerr<<"Load failed\n";return 3;}
 if(call(kOfxActionDescribe)!=kOfxStatOK){std::cerr<<"Describe failed\n";return 4;}
 defining_params=true;
 if(call(kOfxImageEffectActionDescribeInContext)!=kOfxStatOK){std::cerr<<"Context failed\n";return 5;}
 defining_params=false;
 if(call(kOfxActionCreateInstance)!=kOfxStatOK){std::cerr<<"Instance failed\n";return 6;}
 // Confirm descriptors expose a real file picker, a stable Nuke preset,
 // and a visible manual default instead of silent empty black layers.
 if(effect.parameters.params.at("source_mode").get()->props.ints[kOfxParamPropDefault][0]!=0)return 17;
 if(effect.parameters.params.at("ray_grid")->props.ints[kOfxParamPropDefault][0]!=64)return 30;
 if(std::abs(effect.parameters.params.at("ghost_blur")->props.doubles[kOfxParamPropDefault][0]-.003)>1e-9)return 31;
 if(!effect.parameters.params.count("lens_preset"))return 18;
 if(effect.parameters.params.count("show_sources"))return 58;
 if(effect.parameters.params.count("nuke_diagnostics"))return 59;
 if(effect.parameters.params.count("nuke_pairs"))return 60;
 if(effect.parameters.params.at("pair_mode")->props.strings[kOfxParamPropParent][0]!="additional")return 61;
 // A strict OFX host rejects descriptor-time paramGetHandle. Properties
 // must be assigned to the exact paramDefine descriptor returned handle.
 auto& catalog=effect.parameters.params.at("lens_preset")->props.strings[kOfxParamPropChoiceOption];
 if(catalog.size()!=1371 || catalog[1]!="AI_Nikkor_50mm_F1.2S.lens")return 40;
 if(effect.parameters.params.at("gain")->props.strings[kOfxParamPropParent][0]!="nuke_ghost")return 41;
 if(effect.parameters.params.at("fov_h")->props.strings[kOfxParamPropParent][0]!="nuke_camera")return 42;
 auto& outputChoices=effect.parameters.params.at("output_format")->props.strings[kOfxParamPropChoiceOption];
 if(outputChoices.size()!=3 || outputChoices[0]!="Flare as RGB"||
    outputChoices[1]!="Sources Only"||outputChoices[2]!="Composite")return 43;

 if(effect.parameters.params.at("lens_preset")->props.ints[kOfxParamPropDefault][0]!=0)return 32;
 if(effect.parameters.params.at("lens_file")->props.strings[kOfxParamPropDefault][0]!="")return 33;
 if(effect.parameters.params.at("fov_auto_v")->props.ints[kOfxParamPropDefault][0]!=1)return 34;
 if(effect.parameters.params.at("fov_use_sensor")->props.ints[kOfxParamPropDefault][0]!=0)return 35;
 if(effect.parameters.params.at("nuke_spectral_quality")->props.ints[kOfxParamPropDefault][0]!=0)return 36;
 if(effect.parameters.params.at("preview")->props.ints[kOfxParamPropDefault][0]!=0)return 37;

 auto& lensP=effect.parameters.params.at("lens_file")->props.strings;
 if(effect.parameters.params.at("lens_file")->props.ints[kOfxParamPropSecret][0]!=1)return 19;
 if(effect.parameters.params.at("lens_preset")->props.strings[kOfxPropLabel][0]!="Lens File")return 57;
#if defined(_WIN32) || defined(__APPLE__)
 effect.parameters.params.at("source_mode")->props.ints[kOfxParamPropDefault][0]=1;
 // Exercise the real OFX overlay pen lifecycle (without OpenGL draw).
 auto itPtr=effect.props.pointers.find(kOfxImageEffectPluginPropOverlayInteractV1);
 if(itPtr==effect.props.pointers.end()||itPtr->second.empty()||!itPtr->second[0])return 20;
 auto overlay=reinterpret_cast<OfxPluginEntryPoint*>(itPtr->second[0]);
 MockInteract mh;
 auto mp=reinterpret_cast<OfxPropertySetHandle>(&mh.props);
 set_ptr(mp,kOfxPropEffectInstance,0,&effect);
 if(overlay(kOfxActionDescribe,&mh,nullptr,nullptr)!=kOfxStatOK)return 21;
 if(overlay(kOfxActionCreateInstance,&mh,nullptr,nullptr)!=kOfxStatOK)return 22;
 Props intArgs;
 auto aa=reinterpret_cast<OfxPropertySetHandle>(&intArgs);
 set_double(aa,kOfxPropTime,0,0.0);
 set_double(aa,kOfxInteractPropPixelScale,0,1.0);
 set_double(aa,kOfxInteractPropPixelScale,1,1.0);
 set_double(aa,kOfxInteractPropPenPosition,0,32.0);
 set_double(aa,kOfxInteractPropPenPosition,1,16.0);
 if(overlay(kOfxInteractActionPenDown,&mh,aa,nullptr)!=kOfxStatOK)return 23;
 set_double(aa,kOfxInteractPropPenPosition,0,48.0);
 set_double(aa,kOfxInteractPropPenPosition,1,8.0);
 if(overlay(kOfxInteractActionPenMotion,&mh,aa,nullptr)!=kOfxStatOK)return 24;
 if(overlay(kOfxInteractActionPenUp,&mh,aa,nullptr)!=kOfxStatOK)return 25;
 const auto& pt=effect.parameters.params.at("light_position")->props.doubles[kOfxParamPropDefault];
 if(std::abs(pt[0]-.75)>1e-8||std::abs(pt[1]-.25)>1e-8)return 26;
 effect.parameters.params.at("light_position")->props.doubles[kOfxParamPropDefault]={.5,.5};
 if(overlay(kOfxActionDestroyInstance,&mh,nullptr,nullptr)!=kOfxStatOK)return 27;
 // Test portable DrawSuite V2, including the pointer-down transition from
 // automatic Nuke detection to optional manual source control.
 auto v2Ptr=effect.props.pointers.find(kOfxImageEffectPluginPropOverlayInteractV2);
 if(v2Ptr==effect.props.pointers.end()||v2Ptr->second.empty()||!v2Ptr->second[0])return 46;
 auto overlay2=reinterpret_cast<OfxPluginEntryPoint*>(v2Ptr->second[0]);
 MockInteract mh2;
 auto mp2=reinterpret_cast<OfxPropertySetHandle>(&mh2.props);
 set_ptr(mp2,kOfxPropEffectInstance,0,&effect);
 effect.parameters.params.at("source_mode")->props.ints[kOfxParamPropDefault][0]=0;
 effect.parameters.params.at("light_position")->props.doubles[kOfxParamPropDefault]={.5,.5};
 if(overlay2(kOfxActionDescribe,&mh2,nullptr,nullptr)!=kOfxStatOK)return 47;
 if(overlay2(kOfxActionCreateInstance,&mh2,nullptr,nullptr)!=kOfxStatOK)return 48;
 int context_dummy=1;
 set_ptr(aa,kOfxInteractPropDrawContext,0,&context_dummy);
 set_double(aa,kOfxInteractPropPenPosition,0,32.0);
 set_double(aa,kOfxInteractPropPenPosition,1,16.0);
 draw_primitive_calls=0;
 if(overlay2(kOfxInteractActionDraw,&mh2,aa,nullptr)!=kOfxStatOK||
    draw_primitive_calls<4)return 49;
 if(overlay2(kOfxInteractActionPenDown,&mh2,aa,nullptr)!=kOfxStatOK||
    effect.parameters.params.at("source_mode")->props.ints[kOfxParamPropDefault][0]!=1)
   return 50;
 set_double(aa,kOfxInteractPropPenPosition,0,16.0);
 set_double(aa,kOfxInteractPropPenPosition,1,24.0);
 if(overlay2(kOfxInteractActionPenMotion,&mh2,aa,nullptr)!=kOfxStatOK)return 51;
 const auto& moved=effect.parameters.params.at("light_position")->props.doubles[kOfxParamPropDefault];
 if(std::abs(moved[0]-.25)>1e-8||std::abs(moved[1]-.75)>1e-8)return 52;
 if(overlay2(kOfxInteractActionPenUp,&mh2,aa,nullptr)!=kOfxStatOK)return 53;
 if(overlay2(kOfxActionDestroyInstance,&mh2,nullptr,nullptr)!=kOfxStatOK)return 54;
 effect.parameters.params.at("source_mode")->props.ints[kOfxParamPropDefault][0]=0;
#endif

 image_init(source,64,32);image_init(output,64,32);
 const size_t sample=(size_t(16)*64+32)*4;
 source.pixels[sample]=80;source.pixels[sample+1]=90;source.pixels[sample+2]=100;
 for(size_t i=3;i<source.pixels.size();i+=4)source.pixels[i]=1.f;
 auto* mode=effect.parameters.params.at("output_format").get();
 mode->props.ints[kOfxParamPropDefault][0]=1; // Sources Only
 Props args;set_double(reinterpret_cast<OfxPropertySetHandle>(&args),kOfxPropTime,0,0);
 for(int i=0;i<4;i++)set_int(reinterpret_cast<OfxPropertySetHandle>(&args),
    kOfxImageEffectPropRenderWindow,i,(i==2?64:(i==3?32:0)));
 // Source diagnostics require no lens, and are the portable equivalent of
 // the original Nuke Output Mode "Sources Only".
 mode->props.ints[kOfxParamPropDefault][0]=1;
 if(call(kOfxImageEffectActionRender,reinterpret_cast<OfxPropertySetHandle>(&args))!=kOfxStatOK)return 55;
 float sourcePeak=0;
 for(size_t i=0;i<output.pixels.size();i+=4)
   sourcePeak=std::max({sourcePeak,output.pixels[i],output.pixels[i+1],output.pixels[i+2]});
 if(sourcePeak<2.f)return 56;
 std::cout<<"No-lens Sources Only peak="<<sourcePeak<<"\n";
 effect.parameters.params.at("source_mode")->props.ints[kOfxParamPropDefault][0]=1;
 // Regression: a black opaque solid with Manual Position enabled must produce
 // a visible physical flare. This also exercises the bundled default lens.
 const float markerR=source.pixels[sample], markerG=source.pixels[sample+1],
             markerB=source.pixels[sample+2];
 source.pixels[sample]=source.pixels[sample+1]=source.pixels[sample+2]=0.f;
 // No lens is the original Nuke initial state; it must not crash or
 // leave a stale buffer. The optional Composite mode must preserve footage.
 mode->props.ints[kOfxParamPropDefault][0]=2;
 std::fill(output.pixels.begin(),output.pixels.end(),-777.f);
 if(call(kOfxImageEffectActionRender,reinterpret_cast<OfxPropertySetHandle>(&args))!=kOfxStatOK)
   return 44;
 if(output.pixels[sample]<-10.f || output.pixels[sample+3]!=1.f)return 45;
 effect.parameters.params.at("lens_preset")->props.ints[kOfxParamPropDefault][0]=1; // Nikkor
 mode->props.ints[kOfxParamPropDefault][0]=0; // Flare as RGB
 if(call(kOfxImageEffectActionRender,reinterpret_cast<OfxPropertySetHandle>(&args))!=kOfxStatOK) {
   std::cerr<<"Default black plate render failed\n";return 28;
 }
 double flare_energy=0;
 for(size_t pixel=0;pixel<output.pixels.size();pixel+=4)
   flare_energy+=std::max({0.f,output.pixels[pixel],output.pixels[pixel+1],output.pixels[pixel+2]});
 if(!(flare_energy>1e-6 && std::isfinite(flare_energy))) {
   std::cerr<<"Default black plate did not emit flare\n";return 29;
 }
 std::cout<<"Default black plate flare energy="<<flare_energy<<"\n";
 source.pixels[sample]=markerR;source.pixels[sample+1]=markerG;source.pixels[sample+2]=markerB;
 mode->props.ints[kOfxParamPropDefault][0]=1;
 effect.parameters.params.at("source_mode")->props.ints[kOfxParamPropDefault][0]=0;
 std::fill(output.pixels.begin(),output.pixels.end(),0.f);
 if(call(kOfxImageEffectActionRender,reinterpret_cast<OfxPropertySetHandle>(&args))!=kOfxStatOK){
   std::cerr<<"Render failed\n";return 7;
 }
 float peak=0;for(size_t i=0;i<output.pixels.size();i+=4)
   peak=std::max({peak,output.pixels[i],output.pixels[i+1],output.pixels[i+2]});
 if(!(peak>2.f&&std::isfinite(peak))){std::cerr<<"No bright source: "<<peak<<"\n";return 8;}
 mode->props.ints[kOfxParamPropDefault][0]=0; // Flare as RGB
 effect.parameters.params.at("source_mode")->props.ints[kOfxParamPropDefault][0]=1;
 effect.parameters.params.at("ray_grid")->props.ints[kOfxParamPropDefault][0]=64;
 effect.parameters.params.at("source_mode")->props.ints[kOfxParamPropDefault][0]=1;
 effect.parameters.params.at("light_radiance")->props.doubles[kOfxParamPropDefault][0]=100.0;
 effect.parameters.params.at("max_pairs")->props.ints[kOfxParamPropDefault][0]=78;
 if(call(kOfxImageEffectActionRender,reinterpret_cast<OfxPropertySetHandle>(&args))!=kOfxStatOK){
   std::cerr<<"Optical render failed\n";return 9;
 }
 for(float c:output.pixels)if(!std::isfinite(c)){std::cerr<<"Nonfinite output\n";return 10;}
 // Request the CPU path explicitly, then compare to the platform GPU path.
 // These are separate host render actions; never share GPU scratch between calls.
 auto* renderer=effect.parameters.params.at("render_backend").get();
 renderer->props.ints[kOfxParamPropDefault][0]=1;
 if(call(kOfxImageEffectActionRender,reinterpret_cast<OfxPropertySetHandle>(&args))!=kOfxStatOK){
   std::cerr<<"CPU fallback render failed\n";return 13;
 }
 const auto cpu=output.pixels;
 renderer->props.ints[kOfxParamPropDefault][0]=2;
 if(call(kOfxImageEffectActionRender,reinterpret_cast<OfxPropertySetHandle>(&args))!=kOfxStatOK){
   std::cerr<<"GPU preferred render failed\n";return 14;
 }
 double cpu_sum=0,gpu_sum=0,diff_sum=0;
 for(size_t i=0;i<output.pixels.size();i+=4){
   for(int c=0;c<3;c++){
     cpu_sum+=std::abs(cpu[i+c]);
     gpu_sum+=std::abs(output.pixels[i+c]);
     diff_sum+=std::abs(cpu[i+c]-output.pixels[i+c]);
   }
 }
 if(!std::isfinite(cpu_sum+gpu_sum+diff_sum))return 15;
 if(cpu_sum>1.0 && diff_sum>cpu_sum*0.25){std::cerr<<"GPU parity failed\n";return 16;}
 std::cout<<"Ghost CPU L1="<<cpu_sum<<" GPU-preferred L1="<<gpu_sum
          <<" L1-diff="<<diff_sum<<"\n";
 // AE Golden Angle is an appended (fourth) optional sampling mode, not a
 // change to the original three Nuke sampling modes or their default.
 const auto original=output.pixels;
 effect.parameters.params.at("pupil_jitter")->props.ints[kOfxParamPropDefault][0]=3;
 if(call(kOfxImageEffectActionRender,reinterpret_cast<OfxPropertySetHandle>(&args))!=kOfxStatOK){
   std::cerr<<"Golden Angle optical render failed\n";return 38;
 }
 double goldenEnergy=0,goldenDelta=0;
 for(size_t i=0;i<output.pixels.size();i+=4)for(int c=0;c<3;c++){
   goldenEnergy+=std::abs(output.pixels[i+c]);
   goldenDelta+=std::abs(double(output.pixels[i+c])-original[i+c]);
 }
 if(!(goldenEnergy>1e-4 && goldenDelta>1e-4)){
   std::cerr<<"Golden Angle did not change optical sample distribution\n";return 39;
 }
 effect.parameters.params.at("pupil_jitter")->props.ints[kOfxParamPropDefault][0]=0;
 std::cout<<"Golden Angle rendered, energy="<<goldenEnergy
          <<" difference="<<goldenDelta<<"\n";
 if(call(kOfxActionDestroyInstance)!=kOfxStatOK)return 11;
 if(call(kOfxActionUnload)!=kOfxStatOK)return 12;
 std::cout<<"OFX mock-host sources + optical render OK\n";return 0;
}
