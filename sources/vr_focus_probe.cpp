// Steam Frame diagnostic tool, September 28, 2026. AGPL-3.0; see LICENSE.
#include <openvr.h>
#include <cstdio>
#include <dlfcn.h>
int main(){
 auto lib=dlopen("/opt/steamvr/bin/linuxarm64/libopenvr_api.so",RTLD_NOW);
 if(!lib){puts(dlerror());return 1;}
 auto init=(uint32_t(*)(vr::EVRInitError*,vr::EVRApplicationType))dlsym(lib,"VR_InitInternal");
 auto get=(void*(*)(const char*,vr::EVRInitError*))dlsym(lib,"VR_GetGenericInterface");
 auto shutdown=(void(*)())dlsym(lib,"VR_ShutdownInternal");
 vr::EVRInitError error{};init(&error,vr::VRApplication_Background);
 printf("init=%d\n",error);if(error)return 1;
 auto overlay=(vr::IVROverlay*)get(vr::IVROverlay_Version,&error);
 if(overlay)printf("dashboard_visible=%d\n",overlay->IsDashboardVisible());else printf("overlay_error=%d\n",error);
 auto compositor=(vr::IVRCompositor*)get(vr::IVRCompositor_Version,&error);
 if(compositor)printf("scene_focus_pid=%u\n",compositor->GetCurrentSceneFocusProcess());else printf("compositor_error=%d\n",error);
 shutdown();
}
  
