#include "ofxCore.h"
#include <cstring>
#include <iostream>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif
int main(){
#ifdef _WIN32
  HMODULE h=LoadLibraryA(FLARESIM_BUNDLE_BINARY);
  if(!h){std::cerr<<"LoadLibrary failed "<<GetLastError()<<"\n";return 1;}
  auto count=reinterpret_cast<int(*)()>(GetProcAddress(h,"OfxGetNumberOfPlugins"));
  auto get=reinterpret_cast<OfxPlugin*(*)(int)>(GetProcAddress(h,"OfxGetPlugin"));
#else
  void* h=dlopen(FLARESIM_BUNDLE_BINARY,RTLD_NOW);
  if(!h){std::cerr<<dlerror()<<"\n";return 1;}
  auto count=reinterpret_cast<int(*)()>(dlsym(h,"OfxGetNumberOfPlugins"));
  auto get=reinterpret_cast<OfxPlugin*(*)(int)>(dlsym(h,"OfxGetPlugin"));
#endif
  if(!count||!get||count()!=1||!get(0)||get(1)!=nullptr) return 2;
  auto* p=get(0);
  if(!p->pluginIdentifier||std::strcmp(p->pluginIdentifier,"dev.keneizo.FlareSimOFX"))return 3;
  if(!p->mainEntry||!p->setHost)return 4;
  std::cout<<"OFX C ABI entry test OK\n";return 0;
}
