#pragma once
#include "ofxImageEffect.h"
#include "ofxParam.h"
#include "ofxProperty.h"
#include "ofxInteract.h"
#include "ofxDrawSuite.h"
namespace fl::overlay {
// Optional, standard OpenFX 1.0 (OpenGL V1) interact.
// It is not required for the plugin to load in non-overlay hosts.
void initialize(OfxImageEffectSuiteV1*,OfxPropertySuiteV1*,OfxParameterSuiteV1*,OfxInteractSuiteV1*,OfxDrawSuiteV1*);
bool supported() noexcept;
bool supports_v2() noexcept;
OfxStatus entry(const char*,const void*,OfxPropertySetHandle,OfxPropertySetHandle);
OfxStatus entry_v2(const char*,const void*,OfxPropertySetHandle,OfxPropertySetHandle);
}
