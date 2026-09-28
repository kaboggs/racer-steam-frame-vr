// Steam Frame compatibility modifications, September 27–28, 2026.
// Derived from GameOrDie007/Star-Wars-Episode-I-Racer-PCVR's archived OpenVR backend.
// Distributed under AGPL-3.0; see LICENSE and NOTICE.md. No warranty.
#include "vr_probe.h"

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <algorithm>
#include <stdint.h>
#include <stdarg.h>

#include "openvr/openvr.h"
#include "hook_helper.h"// hook_log
#include "imgui.h"

// ---------------------------------------------------------------------------------------------
// Everything is resolved dynamically. We never link openvr_api.lib: the SDK import library is
// MSVC-built and this DLL is MinGW, and a hook DLL that hard-links a runtime it may not find
// would fail to load the game at all. GetProcAddress keeps a missing/older SteamVR to a logged
// warning instead of a startup failure.
// ---------------------------------------------------------------------------------------------

typedef uint32_t(__cdecl *PFN_VR_InitInternal)(vr::EVRInitError *, vr::EVRApplicationType);
typedef void(__cdecl *PFN_VR_ShutdownInternal)(void);
typedef bool(__cdecl *PFN_VR_IsHmdPresent)(void);
typedef bool(__cdecl *PFN_VR_IsRuntimeInstalled)(void);
typedef bool(__cdecl *PFN_VR_IsInterfaceVersionValid)(const char *);
typedef void *(__cdecl *PFN_VR_GetGenericInterface)(const char *, vr::EVRInitError *);
typedef const char *(__cdecl *PFN_VR_GetVRInitErrorAsEnglishDescription)(vr::EVRInitError);

static HMODULE g_openvr_module = nullptr;
static PFN_VR_InitInternal p_VR_InitInternal = nullptr;
static PFN_VR_ShutdownInternal p_VR_ShutdownInternal = nullptr;
static PFN_VR_IsHmdPresent p_VR_IsHmdPresent = nullptr;
static PFN_VR_IsRuntimeInstalled p_VR_IsRuntimeInstalled = nullptr;
static PFN_VR_IsInterfaceVersionValid p_VR_IsInterfaceVersionValid = nullptr;
static PFN_VR_GetGenericInterface p_VR_GetGenericInterface = nullptr;
static PFN_VR_GetVRInitErrorAsEnglishDescription p_VR_GetVRInitErrorAsEnglishDescription = nullptr;

static vr::IVRSystem *g_vr_system = nullptr;
static vr::IVRCompositor *g_vr_compositor = nullptr;
static bool g_session_up = false;
static bool g_is_scene_app = false;// only a Scene app may submit frames
static bool g_gave_up = false;

// Set when the init worker overran its deadline and the main thread gave up on it. The worker
// may still be parked inside the runtime and return minutes later; without this it would go on
// to publish the session, switching VR on mid-run behind the main thread's back. That produced
// a half-initialised stereo path drawing 386 meshes in one eye and 0 in the other.
static volatile LONG g_init_abandoned = 0;

static uint32_t g_target_w = 0;
static uint32_t g_target_h = 0;

// Cached head rotation, world->head, column-major. Identity until a valid pose arrives.
static float g_head_rot[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

// Which eye the renderer is drawing right now (VR_EYE_LEFT / VR_EYE_RIGHT).
static int g_current_eye = VR_EYE_LEFT;

// Reset at the top of every frame; set by vr_submit_eye. Drives the 2D menu fallback.
static bool g_submitted_this_frame = false;

struct VrProbeState {
    bool pose_valid = false;
    bool device_connected = false;
    float yaw_deg = 0.0f;
    float pitch_deg = 0.0f;
    float roll_deg = 0.0f;
    float pos_x = 0.0f, pos_y = 0.0f, pos_z = 0.0f;
    unsigned long frames = 0;
    unsigned long valid_frames = 0;
    unsigned long submits = 0;
    char status[256] = "not initialised";
    char submit_error[128] = "";

    // Runtime toggles, driven from the F5 panel so they can be changed in-headset without a
    // rebuild -- a rebuild costs a headset-on/headset-off cycle, which is the expensive resource.
    bool submit_enabled = true;    // send frames to the compositor
    bool head_drives_camera = true;// apply head rotation to the game camera
    bool flip_v = false;           // GL textures are bottom-up; flip if the view is upside down
    bool swap_eye_order = false;   // draw right eye first (diagnostic, see vr_eye_for_pass)
    // Horizontal correction for flat 2D content (menus, cutscenes). 1.0 = shift each eye by
    // exactly its projection's principal-point offset, which is the amount the compositor's
    // asymmetric frustum would otherwise displace an identical image by. 0 = no correction
    // (the old doubled behaviour). Tunable because the sign is easier to confirm than derive.
    float menu_shift = 1.0f;
    bool dump_eyes = false;// one-shot: write both eye images to disk next frame
    bool hud_redirect = false;// divert 2D into the VR HUD layer (off = vanilla 2D everywhere)
    float hud_scale = 0.70f;  // fraction of the view the 2D layer covers (1.0 = original)
    // Defaults ON: this demonstrably keeps scenery from vanishing when you turn your head, and
    // the slider is session-only (not saved to the ini), so leaving it at 1.0 meant it silently
    // reverted every launch and the fix appeared to stop working.
    float cull_fov_boost = 2.0f;   // widen the ENGINE's culling frustum (1.0 = untouched)
    bool render_all_selectors = false;// draw every NODE_SELECTOR child, not just the chosen one
    // Game units per real-world metre. OpenVR reports IPD in metres (0.063), but SWE1R world
    // units are far larger -- the in-race camera sits ~89x33x175 from origin with zNear at 10 --
    // so an unconverted IPD gives the two eyes a 0.063-unit separation, i.e. no stereo at all.
    // Tunable because the true ratio is not documented anywhere; dial it until depth reads right.
    // 3.2 chosen by eye in-headset. Note this implies zNear (10 game units) sits ~3 m away,
    // so nothing within 3 m of the viewer renders -- suspiciously far for a cockpit you are
    // supposed to be sitting inside. Worth re-deriving from the pod model bounds.
    float world_units_per_metre = 2.4f;
};
static VrProbeState g_state;
static vr::VRControllerState_t g_left{}, g_right{};
static bool g_left_valid=false, g_right_valid=false;
static vr::IVRInput *g_input=nullptr;
static vr::VRActionSetHandle_t g_action_set=0;
static vr::VRInputValueHandle_t g_hand[2]{};
static vr::VRActionHandle_t g_stick=0,g_trigger=0,g_grip=0,g_primary=0,g_secondary=0,g_click=0,g_menu=0;
static unsigned long g_serial=0;
static int g_pod=-1;
static bool button(const vr::VRControllerState_t &s, vr::EVRButtonId id) {
    return (s.ulButtonPressed & vr::ButtonMaskFromId(id)) != 0;
}
static float axis(const vr::VRControllerState_t &s, bool valid, int i, bool y=false) {
    float v=valid?(y?s.rAxis[i].y:s.rAxis[i].x):0.0f;
    return fabsf(v)<0.12f?0.0f:v;
}


static void vr_logf(const char *fmt, ...) {
    if (!hook_log)
        return;
    va_list args;
    va_start(args, fmt);
    fprintf(hook_log, "[VR] ");
    vfprintf(hook_log, fmt, args);
    fprintf(hook_log, "\n");
    va_end(args);
    fflush(hook_log);
}

static void set_status(const char *s) {
    snprintf(g_state.status, sizeof(g_state.status), "%s", s);
}

static const char *const kProbedInterfaceVersions[] = {
    "IVRSystem_026", "IVRSystem_025", "IVRSystem_024", "IVRSystem_023",
    "IVRSystem_022", "IVRSystem_021", "IVRSystem_020", "IVRSystem_019",
};

static void init_frame_actions() {
    vr::EVRInitError err=vr::VRInitError_None;
    g_input=(vr::IVRInput*)p_VR_GetGenericInterface(vr::IVRInput_Version,&err);
    if(!g_input){vr_logf("IVRInput unavailable: %d",(int)err);return;}
    char path[MAX_PATH]{};GetModuleFileNameA(nullptr,path,sizeof(path));
    char *slash=strrchr(path,'\\');if(slash)*slash=0;
    strcat(path,"\\assets\\racer_openvr_actions.json");
    vr::EVRInputError e=g_input->SetActionManifestPath(path);
    vr_logf("SetActionManifestPath %s -> %d",path,(int)e);
    if(e!=vr::VRInputError_None){g_input=nullptr;return;}
    e=g_input->GetActionSetHandle("/actions/racer",&g_action_set);
    g_input->GetInputSourceHandle("/user/hand/left",&g_hand[0]);
    g_input->GetInputSourceHandle("/user/hand/right",&g_hand[1]);
    struct Action{const char*name;vr::VRActionHandle_t*out;};
    const Action actions[]={{"stick",&g_stick},{"trigger",&g_trigger},{"grip",&g_grip},{"primary",&g_primary},{"secondary",&g_secondary},{"stick_click",&g_click},{"menu",&g_menu}};
    for(auto &a:actions){char n[128];snprintf(n,sizeof(n),"/actions/racer/in/%s",a.name);vr_logf("action %s -> %d",a.name,(int)g_input->GetActionHandle(n,a.out));}
    if(e!=vr::VRInputError_None){g_input=nullptr;return;}
}
static bool read_frame_actions() {
    if(!g_input||!g_action_set)return false;
    vr::VRActiveActionSet_t set{};set.ulActionSet=g_action_set;
    auto e=g_input->UpdateActionState(&set,sizeof(set),1);
    if(e!=vr::VRInputError_None){static int n=0;if(n++<3)vr_logf("UpdateActionState error %d",(int)e);return false;}
    vr::VRControllerState_t *states[]={&g_left,&g_right};
    bool *valid[]={&g_left_valid,&g_right_valid};
    for(int hand=0;hand<2;++hand){
        auto &st=*states[hand];st={};bool active=false;
        vr::InputAnalogActionData_t a{};
        if(g_input->GetAnalogActionData(g_stick,&a,sizeof(a),g_hand[hand])==vr::VRInputError_None&&a.bActive){st.rAxis[0].x=a.x;st.rAxis[0].y=a.y;active=true;}
        a={};if(g_input->GetAnalogActionData(g_trigger,&a,sizeof(a),g_hand[hand])==vr::VRInputError_None&&a.bActive){st.rAxis[1].x=a.x;active=true;}
        struct Button{vr::VRActionHandle_t action;vr::EVRButtonId id;};
        const Button buttons[]={{g_primary,vr::k_EButton_A},{g_secondary,vr::k_EButton_ApplicationMenu},{g_grip,vr::k_EButton_Grip},{g_click,vr::k_EButton_SteamVR_Touchpad},{g_menu,vr::k_EButton_ApplicationMenu}};
        for(auto &b:buttons){vr::InputDigitalActionData_t d{};if(g_input->GetDigitalActionData(b.action,&d,sizeof(d),g_hand[hand])==vr::VRInputError_None&&d.bActive){active=true;if(d.bState)st.ulButtonPressed|=vr::ButtonMaskFromId(b.id);}}
        *valid[hand]=active;
        static uint64_t last[2]{};static int reported=0;
        if(st.ulButtonPressed!=last[hand]||reported<2){vr_logf("Frame actions hand=%d active=%d buttons=0x%llx stick=%+.2f,%+.2f trigger=%.2f",hand,active,(unsigned long long)st.ulButtonPressed,st.rAxis[0].x,st.rAxis[0].y,st.rAxis[1].x);last[hand]=st.ulButtonPressed;++reported;}
    }
    return true;
}

static bool load_openvr_module(void) {
    // Copied next to SWEP1RCR.EXE during setup, so the plain name resolves via the exe
    // directory. The absolute SteamVR path is the fallback for a library installed elsewhere.
    const char *candidates[] = {
        "openvr_api.dll",
        "C:\\Program Files (x86)\\Steam\\steamapps\\common\\SteamVR\\bin\\win32\\openvr_api.dll",
    };
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        g_openvr_module = LoadLibraryA(candidates[i]);
        if (g_openvr_module) {
            vr_logf("loaded openvr_api.dll from '%s'", candidates[i]);
            return true;
        }
    }
    vr_logf("could not load openvr_api.dll. GetLastError=%lu", GetLastError());
    return false;
}

#define RESOLVE(sym)                                                                               \
    do {                                                                                           \
        p_##sym = (PFN_##sym) GetProcAddress(g_openvr_module, #sym);                                \
        if (!p_##sym) {                                                                            \
            vr_logf("missing export '%s' -- wrong/old openvr_api.dll", #sym);                       \
            return false;                                                                          \
        }                                                                                          \
    } while (0)

static bool resolve_exports(void) {
    RESOLVE(VR_InitInternal);
    RESOLVE(VR_ShutdownInternal);
    RESOLVE(VR_IsHmdPresent);
    RESOLVE(VR_IsRuntimeInstalled);
    RESOLVE(VR_IsInterfaceVersionValid);
    RESOLVE(VR_GetGenericInterface);
    RESOLVE(VR_GetVRInitErrorAsEnglishDescription);
    return true;
}
#undef RESOLVE

// Everything below runs on a worker thread. VR_InitInternal can block FOREVER when SteamVR is
// left holding the Scene slot (a VR app that died without VR_ShutdownInternal). Called inline
// during DLL init, that took the whole game down with it: no window, a process that cannot be
// killed even with Stop-Process, and a reboot to recover. Three sessions were lost to it.
static void vr_probe_init_blocking(void) {
    vr_logf("---- EXPERIMENTAL FRAME OPENVR PORT: stereo + legacy controller actions ----");

    // Escape hatch. If SteamVR is left in a bad state (a Scene app that died without running
    // VR_ShutdownInternal), VR_InitInternal blocks forever and takes the whole game down with it
    // -- no window, and a process that cannot be killed. Setting SWE1R_NO_VR=1 skips VR entirely
    // so the flat game always launches.
    char no_vr[8] = {0};
    if (GetEnvironmentVariableA("SWE1R_NO_VR", no_vr, sizeof(no_vr)) > 0 && no_vr[0] == '1') {
        vr_logf("SWE1R_NO_VR=1 -- skipping VR init, running flat");
        set_status("disabled via SWE1R_NO_VR");
        g_gave_up = true;
        return;
    }

    if (!load_openvr_module()) {
        set_status("openvr_api.dll not found");
        g_gave_up = true;
        return;
    }
    if (!resolve_exports()) {
        set_status("openvr_api.dll missing exports");
        g_gave_up = true;
        return;
    }

    vr_logf("runtime installed: %s", p_VR_IsRuntimeInstalled() ? "yes" : "NO");
    vr_logf("hmd present      : %s", p_VR_IsHmdPresent() ? "yes" : "NO");

    // Scene first: only a Scene app may submit eye textures. Overlay/Background remain as
    // tracking-only fallbacks so a failure here still leaves a working pose readout rather than
    // nothing. Note Background deliberately refuses to *launch* vrserver (error 121) -- it only
    // attaches to an already-running SteamVR -- which is why it is last.
    struct InitMode {
        vr::EVRApplicationType type;
        const char *name;
        bool can_submit;
    };
    const InitMode modes[] = {
        {vr::VRApplication_Scene, "Scene", true},
        {vr::VRApplication_Overlay, "Overlay", false},
        {vr::VRApplication_Background, "Background", false},
    };

    vr::EVRInitError err = vr::VRInitError_None;
    const InitMode *mode_used = nullptr;
    for (size_t i = 0; i < sizeof(modes) / sizeof(modes[0]); i++) {
        err = vr::VRInitError_None;
        p_VR_InitInternal(&err, modes[i].type);
        if (err == vr::VRInitError_None) {
            mode_used = &modes[i];
            break;
        }
        vr_logf("VR_InitInternal(%s) failed: %s", modes[i].name,
                p_VR_GetVRInitErrorAsEnglishDescription(err));
    }
    if (!mode_used) {
        set_status("VR_InitInternal failed - see hook.log");
        g_gave_up = true;
        return;
    }
    g_is_scene_app = mode_used->can_submit;
    vr_logf("VR_InitInternal ok (VRApplication_%s), submission %s", mode_used->name,
            g_is_scene_app ? "available" : "NOT available (tracking only)");

    const char *matched = nullptr;
    for (size_t i = 0; i < sizeof(kProbedInterfaceVersions) / sizeof(kProbedInterfaceVersions[0]);
         i++) {
        const bool ok = p_VR_IsInterfaceVersionValid(kProbedInterfaceVersions[i]);
        if (ok && !matched)
            matched = kProbedInterfaceVersions[i];
    }
    if (!matched || strcmp(matched, vr::IVRSystem_Version) != 0) {
        // Refuse to bind a mismatched vtable. Better an inert probe than a hard crash.
        vr_logf("header is %s but runtime's newest accepted is %s -- NOT binding.",
                vr::IVRSystem_Version, matched ? matched : "<none>");
        set_status("interface version mismatch - see hook.log");
        g_gave_up = true;
        return;
    }

    err = vr::VRInitError_None;
    g_vr_system = (vr::IVRSystem *) p_VR_GetGenericInterface(vr::IVRSystem_Version, &err);
    if (!g_vr_system || err != vr::VRInitError_None) {
        vr_logf("VR_GetGenericInterface(%s) failed: %s", vr::IVRSystem_Version,
                p_VR_GetVRInitErrorAsEnglishDescription(err));
        set_status("could not get IVRSystem");
        g_gave_up = true;
        return;
    }

    g_vr_system->GetRecommendedRenderTargetSize(&g_target_w, &g_target_h);
    vr_logf("IVRSystem bound (%s). Recommended per-eye target: %ux%u", vr::IVRSystem_Version,
            g_target_w, g_target_h);

    if (g_is_scene_app) {
        err = vr::VRInitError_None;
        g_vr_compositor =
            (vr::IVRCompositor *) p_VR_GetGenericInterface(vr::IVRCompositor_Version, &err);
        if (!g_vr_compositor || err != vr::VRInitError_None) {
            vr_logf("VR_GetGenericInterface(%s) failed: %s -- tracking only",
                    vr::IVRCompositor_Version, p_VR_GetVRInitErrorAsEnglishDescription(err));
            g_is_scene_app = false;
        } else {
            vr_logf("IVRCompositor bound (%s)", vr::IVRCompositor_Version);
        }
    }

    if (InterlockedCompareExchange(&g_init_abandoned, 0, 0) != 0) {
        vr_logf("init finished late, after the main thread gave up -- discarding the session "
                "rather than switching VR on mid-run.");
        if (p_VR_ShutdownInternal)
            p_VR_ShutdownInternal();
        g_vr_system = nullptr;
        g_vr_compositor = nullptr;
        g_session_up = false;
        return;
    }

    init_frame_actions();
    g_session_up = true;
    set_status(g_is_scene_app ? "session up - submitting" : "session up - tracking only");
}

static DWORD WINAPI vr_init_thread(LPVOID) {
    vr_probe_init_blocking();
    return 0;
}

void vr_probe_init(void) {
    // The escape hatch is checked here, on the main thread, so SWE1R_NO_VR=1 costs nothing.
    char no_vr[8] = {0};
    if (GetEnvironmentVariableA("SWE1R_NO_VR", no_vr, sizeof(no_vr)) > 0 && no_vr[0] == '1') {
        vr_logf("SWE1R_NO_VR=1 -- skipping VR init, running flat");
        set_status("disabled via SWE1R_NO_VR");
        g_gave_up = true;
        return;
    }

    // Bounded wait. A wedged runtime must cost this run's VR, not the ability to launch.
    const DWORD kInitTimeoutMs = 10000;
    HANDLE thread = CreateThread(nullptr, 0, vr_init_thread, nullptr, 0, nullptr);
    if (thread == nullptr) {
        vr_probe_init_blocking();// no thread available; old behaviour is better than no VR
        return;
    }

    const DWORD wait = WaitForSingleObject(thread, kInitTimeoutMs);
    CloseHandle(thread);

    if (wait == WAIT_TIMEOUT) {
        // The worker is stuck inside the runtime and will never return. Leave it parked --
        // it is a background thread, so the game runs normally and ExitProcess reaps it.
        InterlockedExchange(&g_init_abandoned, 1);
        g_gave_up = true;
        g_session_up = false;
        set_status("init timed out - running flat");
        vr_logf("VR_InitInternal did not return within %lu ms. SteamVR is almost certainly "
                "holding the Scene slot from an app that died without VR_ShutdownInternal. "
                "CONTINUING FLAT -- restart SteamVR to get VR back.",
                kInitTimeoutMs);
    }
}

// Diagnostic euler extraction only; the camera path consumes the matrix, not these.
static void matrix_to_euler_deg(const vr::HmdMatrix34_t &m, float *yaw, float *pitch, float *roll) {
    const float kRad2Deg = 57.2957795f;
    *yaw = atan2f(m.m[0][2], m.m[2][2]) * kRad2Deg;
    *pitch = asinf(-m.m[1][2]) * kRad2Deg;
    *roll = atan2f(m.m[1][0], m.m[1][1]) * kRad2Deg;
}

void vr_probe_update(void) {
    if (g_gave_up || !g_session_up || !g_vr_system)
        return;

    g_state.frames++;

    vr::TrackedDevicePose_t render_poses[vr::k_unMaxTrackedDeviceCount];
    vr::TrackedDevicePose_t game_poses[vr::k_unMaxTrackedDeviceCount];

    if (g_is_scene_app && g_vr_compositor && g_state.submit_enabled) {
        // For a Scene app this is the frame-pacing sync point: it blocks until the compositor
        // wants the next frame and returns the pose predicted for that frame's display time.
        g_vr_compositor->WaitGetPoses(render_poses, vr::k_unMaxTrackedDeviceCount, game_poses,
                                      vr::k_unMaxTrackedDeviceCount);
    } else {
        // Seated origin: we never want tracked translation moving the pilot out of the pod.
        g_vr_system->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseSeated, 0.0f, render_poses,
                                                     vr::k_unMaxTrackedDeviceCount);
    }

    const vr::TrackedDevicePose_t &hmd = render_poses[vr::k_unTrackedDeviceIndex_Hmd];
    g_state.device_connected = hmd.bDeviceIsConnected;
    g_state.pose_valid = hmd.bPoseIsValid;

    if (!hmd.bPoseIsValid) {
        if (g_state.frames <= 5 || (g_state.frames % 300) == 0)
            vr_logf("frame %lu: HMD pose not valid (connected=%d)", g_state.frames,
                    (int) hmd.bDeviceIsConnected);
        return;
    }

    vr_input_poll();

    const vr::HmdMatrix34_t &m = hmd.mDeviceToAbsoluteTracking;
    matrix_to_euler_deg(m, &g_state.yaw_deg, &g_state.pitch_deg, &g_state.roll_deg);
    g_state.pos_x = m.m[0][3];
    g_state.pos_y = m.m[1][3];
    g_state.pos_z = m.m[2][3];
    g_state.valid_frames++;

    // World->head is the transpose of the pose's 3x3 (rotation only, so transpose == inverse).
    // Stored column-major to match rdMatrix44, whose vA..vD are the four columns.
    // Rotation only, deliberately: seated cockpit, no positional translation of the pilot.
    for (int c = 0; c < 3; c++)
        for (int r = 0; r < 3; r++)
            g_head_rot[c * 4 + r] = m.m[c][r];// transpose of m.m[r][c]
    g_head_rot[3] = g_head_rot[7] = g_head_rot[11] = 0.0f;
    g_head_rot[12] = g_head_rot[13] = g_head_rot[14] = 0.0f;
    g_head_rot[15] = 1.0f;

    if (g_state.valid_frames <= 5 || (g_state.valid_frames % 90) == 0) {
        vr_logf("frame %lu  yaw %+7.2f  pitch %+7.2f  roll %+7.2f   pos [%+6.3f %+6.3f %+6.3f] m",
                g_state.frames, g_state.yaw_deg, g_state.pitch_deg, g_state.roll_deg, g_state.pos_x,
                g_state.pos_y, g_state.pos_z);
    }
}

int vr_is_active(void) {
    if (g_gave_up || InterlockedCompareExchange(&g_init_abandoned, 0, 0) != 0)
        return 0;
    return (g_session_up && g_is_scene_app && g_vr_compositor && g_state.submit_enabled) ? 1 : 0;
}

void vr_get_target_size(unsigned int *width, unsigned int *height) {
    if (width)
        *width = g_target_w;
    if (height)
        *height = g_target_h;
}

int vr_render_all_selectors(void) {
    return (vr_is_active() && g_state.render_all_selectors) ? 1 : 0;
}

float vr_get_cull_fov_boost(void) {
    return g_state.cull_fov_boost;
}

float vr_get_2d_u_shift(int eye) {
    if (g_state.menu_shift == 0.0f)
        return 0.0f;
    float proj[16];
    if (!vr_get_eye_projection(eye, 1.0f, 100.0f, proj))
        return 0.0f;
    // Clip space spans 2 while UV spans 1, hence the half.
    return proj[8] * 0.5f * g_state.menu_shift;
}

void vr_input_poll(void) {
    static int probes=0;
    if(probes++ < 3) vr_logf("input poll: session=%d system=%p",(int)g_session_up,(void*)g_vr_system);
    if (!g_session_up || !g_vr_system)
        return;

    if(read_frame_actions())return;
    g_left_valid=g_right_valid=false;
    g_left={}; g_right={};
    static int logged = 0;
    static unsigned long tick = 0;
    tick++;
    const bool say = (logged < 40) && (tick % 30 == 0);

    for (vr::TrackedDeviceIndex_t i = 0; i < vr::k_unMaxTrackedDeviceCount; i++) {
        if (g_vr_system->GetTrackedDeviceClass(i) != vr::TrackedDeviceClass_Controller)
            continue;

        vr::VRControllerState_t st{};
        if (!g_vr_system->GetControllerState(i, &st, sizeof(st)))
            continue;

        static uint64_t last_buttons[vr::k_unMaxTrackedDeviceCount]{};
        if(st.ulButtonPressed != last_buttons[i]) {
            vr_logf("button change dev=%u pressed=0x%llx",i,(unsigned long long)st.ulButtonPressed);
            last_buttons[i]=st.ulButtonPressed;
        }
        const vr::ETrackedControllerRole role =
            g_vr_system->GetControllerRoleForTrackedDeviceIndex(i);
        if (role==vr::TrackedControllerRole_LeftHand) {g_left=st;g_left_valid=true;}
        if (role==vr::TrackedControllerRole_RightHand) {g_right=st;g_right_valid=true;}

        // Report anything non-neutral, so a single run shows which physical control maps to
        // which axis/button index without having to guess from documentation.
        const bool active = st.ulButtonPressed != 0 || st.ulButtonTouched != 0 ||
                            st.rAxis[0].x != 0.0f || st.rAxis[0].y != 0.0f ||
                            st.rAxis[1].x != 0.0f || st.rAxis[2].x != 0.0f;
        if (say && active && hook_log) {
            logged++;
            fprintf(hook_log,
                    "[VRIN] dev %u role=%d pressed=0x%08llx touched=0x%08llx "
                    "ax0=[%+.2f %+.2f] ax1=[%+.2f] ax2=[%+.2f]\n",
                    i, (int) role, (unsigned long long) st.ulButtonPressed,
                    (unsigned long long) st.ulButtonTouched, st.rAxis[0].x, st.rAxis[0].y,
                    st.rAxis[1].x, st.rAxis[2].x);
            fflush(hook_log);
        }
    }

    // Say once whether ANY controller was seen at all -- distinguishes 'no legacy input' from
    // 'controllers asleep'.
    static bool announced = false;
    if (!announced && hook_log) {
        announced = true;
        int count = 0;
        for (vr::TrackedDeviceIndex_t i = 0; i < vr::k_unMaxTrackedDeviceCount; i++)
            if (g_vr_system->GetTrackedDeviceClass(i) == vr::TrackedDeviceClass_Controller)
                count++;
        fprintf(hook_log, "[VRIN] tracked controllers visible: %d\n", count);
        fflush(hook_log);
    }
}

float vr_get_hud_scale(void) {
    return (g_state.hud_scale > 0.05f) ? g_state.hud_scale : 1.0f;
}

int vr_hud_redirect_enabled(void) {
    return g_state.hud_redirect ? 1 : 0;
}

int vr_should_dump_eyes(void) {
    return g_state.dump_eyes ? 1 : 0;
}

void vr_clear_dump_eyes(void) {
    g_state.dump_eyes = false;
}

int vr_eye_count(void) {
    return vr_is_active() ? 2 : 1;
}

int vr_eye_for_pass(int pass) {
    const int eye = (pass == 0) ? VR_EYE_LEFT : VR_EYE_RIGHT;
    if (!g_state.swap_eye_order)
        return eye;
    return (eye == VR_EYE_LEFT) ? VR_EYE_RIGHT : VR_EYE_LEFT;
}

void vr_frame_end(void) {
    ++g_serial;
    g_submitted_this_frame = false;
}

int vr_did_submit_this_frame(void) {
    return g_submitted_this_frame ? 1 : 0;
}

static void vr_submit_eye_shifted(int eye, unsigned int gl_color_texture, float u_shift);

void vr_submit_both(unsigned int gl_color_texture) {
    // The same pixels in both eyes do NOT fuse: the compositor maps the texture across each
    // eye's asymmetric frustum, whose principal point sits at proj[8] (measured here as
    // -0.24251 / +0.24251). That splits an identical image into two, ~0.24 of a screen width
    // apart. Shift each eye's sampled region by half that (clip space spans 2, UV spans 1) to
    // put the content back in the same apparent place.
    for (int eye = 0; eye < 2; eye++) {
        float u_shift = 0.0f;
        float proj[16];
        if (g_state.menu_shift != 0.0f && vr_get_eye_projection(eye, 1.0f, 100.0f, proj))
            u_shift = proj[8] * 0.5f * g_state.menu_shift;
        vr_submit_eye_shifted(eye, gl_color_texture, u_shift);
    }
}

void vr_set_current_eye(int eye) {
    g_current_eye = eye;
}

int vr_get_current_eye(void) {
    return g_current_eye;
}

int vr_get_eye_projection(int eye, float znear, float zfar, float *out16) {
    if (!out16 || !vr_is_active() || !g_vr_system)
        return 0;

    // MUST NOT use GetProjectionMatrix here. It returns HmdMatrix44_t *by value* from a virtual
    // method, and vrclient.dll is MSVC-built while this DLL is MinGW: the two disagree on where
    // the hidden struct-return pointer lives in the i386 thiscall convention, so the callee
    // writes through garbage and faults inside vrclient. GetProjectionRaw returns void and uses
    // out-params, so there is no struct-return ABI to get wrong.
    float l = 0, r = 0, t = 0, b = 0;
    g_vr_system->GetProjectionRaw(eye == VR_EYE_RIGHT ? vr::Eye_Right : vr::Eye_Left, &l, &r, &t,
                                  &b);

    // Logged once per eye so the actual sign convention is on record rather than assumed --
    // raw top/bottom are y-down in OpenVR, which the (b - t) below accounts for.
    static bool logged[2] = {false, false};
    const int ei = (eye == VR_EYE_RIGHT) ? 1 : 0;
    if (!logged[ei]) {
        logged[ei] = true;
        vr_logf("eye %d raw frustum tangents: l=%+.4f r=%+.4f t=%+.4f b=%+.4f", ei, l, r, t, b);
    }

    const float idx = 1.0f / (r - l);
    const float idy = 1.0f / (b - t);

    for (int i = 0; i < 16; i++)
        out16[i] = 0.0f;
    out16[0] = 2.0f * idx;
    out16[5] = 2.0f * idy;
    out16[8] = (r + l) * idx;
    out16[9] = (b + t) * idy;
    out16[10] = -(zfar + znear) / (zfar - znear);
    out16[11] = -1.0f;
    out16[14] = -2.0f * zfar * znear / (zfar - znear);
    return 1;
}

void vr_get_eye_view(int eye, float *out16) {
    if (!out16)
        return;

    static const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    if (!g_session_up || !g_state.pose_valid || !g_state.head_drives_camera) {
        memcpy(out16, identity, sizeof(identity));
        return;
    }

    // head_to_eye supplies the IPD separation; without it both eyes share an origin and there is
    // no stereo depth at all.
    //
    // MUST NOT use GetEyeToHeadTransform: like GetProjectionMatrix it returns a struct by value
    // from a virtual method, which is the MSVC/MinGW i386 struct-return ABI mismatch that faulted
    // inside vrclient.dll. Read the IPD as a plain float property instead and build the offset
    // ourselves. That models the eyes as a pure horizontal displacement about the head origin,
    // dropping any small forward/vertical offset or lens cant the headset may report -- correct
    // for a seated cockpit view, and revisitable via the C FnTable API if a headset needs it.
    float ipd = 0.064f;// sane default if the property is unavailable
    if (g_vr_system) {
        vr::ETrackedPropertyError perr = vr::TrackedProp_Success;
        const float v = g_vr_system->GetFloatTrackedDeviceProperty(
            vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_UserIpdMeters_Float, &perr);
        if (perr == vr::TrackedProp_Success && v > 0.02f && v < 0.10f)
            ipd = v;
    }
    static bool logged_ipd = false;
    if (!logged_ipd) {
        logged_ipd = true;
        vr_logf("IPD %.4f m (%.1f mm)", ipd, ipd * 1000.0f);
    }

    // World->eye = translate by -(eye offset). Left eye sits at -ipd/2 on the head's X axis, so
    // the view matrix translates the world by +ipd/2, and vice versa.
    // Convert the metric IPD into game units, or the eyes end up effectively co-located.
    const float scale = g_state.world_units_per_metre > 0.01f ? g_state.world_units_per_metre
                                                              : 1.0f;
    const float half_ipd = ipd * 0.5f * scale;
    float head_to_eye[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    head_to_eye[12] = (eye == VR_EYE_RIGHT) ? -half_ipd : half_ipd;

    // out = head_to_eye * head_rot  (column-major: out[c*4+r] = sum_k A[k*4+r] * B[c*4+k])
    for (int c = 0; c < 4; c++) {
        for (int r = 0; r < 4; r++) {
            float v = 0.0f;
            for (int k = 0; k < 4; k++)
                v += head_to_eye[k * 4 + r] * g_head_rot[c * 4 + k];
            out16[c * 4 + r] = v;
        }
    }
}

// u_shift moves the sampled region horizontally, in texture-width units. Zero for real stereo
// (each eye already rendered with its own frustum); non-zero for flat content sent to both
// eyes, to cancel the per-eye frustum asymmetry that would otherwise split it in two.
static void vr_submit_eye_shifted(int eye, unsigned int gl_color_texture, float u_shift) {
    if (!vr_is_active() || gl_color_texture == 0)
        return;

    vr::Texture_t tex{};
    tex.handle = (void *) (uintptr_t) gl_color_texture;
    tex.eType = vr::TextureType_OpenGL;
    tex.eColorSpace = vr::ColorSpace_Gamma;

    // GL renders bottom-up while the compositor samples top-down. Which way round this needs to
    // be depends on how the source FBO was filled, so it is a runtime toggle rather than a
    // guess baked into a build.
    vr::VRTextureBounds_t bounds{};
    bounds.uMin = 0.0f + u_shift;
    bounds.uMax = 1.0f + u_shift;
    bounds.vMin = g_state.flip_v ? 1.0f : 0.0f;
    bounds.vMax = g_state.flip_v ? 0.0f : 1.0f;

    const vr::EVREye vr_eye = (eye == VR_EYE_RIGHT) ? vr::Eye_Right : vr::Eye_Left;
    const vr::EVRCompositorError e = g_vr_compositor->Submit(vr_eye, &tex, &bounds);

    if (e != vr::VRCompositorError_None) {
        snprintf(g_state.submit_error, sizeof(g_state.submit_error), "Submit eye=%d err=%d", eye,
                 (int) e);
        static int logged = 0;
        if (logged < 5) {
            logged++;
            vr_logf("Submit failed: eye=%d err=%d (tex=%u)", eye, (int) e, gl_color_texture);
        }
        return;
    }
    g_state.submit_error[0] = '\0';
    g_state.submits++;
    g_submitted_this_frame = true;
    set_status("submitting to headset");
}

void vr_submit_eye(int eye, unsigned int gl_color_texture) {
    vr_submit_eye_shifted(eye, gl_color_texture, 0.0f);
}

void vr_probe_shutdown(void) {
    if (g_session_up && p_VR_ShutdownInternal) {
        p_VR_ShutdownInternal();
        vr_logf("session shut down after %lu frames (%lu tracked, %lu submitted)", g_state.frames,
                g_state.valid_frames, g_state.submits);
    }
    g_session_up = false;
    g_vr_system = nullptr;
    g_vr_compositor = nullptr;
}

// Per-pass draw counters, owned by renderer_hook.cpp.
extern int g_vr_last_pass_meshes[2];
extern int g_vr_last_pass_verts[2];

void vr_probe_draw_imgui(void) {
    ImGui::TextUnformatted("OpenVR - Milestone B1 (mono to both eyes)");
    ImGui::Separator();
    ImGui::Text("status    : %s", g_state.status);
    ImGui::Text("connected : %s   pose valid: %s", g_state.device_connected ? "yes" : "no",
                g_state.pose_valid ? "yes" : "no");
    ImGui::Text("scene app : %s", g_is_scene_app ? "yes (can submit)" : "no (tracking only)");
    ImGui::Text("target    : %ux%u per eye", g_target_w, g_target_h);
    ImGui::Text("frames    : %lu  tracked %lu  submitted %lu", g_state.frames, g_state.valid_frames,
                g_state.submits);
    if (g_state.submit_error[0])
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "submit err: %s", g_state.submit_error);
    ImGui::Separator();
    ImGui::Text("yaw %+7.2f  pitch %+7.2f  roll %+7.2f", g_state.yaw_deg, g_state.pitch_deg,
                g_state.roll_deg);
    ImGui::Text("pos %+6.3f %+6.3f %+6.3f m", g_state.pos_x, g_state.pos_y, g_state.pos_z);
    ImGui::Separator();
    // Watch these while the ground vanishes. If the counts DROP as it disappears, the geometry is
    // being skipped during traversal. If they hold steady, it is being drawn and simply not
    // visible -- depth, clipping, or a degenerate transform. Every guess so far has failed to
    // tell these two apart.
    ImGui::Text("meshes drawn L/R : %d / %d", g_vr_last_pass_meshes[0], g_vr_last_pass_meshes[1]);
    ImGui::Text("verts  drawn L/R : %d / %d", g_vr_last_pass_verts[0], g_vr_last_pass_verts[1]);

    ImGui::Separator();
    ImGui::Checkbox("HUD into VR layer (experimental)", &g_state.hud_redirect);
    ImGui::SliderFloat("HUD size", &g_state.hud_scale, 0.30f, 1.00f, "%.2f");
    ImGui::TextDisabled("Shrinks the HUD toward the centre so the corners stay in view.");
    ImGui::TextDisabled("Off = HUD on monitor only. On = diverted for the headset;\n"
                        "if it disappears everywhere, turn this back off.");
    if (ImGui::Button("Dump both eye images"))
        g_state.dump_eyes = true;
    ImGui::SameLine();
    ImGui::TextDisabled("-> vr_eye0.ppm / vr_eye1.ppm");
    ImGui::Checkbox("Submit to headset", &g_state.submit_enabled);
    ImGui::Checkbox("Head rotation drives camera", &g_state.head_drives_camera);
    ImGui::Checkbox("Flip image vertically", &g_state.flip_v);
    ImGui::Checkbox("Swap eye render order", &g_state.swap_eye_order);
    ImGui::SliderFloat("Menu/cutscene shift", &g_state.menu_shift, -2.0f, 2.0f, "%.2f");
    ImGui::TextDisabled("Flat 2D content only. 0 = doubled (old behaviour), 1 = corrected.\n"
                        "If menus split further apart, try -1.");
    ImGui::SliderFloat("World units per metre", &g_state.world_units_per_metre, 1.0f, 200.0f,
                       "%.1f", ImGuiSliderFlags_Logarithmic);
    ImGui::TextDisabled("Stereo separation. Too low = flat (no depth); too high = diorama.");
    ImGui::Checkbox("Draw all selector children", &g_state.render_all_selectors);
    ImGui::TextDisabled("For scenery that vanishes when you turn your head: the engine picks\n"
                        "visible track sections from the POD view, not yours.");
    ImGui::SliderFloat("Engine cull FOV boost", &g_state.cull_fov_boost, 1.0f, 3.0f, "%.2fx");
    ImGui::TextDisabled("Raise if geometry vanishes when you turn your head: the engine\n"
                        "culls against the POD's view, not yours. Costs offscreen work.");
    ImGui::TextDisabled("If the artifact follows the SECOND eye drawn, it is state leaking\n"
                        "between passes rather than that eye+s projection.");
    ImGui::TextDisabled("If the headset view is upside down, tick 'Flip image vertically'.");
}

float vr_world_units_per_metre_or_1(void) { return g_state.world_units_per_metre; }

float vr_harness_quit_seconds(void) { return 0; }

int vr_want_dpi_unaware(void) { return 1; }

void vr_begin_frame(void) { vr_input_poll(); }

int vr_cull_head_track(void) { return 0; }

float vr_eye_cone_half_deg(int eye) { return 0; }

float vr_head_deviation_deg(int eye) { return 0; }

void vr_menu_lock_axes(float *x, float *y) { if(fabsf(*x)>fabsf(*y))*y=0;else *x=0; }

float vr_cockpit_smooth(void) { return 0; }

float vr_cockpit_smooth_seconds(void) { return 0; }

float vr_cockpit_pitch_smooth(void) { return 0; }

float vr_cockpit_pitch_seconds(void) { return 0; }

unsigned long vr_frame_serial(void) { return g_serial; }

double vr_frame_time_s(void) { return GetTickCount64()/1000.0; }

float vr_overlay_scale(void) { return 1.65f; }

int vr_input_available(void) { return vr_is_active() && (g_left_valid || g_right_valid); }

float vr_input_steer(void) { return axis(g_left,g_left_valid,0); }

float vr_input_throttle(void) { return g_right_valid ? std::clamp(g_right.rAxis[1].x,0.0f,1.0f) : 0; }

float vr_input_brake(void) { return g_left_valid ? std::clamp(g_left.rAxis[1].x,0.0f,1.0f) : 0; }

int vr_input_boost(void) { return g_right_valid && button(g_right,vr::k_EButton_A); }

int vr_input_cancel(void) { return g_right_valid && button(g_right,vr::k_EButton_ApplicationMenu); }

int vr_input_menu(void) { return (g_left_valid && button(g_left,vr::k_EButton_ApplicationMenu)) || vr_input_cancel(); }

float vr_input_stick_y(void) { return axis(g_left,g_left_valid,0,true); }

float vr_input_pitch(void) { return axis(g_right,g_right_valid,0,true); }

float vr_input_pitch_x(void) { return axis(g_right,g_right_valid,0); }

int vr_input_view(void) { return g_left_valid && button(g_left,vr::k_EButton_SteamVR_Touchpad); }

void vr_perf_note_eye_render(double ms) {  }

void vr_perf_note_eye_geometry(int meshes, int verts) {  }

void vr_perf_mark(const char *name) {  }

int vr_cockpit_view(void) { return 1; }

void vr_cockpit_seat_offset(float *up, float *back, float *right) { if(up)*up=0;if(back)*back=0;if(right)*right=0; }

void vr_cockpit_note_pod(int pod) { g_pod=pod; }

int vr_cockpit_active_pod(void) { return g_pod; }

float vr_cockpit_roll(void) { return 0.35f; }

int vr_input_lookback(void) { return g_left_valid && button(g_left,vr::k_EButton_A); }

int vr_input_slide(void) { return g_right_valid && button(g_right,vr::k_EButton_SteamVR_Touchpad); }

int vr_input_repair(void) { return g_left_valid && button(g_left,vr::k_EButton_ApplicationMenu); }

int vr_input_roll_left(void) { return g_left_valid && button(g_left,vr::k_EButton_Grip); }

int vr_input_roll_right(void) { return g_right_valid && button(g_right,vr::k_EButton_Grip); }

void vr_haptic_pulse(float amplitude, float duration_ms) {  }

void vr_haptic_impact(float speed_loss) {  }

void vr_haptic_wall(float push) {  }

void vr_haptic_event(float amplitude, float duration_ms) {  }

void vr_draw_wheel_settings(void) {  }

int vr_wheel_enabled(void) { return 0; }

int vr_wheel_steer_axis(void) { return 0; }

int vr_wheel_range(void) { return 0; }

float vr_wheel_deadzone(void) { return 0; }

float vr_wheel_sensitivity(void) { return 0; }

int vr_wheel_invert(void) { return 0; }

int vr_wheel_suppress_game_input(void) { return 0; }

int vr_wheel_throttle_axis(void) { return 0; }

int vr_wheel_brake_axis(void) { return 0; }

float vr_wheel_pedal_threshold(void) { return 0; }

int vr_wheel_clutch_axis(void) { return 0; }

int vr_wheel_clutch_action(void) { return 0; }

int vr_wheel_dpad_base(void) { return 0; }

int vr_wheel_btn_index(int slot) { return 0; }

int vr_wheel_btn_action(int slot) { return 0; }

int vr_wheel_recal_generation(void) { return 0; }

int vr_analog_steering_enabled(void) { return 1; }

void vr_set_2d_layer(unsigned int gl_texture, int w, int h) {  }

void vr_recenter_panel(void) {  }

int vr_suppress_flares(void) { return 0; }

int vr_world_flares(void) { return 0; }

float vr_flare_size_units(void) { return 0; }

float vr_flare_angular_tan(void) { return 0; }

float vr_weather_size_mul(void) { return 1.0f; }

float vr_weather_streak_mul(void) { return 0.5f; }
  
