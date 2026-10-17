// Minimal self-contained OpenFX host ABI integration test.
// Not a substitute for testing DaVinci Resolve, Natron, or Nuke.
#include "ofxImageEffect.h"
#include "ofxParam.h"
#include "ofxProperty.h"
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
static Effect effect;
static MockImage source,output;
static OfxImageEffectSuiteV1 fx{};
static OfxPropertySuiteV1 pr{};
static OfxParameterSuiteV1 pa{};
static OfxHost host{};

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
 pa.paramDefine=&param_define;pa.paramGetHandle=&param_handle;
 pa.paramGetValueAtTime=&param_value_at_time;
 host.fetchSuite=&suite;
 plugin->setHost(&host);
 auto call=[&](const char* action,OfxPropertySetHandle in=nullptr){
   return plugin->mainEntry(action,&effect,in,nullptr);
 };
 if(call(kOfxActionLoad)!=kOfxStatOK){std::cerr<<"Load failed\n";return 3;}
 if(call(kOfxActionDescribe)!=kOfxStatOK){std::cerr<<"Describe failed\n";return 4;}
 if(call(kOfxImageEffectActionDescribeInContext)!=kOfxStatOK){std::cerr<<"Context failed\n";return 5;}
 if(call(kOfxActionCreateInstance)!=kOfxStatOK){std::cerr<<"Instance failed\n";return 6;}
 image_init(source,64,32);image_init(output,64,32);
 const size_t sample=(size_t(16)*64+32)*4;
 source.pixels[sample]=80;source.pixels[sample+1]=90;source.pixels[sample+2]=100;
 for(size_t i=3;i<source.pixels.size();i+=4)source.pixels[i]=1.f;
 auto* mode=effect.parameters.params.at("output_mode").get();
 mode->props.ints[kOfxParamPropDefault][0]=5; // Sources Only
 Props args;set_double(reinterpret_cast<OfxPropertySetHandle>(&args),kOfxPropTime,0,0);
 for(int i=0;i<4;i++)set_int(reinterpret_cast<OfxPropertySetHandle>(&args),
    kOfxImageEffectPropRenderWindow,i,(i==2?64:(i==3?32:0)));
 if(call(kOfxImageEffectActionRender,reinterpret_cast<OfxPropertySetHandle>(&args))!=kOfxStatOK){
   std::cerr<<"Render failed\n";return 7;
 }
 float peak=0;for(size_t i=0;i<output.pixels.size();i+=4)
   peak=std::max({peak,output.pixels[i],output.pixels[i+1],output.pixels[i+2]});
 if(!(peak>2.f&&std::isfinite(peak))){std::cerr<<"No bright source: "<<peak<<"\n";return 8;}
 mode->props.ints[kOfxParamPropDefault][0]=2; // Ghost Only
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
 if(call(kOfxActionDestroyInstance)!=kOfxStatOK)return 11;
 if(call(kOfxActionUnload)!=kOfxStatOK)return 12;
 std::cout<<"OFX mock-host sources + optical render OK\n";return 0;
}
