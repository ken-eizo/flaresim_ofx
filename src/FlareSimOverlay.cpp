// Standard OpenFX overlay, modeled on AcademySoftwareFoundation/openfx Examples/Overlay.
// Coordinates are canonical (RoD), not local image pixels, so any host/scale works.
#include "FlareSimOverlay.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>
#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#include <GL/gl.h>
#elif defined(__APPLE__)
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif
namespace fl::overlay {
namespace {
OfxImageEffectSuiteV1* fx=nullptr;
OfxPropertySuiteV1* p=nullptr;
OfxParameterSuiteV1* param=nullptr;
OfxInteractSuiteV1* interact=nullptr;
OfxDrawSuiteV1* draw=nullptr;
struct Data {
  OfxParamHandle position=nullptr;
  OfxParamHandle sourceMode=nullptr;
  OfxImageClipHandle source=nullptr;
  OfxImageEffectHandle effect=nullptr;
  bool dragging=false;
};
OfxPropertySetHandle properties(OfxInteractHandle h) {
  OfxPropertySetHandle out=nullptr;
  return interact&&interact->interactGetPropertySet(h,&out)==kOfxStatOK?out:nullptr;
}
Data* get_data(OfxInteractHandle h) {
  void* v=nullptr;
  if(auto* props=properties(h))p->propGetPointer(props,kOfxPropInstanceData,0,&v);
  return static_cast<Data*>(v);
}
bool region(Data* d,OfxTime t,OfxRectD& r) {
  if(!d)return false;
  if(d->source&&fx->clipGetRegionOfDefinition &&
     fx->clipGetRegionOfDefinition(d->source,t,&r)==kOfxStatOK &&
     std::isfinite(r.x1+r.x2+r.y1+r.y2) &&
     r.x2-r.x1>1e-6&&r.y2-r.y1>1e-6)return true;
  // Host-neutral fallback when a clip does not implement RoD queries.
  if(d->effect){
    OfxPropertySetHandle ep=nullptr;
    if(fx->getPropertySet(d->effect,&ep)==kOfxStatOK&&ep){
      double size[2]{},offset[2]{};
      if(p->propGetDoubleN(ep,kOfxImageEffectPropProjectSize,2,size)==kOfxStatOK &&
         size[0]>0&&size[1]>0&&std::isfinite(size[0]+size[1])){
        p->propGetDoubleN(ep,kOfxImageEffectPropProjectOffset,2,offset);
        r={offset[0],offset[1],offset[0]+size[0],offset[1]+size[1]};
        return true;
      }
    }
  }
  return false;
}
OfxTime frame_time(OfxPropertySetHandle a) {
  double t=0;if(a)p->propGetDouble(a,kOfxPropTime,0,&t);
  return t;
}
bool active(Data* d,OfxTime t) {
  if(!d||!d->sourceMode)return false;
  int mode=0;
  return param->paramGetValueAtTime(d->sourceMode,t,&mode)==kOfxStatOK && mode==1;
}
bool current_point(Data* d,OfxTime t,const OfxRectD& r,double& x,double& y) {
  if(!d||!d->position)return false;
  double u=.5,v=.5;
  if(param->paramGetValueAtTime(d->position,t,&u,&v)!=kOfxStatOK)return false;
  x=r.x1+u*(r.x2-r.x1);
  y=r.y1+v*(r.y2-r.y1);
  return std::isfinite(x)&&std::isfinite(y);
}
bool set_point(Data* d,OfxTime t,OfxPropertySetHandle args,const OfxRectD& r) {
  double q[2]{};
  if(p->propGetDoubleN(args,kOfxInteractPropPenPosition,2,q)!=kOfxStatOK)return false;
  const double u=std::clamp((q[0]-r.x1)/(r.x2-r.x1),0.,1.);
  const double v=std::clamp((q[1]-r.y1)/(r.y2-r.y1),0.,1.);
  return param->paramSetValueAtTime(d->position,t,u,v)==kOfxStatOK;
}
void cross(double x,double y,double sx,double sy) {
  glBegin(GL_LINES);
  glVertex2d(x-10*sx,y);glVertex2d(x+10*sx,y);
  glVertex2d(x,y-10*sy);glVertex2d(x,y+10*sy);
  glEnd();
  // Small gap between center and circular ring is deliberate.
  glBegin(GL_LINE_LOOP);
  for(int i=0;i<32;i++){
    const double a=6.283185307179586*double(i)/32.;
    glVertex2d(x+6.*sx*std::cos(a),y+6.*sy*std::sin(a));
  }
  glEnd();
}
OfxStatus perform(const char* action,OfxInteractHandle h,OfxPropertySetHandle args,bool use_v2) {
  if(std::strcmp(action,kOfxActionDescribe)==0)return kOfxStatOK;
  if(std::strcmp(action,kOfxActionCreateInstance)==0){
    auto* ps=properties(h);
    if(!ps)return kOfxStatReplyDefault;
    void* effect=nullptr;
    if(p->propGetPointer(ps,kOfxPropEffectInstance,0,&effect)!=kOfxStatOK||!effect)
      return kOfxStatReplyDefault;
    auto* d=new(std::nothrow) Data();
    if(!d)return kOfxStatErrMemory;
    d->effect=static_cast<OfxImageEffectHandle>(effect);
    OfxParamSetHandle set=nullptr;
    fx->getParamSet(static_cast<OfxImageEffectHandle>(effect),&set);
    if(!set||param->paramGetHandle(set,"light_position",&d->position,nullptr)!=kOfxStatOK||
       param->paramGetHandle(set,"source_mode",&d->sourceMode,nullptr)!=kOfxStatOK||
       fx->clipGetHandle(static_cast<OfxImageEffectHandle>(effect),
                         kOfxImageEffectSimpleSourceClipName,&d->source,nullptr)!=kOfxStatOK) {
      delete d;return kOfxStatReplyDefault;
    }
    p->propSetPointer(ps,kOfxPropInstanceData,0,d);
    p->propSetString(ps,kOfxInteractPropSlaveToParam,0,"light_position");
    p->propSetString(ps,kOfxInteractPropSlaveToParam,1,"source_mode");
    return kOfxStatOK;
  }
  if(std::strcmp(action,kOfxActionDestroyInstance)==0) {
    if(auto* d=get_data(h))delete d;
    if(auto* ps=properties(h))p->propSetPointer(ps,kOfxPropInstanceData,0,nullptr);
    return kOfxStatOK;
  }
  auto* d=get_data(h);
  if(!d||!args)return kOfxStatReplyDefault;
  const OfxTime t=frame_time(args);
  OfxRectD r{};
  if(!region(d,t,r))return kOfxStatReplyDefault;
  if(std::strcmp(action,kOfxInteractActionDraw)==0){
    double x=0,y=0;
    if(!current_point(d,t,r,x,y))return kOfxStatReplyDefault;
    double scale[2]{1.,1.};
    p->propGetDoubleN(args,kOfxInteractPropPixelScale,2,scale);
    const double sx=std::max(1.e-5,std::abs(scale[0])),sy=std::max(1.e-5,std::abs(scale[1]));
    if(use_v2){
      void* ptr=nullptr;
      if(!draw||p->propGetPointer(args,kOfxInteractPropDrawContext,0,&ptr)!=kOfxStatOK||!ptr)
        return kOfxStatReplyDefault;
      auto ctx=static_cast<OfxDrawContextHandle>(ptr);
      const OfxPointD crosspts[4]{{x-10*sx,y},{x+10*sx,y},{x,y-10*sy},{x,y+10*sy}};
      const OfxPointD ring[2]{{x-6*sx,y-6*sy},{x+6*sx,y+6*sy}};
      const bool manual=active(d,t);
      const OfxRGBAColourF black{0,0,0,manual?1.f:.65f};
      const OfxRGBAColourF white{1,1,1,manual?1.f:.65f};
      draw->setColour(ctx,&black);
      draw->setLineWidth(ctx,3.f);
      draw->draw(ctx,kOfxDrawPrimitiveLines,crosspts,4);
      draw->draw(ctx,kOfxDrawPrimitiveEllipse,ring,2);
      draw->setColour(ctx,&white);
      draw->setLineWidth(ctx,1.f);
      draw->draw(ctx,kOfxDrawPrimitiveLines,crosspts,4);
      draw->draw(ctx,kOfxDrawPrimitiveEllipse,ring,2);
    }else{
      glPushAttrib(GL_CURRENT_BIT|GL_ENABLE_BIT|GL_LINE_BIT);
      glDisable(GL_LINE_SMOOTH);
      glDisable(GL_BLEND);
      glLineWidth(3.f);
      glColor3f(0.f,0.f,0.f);
      cross(x,y,sx,sy);
      glLineWidth(1.f);
      glColor3f(1.f,1.f,1.f);
      cross(x,y,sx,sy);
      glPopAttrib();
    }
    return kOfxStatOK;
  }
  if(std::strcmp(action,kOfxInteractActionPenDown)==0){
    double q[2]{};
    if(p->propGetDoubleN(args,kOfxInteractPropPenPosition,2,q)!=kOfxStatOK)return kOfxStatReplyDefault;
    double x=0,y=0,scale[2]{1,1};
    if(!current_point(d,t,r,x,y))return kOfxStatReplyDefault;
    p->propGetDoubleN(args,kOfxInteractPropPixelScale,2,scale);
    if(std::abs(q[0]-x)>12.*std::abs(scale[0])||
       std::abs(q[1]-y)>12.*std::abs(scale[1]))return kOfxStatReplyDefault;
    // Dragging a guide intentionally turns on Manual Position; merely
    // displaying the guide never changes the canonical Nuke highlight mode.
    if(!active(d,t) &&
       param->paramSetValueAtTime(d->sourceMode,t,1)!=kOfxStatOK)
      return kOfxStatReplyDefault;
    d->dragging=true;
    if(interact)interact->interactRedraw(h);
    return kOfxStatOK;
  }
  if(std::strcmp(action,kOfxInteractActionPenMotion)==0){
    if(!d->dragging)return kOfxStatReplyDefault;
    if(set_point(d,t,args,r)){
      if(interact)interact->interactRedraw(h);
      return kOfxStatOK;
    }
    return kOfxStatReplyDefault;
  }
  if(std::strcmp(action,kOfxInteractActionPenUp)==0){
    if(!d->dragging)return kOfxStatReplyDefault;
    set_point(d,t,args,r);
    d->dragging=false;
    return kOfxStatOK;
  }
  return kOfxStatReplyDefault;
}
}
void initialize(OfxImageEffectSuiteV1* a,OfxPropertySuiteV1* b,
                OfxParameterSuiteV1* c,OfxInteractSuiteV1* v,OfxDrawSuiteV1* g){
  fx=a;p=b;param=c;interact=v;draw=g;
}
bool supported() noexcept{return fx&&p&&param&&interact;}
bool supports_v2() noexcept{return supported()&&draw&&draw->draw&&draw->setColour&&draw->setLineWidth;}
OfxStatus entry(const char* action,const void* h,OfxPropertySetHandle in,OfxPropertySetHandle){
  try {return supported()?perform(action,(OfxInteractHandle)h,in,false):kOfxStatReplyDefault;}
  catch (...) {return kOfxStatReplyDefault;}
}
}

namespace fl::overlay {
OfxStatus entry_v2(const char* action,const void* h,OfxPropertySetHandle in,OfxPropertySetHandle){
 try {return supports_v2()?perform(action,(OfxInteractHandle)h,in,true):kOfxStatReplyDefault;}
 catch (...) {return kOfxStatReplyDefault;}
}
}
