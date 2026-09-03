// ========================================================================
// rak-hus-legit  –  CS2 Internal  (build 14178)
// Smooth aim, soft RCS, improved triggerbot, subtle ESP, bomb timer
// ========================================================================

// -------------------- TUNABLES (edit these) --------------------
// Head bone Z offset (world units). Positive lifts the aim/ESP point
// toward the top of the skull. Typical useful range: 0.0 – 8.0
// 0.0 = raw bone  |  3.5 = default sweet spot  |  6–7 = top of head
static float g_HeadBoneZOffset = 3.5f;

// Triggerbot crosshair FOV tolerance in pixels (how close head must be
// to screen center before firing). Smaller = stricter. 0 = disable FOV check.
static float g_TriggerFovPx = 18.f;

#include <Windows.h>
#include <Psapi.h>
#include <cstdint>
#include <cstdio>
#include <algorithm>
#include <cmath>
#include <thread>
#include <chrono>
#include <fstream>
#include <string>
#include <sstream>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <d3d11.h>
#include <dxgi.h>
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_dx11.h"
#include "offsets.h"
#include "pattern_scan.h"
#include "core/runtime.h"
#include "kiero/kiero.h"
#include "kiero/minhook/include/MinHook.h"

#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

void InitConsole() {
    AllocConsole();
    FILE* f;
    freopen_s(&f, "CONOUT$", "w", stdout);
    freopen_s(&f, "CONOUT$", "w", stderr);
    SetConsoleTitleA("rak-hus-legit");
    printf("[+] rak-hus-legit console ready\n");
}
#define LOG(m)          printf("[+] %s\n", m)
#define LOG_FMT(f, ...) printf(f, __VA_ARGS__)

// -------------------- GLOBALS --------------------
static uintptr_t g_pES = 0;
static uintptr_t hClient = 0;
static bool g_patternsOk = false;
using SmokeDrawFn = void* (__fastcall*)(void*, void*, void*, void*, void*, void*);
static SmokeDrawFn oSmokeDrawArray = nullptr;

HMODULE g_hModule = nullptr;
volatile bool g_running = true;
volatile bool g_unloadRequested = false;
volatile bool g_featuresEnabled = true;  // END toggles this; DLL always stays loaded
volatile long g_presentBusy = 0; // Present re-entrancy for safe unload
static bool g_smokeNearby = false;
static int  g_smokeScanTick = 0;
HWND g_gameHwnd = nullptr;

ID3D11Device* g_pd3dDevice = nullptr;
ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
IDXGISwapChain* g_pSwapChain = nullptr;
ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;
bool g_imGuiInitialized = false;
WNDPROC g_OriginalWndProc = nullptr;
LRESULT CALLBACK HookedWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

struct Vector3 { float x, y, z; };
struct Vector2 { float x, y; };

static Vector3 g_targetAngles{};
static bool g_hasTarget = false;
static bool g_hitMarkerActive = false;
static std::chrono::steady_clock::time_point g_hitMarkerTime;
static std::chrono::steady_clock::time_point g_lastTrigger;
static float g_rcsPunchX = 0.f, g_rcsPunchY = 0.f;
static int g_localCtrlIndex = -1; // local controller entity-list slot (1..64)

// -------------------- LEGIT CONFIG --------------------
#define AIM_KEY_DEFAULT VK_XBUTTON2  // Mouse5 hold — not LMB
#define TRIGGER_KEY_DEFAULT VK_XBUTTON2

struct Config {
    // Aimbot (legit)
    bool aimEnabled = true;
    float aimFov = 50.0f;
    float aimSmooth = 0.72f;
    int aimBone = 0;               // 0 head 1 neck 2 chest
    int aimKey = AIM_KEY_DEFAULT;
    bool aimTeamCheck = true;
    bool aimVisibleOnly = false;  // spotted-only often blocks all targets
    bool aimOnlyWhenScoped = false;
    float aimHumanize = 0.0f;      // randomness causes jitter — off
    bool aimDrawFov = true;
    bool aimRecoilComp = false;    // off by default — was fighting mouse / soft RCS
    float aimRecoilCompStr = 1.0f; // 0-1 how much punch to bake into aim angles


    // Soft RCS
    bool rcsEnabled = false;  // angle write disabled in code
    float rcsStrength = 0.35f;     // keep mild if enabled
    int rcsStartBullet = 2;        // start after N shots

    // Triggerbot
    bool triggerEnabled = false;
    int triggerKey = TRIGGER_KEY_DEFAULT;
    int triggerDelayMin = 45;   // ms between shots (min)
    int triggerDelayMax = 95;   // ms between shots (max, randomized)
    bool triggerTeamCheck = true;
    bool triggerVisibleOnly = false;
    int  triggerBone = 0;            // 0 head, 1 neck, 2 chest — only fire on this bone
    bool triggerRcs = false;         // off by default — was fighting aimbot
    float triggerRcsStrength = 1.0f; // 0–1 full punch compensation
    float triggerBoneFov = 12.f;     // max pixels: bone must be this close to crosshair
    bool triggerHitchance = true;      // gate fire by estimated hit chance
    float triggerHitchanceMin = 55.f;  // only fire if HC >= this %
    bool triggerWeaponProfiles = true; // AWP slower / pistol faster delays
    bool triggerFlashCheck = true;     // don't fire while flashed
    bool triggerSmokeCheck = true;     // don't fire while in smoke cloud
    float triggerFlashMax = 0.35f;     // max m_flFlashDuration to still allow fire

    // ESP (subtle)
    bool espEnabled = true;
    bool espBox = true;
    bool espBoxOutline = true;
    bool espName = true;
    bool espHealth = true;
    bool espArmor = false;
    bool espWeapon = true;
    bool espDistance = true;
    bool espSkeleton = false;
    bool espHeadDot = true;
    bool espTeamCheck = true;
    bool espVisibleOnly = false;
    float espColorR = 0.95f, espColorG = 0.35f, espColorB = 0.35f;
    float espVisColorR = 0.25f, espVisColorG = 0.85f, espVisColorB = 0.45f;
    float espBoxThickness = 1.4f;
    float espMaxDistance = 80.f;
    float espYBias = 0.f;          // screen-space vertical bias (px); + = down
    bool  espSkeletonEveryOther = true; // cheaper skeleton
    bool  espOptimize = true;   // meters

    // Misc legit
    bool noFlash = true;
    bool noSmoke = false;          // often considered less legit
    bool noVisualRecoil = false; // only while spraying if enabled
    bool spectatorList = true;
    bool hitmarker = true;
    bool bombTimer = true;
    bool customCrosshair = false;
    float chSize = 6.f;
    float chGap = 3.f;
    float chThick = 1.5f;
    float chR = 0.f, chG = 1.f, chB = 0.4f;

    bool aimBonePriority = true;

    bool glowEnabled = false;
    float glowR = 0.9f, glowG = 0.2f, glowB = 0.2f;
    float glowA = 0.85f;
    int glowType = 3;

    bool soundEsp = true;
    float soundMinSpeed = 80.f;
    float soundMaxDist = 25.f;


    // Third person (hold or toggle)
    bool thirdPerson = false;      // master enable
    bool thirdPersonToggle = false; // false = hold key, true = toggle on key press
    int  thirdPersonKey = 0x05;  // VK_XBUTTON1 default (Mouse 4)
    float thirdPersonDist = 120.f; // reserved / future OverrideView distance

    // Watermark / UI
    bool watermark = true;

    // FOV changer (OverrideView when hooked)
    bool fovChanger = false; // permanently unused
    float fovValue = 100.f;

    // Sniper crosshair when unscoped
    bool sniperCrosshair = true;

    // Bomb world ESP
    bool bombEsp = true;

    // Grenade trajectory preview
    bool nadePred = true;
    int nadePredSteps = 40;

    // Hitlog floating numbers
    bool hitlog = true;

    // Punch authority: soft RCS yields while aiming with recoil-comp
    bool punchUnified = true;

    // CreateMove-style early punch path (Present early stage)
    bool earlyPunchPath = false;

    // Entity cache listener-style (stale names ok, less full work)
    bool entityCacheLite = true;

    // Environment visual grade (client overlay — safe, no engine sky writes)
    bool envEnabled = false;
    int  envPreset = 0;       // 0 off handled by envEnabled; 1 Night 2 Warm 3 Cold 4 Dark 5 Bright
    float envStrength = 0.45f;

    bool showMenu = false;
} g_config;

static int g_menuTab = 0;

static void* __fastcall hkSmokeDrawArray(void* a1, void* a2, void* a3, void* a4, void* a5, void* a6) {
    if (g_config.noSmoke)
        return nullptr;
    if (oSmokeDrawArray)
        return oSmokeDrawArray(a1, a2, a3, a4, a5, a6);
    return nullptr;
}


struct CachedPlayer {
    uintptr_t ctrl = 0;
    uintptr_t pawn = 0;
    int team = 0;
    int hp = 0;
    bool alive = false;
    int index = 0;
    // text cache (refresh every N ticks — ESP perf)
    char name[64]{};
    char weapon[32]{};
    int  textTick = 0;
};
static CachedPlayer g_cache[64];
static int g_cacheCount = 0;
static int g_cacheTick = 0;


// -------------------- CONFIG I/O --------------------
std::string GetConfigPath() {
    char path[MAX_PATH];
    GetModuleFileNameA(g_hModule, path, MAX_PATH);
    char* s = strrchr(path, '\\');
    if (s) *(s + 1) = 0;
    return std::string(path) + "legit.ini";
}

void SaveConfig() {
    std::ofstream f(GetConfigPath());
    if (!f) return;
    auto w = [&](const char* k, auto v) { f << k << "=" << v << "\n"; };
    w("aimEnabled", g_config.aimEnabled ? 1 : 0);
    w("aimFov", g_config.aimFov);
    w("aimSmooth", g_config.aimSmooth);
    w("aimBone", g_config.aimBone);
    w("aimKey", g_config.aimKey);
    w("aimTeamCheck", g_config.aimTeamCheck ? 1 : 0);
    w("aimVisibleOnly", g_config.aimVisibleOnly ? 1 : 0);
    w("aimOnlyWhenScoped", g_config.aimOnlyWhenScoped ? 1 : 0);
    w("aimHumanize", g_config.aimHumanize);
    w("aimDrawFov", g_config.aimDrawFov ? 1 : 0);
    w("aimRecoilComp", g_config.aimRecoilComp ? 1 : 0);
    w("aimRecoilCompStr", g_config.aimRecoilCompStr);
    w("rcsEnabled", g_config.rcsEnabled ? 1 : 0);
    w("rcsStrength", g_config.rcsStrength);
    w("rcsStartBullet", g_config.rcsStartBullet);
    w("triggerEnabled", g_config.triggerEnabled ? 1 : 0);
    w("triggerKey", g_config.triggerKey);
    w("triggerDelayMin", g_config.triggerDelayMin);
    w("triggerDelayMax", g_config.triggerDelayMax);
    w("triggerTeamCheck", g_config.triggerTeamCheck ? 1 : 0);
    w("triggerVisibleOnly", g_config.triggerVisibleOnly ? 1 : 0);
    w("triggerBone", g_config.triggerBone);
    w("triggerRcs", g_config.triggerRcs ? 1 : 0);
    w("triggerRcsStrength", g_config.triggerRcsStrength);
    w("triggerBoneFov", g_config.triggerBoneFov);
    w("triggerHitchance", g_config.triggerHitchance ? 1 : 0);
    w("triggerHitchanceMin", g_config.triggerHitchanceMin);
    w("triggerWeaponProfiles", g_config.triggerWeaponProfiles ? 1 : 0);
    w("triggerFlashCheck", g_config.triggerFlashCheck ? 1 : 0);
    w("triggerSmokeCheck", g_config.triggerSmokeCheck ? 1 : 0);
    w("triggerFlashMax", g_config.triggerFlashMax);
    w("espEnabled", g_config.espEnabled ? 1 : 0);
    w("espBox", g_config.espBox ? 1 : 0);
    w("espBoxOutline", g_config.espBoxOutline ? 1 : 0);
    w("espName", g_config.espName ? 1 : 0);
    w("espHealth", g_config.espHealth ? 1 : 0);
    w("espArmor", g_config.espArmor ? 1 : 0);
    w("espWeapon", g_config.espWeapon ? 1 : 0);
    w("espDistance", g_config.espDistance ? 1 : 0);
    w("espSkeleton", g_config.espSkeleton ? 1 : 0);
    w("espHeadDot", g_config.espHeadDot ? 1 : 0);
    w("espTeamCheck", g_config.espTeamCheck ? 1 : 0);
    w("espVisibleOnly", g_config.espVisibleOnly ? 1 : 0);
    w("espColorR", g_config.espColorR); w("espColorG", g_config.espColorG); w("espColorB", g_config.espColorB);
    w("espVisColorR", g_config.espVisColorR); w("espVisColorG", g_config.espVisColorG); w("espVisColorB", g_config.espVisColorB);
    w("espBoxThickness", g_config.espBoxThickness);
    w("espMaxDistance", g_config.espMaxDistance);
    w("espYBias", g_config.espYBias);
    w("espSkeletonEveryOther", g_config.espSkeletonEveryOther ? 1 : 0);
    w("espOptimize", g_config.espOptimize ? 1 : 0);
    w("noFlash", g_config.noFlash ? 1 : 0);
    w("noSmoke", g_config.noSmoke ? 1 : 0);
    w("noVisualRecoil", g_config.noVisualRecoil ? 1 : 0);
    w("spectatorList", g_config.spectatorList ? 1 : 0);
    w("hitmarker", g_config.hitmarker ? 1 : 0);
    w("bombTimer", g_config.bombTimer ? 1 : 0);
    w("thirdPerson", g_config.thirdPerson ? 1 : 0);
    w("thirdPersonToggle", g_config.thirdPersonToggle ? 1 : 0);
    w("thirdPersonKey", g_config.thirdPersonKey);
    w("thirdPersonDist", g_config.thirdPersonDist);
    w("entityCacheLite", g_config.entityCacheLite ? 1 : 0);
    w("envEnabled", g_config.envEnabled ? 1 : 0);
    w("envPreset", g_config.envPreset);
    w("envStrength", g_config.envStrength);
    w("earlyPunchPath", g_config.earlyPunchPath ? 1 : 0);
    w("punchUnified", g_config.punchUnified ? 1 : 0);
    w("hitlog", g_config.hitlog ? 1 : 0);
    w("nadePred", g_config.nadePred ? 1 : 0);
    w("nadePredSteps", g_config.nadePredSteps);
    w("bombEsp", g_config.bombEsp ? 1 : 0);
    w("sniperCrosshair", g_config.sniperCrosshair ? 1 : 0);
    w("fovValue", g_config.fovValue);
    w("fovChanger", g_config.fovChanger ? 1 : 0);
    w("watermark", g_config.watermark ? 1 : 0);
    w("customCrosshair", g_config.customCrosshair ? 1 : 0);
    w("chSize", g_config.chSize); w("chGap", g_config.chGap); w("chThick", g_config.chThick);
    w("chR", g_config.chR); w("chG", g_config.chG); w("chB", g_config.chB);
    w("aimBonePriority", g_config.aimBonePriority ? 1 : 0);
    w("glowEnabled", g_config.glowEnabled ? 1 : 0);
    w("glowR", g_config.glowR); w("glowG", g_config.glowG); w("glowB", g_config.glowB); w("glowA", g_config.glowA);
    w("glowType", g_config.glowType);
    w("soundEsp", g_config.soundEsp ? 1 : 0);
    w("soundMinSpeed", g_config.soundMinSpeed);
    w("soundMaxDist", g_config.soundMaxDist);
}

void LoadConfig() {
    std::ifstream file(GetConfigPath());
    if (!file.is_open()) { SaveConfig(); return; }
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        try {
            if (k == "aimEnabled") g_config.aimEnabled = std::stoi(v) != 0;
            else if (k == "aimFov") g_config.aimFov = std::stof(v);
            else if (k == "aimSmooth") g_config.aimSmooth = std::stof(v);
            else if (k == "aimBone") g_config.aimBone = std::stoi(v);
            else if (k == "aimKey") g_config.aimKey = std::stoi(v);
            else if (k == "aimTeamCheck") g_config.aimTeamCheck = std::stoi(v) != 0;
            else if (k == "aimVisibleOnly") g_config.aimVisibleOnly = std::stoi(v) != 0;
            else if (k == "aimOnlyWhenScoped") g_config.aimOnlyWhenScoped = std::stoi(v) != 0;
            else if (k == "aimHumanize") g_config.aimHumanize = std::stof(v);
            else if (k == "aimDrawFov") g_config.aimDrawFov = std::stoi(v) != 0;
            else if (k == "aimRecoilComp") g_config.aimRecoilComp = std::stoi(v) != 0;
            else if (k == "aimRecoilCompStr") g_config.aimRecoilCompStr = std::stof(v);
            else if (k == "rcsEnabled") g_config.rcsEnabled = std::stoi(v) != 0;
            else if (k == "rcsStrength") g_config.rcsStrength = std::stof(v);
            else if (k == "rcsStartBullet") g_config.rcsStartBullet = std::stoi(v);
            else if (k == "triggerEnabled") g_config.triggerEnabled = std::stoi(v) != 0;
            else if (k == "triggerKey") g_config.triggerKey = std::stoi(v);
            else if (k == "triggerDelayMin") g_config.triggerDelayMin = std::stoi(v);
            else if (k == "triggerDelayMax") g_config.triggerDelayMax = std::stoi(v);
            else if (k == "triggerTeamCheck") g_config.triggerTeamCheck = std::stoi(v) != 0;
            else if (k == "triggerVisibleOnly") g_config.triggerVisibleOnly = std::stoi(v) != 0;
            else if (k == "triggerBone") g_config.triggerBone = std::stoi(v);
            else if (k == "triggerRcs") g_config.triggerRcs = std::stoi(v) != 0;
            else if (k == "triggerRcsStrength") g_config.triggerRcsStrength = std::stof(v);
            else if (k == "triggerBoneFov") g_config.triggerBoneFov = std::stof(v);
            else if (k == "triggerHitchance") g_config.triggerHitchance = std::stoi(v) != 0;
            else if (k == "triggerHitchanceMin") g_config.triggerHitchanceMin = std::stof(v);
            else if (k == "triggerWeaponProfiles") g_config.triggerWeaponProfiles = std::stoi(v) != 0;
            else if (k == "triggerFlashCheck") g_config.triggerFlashCheck = std::stoi(v) != 0;
            else if (k == "triggerSmokeCheck") g_config.triggerSmokeCheck = std::stoi(v) != 0;
            else if (k == "triggerFlashMax") g_config.triggerFlashMax = std::stof(v);
            else if (k == "espEnabled") g_config.espEnabled = std::stoi(v) != 0;
            else if (k == "espBox") g_config.espBox = std::stoi(v) != 0;
            else if (k == "espBoxOutline") g_config.espBoxOutline = std::stoi(v) != 0;
            else if (k == "espName") g_config.espName = std::stoi(v) != 0;
            else if (k == "espHealth") g_config.espHealth = std::stoi(v) != 0;
            else if (k == "espArmor") g_config.espArmor = std::stoi(v) != 0;
            else if (k == "espWeapon") g_config.espWeapon = std::stoi(v) != 0;
            else if (k == "espDistance") g_config.espDistance = std::stoi(v) != 0;
            else if (k == "espSkeleton") g_config.espSkeleton = std::stoi(v) != 0;
            else if (k == "espHeadDot") g_config.espHeadDot = std::stoi(v) != 0;
            else if (k == "espTeamCheck") g_config.espTeamCheck = std::stoi(v) != 0;
            else if (k == "espVisibleOnly") g_config.espVisibleOnly = std::stoi(v) != 0;
            else if (k == "espColorR") g_config.espColorR = std::stof(v);
            else if (k == "espColorG") g_config.espColorG = std::stof(v);
            else if (k == "espColorB") g_config.espColorB = std::stof(v);
            else if (k == "espVisColorR") g_config.espVisColorR = std::stof(v);
            else if (k == "espVisColorG") g_config.espVisColorG = std::stof(v);
            else if (k == "espVisColorB") g_config.espVisColorB = std::stof(v);
            else if (k == "espBoxThickness") g_config.espBoxThickness = std::stof(v);
            else if (k == "espMaxDistance") g_config.espMaxDistance = std::stof(v);
            else if (k == "espYBias") g_config.espYBias = std::stof(v);
            else if (k == "espSkeletonEveryOther") g_config.espSkeletonEveryOther = std::stoi(v) != 0;
            else if (k == "espOptimize") g_config.espOptimize = std::stoi(v) != 0;
            else if (k == "noFlash") g_config.noFlash = std::stoi(v) != 0;
            else if (k == "noSmoke") g_config.noSmoke = std::stoi(v) != 0;
            else if (k == "noVisualRecoil") g_config.noVisualRecoil = std::stoi(v) != 0;
            else if (k == "spectatorList") g_config.spectatorList = std::stoi(v) != 0;
            else if (k == "hitmarker") g_config.hitmarker = std::stoi(v) != 0;
            else if (k == "bombTimer") g_config.bombTimer = std::stoi(v) != 0;
            else if (k == "thirdPerson") g_config.thirdPerson = std::stoi(v) != 0;
            else if (k == "thirdPersonToggle") g_config.thirdPersonToggle = std::stoi(v) != 0;
            else if (k == "thirdPersonKey") g_config.thirdPersonKey = std::stoi(v);
            else if (k == "thirdPersonDist") g_config.thirdPersonDist = std::stof(v);
            else if (k == "entityCacheLite") g_config.entityCacheLite = std::stoi(v) != 0;
            else if (k == "envEnabled") g_config.envEnabled = std::stoi(v) != 0;
            else if (k == "envPreset") g_config.envPreset = std::stoi(v);
            else if (k == "envStrength") g_config.envStrength = std::stof(v);
            else if (k == "earlyPunchPath") g_config.earlyPunchPath = std::stoi(v) != 0;
            else if (k == "punchUnified") g_config.punchUnified = std::stoi(v) != 0;
            else if (k == "hitlog") g_config.hitlog = std::stoi(v) != 0;
            else if (k == "nadePred") g_config.nadePred = std::stoi(v) != 0;
            else if (k == "nadePredSteps") g_config.nadePredSteps = std::stoi(v);
            else if (k == "bombEsp") g_config.bombEsp = std::stoi(v) != 0;
            else if (k == "sniperCrosshair") g_config.sniperCrosshair = std::stoi(v) != 0;
            else if (k == "fovValue") g_config.fovValue = std::stof(v);
            else if (k == "fovChanger") g_config.fovChanger = std::stoi(v) != 0;
            else if (k == "watermark") g_config.watermark = std::stoi(v) != 0;
            else if (k == "customCrosshair") g_config.customCrosshair = std::stoi(v) != 0;
            else if (k == "chSize") g_config.chSize = std::stof(v);
            else if (k == "chGap") g_config.chGap = std::stof(v);
            else if (k == "chThick") g_config.chThick = std::stof(v);
            else if (k == "chR") g_config.chR = std::stof(v);
            else if (k == "chG") g_config.chG = std::stof(v);
            else if (k == "chB") g_config.chB = std::stof(v);
            else if (k == "aimBonePriority") g_config.aimBonePriority = std::stoi(v) != 0;
            else if (k == "glowEnabled") g_config.glowEnabled = std::stoi(v) != 0;
            else if (k == "glowR") g_config.glowR = std::stof(v);
            else if (k == "glowG") g_config.glowG = std::stof(v);
            else if (k == "glowB") g_config.glowB = std::stof(v);
            else if (k == "glowA") g_config.glowA = std::stof(v);
            else if (k == "glowType") g_config.glowType = std::stoi(v);
            else if (k == "soundEsp") g_config.soundEsp = std::stoi(v) != 0;
            else if (k == "soundMinSpeed") g_config.soundMinSpeed = std::stof(v);
            else if (k == "soundMaxDist") g_config.soundMaxDist = std::stof(v);
        }
        catch (...) {}
    }
}

// -------------------- SAFE MEMORY --------------------
static bool IsValid(uintptr_t a) { return a > 0x10000 && a < 0x7FFFFFFFFFFF; }

template<typename T>
static T SafeRead(uintptr_t address, T fallback = T{}) {
    __try { return *(T*)address; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return fallback; }
}
template<typename T>
static void SafeWrite(uintptr_t address, T value) {
    __try { *(T*)address = value; }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}
static void SafeMemcpy(void* dst, const void* src, size_t n) {
    __try { memcpy(dst, src, n); }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}
static void SafeReadArray(uintptr_t address, char* buffer, size_t maxLen) {
    __try {
        const char* p = (const char*)address;
        for (size_t i = 0; i < maxLen - 1; i++) { buffer[i] = p[i]; if (!p[i]) break; }
        buffer[maxLen - 1] = 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { buffer[0] = 0; }
}
static bool IsKeyDown(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

static float RandF(float a, float b) {
    return a + (b - a) * (float)(rand() % 10000) / 10000.f;
}

// -------------------- ENTITY --------------------
static uintptr_t GetEntity(int idx) {
    if (idx < 0 || idx > 4096 || !g_pES || !IsValid(g_pES)) return 0;
    __try {
        uintptr_t listPtr = g_pES + O::kListOffset;
        uintptr_t chunk = SafeRead<uintptr_t>(listPtr + (idx / O::kChunk) * 8, 0);
        if (!IsValid(chunk)) return 0;
        uintptr_t identity = chunk + (idx % O::kChunk) * O::kStride;
        uintptr_t ent = SafeRead<uintptr_t>(identity, 0);
        return IsValid(ent) ? ent : 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
static uintptr_t HandleToEnt(uint32_t h) {
    if (!h || h == 0xFFFFFFFF || h == 0xFFFFFF) return 0;
    int idx = (int)(h & 0x7FFF);
    if (idx <= 0 || idx > 0x7FFE) return 0;
    return GetEntity(idx);
}
static int HP(uintptr_t e) {
    if (!IsValid(e)) return 0;
    int v = SafeRead<int>(e + O::m_iHealth, 0);
    if (v < 0 || v > 200) return 0;
    return v;
}
static int Team(uintptr_t e) {
    if (!IsValid(e)) return 0;
    return (int)SafeRead<uint8_t>(e + O::m_iTeamNum, 0);
}
static int Armor(uintptr_t e) {
    if (!IsValid(e)) return 0;
    int v = SafeRead<int>(e + O::m_ArmorValue, 0);
    if (v < 0 || v > 200) return 0;
    return v;
}

// Forward decls used by IsAlive / cache
static Vector3 GetOrigin(uintptr_t pawn);
static Vector3 GetViewOffset(uintptr_t pawn);
static int ResolveLocalControllerIndex(uintptr_t localPawn);

static bool IsDormant(uintptr_t pawn) {
    if (!IsValid(pawn)) return true;
    uintptr_t sn = SafeRead<uintptr_t>(pawn + O::m_pGameSceneNode, 0);
    if (!IsValid(sn)) return true;
    return SafeRead<uint8_t>(sn + O::m_bDormant, 1) != 0;
}

static bool OriginSane(const Vector3& o) {
    if (!std::isfinite(o.x) || !std::isfinite(o.y) || !std::isfinite(o.z)) return false;
    if (fabsf(o.x) < 1.f && fabsf(o.y) < 1.f && fabsf(o.z) < 1.f) return false;
    if (fabsf(o.x) > 16384.f || fabsf(o.y) > 16384.f || fabsf(o.z) > 2048.f) return false;
    return true;
}

static bool IsAlive(uintptr_t pawn) {
    if (!IsValid(pawn)) return false;
    if (SafeRead<uint8_t>(pawn + O::m_lifeState, 1) != 0) return false;
    int hp = HP(pawn);
    if (hp <= 0 || hp > 100) return false;
    int team = Team(pawn);
    if (team != 2 && team != 3) return false;
    if (IsDormant(pawn)) return false;
    return true;
}

static bool ControllerPawnAlive(uintptr_t ctrl) {
    if (!IsValid(ctrl)) return false;
    return SafeRead<uint8_t>(ctrl + O::m_bPawnIsAlive, 0) != 0;
}

static bool IsInGame() {
    if (!hClient || !IsValid(hClient)) return false;
    if (!g_pES || !IsValid(g_pES)) return false;
    uintptr_t lc = SafeRead<uintptr_t>(hClient + O::dwLocalPlayerController, 0);
    if (!IsValid(lc)) return false;
    uintptr_t lp = SafeRead<uintptr_t>(hClient + O::dwLocalPlayerPawn, 0);
    return IsValid(lp);
}

static void GetPlayerName(uintptr_t ctrl, char* buf, size_t len);
static void GetWeaponName(uintptr_t pawn, char* buf, size_t len);

static void UpdatePlayerTextCache(CachedPlayer& c) {
    if (!c.alive) { c.name[0] = 0; c.weapon[0] = 0; return; }
    if (c.textTick + 20 >= g_cacheTick && c.name[0]) return; // still fresh
    c.name[0] = 0; c.weapon[0] = 0;
    if (IsValid(c.ctrl)) GetPlayerName(c.ctrl, c.name, sizeof(c.name));
    if (IsValid(c.pawn)) GetWeaponName(c.pawn, c.weapon, sizeof(c.weapon));
    c.textTick = g_cacheTick;
}

void RefreshEntityCache() {
    g_cacheTick++;

    if (!IsInGame()) {
        g_cacheCount = 0;
        g_localCtrlIndex = -1;
        memset(g_cache, 0, sizeof(g_cache));
        return;
    }

    uintptr_t localCtrl = SafeRead<uintptr_t>(hClient + O::dwLocalPlayerController, 0);
    uintptr_t localPawn = SafeRead<uintptr_t>(hClient + O::dwLocalPlayerPawn, 0);

    g_localCtrlIndex = ResolveLocalControllerIndex(localPawn);
    if (g_localCtrlIndex < 1 && IsValid(localCtrl)) {
        for (int i = 1; i <= 64; i++) {
            if (GetEntity(i) == localCtrl) { g_localCtrlIndex = i; break; }
        }
    }

    // Optimized: only 1..64 controllers (not full entity list)
    int n = 0;
    for (int i = 1; i <= 64 && n < 64; i++) {
        uintptr_t ctrl = GetEntity(i);
        if (!IsValid(ctrl)) continue;
        if (localCtrl && ctrl == localCtrl && g_localCtrlIndex < 1)
            g_localCtrlIndex = i;

        if (ctrl != localCtrl && !ControllerPawnAlive(ctrl))
            continue;

        uint32_t hPawn = SafeRead<uint32_t>(ctrl + O::m_hPlayerPawn, 0);
        uintptr_t pawn = HandleToEnt(hPawn);
        if (ctrl != localCtrl) {
            if (!IsValid(pawn) || !IsAlive(pawn)) continue;
            Vector3 o = GetOrigin(pawn);
            if (!OriginSane(o)) continue;
        }

        CachedPlayer& c = g_cache[n++];
        c.ctrl = ctrl; c.pawn = pawn; c.index = i;
        c.alive = IsValid(pawn) && IsAlive(pawn);
        c.team = IsValid(pawn) ? Team(pawn) : 0;
        c.hp = IsValid(pawn) ? HP(pawn) : 0;
        // text fields filled lazily by UpdatePlayerTextCache()
    }
    g_cacheCount = n;
}

static Vector3 GetOrigin(uintptr_t pawn) {
    Vector3 o{};
    if (!IsValid(pawn)) return o;
    uintptr_t sn = SafeRead<uintptr_t>(pawn + O::m_pGameSceneNode, 0);
    if (!IsValid(sn)) return o;
    // MUST use m_vecAbsOrigin (0xC8). m_vecOrigin (0x80) is quantized cell coords — wrong for W2S.
    __try {
        o.x = *(float*)(sn + O::m_vecAbsOrigin);
        o.y = *(float*)(sn + O::m_vecAbsOrigin + 4);
        o.z = *(float*)(sn + O::m_vecAbsOrigin + 8);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
    return o;
}
static Vector3 GetViewOffset(uintptr_t pawn) {
    Vector3 o{};
    if (!IsValid(pawn)) return o;
    __try {
        o.x = *(float*)(pawn + O::m_vecViewOffset);
        o.y = *(float*)(pawn + O::m_vecViewOffset + 4);
        o.z = *(float*)(pawn + O::m_vecViewOffset + 8);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
    return o;
}

static bool GetBonePos(uintptr_t pawn, int boneIdx, Vector3& out) {
    out = {};
    if (!IsValid(pawn)) return false;

    Vector3 origin = GetOrigin(pawn);
    Vector3 vo = GetViewOffset(pawn);
    // View offset Z tracks stand/crouch correctly; X/Y usually ~0
    float headZ = (vo.z > 8.f) ? vo.z : 72.f;
    // Ducking: flags or small view offset
    uint32_t flags = SafeRead<uint32_t>(pawn + O::m_fFlags, 0);
    if (flags & O::FL_DUCKING)
        headZ = (vo.z > 8.f) ? vo.z : 54.f;

    // Reliable approx (works rotated + crouched) — used as default for head/neck
    // Head uses g_HeadBoneZOffset (tunable at top of file)
    Vector3 approx = origin;
    if (boneIdx == O::Bone::head || boneIdx == 6) {
        approx.z = origin.z + headZ + g_HeadBoneZOffset;
    }
    else if (boneIdx == O::Bone::neck || boneIdx == 5) {
        approx.z = origin.z + headZ * 0.82f;
    }
    else if (boneIdx == O::Bone::spine || boneIdx == 4) {
        approx.z = origin.z + headZ * 0.55f;
    }
    else if (boneIdx == O::Bone::pelvis || boneIdx == 0) {
        approx.z = origin.z + 12.f;
    }
    else {
        approx.z = origin.z + headZ * 0.4f;
    }

    uintptr_t sn = SafeRead<uintptr_t>(pawn + O::m_pGameSceneNode, 0);
    if (!IsValid(sn)) { out = approx; return true; }

    uintptr_t boneArray = SafeRead<uintptr_t>(sn + O::m_modelState + O::m_boneArray, 0);
    if (!IsValid(boneArray))
        boneArray = SafeRead<uintptr_t>(sn + 0x1E0, 0);
    if (!IsValid(boneArray)) { out = approx; return true; }

    auto applyHeadOffset = [&](float& x, float& y, float& z) {
        // Lift head bone toward top of skull using tunable
        if (boneIdx == O::Bone::head || boneIdx == 6)
            z += g_HeadBoneZOffset;
        out.x = x; out.y = y; out.z = z;
        };

    __try {
        // CS2 bone_data: 32 bytes, world position at +0
        float x = *(float*)(boneArray + boneIdx * 32 + 0);
        float y = *(float*)(boneArray + boneIdx * 32 + 4);
        float z = *(float*)(boneArray + boneIdx * 32 + 8);
        float dx = x - origin.x, dy = y - origin.y, dz = z - origin.z;
        float dist2 = dx * dx + dy * dy + dz * dz;
        // Bone must sit near the body (rejects wrong stride / stale matrices when rotated)
        if (dist2 > 1.f && dist2 < 95.f * 95.f) {
            applyHeadOffset(x, y, z);
            return true;
        }
        // matrix3x4 48-byte fallback
        float* m48 = (float*)(boneArray + boneIdx * 48);
        x = m48[3]; y = m48[7]; z = m48[11];
        dx = x - origin.x; dy = y - origin.y; dz = z - origin.z;
        dist2 = dx * dx + dy * dy + dz * dz;
        if (dist2 > 1.f && dist2 < 95.f * 95.f) {
            applyHeadOffset(x, y, z);
            return true;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}

    out = approx;
    return true;
}

static int AimBoneIndex() {
    switch (g_config.aimBone) {
    case 1: return O::Bone::neck;
    case 2: return O::Bone::spine;
    default: return O::Bone::head;
    }
}

static float viewMatrix[16];
bool WorldToScreen(const Vector3& world, Vector2& screen, int sw, int sh, bool applyEspBias = false) {
    __try {
        float cx = viewMatrix[0]*world.x + viewMatrix[1]*world.y + viewMatrix[2]*world.z + viewMatrix[3];
        float cy = viewMatrix[4]*world.x + viewMatrix[5]*world.y + viewMatrix[6]*world.z + viewMatrix[7];
        float cw = viewMatrix[12]*world.x + viewMatrix[13]*world.y + viewMatrix[14]*world.z + viewMatrix[15];
        if (cw < 0.001f) return false;
        float inv = 1.f / cw;
        screen.x = (sw * 0.5f) + (cx * inv) * (sw * 0.5f);
        screen.y = (sh * 0.5f) - (cy * inv) * (sh * 0.5f);
        if (applyEspBias)
            screen.y += g_config.espYBias;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static void GetPlayerName(uintptr_t ctrl, char* buf, size_t len) {
    buf[0] = 0;
    if (IsValid(ctrl)) SafeReadArray(ctrl + O::m_iszPlayerName, buf, len);
}

// CS2 EntitySpottedState_t::m_bSpottedByMask = uint32[2]
// Slot bits are 0-based: controller entity index - 1
static bool SpottedMaskBit(uint32_t mask0, uint32_t mask1, int zeroBasedSlot) {
    if (zeroBasedSlot < 0 || zeroBasedSlot >= 64) return false;
    if (zeroBasedSlot < 32) return (mask0 & (1u << zeroBasedSlot)) != 0;
    return (mask1 & (1u << (zeroBasedSlot - 32))) != 0;
}

static void ReadSpottedMask(uintptr_t pawn, uint32_t& mask0, uint32_t& mask1) {
    uint64_t m64 = SafeRead<uint64_t>(pawn + O::m_entitySpottedState + O::Spotted::m_bSpottedByMask, 0);
    mask0 = (uint32_t)(m64 & 0xFFFFFFFFu);
    mask1 = (uint32_t)(m64 >> 32);
    if (mask0 == 0 && mask1 == 0) {
        mask0 = SafeRead<uint32_t>(pawn + O::m_entitySpottedState + O::Spotted::m_bSpottedByMask, 0);
        mask1 = SafeRead<uint32_t>(pawn + O::m_entitySpottedState + O::Spotted::m_bSpottedByMask + 4, 0);
    }
}

static int ResolveLocalControllerIndex(uintptr_t localPawn) {
    if (IsValid(localPawn)) {
        uint32_t hCtrl = SafeRead<uint32_t>(localPawn + O::m_hController, 0);
        int idx = (int)(hCtrl & 0x7FFF);
        if (idx >= 1 && idx <= 64) return idx;
    }
    if (hClient) {
        uintptr_t lc = SafeRead<uintptr_t>(hClient + O::dwLocalPlayerController, 0);
        if (IsValid(lc)) {
            for (int i = 1; i <= 64; i++) {
                if (GetEntity(i) == lc) return i;
            }
        }
    }
    return g_localCtrlIndex;
}

// Sticky hold after positive LOS — engine mask flickers / lags
static uint64_t g_visStickyUntil[65] = {};

static int VisStickyKey(uintptr_t pawn) {
    for (int i = 0; i < g_cacheCount; i++) {
        if (g_cache[i].pawn == pawn && g_cache[i].index >= 1 && g_cache[i].index <= 64)
            return g_cache[i].index;
    }
    return (int)((pawn >> 6) % 64) + 1;
}

static bool IsCrosshairOnPawn(uintptr_t localPawn, uintptr_t target) {
    if (!IsValid(localPawn) || !IsValid(target)) return false;
    int cross = SafeRead<int>(localPawn + O::m_iIDEntIndex, -1);
    if (cross <= 0 || cross > 0x7FFE) return false;
    uintptr_t ent = GetEntity(cross);
    if (!IsValid(ent)) return false;
    if (ent == target) return true;
    uintptr_t maybe = HandleToEnt(SafeRead<uint32_t>(ent + O::m_hPlayerPawn, 0));
    return maybe == target;
}

// Instant signals only (no sticky)
static bool IsSpottedRaw(uintptr_t pawn) {
    if (!IsValid(pawn)) return false;

    uintptr_t lp = hClient ? SafeRead<uintptr_t>(hClient + O::dwLocalPlayerPawn, 0) : 0;
    int localIdx = ResolveLocalControllerIndex(lp);
    if (localIdx >= 1 && localIdx <= 64)
        g_localCtrlIndex = localIdx;

    uint32_t mask0 = 0, mask1 = 0;
    ReadSpottedMask(pawn, mask0, mask1);

    if (localIdx >= 1 && localIdx <= 64) {
        if (SpottedMaskBit(mask0, mask1, localIdx - 1)) return true;
        if (SpottedMaskBit(mask0, mask1, localIdx)) return true;
    }

    // Crosshair on target — no engine delay
    if (IsCrosshairOnPawn(lp, pawn)) return true;

    // Faster acquire: team m_bSpotted ONLY if head is on-screen (facing them)
    bool teamSpotted = SafeRead<uint8_t>(pawn + O::m_entitySpottedState + O::Spotted::m_bSpotted, 0) != 0;
    if (teamSpotted && g_imGuiInitialized && IsValid(lp)) {
        Vector3 head{};
        if (GetBonePos(pawn, O::Bone::head, head) && OriginSane(head)) {
            int sw = (int)ImGui::GetIO().DisplaySize.x;
            int sh = (int)ImGui::GetIO().DisplaySize.y;
            Vector2 scr{};
            if (sw > 0 && sh > 0 && WorldToScreen(head, scr, sw, sh)) {
                if (scr.x > 0.f && scr.x < (float)sw && scr.y > 0.f && scr.y < (float)sh)
                    return true;
            }
        }
    }
    return false;
}

// Aim / trigger / ESP visibility — sticky ~280ms to kill flicker & dropouts
static bool IsSpotted(uintptr_t pawn) {
    if (!IsValid(pawn)) return false;
    const uint64_t now = GetTickCount64();
    const int key = VisStickyKey(pawn);
    const uint64_t stickyMs = 280;

    if (IsSpottedRaw(pawn)) {
        if (key >= 1 && key <= 64)
            g_visStickyUntil[key] = now + stickyMs;
        return true;
    }
    if (key >= 1 && key <= 64 && g_visStickyUntil[key] > now)
        return true;
    return false;
}

static uintptr_t GetActiveWeapon(uintptr_t pawn) {
    if (!IsValid(pawn)) return 0;
    uintptr_t ws = SafeRead<uintptr_t>(pawn + O::m_pWeaponServices, 0);
    if (!IsValid(ws)) return 0;
    return HandleToEnt(SafeRead<uint32_t>(ws + O::WeaponServices::m_hActiveWeapon, 0));
}

static int WeaponDelayProfileMs(uintptr_t localPawn, int baseDelay) {
    if (!g_config.triggerWeaponProfiles || !IsValid(localPawn)) return baseDelay;
    uintptr_t wep = GetActiveWeapon(localPawn);
    if (!IsValid(wep)) return baseDelay;
    uint16_t def = SafeRead<uint16_t>(wep + O::m_AttributeManager + O::m_Item + O::m_iItemDefinitionIndex, 0);
    // AWP, SSG08, SCAR20, G3SG1
    if (def == 9 || def == 40 || def == 38 || def == 11)
        return (std::max)(baseDelay + 250, 320);
    // Common rifles / SMGs
    if (def == 7 || def == 16 || def == 60 || def == 8 || def == 10 || def == 13 || def == 17 || def == 33 || def == 34)
        return baseDelay + 15;
    // Pistols
    if (def == 1 || def == 64 || def == 4 || def == 32 || def == 61 || def == 36 || def == 30 || def == 63 || def == 3 || def == 2)
        return (std::max)(baseDelay - 25, 25);
    // Shotguns
    if (def == 25 || def == 27 || def == 29 || def == 35 || def == 14)
        return baseDelay + 120;
    return baseDelay;
}

static bool IsLocalFlashed(uintptr_t localPawn) {
    if (!IsValid(localPawn)) return false;
    float d = SafeRead<float>(localPawn + O::m_flFlashDuration, 0.f);
    return d > g_config.triggerFlashMax;
}

static uintptr_t GetIdentityPtr(int idx);
static bool DesignerNameEquals(uintptr_t identity, const char* want);

static void UpdateSmokeNearby(uintptr_t localPawn) {
    g_smokeNearby = false;
    if (!IsValid(localPawn) || !g_pES) return;
    if ((++g_smokeScanTick % 15) != 0) return; // rare scan
    Vector3 eye = GetOrigin(localPawn);
    Vector3 vo = GetViewOffset(localPawn);
    eye.x += vo.x; eye.y += vo.y; eye.z += vo.z;
    int highest = SafeRead<int>(g_pES + O::dwGameEntitySystem_highestEntityIndex, 512);
    if (highest > 768) highest = 768;
    for (int i = 64; i <= highest; i++) {
        uintptr_t id = GetIdentityPtr(i);
        if (!IsValid(id)) continue;
        if (!DesignerNameEquals(id, "smokegrenade_projectile")) continue;
        uintptr_t e = SafeRead<uintptr_t>(id, 0);
        if (!IsValid(e)) continue;
        Vector3 so = GetOrigin(e);
        if (!OriginSane(so)) continue;
        float dx = so.x - eye.x, dy = so.y - eye.y, dz = so.z - eye.z;
        if (dx*dx + dy*dy + dz*dz < 144.f * 144.f) { g_smokeNearby = true; return; }
    }
}
static bool IsLocalInSmoke(uintptr_t localPawn) {
    (void)localPawn;
    return g_smokeNearby;
}


static void GetWeaponName(uintptr_t pawn, char* buf, size_t len) {
    buf[0] = 0;
    uintptr_t wep = GetActiveWeapon(pawn);
    if (!IsValid(wep)) return;
    uint16_t def = 0;
    def = SafeRead<uint16_t>(wep + O::m_AttributeManager + O::m_Item + O::m_iItemDefinitionIndex, 0);
    struct { uint16_t id; const char* n; } t[] = {
        {7,"AK-47"},{9,"AWP"},{16,"M4A4"},{60,"M4A1-S"},{1,"Deagle"},{4,"Glock"},{61,"USP-S"},
        {32,"P2000"},{36,"P250"},{63,"CZ75"},{2,"Duals"},{3,"Five-SeveN"},{30,"Tec-9"},{64,"R8"},
        {17,"MAC-10"},{19,"P90"},{24,"UMP-45"},{26,"Bizon"},{33,"MP7"},{34,"MP9"},
        {10,"FAMAS"},{13,"Galil"},{39,"SG553"},{38,"SCAR-20"},{11,"G3SG1"},{40,"SSG08"},{8,"AUG"},
        {14,"M249"},{28,"Negev"},{25,"XM1014"},{27,"MAG-7"},{29,"Sawed-Off"},{35,"Nova"},
        {42,"Knife"},{59,"Knife"},{43,"Flash"},{44,"HE"},{45,"Smoke"},{46,"Molly"},{47,"Decoy"},
        {48,"Incendiary"},{49,"C4"},{31,"Zeus"},{0,nullptr}
    };
    for (int i = 0; t[i].n; i++) if (t[i].id == def) { strncpy_s(buf, len, t[i].n, _TRUNCATE); return; }
    if (def) sprintf_s(buf, len, "#%u", def);
}

// -------------------- FEATURES --------------------

// Third person (hold key) — SAFE version
// Previous multi-offset CSGOInput writes + blind code patch caused crashes.
// This only:
//   1) patches ThirdPersonReset if the exact JE (0x75) byte is present
//   2) writes a single verified bool on CSGOInput when pattern-resolved
// Distance control needs OverrideView (not done here — no crash risk).
static void SetThirdPersonResetPatch(bool enable) {
    static uint8_t original = 0x75;
    static bool patched = false;
    static uintptr_t jeAddr = 0;

    if (!Pat::g_res.thirdPersonReset) return;

    if (!jeAddr) {
        // Locate JE (0x75) within first 16 bytes of the match
        uintptr_t base = Pat::g_res.thirdPersonReset;
        __try {
            for (int i = 0; i < 16; i++) {
                uint8_t b = *(uint8_t*)(base + i);
                if (b == 0x75) { // JE rel8
                    jeAddr = base + i;
                    original = b;
                    break;
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { jeAddr = 0; }
        if (!jeAddr) return;
    }

    __try {
        uint8_t cur = *(uint8_t*)jeAddr;
        if (enable && !patched) {
            if (cur != 0x75 && cur != 0xEB) return; // unexpected — do not patch
            DWORD old = 0;
            if (!VirtualProtect((void*)jeAddr, 1, PAGE_EXECUTE_READWRITE, &old)) return;
            original = 0x75;
            *(uint8_t*)jeAddr = 0xEB; // JMP — skip reset-to-firstperson
            VirtualProtect((void*)jeAddr, 1, old, &old);
            patched = true;
        }
        else if (!enable && patched) {
            DWORD old = 0;
            if (!VirtualProtect((void*)jeAddr, 1, PAGE_EXECUTE_READWRITE, &old)) return;
            *(uint8_t*)jeAddr = original;
            VirtualProtect((void*)jeAddr, 1, old, &old);
            patched = false;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        patched = false;
        jeAddr = 0;
    }
}

void DoThirdPerson(uintptr_t localPawn) {
    // Engine path (stable):
    //   1) ThirdPersonReset JE->JMP while active (pattern, only if 0x75 found)
    //   2) CSGOInput thirdperson flag at +0x229 (pattern ptr or dwCSGOInput)
    // Modes: hold key (default) or toggle on key edge
    static bool toggledOn = false;
    static bool keyWasDown = false;

    if (!g_running || !g_config.thirdPerson || !IsValid(localPawn) || !IsAlive(localPawn) || !hClient) {
        SetThirdPersonResetPatch(false);
        toggledOn = false;
        keyWasDown = false;
        // Force flag off when disabled
        uintptr_t inputOff = 0;
        if (Pat::g_res.csgoInputPtr)
            inputOff = Pat::ReadPtr(Pat::g_res.csgoInputPtr);
        if (!IsValid(inputOff) && hClient)
            inputOff = SafeRead<uintptr_t>(hClient + O::dwCSGOInput, 0);
        if (IsValid(inputOff)) {
            __try { SafeWrite<uint8_t>(inputOff + 0x229, 0); }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
        return;
    }

    bool keyDown = (g_config.thirdPersonKey != 0) && IsKeyDown(g_config.thirdPersonKey);
    bool active = false;
    if (g_config.thirdPersonToggle) {
        if (keyDown && !keyWasDown)
            toggledOn = !toggledOn;
        keyWasDown = keyDown;
        active = toggledOn;
    } else {
        toggledOn = false;
        keyWasDown = keyDown;
        active = keyDown;
    }

    if (Pat::g_res.thirdPersonReset)
        SetThirdPersonResetPatch(active);
    else
        SetThirdPersonResetPatch(false);

    uintptr_t input = 0;
    if (Pat::g_res.csgoInputPtr)
        input = Pat::ReadPtr(Pat::g_res.csgoInputPtr);
    if (!IsValid(input) && hClient)
        input = SafeRead<uintptr_t>(hClient + O::dwCSGOInput, 0);
    if (!IsValid(input)) return;
    __try {
        SafeWrite<uint8_t>(input + 0x229, active ? 1 : 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

void DoNoFlash(uintptr_t p) {

    if (!g_config.noFlash || !IsValid(p)) return;
    SafeWrite<float>(p + O::m_flFlashBangTime, 0.f);
    SafeWrite<float>(p + O::m_flFlashOverlayAlpha, 0.f);
    SafeWrite<float>(p + O::m_flFlashMaxAlpha, 0.f);
    SafeWrite<float>(p + O::m_flFlashDuration, 0.f);
}
// Safe NoSmoke:
// 1) Clear local pawn smoke overlay only (always valid on C_CSPlayerPawn).
// 2) Only touch entities whose designer name is "smokegrenade_projectile".
//    Writing smoke-schema fields on arbitrary entities was the crash source.
// 3) Prefer m_nSmokeEffectTickBegin = 0 (community-safe remove) + transparent color.
// 4) Never bulk-write m_bSmokeEffectSpawned on non-smoke entities.
// 5) Rate-limit + highest-index bound + per-entity SEH.
static uintptr_t GetIdentityPtr(int idx) {
    if (idx < 0 || idx > 16384 || !g_pES || !IsValid(g_pES)) return 0;
    __try {
        uintptr_t listPtr = g_pES + O::kListOffset;
        uintptr_t chunk = SafeRead<uintptr_t>(listPtr + (idx / O::kChunk) * 8, 0);
        if (!IsValid(chunk)) return 0;
        return chunk + (idx % O::kChunk) * O::kStride;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

static bool DesignerNameEquals(uintptr_t identity, const char* want) {
    if (!IsValid(identity) || !want) return false;
    // CEntityIdentity::m_designerName is CUtlSymbolLarge (pointer to string) at +0x20
    uintptr_t strPtr = SafeRead<uintptr_t>(identity + O::Identity::m_designerName, 0);
    if (!IsValid(strPtr)) return false;
    char buf[48]{};
    SafeReadArray(strPtr, buf, sizeof(buf));
    if (!buf[0]) return false;
    // case-sensitive match used by engine designer names
    for (int i = 0; want[i]; i++) {
        if (buf[i] != want[i]) return false;
    }
    return true;
}

void DoNoSmoke(uintptr_t p) {
    if (!g_config.noSmoke || !IsInGame()) return;

    // Local overlay (screen fog when inside smoke) — safe on player pawn
    if (IsValid(p)) {
        SafeWrite<float>(p + O::m_flLastSmokeOverlayAlpha, 0.f);
        SafeWrite<float>(p + O::m_flLastSmokeAge, 0.f);
        SafeWrite<Vector3>(p + O::m_vLastSmokeOverlayColor, Vector3{ 0.f, 0.f, 0.f });
    }

    // Preferred path: DrawSmokeArray is hooked → no entity writes needed
    if (oSmokeDrawArray)
        return;

    if (!g_pES || !IsValid(g_pES)) return;

    // Fallback: memory path (entity-name filtered) when hook missing
    static int smokeTick = 0;
    if ((++smokeTick % 3) != 0) return;

    int highest = SafeRead<int>(g_pES + O::dwGameEntitySystem_highestEntityIndex, 0);
    if (highest < 64) highest = 2048;
    if (highest > 4096) highest = 4096;

    for (int i = 64; i <= highest; i++) {
        __try {
            uintptr_t identity = GetIdentityPtr(i);
            if (!IsValid(identity)) continue;
            if (!DesignerNameEquals(identity, "smokegrenade_projectile")) continue;

            uintptr_t e = SafeRead<uintptr_t>(identity, 0);
            if (!IsValid(e)) continue;

            // Primary: kill effect start tick (stops / prevents smoke volume render)
            SafeWrite<int>(e + O::m_nSmokeEffectTickBegin, 0);
            // Secondary: force transparent color if volume already exists
            SafeWrite<Vector3>(e + O::m_vSmokeColor, Vector3{ 0.f, 0.f, 0.f });
            // Do NOT toggle m_bSmokeEffectSpawned / m_bDidSmokeEffect every frame —
            // that desyncs the particle/voxel path and is a known crash vector.
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { continue; }
    }
}
// NoVisualRecoil: zero punch every frame WITHOUT fighting soft RCS
// When NVR is on, soft RCS is skipped so they don't cancel each other.
void DoNoVisualRecoil(uintptr_t p) {
    if (!g_config.noVisualRecoil || !IsValid(p)) return;
    // Only while shots are in progress — zeroing punch every idle frame glitches viewangles
    int shots = SafeRead<int>(p + O::m_iShotsFired, 0);
    if (shots <= 0 && !(GetAsyncKeyState(VK_LBUTTON) & 0x8000))
        return;

    uintptr_t punch = SafeRead<uintptr_t>(p + O::m_pAimPunchServices, 0);
    if (!IsValid(punch)) return;
    auto zero_qangle = [&](uintptr_t off) {
        SafeWrite<float>(punch + off + 0, 0.f);
        SafeWrite<float>(punch + off + 4, 0.f);
        SafeWrite<float>(punch + off + 8, 0.f);
    };
    zero_qangle(O::AimPunch::m_predictableBaseAngle);
    zero_qangle(O::AimPunch::m_predictableBaseAngleVel);
    g_rcsPunchX = 0.f;
    g_rcsPunchY = 0.f;
}

// Soft RCS – compensates a fraction of aim punch (legit style)
// Disabled automatically while NoVisualRecoil is active (avoids camera glitch)
void DoSoftRCS(uintptr_t localPawn) {
    // Soft RCS no longer writes dwViewAngles.
    // Continuous angle writes were locking the camera to 1–2 angles even when not aiming.
    // Use aimbot recoil-comp while aiming, or No Visual Recoil for visual punch only.
    (void)localPawn;
    g_rcsPunchX = 0.f;
    g_rcsPunchY = 0.f;
}

// Trigger bone index (same mapping as aimbot)
static int TriggerBoneIndex() {
    switch (g_config.triggerBone) {
    case 1: return O::Bone::neck;
    case 2: return O::Bone::spine;
    default: return O::Bone::head;
    }
}

// Trigger never writes viewangles (was the aim jitter source when combined with aimbot).
static void TriggerApplyRcsToBone(uintptr_t localPawn, const Vector3& bonePos) {
    (void)localPawn; (void)bonePos;
    // intentionally empty — trigger only presses attack
}

// Estimated hitchance 0–100 using weapon accuracy + movement + distance + bone FOV.
// Not engine seed sim — practical gate so trigger only fires when the shot is likely.
static float g_lastTriggerHC = 0.f;

static float CalcTriggerHitchance(uintptr_t localPawn, uintptr_t target, const Vector3& bonePos,
                                  float distPx, float maxFovPx) {
    if (!IsValid(localPawn) || !IsValid(target)) return 0.f;

    Vector3 eye = GetOrigin(localPawn);
    eye.z += GetViewOffset(localPawn).z;
    float dx = bonePos.x - eye.x, dy = bonePos.y - eye.y, dz = bonePos.z - eye.z;
    float dist = sqrtf(dx * dx + dy * dy + dz * dz);
    if (dist < 1.f) dist = 1.f;

    // Weapon accuracy penalty (higher = worse)
    float inacc = 0.f;
    float turnInacc = 0.f;
    uintptr_t wep = GetActiveWeapon(localPawn);
    if (IsValid(wep)) {
        inacc = SafeRead<float>(wep + O::m_fAccuracyPenalty, 0.f);
        if (inacc < 0.f) inacc = 0.f;
        // turning inaccuracy lives on weapon in current schema
        turnInacc = SafeRead<float>(wep + O::m_flTurningInaccuracy, 0.f);
        if (turnInacc < 0.f) turnInacc = 0.f;
    }
    float totalInacc = inacc + turnInacc * 0.85f;

    // Local movement (units/s)
    Vector3 vel = SafeRead<Vector3>(localPawn + O::m_vecVelocity, Vector3{});
    float speed = sqrtf(vel.x * vel.x + vel.y * vel.y);
    Vector3 tvel = SafeRead<Vector3>(target + O::m_vecVelocity, Vector3{});
    float tspeed = sqrtf(tvel.x * tvel.x + tvel.y * tvel.y);

    bool scoped = SafeRead<uint8_t>(localPawn + O::m_bIsScoped, 0) != 0;
    int shots = SafeRead<int>(localPawn + O::m_iShotsFired, 0);
    uint32_t flags = SafeRead<uint32_t>(localPawn + O::m_fFlags, 0);
    bool ducked = (flags & O::FL_DUCKING) != 0;
    bool onGround = (flags & O::FL_ONGROUND) != 0;

    // Bone size heuristic (world units radius)
    float boneR = 4.5f; // head-ish
    switch (g_config.triggerBone) {
    case 1: boneR = 5.5f; break; // neck
    case 2: boneR = 8.0f; break; // chest
    default: boneR = 4.2f; break;
    }

    // Spread cone radius at distance (scaled accuracy penalty)
    // accuracy penalty is roughly in radians-ish scale; clamp for stability
    float spreadRad = totalInacc * 0.55f + 0.0015f;
    if (!onGround) spreadRad += 0.02f;
    if (speed > 30.f) spreadRad += (speed / 250.f) * 0.025f;
    if (shots > 1) spreadRad += shots * 0.0035f;
    if (scoped) spreadRad *= 0.35f;
    if (ducked) spreadRad *= 0.75f;

    float coneR = tanf(spreadRad) * dist;
    if (coneR < 0.5f) coneR = 0.5f;

    // Geometric HC: bone area vs cone area (clamped)
    float areaRatio = (boneR * boneR) / (coneR * coneR);
    if (areaRatio > 1.f) areaRatio = 1.f;
    float hc = areaRatio * 100.f;

    // Crosshair-to-bone: farther from center inside FOV → lower HC
    if (maxFovPx > 1.f) {
        float fovFactor = 1.f - (distPx / maxFovPx) * 0.45f;
        if (fovFactor < 0.35f) fovFactor = 0.35f;
        hc *= fovFactor;
    }

    // Target strafe penalty
    if (tspeed > 50.f) {
        float tp = 1.f - (tspeed / 300.f) * 0.25f;
        if (tp < 0.6f) tp = 0.6f;
        hc *= tp;
    }

    // Distance soft falloff past ~25m
    float distM = dist * 0.01905f;
    if (distM > 25.f) {
        float df = 1.f - (distM - 25.f) / 80.f;
        if (df < 0.4f) df = 0.4f;
        hc *= df;
    }

    if (hc < 0.f) hc = 0.f;
    if (hc > 99.f) hc = 99.f;
    g_lastTriggerHC = hc;
    return hc;
}

void DoTriggerbot(uintptr_t localPawn, int localTeam) {
    // ---- State machine (no threads) ----
    static bool     attackHeld = false;
    static auto     pressTime = std::chrono::steady_clock::now();
    static auto     lastShotTime = std::chrono::steady_clock::now();
    static int      nextDelayMs = 90;
    static int      holdMs = 30;
    static uintptr_t holdTarget = 0; // keep RCS on same target while attack held

    auto now = std::chrono::steady_clock::now();
    auto msSince = [](const std::chrono::steady_clock::time_point& tp) {
        return (int)std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - tp).count();
        };

    // While attack is held: keep RCS locked on bone, then release
    if (attackHeld) {
        // Do not touch viewangles if aimbot is actively aiming
        const bool aimBusy = g_config.aimEnabled && IsKeyDown(g_config.aimKey);
        if (!aimBusy && g_config.triggerRcs && IsValid(holdTarget) && IsAlive(holdTarget) && IsValid(localPawn)) {
            Vector3 bonePos;
            if (GetBonePos(holdTarget, TriggerBoneIndex(), bonePos))
                TriggerApplyRcsToBone(localPawn, bonePos);
        }
        if (msSince(pressTime) >= holdMs) {
            if (hClient) SafeWrite<int>(hClient + O::attack, 256);
            attackHeld = false;
            holdTarget = 0;
        }
        return;
    }

    if (!g_config.triggerEnabled || !IsInGame()) return;
    if (!IsValid(localPawn) || !IsAlive(localPawn)) return;
    if (!IsKeyDown(g_config.triggerKey)) return;
    if (!hClient) return;

    if (g_config.triggerFlashCheck && IsLocalFlashed(localPawn)) return;
    if (g_config.triggerSmokeCheck && IsLocalInSmoke(localPawn)) return;
    if (msSince(lastShotTime) < nextDelayMs) return;

    // ---- Entity under crosshair ----
    int cross = SafeRead<int>(localPawn + O::m_iIDEntIndex, -1);
    if (cross <= 0 || cross > 0x7FFE) return;

    uintptr_t ent = GetEntity(cross);
    if (!IsValid(ent)) return;

    uintptr_t target = ent;
    if (!IsAlive(target)) {
        uint32_t hPawn = SafeRead<uint32_t>(ent + O::m_hPlayerPawn, 0);
        uintptr_t maybePawn = HandleToEnt(hPawn);
        if (IsValid(maybePawn) && IsAlive(maybePawn))
            target = maybePawn;
        else
            return;
    }

    if (!IsValid(target) || !IsAlive(target) || target == localPawn) return;
    if (HP(target) <= 0) return;
    if (g_config.triggerTeamCheck && Team(target) == localTeam) return;
    if (g_config.triggerVisibleOnly && !IsSpotted(target)) return;

    // ---- REQUIRED: selected bone must be under crosshair ----
    Vector3 bonePos;
    if (!GetBonePos(target, TriggerBoneIndex(), bonePos)) return;
    if (!OriginSane(bonePos)) return;

    int sw = 0, sh = 0;
    if (g_imGuiInitialized) {
        sw = (int)ImGui::GetIO().DisplaySize.x;
        sh = (int)ImGui::GetIO().DisplaySize.y;
    }
    if (sw <= 0 || sh <= 0) return;

    Vector2 scr;
    if (!WorldToScreen(bonePos, scr, sw, sh)) return;

    float dx = scr.x - sw * 0.5f;
    float dy = scr.y - sh * 0.5f;
    float distPx = sqrtf(dx * dx + dy * dy);

    // Use menu FOV; fall back to file tunable if needed
    float maxFov = g_config.triggerBoneFov;
    if (maxFov < 1.f) maxFov = g_TriggerFovPx;
    if (maxFov < 1.f) maxFov = 12.f;
    if (distPx > maxFov) return; // not on the bone → don't shoot

    // ---- Hitchance gate ----
    if (g_config.triggerHitchance) {
        float hc = CalcTriggerHitchance(localPawn, target, bonePos, distPx, maxFov);
        float need = g_config.triggerHitchanceMin;
        if (need < 1.f) need = 1.f;
        if (need > 99.f) need = 99.f;
        if (hc < need) return;
    } else {
        g_lastTriggerHC = 100.f;
    }

    // ---- Anti-recoil: aim through punch so bullet hits the bone ----
    if (g_config.triggerRcs)
        TriggerApplyRcsToBone(localPawn, bonePos);

    // ---- Fire ----
    SafeWrite<int>(hClient + O::attack, 65537);
    attackHeld = true;
    holdTarget = target;
    pressTime = now;
    lastShotTime = now;
    holdMs = 22 + (rand() % 28);

    int delay = g_config.triggerDelayMin;
    if (g_config.triggerDelayMax > g_config.triggerDelayMin)
        delay = g_config.triggerDelayMin + (rand() % (g_config.triggerDelayMax - g_config.triggerDelayMin + 1));
    int shots = SafeRead<int>(localPawn + O::m_iShotsFired, 0);
    if (shots >= 2)
        delay += 35 + shots * 6;
    nextDelayMs = WeaponDelayProfileMs(localPawn, delay);
}

static Vector3 CalcAngles(const Vector3& s, const Vector3& d) {
    Vector3 delta{ d.x - s.x, d.y - s.y, d.z - s.z };
    float hyp = sqrtf(delta.x * delta.x + delta.y * delta.y);
    Vector3 a;
    a.x = -atan2f(delta.z, hyp) * (180.f / 3.14159265f);
    a.y = atan2f(delta.y, delta.x) * (180.f / 3.14159265f);
    a.z = 0.f;
    return a;
}
static void NormAngles(Vector3& a) {
    if (a.x > 89.f) a.x = 89.f;
    if (a.x < -89.f) a.x = -89.f;
    while (a.y > 180.f) a.y -= 360.f;
    while (a.y < -180.f) a.y += 360.f;
    a.z = 0.f;
}


void DoLegitAim(uintptr_t localPawn, int localTeam, int sw, int sh) {
    // Sticky lock at function scope so we can clear on key-up
    static uintptr_t lockPawn = 0;
    static int lockBone = -1;
    static int lockMiss = 0;

    if (!g_config.aimEnabled || !IsInGame()) return;
    if (!IsValid(localPawn) || !IsAlive(localPawn)) return;
    if (!IsKeyDown(g_config.aimKey)) {
        g_hasTarget = false;
        lockPawn = 0;
        lockBone = -1;
        lockMiss = 0;
        return;
    }
    if (g_config.aimOnlyWhenScoped && !SafeRead<uint8_t>(localPawn + O::m_bIsScoped, 0)) return;

    uintptr_t va = hClient + O::dwViewAngles;
    if (!IsValid(va)) return;

    Vector3 eye = GetOrigin(localPawn);
    if (!OriginSane(eye)) return;
    Vector3 vo = GetViewOffset(localPawn);
    eye.x += vo.x; eye.y += vo.y; eye.z += vo.z;


    int boneIdx = AimBoneIndex(); // fixed bone from menu — no priority flip while locked

    auto bonePosOf = [&](uintptr_t pawn, int bone, Vector3& out) -> bool {
        if (!GetBonePos(pawn, bone, out)) return false;
        return OriginSane(out);
    };

    auto validTarget = [&](uintptr_t pawn) -> bool {
        if (!IsValid(pawn) || pawn == localPawn || !IsAlive(pawn)) return false;
        if (g_config.aimTeamCheck && Team(pawn) == localTeam) return false;
        if (g_config.aimVisibleOnly && !IsSpotted(pawn)) return false;
        return true;
    };

    auto screenFov = [&](const Vector3& bp, float& outFov) -> bool {
        Vector2 scr;
        if (!WorldToScreen(bp, scr, sw, sh, false)) return false;
        float dx = scr.x - sw * 0.5f, dy = scr.y - sh * 0.5f;
        outFov = sqrtf(dx * dx + dy * dy);
        return true;
    };

    Vector3 aimPos{};
    bool have = false;

    // Try keep sticky target
    if (lockPawn && validTarget(lockPawn)) {
        Vector3 bp;
        int b = (lockBone >= 0) ? lockBone : boneIdx;
        if (bonePosOf(lockPawn, b, bp)) {
            float fov = 9999.f;
            if (screenFov(bp, fov) && fov < g_config.aimFov * 1.35f) {
                aimPos = bp;
                have = true;
                lockMiss = 0;
            } else {
                lockMiss++;
            }
        } else {
            lockMiss++;
        }
        if (lockMiss > 12) { lockPawn = 0; lockBone = -1; lockMiss = 0; }
    } else {
        lockPawn = 0;
        lockBone = -1;
        lockMiss = 0;
    }

    // Acquire new target if no lock
    if (!have) {
        float bestFov = g_config.aimFov;
        uintptr_t bestPawn = 0;
        Vector3 bestPos{};

        Vector3 curAng = SafeRead<Vector3>(va, {});
        auto consider = [&](uintptr_t pawn) {
            if (!validTarget(pawn)) return;
            Vector3 bp;
            if (!bonePosOf(pawn, boneIdx, bp)) {
                if (boneIdx != O::Bone::head && bonePosOf(pawn, O::Bone::head, bp)) { }
                else return;
            }
            float fov = 0.f;
            if (!screenFov(bp, fov)) {
                // Angular FOV fallback if W2S fails (matrix lag)
                Vector3 ang = CalcAngles(eye, bp);
                NormAngles(ang);
                float dpx = ang.x - curAng.x, dpy = ang.y - curAng.y;
                while (dpy > 180.f) dpy -= 360.f;
                while (dpy < -180.f) dpy += 360.f;
                fov = sqrtf(dpx * dpx + dpy * dpy) * 12.f; // rough px scale
            }
            if (fov >= bestFov) return;
            bestFov = fov;
            bestPawn = pawn;
            bestPos = bp;
        };

        if (g_cacheCount > 0) {
            for (int i = 0; i < g_cacheCount; i++)
                consider(g_cache[i].pawn);
        } else {
            for (int i = 1; i <= 64; i++) {
                uintptr_t ctrl = GetEntity(i);
                if (!ctrl) continue;
                uintptr_t pawn = HandleToEnt(SafeRead<uint32_t>(ctrl + O::m_hPlayerPawn, 0));
                consider(pawn);
            }
        }

        if (bestPawn) {
            lockPawn = bestPawn;
            lockBone = boneIdx;
            aimPos = bestPos;
            have = true;
            lockMiss = 0;
        }
    }

    if (!have) { g_hasTarget = false; return; }
    g_hasTarget = true;

    // Refresh bone pos every frame for locked target (track movement)
    {
        Vector3 bp;
        int b = (lockBone >= 0) ? lockBone : boneIdx;
        if (bonePosOf(lockPawn, b, bp))
            aimPos = bp;
    }

    Vector3 best = CalcAngles(eye, aimPos);
    NormAngles(best);

    if (g_config.aimRecoilComp) {
        uintptr_t punchSvc = SafeRead<uintptr_t>(localPawn + O::m_pAimPunchServices, 0);
        if (IsValid(punchSvc)) {
            Vector3 punch = SafeRead<Vector3>(punchSvc + O::AimPunch::m_predictableBaseAngle, {});
            float s = g_config.aimRecoilCompStr * 0.35f;
            best.x -= punch.x * s;
            best.y -= punch.y * s;
            NormAngles(best);
        }
    }
    g_targetAngles = best;

    __try {
        Vector3 cur = SafeRead<Vector3>(va, {});
        float dp = best.x - cur.x;
        float dy = best.y - cur.y;
        while (dy > 180.f) dy -= 360.f;
        while (dy < -180.f) dy += 360.f;

        float angDist = sqrtf(dp * dp + dy * dy);
        // Small deadzone only — still track moving targets
        if (angDist < 0.04f) return;

        float sm = g_config.aimSmooth;
        if (sm < 0.05f) sm = 0.05f;
        if (sm > 0.95f) sm = 0.95f;
        float t = 1.f - sm;
        if (angDist > 5.f) t = (std::min)(t * 1.5f, 0.7f);

        float sx = dp * t;
        float sy = dy * t;
        const float kMaxStep = 4.0f;
        if (sx > kMaxStep) sx = kMaxStep; if (sx < -kMaxStep) sx = -kMaxStep;
        if (sy > kMaxStep) sy = kMaxStep; if (sy < -kMaxStep) sy = -kMaxStep;

        Vector3 n;
        n.x = cur.x + sx;
        n.y = cur.y + sy;
        n.z = 0.f;
        NormAngles(n);
        if (!std::isnan(n.x) && !std::isnan(n.y) && fabsf(n.x) <= 89.f)
            *(Vector3*)va = n;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// -------------------- DRAW HELPERS --------------------
static const int kBones[][2] = {
    {O::Bone::head, O::Bone::neck},{O::Bone::neck, O::Bone::spine},{O::Bone::spine, O::Bone::pelvis},
    {O::Bone::neck, O::Bone::left_shoulder},{O::Bone::left_shoulder, O::Bone::left_elbow},{O::Bone::left_elbow, O::Bone::left_hand},
    {O::Bone::neck, O::Bone::right_shoulder},{O::Bone::right_shoulder, O::Bone::right_elbow},{O::Bone::right_elbow, O::Bone::right_hand},
    {O::Bone::pelvis, O::Bone::left_hip},{O::Bone::left_hip, O::Bone::left_knee},{O::Bone::left_knee, O::Bone::left_foot},
    {O::Bone::pelvis, O::Bone::right_hip},{O::Bone::right_hip, O::Bone::right_knee},{O::Bone::right_knee, O::Bone::right_foot},
};

void DrawSkeleton(ImDrawList* dl, uintptr_t pawn, int sw, int sh, ImU32 col) {
    for (auto& p : kBones) {
        Vector3 a, b;
        if (!GetBonePos(pawn, p[0], a) || !GetBonePos(pawn, p[1], b)) continue;
        Vector2 sa, sb;
        if (!WorldToScreen(a, sa, sw, sh) || !WorldToScreen(b, sb, sw, sh)) continue;
        dl->AddLine(ImVec2(sa.x, sa.y), ImVec2(sb.x, sb.y), col, 1.2f);
    }
}

void DrawSpectatorList(uintptr_t localPawn) {
    if (!g_config.spectatorList || !IsValid(localPawn) || !g_pES) return;

    char names[24][128];
    int count = 0;

    for (int i = 1; i <= 64 && count < 24; i++) {
        __try {
            uintptr_t ctrl = GetEntity(i);
            if (!IsValid(ctrl)) continue;

            uintptr_t playerPawn = HandleToEnt(SafeRead<uint32_t>(ctrl + O::m_hPlayerPawn, 0));
            uintptr_t obsPawn = HandleToEnt(SafeRead<uint32_t>(ctrl + O::m_hObserverPawn, 0));
            if (playerPawn == localPawn || obsPawn == localPawn) continue;

            // Alive players are not spectators
            if (IsValid(playerPawn) && IsAlive(playerPawn)) continue;

            uintptr_t check = IsValid(obsPawn) ? obsPawn : playerPawn;
            if (!IsValid(check)) continue;

            uintptr_t obs = SafeRead<uintptr_t>(check + O::m_pObserverServices, 0);
            if (!IsValid(obs)) continue;

            uint8_t mode = SafeRead<uint8_t>(obs + O::Observer::m_iObserverMode, 0);
            // 0 = none / fixed; >0 typically in-eye / chase
            if (mode == 0) continue;

            uint32_t th = SafeRead<uint32_t>(obs + O::Observer::m_hObserverTarget, 0);
            uintptr_t target = HandleToEnt(th);
            if (target != localPawn) continue;

            GetPlayerName(ctrl, names[count], 128);
            if (!names[count][0]) sprintf_s(names[count], "player %d", i);
            count++;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { continue; }
    }

    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    float x = 16.f, y = 120.f;
    if (count == 0) {
        // still show header so user knows feature is alive
        dl->AddText(ImVec2(x, y), IM_COL32(140, 140, 150, 180), "SPECTATORS (0)");
        return;
    }
    float boxH = 22.f + count * 16.f;
    dl->AddRectFilled(ImVec2(x - 10, y - 8), ImVec2(x + 190, y + boxH), IM_COL32(12, 14, 20, 210), 8.f);
    dl->AddRect(ImVec2(x - 10, y - 8), ImVec2(x + 190, y + boxH), IM_COL32(90, 110, 170, 140), 8.f, 0, 1.2f);
    dl->AddText(ImVec2(x, y), IM_COL32(255, 110, 110, 255), "SPECTATORS");
    char hdr[32]; sprintf_s(hdr, "(%d)", count);
    dl->AddText(ImVec2(x + 95, y), IM_COL32(180, 180, 190, 220), hdr);
    for (int i = 0; i < count; i++)
        dl->AddText(ImVec2(x, y + 18 + i * 16), IM_COL32(235, 235, 240, 235), names[i]);
}

struct HitLogEntry {
    float x, y, born;
    int dmg;
    char name[32];
};
static HitLogEntry g_hitlog[12];
static int g_hitlogN = 0;

static void PushHitlog(float sx, float sy, int dmg, const char* name) {
    if (!g_config.hitlog) return;
    HitLogEntry e{};
    e.x = sx; e.y = sy; e.dmg = dmg;
    e.born = (float)GetTickCount64();
    e.name[0] = 0;
    if (name) { strncpy_s(e.name, name, _TRUNCATE); }
    if (g_hitlogN < 12) g_hitlog[g_hitlogN++] = e;
    else {
        for (int i = 1; i < 12; i++) g_hitlog[i-1] = g_hitlog[i];
        g_hitlog[11] = e;
    }
}

void DrawHitlog(ImDrawList* dl) {
    if (!g_config.hitlog || !dl) return;
    float now = (float)GetTickCount64();
    int w = 0;
    for (int i = 0; i < g_hitlogN; i++) {
        float age = (now - g_hitlog[i].born) / 1000.f;
        if (age > 2.2f) continue;
        float a = 1.f - age / 2.2f;
        int alpha = (int)(a * 255);
        float yy = g_hitlog[i].y - age * 40.f;
        char buf[64];
        if (g_hitlog[i].name[0])
            sprintf_s(buf, "-%d %s", g_hitlog[i].dmg, g_hitlog[i].name);
        else
            sprintf_s(buf, "-%d", g_hitlog[i].dmg);
        dl->AddText(ImVec2(g_hitlog[i].x, yy), IM_COL32(255, 220, 80, alpha), buf);
        g_hitlog[w++] = g_hitlog[i];
    }
    g_hitlogN = w;
}


// Hitmarker: detect HP drop on enemies while local recently fired
static int g_prevEnemyHp[65];
static int g_prevShotsFired = 0;
static bool g_hpInit = false;

void UpdateHitmarker(uintptr_t localPawn, int localTeam) {
    if (!g_config.hitmarker || !IsValid(localPawn)) return;

    int shots = SafeRead<int>(localPawn + O::m_iShotsFired, 0);
    bool firedRecently = (shots > g_prevShotsFired) || IsKeyDown(VK_LBUTTON);
    g_prevShotsFired = shots;

    // Also try damage-list path on local controller (dealt/received records)
    uintptr_t localCtrl = SafeRead<uintptr_t>(hClient + O::dwLocalPlayerController, 0);
    if (IsValid(localCtrl)) {
        uintptr_t dmgSvc = SafeRead<uintptr_t>(localCtrl + O::m_pDamageServices, 0);
        if (IsValid(dmgSvc)) {
            // C_UtlVectorEmbeddedNetworkVar: size often at +0x10 or classic CUtlVector size at +0x8
            static int lastDmgCount = 0;
            int c1 = SafeRead<int>(dmgSvc + O::Damage::m_DamageList + 0x8, 0);
            int c2 = SafeRead<int>(dmgSvc + O::Damage::m_DamageList + 0x10, 0);
            int count = (std::max)(c1, c2);
            if (count < 0 || count > 512) count = 0;
            if (count > lastDmgCount && lastDmgCount >= 0) {
                g_hitMarkerActive = true;
                g_hitMarkerTime = std::chrono::steady_clock::now();
            }
            lastDmgCount = count;
        }
    }

    // Primary: enemy HP decreased while we were shooting
    for (int i = 1; i <= 64; i++) {
        uintptr_t ctrl = GetEntity(i);
        if (!ctrl) {
            g_prevEnemyHp[i] = 0;
            continue;
        }
        uintptr_t pawn = HandleToEnt(SafeRead<uint32_t>(ctrl + O::m_hPlayerPawn, 0));
        if (!IsValid(pawn) || pawn == localPawn) {
            g_prevEnemyHp[i] = 0;
            continue;
        }
        if (Team(pawn) == localTeam) {
            g_prevEnemyHp[i] = HP(pawn);
            continue;
        }
        int hp = HP(pawn);
        if (!g_hpInit) {
            g_prevEnemyHp[i] = hp;
            continue;
        }
        if (firedRecently && g_prevEnemyHp[i] > 0 && hp >= 0 && hp < g_prevEnemyHp[i]) {
            g_hitMarkerActive = true;
            g_hitMarkerTime = std::chrono::steady_clock::now();
            int dmg = g_prevEnemyHp[i] - hp;
            float cx = ImGui::GetIO().DisplaySize.x * 0.5f;
            float cy = ImGui::GetIO().DisplaySize.y * 0.5f;
            // Prefer head screen if possible
            Vector3 hp3; Vector2 scr;
            int sw = (int)ImGui::GetIO().DisplaySize.x, sh = (int)ImGui::GetIO().DisplaySize.y;
            if (GetBonePos(pawn, O::Bone::head, hp3) && WorldToScreen(hp3, scr, sw, sh)) {
                cx = scr.x; cy = scr.y;
            }
            char nm[64]{}; GetPlayerName(ctrl, nm, sizeof(nm));
            PushHitlog(cx, cy - 20.f, dmg, nm);
        }
        g_prevEnemyHp[i] = hp;
    }
    g_hpInit = true;
}

void DrawHitmarker(ImDrawList* dl) {
    if (!g_config.hitmarker || !g_hitMarkerActive) return;
    auto now = std::chrono::steady_clock::now();
    int elapsed = (int)std::chrono::duration_cast<std::chrono::milliseconds>(now - g_hitMarkerTime).count();
    if (elapsed > 320) {
        g_hitMarkerActive = false;
        return;
    }
    // Fade out
    int alpha = 255 - (elapsed * 255 / 320);
    if (alpha < 0) alpha = 0;
    ImVec2 c(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f);
    float s = 10.f, t = 2.0f;
    ImU32 col = IM_COL32(255, 255, 255, alpha);
    dl->AddLine(ImVec2(c.x - s, c.y), ImVec2(c.x - s * 0.35f, c.y), col, t);
    dl->AddLine(ImVec2(c.x + s * 0.35f, c.y), ImVec2(c.x + s, c.y), col, t);
    dl->AddLine(ImVec2(c.x, c.y - s), ImVec2(c.x, c.y - s * 0.35f), col, t);
    dl->AddLine(ImVec2(c.x, c.y + s * 0.35f), ImVec2(c.x, c.y + s), col, t);
}

void DrawFovCircle(ImDrawList* dl) {
    if (!g_config.aimDrawFov || !g_config.aimEnabled) return;
    ImVec2 c(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f);
    dl->AddCircle(c, g_config.aimFov, IM_COL32(255, 255, 255, 55), 64, 1.2f);
}

void DrawCrosshair(ImDrawList* dl) {
    if (!g_config.customCrosshair) return;
    ImVec2 c(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f);
    ImU32 col = IM_COL32((int)(g_config.chR * 255), (int)(g_config.chG * 255), (int)(g_config.chB * 255), 230);
    float s = g_config.chSize, g = g_config.chGap, t = g_config.chThick;
    dl->AddLine(ImVec2(c.x - s - g, c.y), ImVec2(c.x - g, c.y), col, t);
    dl->AddLine(ImVec2(c.x + g, c.y), ImVec2(c.x + s + g, c.y), col, t);
    dl->AddLine(ImVec2(c.x, c.y - s - g), ImVec2(c.x, c.y - g), col, t);
    dl->AddLine(ImVec2(c.x, c.y + g), ImVec2(c.x, c.y + s + g), col, t);
}

static float GetCurTime() {
    uintptr_t gvPtr = 0;
    if (Pat::g_res.globalVarsPtr)
        gvPtr = Pat::ReadPtr(Pat::g_res.globalVarsPtr);
    if (!IsValid(gvPtr) && hClient)
        gvPtr = SafeRead<uintptr_t>(hClient + O::dwGlobalVars, 0);
    if (!IsValid(gvPtr)) return 0.f;
    // Probe common Source2 curtime slots for a sane increasing game time
    const int cand[] = { 0x30, 0x34, 0x2C, 0x38, 0x40, 0x48 };
    for (int off : cand) {
        float t = SafeRead<float>(gvPtr + off, 0.f);
        if (t > 10.f && t < 1.0e7f) return t;
    }
    return 0.f;
}

void DrawBombTimer(ImDrawList* dl) {
    if (!g_config.bombTimer || !hClient || !dl) return;

    uintptr_t c4 = 0;
    // Path A: dwPlantedC4 -> ptr -> entity
    uintptr_t slot = SafeRead<uintptr_t>(hClient + O::dwPlantedC4, 0);
    if (IsValid(slot)) {
        c4 = SafeRead<uintptr_t>(slot, 0);
        if (!IsValid(c4)) c4 = slot;
    }
    // Path B: scan entities for planted bomb (designer name)
    if (!IsValid(c4)) {
        int highest = SafeRead<int>(g_pES + O::dwGameEntitySystem_highestEntityIndex, 2048);
        if (highest > 4096) highest = 4096;
        for (int i = 64; i <= highest; i++) {
            uintptr_t id = GetIdentityPtr(i);
            if (!IsValid(id)) continue;
            if (!DesignerNameEquals(id, "planted_c4")) continue;
            c4 = SafeRead<uintptr_t>(id, 0);
            if (IsValid(c4)) break;
        }
    }
    if (!IsValid(c4)) return;

    uint8_t ticking = SafeRead<uint8_t>(c4 + O::C4::m_bBombTicking, 0);
    float blow = SafeRead<float>(c4 + O::C4::m_flC4Blow, 0.f);
    float timerLen = SafeRead<float>(c4 + O::C4::m_flTimerLength, 0.f);
    int site = SafeRead<int>(c4 + O::C4::m_nBombSite, -1);

    // Local countdown fallback (independent of globalvars)
    static bool wasPlanted = false;
    static double plantStartMs = 0.0;
    static float plantDur = 40.f;
    double nowMs = (double)GetTickCount64();
    bool planted = ticking != 0 || (blow > 1.f);
    if (!planted) { wasPlanted = false; return; }
    if (!wasPlanted) {
        wasPlanted = true;
        plantStartMs = nowMs;
        plantDur = (timerLen > 5.f && timerLen < 60.f) ? timerLen : 40.f;
    }

    float remain = -1.f;
    float cur = GetCurTime();
    if (blow > 1.f && cur > 1.f) {
        float r = blow - cur;
        if (r >= 0.f && r <= 60.f) remain = r;
    }
    if (remain < 0.f) {
        float elapsed = (float)((nowMs - plantStartMs) / 1000.0);
        remain = plantDur - elapsed;
    }
    if (remain < 0.f) remain = 0.f;
    if (remain > 60.f) return;

    char siteStr[8] = "";
    if (site == 0) strcpy_s(siteStr, "A ");
    else if (site == 1) strcpy_s(siteStr, "B ");
    // else leave empty (unknown site — avoid "?")

    char buf[80];
    sprintf_s(buf, "BOMB %s·  %.1fs", siteStr, remain);

    ImU32 col = IM_COL32(255, 200, 60, 255);
    if (remain < 5.f) col = IM_COL32(255, 40, 40, 255);
    else if (remain < 10.f) col = IM_COL32(255, 140, 40, 255);

    float sw = ImGui::GetIO().DisplaySize.x;
    ImVec2 ts = ImGui::CalcTextSize(buf);
    float x = (sw - ts.x) * 0.5f;
    float y = 48.f;
    dl->AddRectFilled(ImVec2(x - 12, y - 6), ImVec2(x + ts.x + 12, y + ts.y + 6), IM_COL32(10, 12, 18, 200), 6.f);
    dl->AddRect(ImVec2(x - 12, y - 6), ImVec2(x + ts.x + 12, y + ts.y + 6), col, 6.f, 0, 1.5f);
    dl->AddText(ImVec2(x, y), col, buf);
}

// -------------------- GLOW --------------------
void ApplyGlow(uintptr_t pawn, bool enemy) {
    if (!IsValid(pawn)) return;
    uintptr_t glow = pawn + O::m_Glow;
    SafeWrite<int>(glow + O::Glow::m_iGlowType, g_config.glowType);
    SafeWrite<int>(glow + O::Glow::m_nGlowRange, 5000);
    SafeWrite<int>(glow + O::Glow::m_nGlowRangeMin, 0);
    // Color override as 4 bytes RGBA
    uint8_t col[4];
    col[0] = (uint8_t)(g_config.glowR * 255.f);
    col[1] = (uint8_t)(g_config.glowG * 255.f);
    col[2] = (uint8_t)(g_config.glowB * 255.f);
    col[3] = (uint8_t)(g_config.glowA * 255.f);
    __try {
        *(uint32_t*)(glow + O::Glow::m_glowColorOverride) = *(uint32_t*)col;
        // vector color
        *(float*)(glow + O::Glow::m_fGlowColor) = g_config.glowR;
        *(float*)(glow + O::Glow::m_fGlowColor + 4) = g_config.glowG;
        *(float*)(glow + O::Glow::m_fGlowColor + 8) = g_config.glowB;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
    SafeWrite<uint8_t>(glow + O::Glow::m_bGlowing, 1);
    SafeWrite<uint8_t>(glow + O::Glow::m_bEligibleForScreenHighlight, 1);
}

void DoGlow(uintptr_t localPawn, int localTeam) {
    if (!g_config.glowEnabled || !IsInGame()) return;
    for (int i = 0; i < g_cacheCount; i++) {
        auto& c = g_cache[i];
        if (!c.alive || !IsValid(c.pawn) || c.pawn == localPawn) continue;
        if (!IsAlive(c.pawn)) continue;
        if (g_config.espTeamCheck && c.team == localTeam) continue;
        if (!OriginSane(GetOrigin(c.pawn))) continue;
        ApplyGlow(c.pawn, c.team != localTeam);
    }
}

// -------------------- SOUND / MOVEMENT ESP --------------------
void DrawSoundEsp(ImDrawList* dl, uintptr_t localPawn, int localTeam, int sw, int sh) {
    if (!g_config.soundEsp || !IsValid(localPawn) || !IsInGame()) return;
    Vector3 eye = GetOrigin(localPawn);
    if (!OriginSane(eye)) return;
    eye.z += GetViewOffset(localPawn).z;
    Vector3 viewAng = SafeRead<Vector3>(hClient + O::dwViewAngles, {});

    for (int i = 0; i < g_cacheCount; i++) {
        auto& c = g_cache[i];
        if (!c.alive || !IsValid(c.pawn) || c.pawn == localPawn) continue;
        if (!IsAlive(c.pawn)) continue;
        if (c.team == localTeam) continue;
        if (IsSpotted(c.pawn)) continue; // only non-visible

        Vector3 pos = GetOrigin(c.pawn);
        if (!OriginSane(pos)) continue;

        Vector3 vel{};
        __try {
            vel.x = *(float*)(c.pawn + O::m_vecVelocity);
            vel.y = *(float*)(c.pawn + O::m_vecVelocity + 4);
            vel.z = *(float*)(c.pawn + O::m_vecVelocity + 8);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { continue; }
        float speed = sqrtf(vel.x * vel.x + vel.y * vel.y);
        if (speed < g_config.soundMinSpeed) continue;

        float dx = pos.x - eye.x, dy = pos.y - eye.y, dz = pos.z - eye.z;
        float distM = sqrtf(dx * dx + dy * dy + dz * dz) * 0.01905f;
        if (distM > g_config.soundMaxDist) continue;

        // Angle relative to view yaw
        float yawTo = atan2f(dy, dx) * (180.f / 3.14159265f);
        float deltaYaw = yawTo - viewAng.y;
        while (deltaYaw > 180.f) deltaYaw -= 360.f;
        while (deltaYaw < -180.f) deltaYaw += 360.f;

        // Screen-edge indicator
        float rad = deltaYaw * (3.14159265f / 180.f);
        float radius = (std::min)(sw, sh) * 0.28f;
        ImVec2 center(sw * 0.5f, sh * 0.5f);
        ImVec2 tip(center.x + sinf(rad) * radius, center.y - cosf(rad) * radius);
        float alpha = 1.f - (distM / g_config.soundMaxDist);
        ImU32 col = IM_COL32(255, 200, 60, (int)(200 * alpha));
        // arrow triangle
        ImVec2 left(tip.x - cosf(rad) * 8.f - sinf(rad) * 5.f, tip.y - sinf(rad) * 8.f + cosf(rad) * 5.f);
        ImVec2 right(tip.x + cosf(rad) * 8.f - sinf(rad) * 5.f, tip.y + sinf(rad) * 8.f + cosf(rad) * 5.f);
        dl->AddTriangleFilled(tip, left, right, col);
        char db[16]; sprintf_s(db, "%.0fm", distM);
        dl->AddText(ImVec2(tip.x + 6, tip.y - 6), col, db);
    }
}

// -------------------- SKIN / KNIFE CHANGER (fallback fields) --------------------


// -------------------- rak-hus-legit PREMIUM GUI STYLE --------------------
// Crab-meat palette: deep crimson, coral, cream, dark blood-red undertones
namespace RH {
    // Core palette
    static const ImVec4 BgDeep = ImVec4(0.06f, 0.035f, 0.04f, 0.92f);   // almost black with red (slightly transparent for shader)
    static const ImVec4 BgSidebar = ImVec4(0.09f, 0.045f, 0.05f, 0.88f);
    static const ImVec4 BgChild = ImVec4(0.08f, 0.04f, 0.045f, 0.55f);   // more transparent so shader effects show through
    static const ImVec4 Accent = ImVec4(0.85f, 0.18f, 0.22f, 1.00f);   // crab red
    static const ImVec4 AccentBright = ImVec4(1.00f, 0.35f, 0.32f, 1.00f);   // coral highlight
    static const ImVec4 AccentSoft = ImVec4(0.70f, 0.22f, 0.25f, 0.55f);
    static const ImVec4 Cream = ImVec4(0.96f, 0.90f, 0.86f, 1.00f);
    static const ImVec4 TextMuted = ImVec4(0.62f, 0.50f, 0.48f, 1.00f);
    static const ImVec4 Frame = ImVec4(0.16f, 0.09f, 0.10f, 1.00f);
    static const ImVec4 FrameHover = ImVec4(0.24f, 0.12f, 0.13f, 1.00f);
    static const ImVec4 Separator = ImVec4(0.45f, 0.18f, 0.20f, 0.45f);
}

void SetupImGuiStyle() {
    ImGuiStyle& s = ImGui::GetStyle();

    // Geometry – soft, premium feel
    s.WindowRounding = 14.f;
    s.ChildRounding = 10.f;
    s.FrameRounding = 8.f;
    s.GrabRounding = 8.f;
    s.PopupRounding = 10.f;
    s.ScrollbarRounding = 10.f;
    s.TabRounding = 8.f;
    s.WindowPadding = ImVec2(0, 0);
    s.FramePadding = ImVec2(12, 7);
    s.ItemSpacing = ImVec2(12, 9);
    s.ItemInnerSpacing = ImVec2(8, 5);
    s.ScrollbarSize = 8.f;
    s.GrabMinSize = 12.f;
    s.WindowBorderSize = 0.f;
    s.ChildBorderSize = 0.f;
    s.FrameBorderSize = 0.f;
    s.PopupBorderSize = 0.f;
    s.AntiAliasedLines = true;
    s.AntiAliasedFill = true;

    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg] = RH::BgDeep;
    c[ImGuiCol_ChildBg] = RH::BgChild;
    c[ImGuiCol_PopupBg] = ImVec4(0.11f, 0.06f, 0.07f, 0.98f);
    c[ImGuiCol_Border] = ImVec4(0.55f, 0.20f, 0.22f, 0.25f);
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);

    c[ImGuiCol_FrameBg] = RH::Frame;
    c[ImGuiCol_FrameBgHovered] = RH::FrameHover;
    c[ImGuiCol_FrameBgActive] = ImVec4(0.30f, 0.14f, 0.15f, 1.00f);

    c[ImGuiCol_TitleBg] = RH::BgDeep;
    c[ImGuiCol_TitleBgActive] = RH::BgDeep;
    c[ImGuiCol_TitleBgCollapsed] = RH::BgDeep;

    c[ImGuiCol_CheckMark] = RH::AccentBright;
    c[ImGuiCol_SliderGrab] = RH::Accent;
    c[ImGuiCol_SliderGrabActive] = RH::AccentBright;

    c[ImGuiCol_Button] = ImVec4(0.28f, 0.12f, 0.14f, 0.90f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.42f, 0.16f, 0.18f, 1.00f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.55f, 0.18f, 0.20f, 1.00f);

    c[ImGuiCol_Header] = ImVec4(0.32f, 0.13f, 0.15f, 0.75f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.45f, 0.16f, 0.18f, 0.90f);
    c[ImGuiCol_HeaderActive] = ImVec4(0.55f, 0.18f, 0.20f, 1.00f);

    c[ImGuiCol_Separator] = RH::Separator;
    c[ImGuiCol_SeparatorHovered] = RH::AccentSoft;
    c[ImGuiCol_SeparatorActive] = RH::Accent;

    c[ImGuiCol_Text] = RH::Cream;
    c[ImGuiCol_TextDisabled] = RH::TextMuted;
    c[ImGuiCol_TextSelectedBg] = ImVec4(0.70f, 0.20f, 0.22f, 0.35f);

    c[ImGuiCol_ScrollbarBg] = ImVec4(0.08f, 0.04f, 0.045f, 0.80f);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(0.45f, 0.18f, 0.20f, 0.70f);
    c[ImGuiCol_ScrollbarGrabHovered] = RH::Accent;
    c[ImGuiCol_ScrollbarGrabActive] = RH::AccentBright;

    c[ImGuiCol_PlotLines] = RH::Accent;
    c[ImGuiCol_PlotHistogram] = RH::AccentBright;
}

// Smooth animated sidebar button with glowing active indicator

static void ResetAimTab() {
    g_config.aimEnabled = true; g_config.aimFov = 28.f; g_config.aimSmooth = 0.78f;
    g_config.aimBone = 0; g_config.aimTeamCheck = true; g_config.aimVisibleOnly = true;
    g_config.aimHumanize = 0.35f; g_config.aimRecoilComp = true; g_config.aimRecoilCompStr = 1.f;
    g_config.rcsEnabled = true; g_config.rcsStrength = 0.55f; g_config.rcsStartBullet = 2;
    g_config.punchUnified = true; g_config.earlyPunchPath = true;
}
static void ResetTriggerTab() {
    g_config.triggerEnabled = false; g_config.triggerDelayMin = 45; g_config.triggerDelayMax = 95;
    g_config.triggerWeaponProfiles = true; g_config.triggerFlashCheck = true;
    g_config.triggerSmokeCheck = true; g_config.triggerRcs = true;
}
static void ResetVisualsTab() {
    g_config.espEnabled = true; g_config.espBox = true; g_config.espYBias = 0.f;
    g_config.espSkeleton = false; g_config.espSkeletonEveryOther = true;
}

static bool SidebarButton(const char* label, int id, int& current) {
    bool active = (current == id);
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID wid = window->GetID(label);
    const ImVec2 label_size = ImGui::CalcTextSize(label, nullptr, true);

    ImVec2 pos = window->DC.CursorPos;
    ImVec2 size = ImVec2(ImGui::GetContentRegionAvail().x, 40.f);
    const ImRect bb(pos, ImVec2(pos.x + size.x, pos.y + size.y));

    ImGui::ItemSize(size, style.FramePadding.y);
    if (!ImGui::ItemAdd(bb, wid)) return false;

    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(bb, wid, &hovered, &held);
    if (pressed) current = id;

    // Animated lerp for background
    static float anim[8] = {};
    float& t = anim[id < 8 ? id : 0];
    float target = active ? 1.f : (hovered ? 0.45f : 0.f);
    t = t + (target - t) * ImGui::GetIO().DeltaTime * 12.f;

    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Soft background fill
    if (t > 0.01f) {
        ImU32 col = ImGui::GetColorU32(ImVec4(
            0.55f * t, 0.12f * t, 0.14f * t, 0.55f + 0.25f * t));
        dl->AddRectFilled(bb.Min, bb.Max, col, 8.f);
    }

    // Left accent bar (animated width + glow)
    if (t > 0.05f) {
        float barW = 3.5f + 2.5f * t;
        ImU32 barCol = ImGui::GetColorU32(ImVec4(1.f, 0.32f + 0.2f * t, 0.30f, 0.7f + 0.3f * t));
        dl->AddRectFilled(ImVec2(bb.Min.x, bb.Min.y + 6), ImVec2(bb.Min.x + barW, bb.Max.y - 6), barCol, 3.f);

        // Soft outer glow
        dl->AddRectFilled(ImVec2(bb.Min.x, bb.Min.y + 4), ImVec2(bb.Min.x + barW + 4.f, bb.Max.y - 4),
            ImGui::GetColorU32(ImVec4(0.9f, 0.2f, 0.22f, 0.15f * t)), 4.f);
    }

    // Text
    ImVec4 txtCol = active ? RH::Cream : (hovered ? ImVec4(0.90f, 0.78f, 0.75f, 1.f) : RH::TextMuted);
    ImVec2 text_pos = ImVec2(bb.Min.x + 18.f, bb.Min.y + (size.y - label_size.y) * 0.5f);
    dl->AddText(text_pos, ImGui::GetColorU32(txtCol), label);

    return pressed;
}

static void SectionHeader(const char* title) {
    ImGui::Spacing();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();

    // Accent diamond / marker
    float pulse = 0.65f + 0.35f * sinf((float)ImGui::GetTime() * 3.2f);
    dl->AddCircleFilled(ImVec2(p.x + 5, p.y + 8), 3.5f,
        ImGui::GetColorU32(ImVec4(0.95f, 0.28f, 0.30f, pulse)));

    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 16);
    ImGui::TextColored(RH::AccentBright, "%s", title);

    // Thin animated underline
    ImVec2 p2 = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;
    float t = (float)ImGui::GetTime();
    dl->AddRectFilled(ImVec2(p2.x, p2.y), ImVec2(p2.x + w * 0.55f, p2.y + 1.5f),
        ImGui::GetColorU32(ImVec4(0.75f, 0.20f, 0.22f, 0.55f + 0.2f * sinf(t * 2.1f))), 1.f);
    ImGui::Spacing();
    ImGui::Spacing();
}

void DrawMenu() {
    static float alpha = 0.f;
    const float dt = ImGui::GetIO().DeltaTime;
    if (g_config.showMenu) alpha = (std::min)(1.f, alpha + dt * 7.5f);
    else                   alpha = (std::max)(0.f, alpha - dt * 8.5f);

    if (alpha < 0.015f) {
        ImGui::SetNextWindowPos(ImVec2(18, 18), ImGuiCond_Always);
        ImGui::Begin("##hint", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_AlwaysAutoResize);
        float pulse = 0.45f + 0.25f * sinf((float)ImGui::GetTime() * 2.8f);
        ImGui::TextColored(ImVec4(0.90f, 0.35f, 0.35f, pulse), "INSERT  ·  rak-hus-legit");
        ImGui::End();
        return;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
    ImGui::SetNextWindowSize(ImVec2(700, 540), ImGuiCond_Always);
    ImGui::Begin("##rak-hus-legit", &g_config.showMenu,
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoScrollbar);

    ImDrawList* wdl = ImGui::GetWindowDrawList();
    ImVec2 wp = ImGui::GetWindowPos();
    ImVec2 ws = ImGui::GetWindowSize();
    float t = (float)ImGui::GetTime();

    // ============================================================
    //  SHADER-STYLE PROCEDURAL BACKGROUND (ImDrawList based)
    //  Animated glow orbs, scanlines, vignette, light rays, waves
    // ============================================================

    // 1) Deep base fill
    wdl->AddRectFilled(wp, ImVec2(wp.x + ws.x, wp.y + ws.y),
        ImGui::GetColorU32(ImVec4(0.055f, 0.028f, 0.032f, 0.97f)), 14.f);

    // 2) Soft animated glow orbs (fake volumetric light)
    auto drawGlowOrb = [&](float cx, float cy, float radius, float intensity, float r, float g, float b) {
        const int layers = 6;
        for (int i = layers; i >= 1; --i) {
            float f = (float)i / layers;
            float rad = radius * (0.35f + 0.65f * f);
            float a = intensity * (1.f - f) * (1.f - f) * 0.55f;
            wdl->AddCircleFilled(ImVec2(wp.x + cx, wp.y + cy), rad,
                ImGui::GetColorU32(ImVec4(r, g, b, a)), 32);
        }
        };

    float orb1x = ws.x * 0.25f + sinf(t * 0.45f) * 40.f;
    float orb1y = ws.y * 0.30f + cosf(t * 0.38f) * 30.f;
    drawGlowOrb(orb1x, orb1y, 160.f, 0.55f + 0.15f * sinf(t * 1.2f), 0.85f, 0.12f, 0.16f);

    float orb2x = ws.x * 0.78f + cosf(t * 0.32f) * 50.f;
    float orb2y = ws.y * 0.70f + sinf(t * 0.41f) * 35.f;
    drawGlowOrb(orb2x, orb2y, 190.f, 0.40f + 0.12f * cosf(t * 0.9f), 0.70f, 0.15f, 0.22f);

    float orb3x = ws.x * 0.55f + sinf(t * 0.28f + 1.5f) * 60.f;
    float orb3y = ws.y * 0.15f + cosf(t * 0.55f) * 25.f;
    drawGlowOrb(orb3x, orb3y, 110.f, 0.35f, 1.00f, 0.28f, 0.25f);

    // 3) Diagonal light streaks
    for (int i = 0; i < 4; ++i) {
        float phase = t * (0.15f + i * 0.04f) + i * 1.7f;
        float x0 = fmodf(phase * 80.f, ws.x + 200.f) - 100.f;
        float y0 = -40.f + i * 30.f;
        float x1 = x0 + 220.f;
        float y1 = y0 + ws.y + 80.f;
        ImU32 col = ImGui::GetColorU32(ImVec4(0.95f, 0.22f, 0.25f, 0.035f + 0.015f * sinf(t * 2.f + i)));
        wdl->AddLine(ImVec2(wp.x + x0, wp.y + y0), ImVec2(wp.x + x1, wp.y + y1), col, 18.f + i * 4.f);
    }

    // 4) Horizontal scanlines
    for (float y = 0.f; y < ws.y; y += 3.5f) {
        float a = 0.018f + 0.012f * sinf(y * 0.08f + t * 3.5f);
        wdl->AddLine(ImVec2(wp.x, wp.y + y), ImVec2(wp.x + ws.x, wp.y + y),
            ImGui::GetColorU32(ImVec4(1.f, 0.3f, 0.3f, a)), 1.0f);
    }

    // 5) Bottom wave
    {
        const int segs = 48;
        ImVec2 pts[49];
        for (int i = 0; i <= segs; ++i) {
            float x = (float)i / segs * ws.x;
            float y = ws.y - 28.f + sinf(x * 0.025f + t * 1.8f) * 7.f
                + sinf(x * 0.05f - t * 2.3f) * 4.f;
            pts[i] = ImVec2(wp.x + x, wp.y + y);
        }
        for (int i = 0; i < segs; ++i) {
            wdl->AddLine(pts[i], pts[i + 1],
                ImGui::GetColorU32(ImVec4(0.90f, 0.25f, 0.28f, 0.22f)), 1.8f);
        }
    }

    // 6) Vignette
    {
        float vig = 0.55f;
        wdl->AddRectFilledMultiColor(wp, ImVec2(wp.x + ws.x, wp.y + 70.f),
            ImGui::GetColorU32(ImVec4(0, 0, 0, vig)), ImGui::GetColorU32(ImVec4(0, 0, 0, vig)),
            ImGui::GetColorU32(ImVec4(0, 0, 0, 0)), ImGui::GetColorU32(ImVec4(0, 0, 0, 0)));
        wdl->AddRectFilledMultiColor(ImVec2(wp.x, wp.y + ws.y - 80.f), ImVec2(wp.x + ws.x, wp.y + ws.y),
            ImGui::GetColorU32(ImVec4(0, 0, 0, 0)), ImGui::GetColorU32(ImVec4(0, 0, 0, 0)),
            ImGui::GetColorU32(ImVec4(0, 0, 0, vig)), ImGui::GetColorU32(ImVec4(0, 0, 0, vig)));
        wdl->AddRectFilledMultiColor(wp, ImVec2(wp.x + 60.f, wp.y + ws.y),
            ImGui::GetColorU32(ImVec4(0, 0, 0, vig * 0.7f)), ImGui::GetColorU32(ImVec4(0, 0, 0, 0)),
            ImGui::GetColorU32(ImVec4(0, 0, 0, 0)), ImGui::GetColorU32(ImVec4(0, 0, 0, vig * 0.7f)));
        wdl->AddRectFilledMultiColor(ImVec2(wp.x + ws.x - 60.f, wp.y), ImVec2(wp.x + ws.x, wp.y + ws.y),
            ImGui::GetColorU32(ImVec4(0, 0, 0, 0)), ImGui::GetColorU32(ImVec4(0, 0, 0, vig * 0.7f)),
            ImGui::GetColorU32(ImVec4(0, 0, 0, vig * 0.7f)), ImGui::GetColorU32(ImVec4(0, 0, 0, 0)));
    }

    // 7) Breathing outer glow border
    float glowA = 0.22f + 0.10f * sinf(t * 1.6f);
    wdl->AddRect(ImVec2(wp.x - 1.5f, wp.y - 1.5f), ImVec2(wp.x + ws.x + 1.5f, wp.y + ws.y + 1.5f),
        ImGui::GetColorU32(ImVec4(0.90f, 0.22f, 0.24f, glowA)), 15.f, 0, 2.2f);
    wdl->AddRect(ImVec2(wp.x - 4.f, wp.y - 4.f), ImVec2(wp.x + ws.x + 4.f, wp.y + ws.y + 4.f),
        ImGui::GetColorU32(ImVec4(0.80f, 0.15f, 0.18f, glowA * 0.35f)), 17.f, 0, 1.0f);

    // 8) Top accent highlight
    wdl->AddRectFilledMultiColor(
        ImVec2(wp.x + 14, wp.y + 1), ImVec2(wp.x + ws.x - 14, wp.y + 3.5f),
        ImGui::GetColorU32(ImVec4(0.9f, 0.15f, 0.18f, 0.0f)),
        ImGui::GetColorU32(ImVec4(1.0f, 0.40f, 0.32f, 0.90f)),
        ImGui::GetColorU32(ImVec4(1.0f, 0.40f, 0.32f, 0.90f)),
        ImGui::GetColorU32(ImVec4(0.9f, 0.15f, 0.18f, 0.0f)));

    // ===== SIDEBAR =====
    ImGui::BeginChild("##side", ImVec2(168, 0), false);
    {
        ImDrawList* sdl = ImGui::GetWindowDrawList();
        ImVec2 sp = ImGui::GetWindowPos();
        ImVec2 ss = ImGui::GetWindowSize();

        // Sidebar background (semi-transparent so shader orbs bleed through)
        sdl->AddRectFilledMultiColor(sp, ImVec2(sp.x + ss.x, sp.y + ss.y),
            ImGui::GetColorU32(ImVec4(0.10f, 0.045f, 0.05f, 0.82f)),
            ImGui::GetColorU32(ImVec4(0.07f, 0.035f, 0.04f, 0.88f)),
            ImGui::GetColorU32(ImVec4(0.07f, 0.035f, 0.04f, 0.88f)),
            ImGui::GetColorU32(ImVec4(0.10f, 0.045f, 0.05f, 0.82f)));

        // Vertical accent
        sdl->AddRectFilled(ImVec2(sp.x + ss.x - 2, sp.y + 20), ImVec2(sp.x + ss.x, sp.y + ss.y - 20),
            ImGui::GetColorU32(ImVec4(0.75f, 0.18f, 0.20f, 0.35f)));

        // Logo / Title
        ImGui::SetCursorPos(ImVec2(16, 22));
        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);
        float logoPulse = 0.75f + 0.25f * sinf(t * 2.4f);
        ImGui::TextColored(ImVec4(1.f, 0.32f + 0.1f * logoPulse, 0.30f, 1.f), "rak-hus");
        ImGui::PopFont();
        ImGui::SetCursorPosX(16);
        ImGui::TextColored(RH::TextMuted, "legit  ·  cs2");
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6);

        // Decorative line under logo
        ImVec2 lp = ImGui::GetCursorScreenPos();
        sdl->AddRectFilled(ImVec2(lp.x + 4, lp.y), ImVec2(lp.x + 110, lp.y + 1.5f),
            ImGui::GetColorU32(ImVec4(0.70f, 0.18f, 0.20f, 0.5f)));

        ImGui::Spacing(); ImGui::Spacing(); ImGui::Spacing();

        ImGui::SetCursorPosX(6);
        ImGui::BeginGroup();
        SidebarButton("  Aimbot", 0, g_menuTab);
        SidebarButton("  Trigger", 1, g_menuTab);
        SidebarButton("  Visuals", 2, g_menuTab);
        SidebarButton("  Misc", 3, g_menuTab);
        SidebarButton("  Glow", 4, g_menuTab);
        SidebarButton("  Config", 5, g_menuTab);
        ImGui::EndGroup();

        // Bottom status
        ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 48);
        ImGui::SetCursorPosX(14);
        ImGui::TextColored(ImVec4(0.50f, 0.35f, 0.35f, 0.8f), "INSERT  close");
        ImGui::SetCursorPosX(14);
        ImGui::TextColored(ImVec4(0.40f, 0.28f, 0.28f, 0.6f), "v1.4  premium");
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("##content", ImVec2(0, 0), false);
    ImGui::SetCursorPos(ImVec2(16, 12));
    ImGui::BeginGroup();

    if (g_menuTab == 0) {
        SectionHeader("AIMBOT");
        if (ImGui::SmallButton("Reset aim tab")) ResetAimTab();
        ImGui::Checkbox("Enable", &g_config.aimEnabled);
        ImGui::SliderFloat("FOV", &g_config.aimFov, 5.f, 120.f, "%.0f px");
        ImGui::SliderFloat("Smooth", &g_config.aimSmooth, 0.50f, 0.97f, "%.2f");
        ImGui::TextDisabled("Hold Mouse5 (default). Smooth 0.6–0.8. Delete legit.ini after update.");
        ImGui::SliderFloat("Humanize", &g_config.aimHumanize, 0.f, 1.f, "%.2f");
        const char* bones[] = { "Head", "Neck", "Chest" };
        ImGui::Combo("Bone", &g_config.aimBone, bones, 3);
        ImGui::Checkbox("Team check", &g_config.aimTeamCheck);
        ImGui::Checkbox("Visible only", &g_config.aimVisibleOnly);
        ImGui::Checkbox("Only when scoped", &g_config.aimOnlyWhenScoped);
        ImGui::Checkbox("Bone priority (H→N→C)", &g_config.aimBonePriority);
        ImGui::Checkbox("Draw FOV circle", &g_config.aimDrawFov);

        ImGui::Spacing();
        static bool bindAim = false;
        ImGui::Text("Aim key:");
        ImGui::SameLine();
        if (bindAim) {
            ImGui::TextColored(ImVec4(1.f, 0.45f, 0.35f, 1.f), "press key...");
            for (int vk = 1; vk < 256; vk++) {
                if (vk == VK_INSERT) continue;
                if (GetAsyncKeyState(vk) & 0x8000) { g_config.aimKey = vk; bindAim = false; SaveConfig(); break; }
            }
        }
        else {
            char kb[24]; sprintf_s(kb, "0x%02X", g_config.aimKey);
            if (ImGui::Button(kb, ImVec2(70, 0))) bindAim = true;
            ImGui::SameLine();
            if (ImGui::Button("LMB")) { g_config.aimKey = VK_LBUTTON; SaveConfig(); }
            ImGui::SameLine();
            ImGui::TextDisabled("Avoid LMB — fights mouse while shooting");
            ImGui::SameLine();
            if (ImGui::Button("M4")) { g_config.aimKey = VK_XBUTTON1; SaveConfig(); }
        }

        SectionHeader("SOFT RCS");
        ImGui::Checkbox("Enable RCS", &g_config.rcsEnabled);
        ImGui::TextDisabled("Soft RCS angle write is disabled (was locking view). Use aim recoil-comp.");
        ImGui::SliderFloat("Strength (unused)", &g_config.rcsStrength, 0.1f, 1.f, "%.2f");
        ImGui::SliderInt("Start bullet", &g_config.rcsStartBullet, 1, 6);
    }
    else if (g_menuTab == 1) {
        SectionHeader("TRIGGERBOT");
        if (ImGui::SmallButton("Reset trigger tab")) ResetTriggerTab();
        ImGui::Checkbox("Enable", &g_config.triggerEnabled);
        const char* tbones[] = { "Head", "Neck", "Chest" };
        ImGui::Combo("Bone (only fire on)", &g_config.triggerBone, tbones, 3);
        ImGui::SliderFloat("Bone FOV (px)", &g_config.triggerBoneFov, 3.f, 40.f, "%.0f");
        ImGui::Checkbox("Hitchance", &g_config.triggerHitchance);
        if (g_config.triggerHitchance) {
            ImGui::SliderFloat("Min hitchance %", &g_config.triggerHitchanceMin, 10.f, 95.f, "%.0f");
            ImGui::TextDisabled("Last HC: %.0f%%  (accuracy + move + dist + FOV)", g_lastTriggerHC);
        }
        ImGui::TextDisabled("Only shoots when that bone is under the crosshair.");

        ImGui::SliderInt("Delay min (ms)", &g_config.triggerDelayMin, 20, 250);
        ImGui::SliderInt("Delay max (ms)", &g_config.triggerDelayMax, 30, 350);
        if (g_config.triggerDelayMax < g_config.triggerDelayMin)
            g_config.triggerDelayMax = g_config.triggerDelayMin;
        ImGui::Checkbox("Team check", &g_config.triggerTeamCheck);
        ImGui::Checkbox("Visible only", &g_config.triggerVisibleOnly);

        SectionHeader("TRIGGER RCS");
        ImGui::Checkbox("Anti-recoil (hit through spray)", &g_config.triggerRcs);
        ImGui::SliderFloat("RCS strength", &g_config.triggerRcsStrength, 0.1f, 1.f, "%.2f");
        ImGui::TextDisabled("Compensates aim punch so the bullet still hits the bone.");

        static bool bindTrig = false;
        ImGui::Text("Trigger key:");
        ImGui::SameLine();
        if (bindTrig) {
            ImGui::TextColored(ImVec4(1.f, 0.45f, 0.35f, 1.f), "press key...");
            for (int vk = 1; vk < 256; vk++) {
                if (vk == VK_INSERT) continue;
                if (GetAsyncKeyState(vk) & 0x8000) { g_config.triggerKey = vk; bindTrig = false; SaveConfig(); break; }
            }
        }
        else {
            char kb[24]; sprintf_s(kb, "0x%02X", g_config.triggerKey);
            if (ImGui::Button(kb, ImVec2(70, 0))) bindTrig = true;
            ImGui::SameLine();
            if (ImGui::Button("M5")) { g_config.triggerKey = VK_XBUTTON2; SaveConfig(); }
        }
    }
    else if (g_menuTab == 2) {
        SectionHeader("ESP");
        if (ImGui::SmallButton("Reset visuals tab")) ResetVisualsTab();
        ImGui::Checkbox("Enable ESP", &g_config.espEnabled);
        ImGui::Checkbox("Box", &g_config.espBox); ImGui::SameLine();
        ImGui::Checkbox("Outline", &g_config.espBoxOutline);
        ImGui::Checkbox("Name", &g_config.espName); ImGui::SameLine();
        ImGui::Checkbox("Health", &g_config.espHealth);
        ImGui::Checkbox("Armor", &g_config.espArmor); ImGui::SameLine();
        ImGui::Checkbox("Weapon", &g_config.espWeapon);
        ImGui::Checkbox("Distance", &g_config.espDistance); ImGui::SameLine();
        ImGui::Checkbox("Head dot", &g_config.espHeadDot);
        ImGui::Checkbox("Skeleton", &g_config.espSkeleton);
        ImGui::Checkbox("Team check", &g_config.espTeamCheck);
        ImGui::Checkbox("Visible only", &g_config.espVisibleOnly);
        ImGui::SliderFloat("Box thickness", &g_config.espBoxThickness, 0.8f, 3.f, "%.1f");
        ImGui::SliderFloat("Max distance (m)", &g_config.espMaxDistance, 15.f, 150.f, "%.0f");
        ImGui::SliderFloat("ESP Y bias (px)", &g_config.espYBias, -40.f, 40.f, "%.0f");
        ImGui::TextDisabled("+ shifts ESP down; - shifts ESP up");
        ImGui::Checkbox("Skeleton every other frame", &g_config.espSkeletonEveryOther);
        ImGui::Checkbox("ESP optimize (cache path)", &g_config.espOptimize);
        ImGui::Checkbox("Entity cache lite", &g_config.entityCacheLite);
        ImGui::ColorEdit3("Enemy (hidden)", &g_config.espColorR);
        ImGui::ColorEdit3("Enemy (visible)", &g_config.espVisColorR);
    }
    else if (g_menuTab == 3) {
        SectionHeader("MISC");
        ImGui::Checkbox("No Flash", &g_config.noFlash);
        ImGui::Checkbox("No Smoke", &g_config.noSmoke);
        ImGui::Checkbox("No Visual Recoil", &g_config.noVisualRecoil);
        ImGui::Checkbox("Spectator list", &g_config.spectatorList);
        ImGui::Checkbox("Hitmarker", &g_config.hitmarker);
        ImGui::Checkbox("Hitlog (floating dmg)", &g_config.hitlog);
        ImGui::Checkbox("Watermark", &g_config.watermark);
        ImGui::Checkbox("FOV changer", &g_config.fovChanger);
        if (g_config.fovChanger)
            ImGui::SliderFloat("FOV", &g_config.fovValue, 70.f, 120.f, "%.0f");
        ImGui::TextDisabled("Default OFF. Uses one CViewSetup FOV slot only.");
        ImGui::Checkbox("Sniper crosshair (unscoped)", &g_config.sniperCrosshair);
        ImGui::Checkbox("Bomb world ESP", &g_config.bombEsp);
        ImGui::Checkbox("Nade prediction", &g_config.nadePred);
        SectionHeader("ENVIRONMENT");
        ImGui::Checkbox("Environment changer", &g_config.envEnabled);
        if (g_config.envEnabled) {
            const char* presets[] = { "Custom", "Night", "Warm", "Cold", "Dark", "Bright" };
            if (g_config.envPreset < 0 || g_config.envPreset > 5) g_config.envPreset = 1;
            ImGui::Combo("Preset", &g_config.envPreset, presets, 6);
            ImGui::SliderFloat("Strength", &g_config.envStrength, 0.05f, 1.f, "%.2f");
            ImGui::TextDisabled("C_EnvSky tint + fog_controller + post process exposure");
            ImGui::TextDisabled("(schema offsets, cached entity scan).");
        }
        if (g_config.nadePred)
            ImGui::SliderInt("Nade steps", &g_config.nadePredSteps, 15, 80);
        ImGui::Checkbox("Bomb timer (precise)", &g_config.bombTimer);

        SectionHeader("THIRD PERSON");
        ImGui::Checkbox("Enable third person", &g_config.thirdPerson);
        ImGui::Checkbox("Toggle mode (else hold)", &g_config.thirdPersonToggle);
        ImGui::TextDisabled("Engine path: CSGOInput +0x229 + ThirdPersonReset pattern.");
        ImGui::TextDisabled("Hold key, or toggle on key press if toggle mode is on.");
        {
            static bool bindTp = false;
            ImGui::Text("Hold key:");
            ImGui::SameLine();
            if (bindTp) {
                ImGui::TextColored(ImVec4(1.f, 0.45f, 0.35f, 1.f), "press...");
                for (int vk = 1; vk < 256; vk++) {
                    if (vk == VK_INSERT) continue;
                    if (GetAsyncKeyState(vk) & 0x8000) { g_config.thirdPersonKey = vk; bindTp = false; SaveConfig(); break; }
                }
            }
            else {
                char kb[24]; sprintf_s(kb, "0x%02X", g_config.thirdPersonKey);
                if (ImGui::Button(kb, ImVec2(70, 0))) bindTp = true;
                ImGui::SameLine();
                if (ImGui::Button("M4##tp")) { g_config.thirdPersonKey = VK_XBUTTON1; SaveConfig(); }
                ImGui::SameLine();
                if (ImGui::Button("M5##tp")) { g_config.thirdPersonKey = VK_XBUTTON2; SaveConfig(); }
                ImGui::SameLine();
                if (ImGui::Button("Alt##tp")) { g_config.thirdPersonKey = VK_MENU; SaveConfig(); }
            }
        }

        SectionHeader("SOUND ESP");
        ImGui::Checkbox("Movement indicators", &g_config.soundEsp);
        ImGui::SliderFloat("Min speed", &g_config.soundMinSpeed, 20.f, 250.f, "%.0f");
        ImGui::SliderFloat("Max distance (m)", &g_config.soundMaxDist, 5.f, 50.f, "%.0f");
        ImGui::TextDisabled("Arrows for non-visible enemies who are moving.");

        SectionHeader("CROSSHAIR");
        ImGui::Checkbox("Custom crosshair", &g_config.customCrosshair);
        ImGui::SliderFloat("Size", &g_config.chSize, 2.f, 20.f, "%.0f");
        ImGui::SliderFloat("Gap", &g_config.chGap, 0.f, 12.f, "%.0f");
        ImGui::SliderFloat("Thickness", &g_config.chThick, 0.5f, 4.f, "%.1f");
        ImGui::ColorEdit3("Color", &g_config.chR);
    }
    else if (g_menuTab == 4) {
        SectionHeader("GLOW");
        ImGui::Checkbox("Enable Glow", &g_config.glowEnabled);
        ImGui::SliderInt("Glow type", &g_config.glowType, 0, 3);
        ImGui::ColorEdit4("Glow color", &g_config.glowR);



    }
    else if (g_menuTab == 5) {
        SectionHeader("CONFIG");
        SectionHeader("PATTERN HEALTH");
        auto patLine = [](const char* name, uintptr_t addr) {
            ImGui::Text("%s", name);
            ImGui::SameLine(180);
            if (addr) ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.4f, 1.f), "OK  %p", (void*)addr);
            else ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.f), "MISS");
        };
        patLine("ViewMatrix", Pat::g_res.viewMatrix);
        patLine("EntitySystem", Pat::g_res.gameEntitySystemPtr);
        patLine("LocalController", Pat::g_res.localPlayerControllerPtr);
        patLine("GlobalVars", Pat::g_res.globalVarsPtr);
        patLine("DrawSmokeArray", Pat::g_res.drawSmokeArray);
        patLine("CSGOInput", Pat::g_res.csgoInputPtr);
        patLine("ThirdPersonReset", Pat::g_res.thirdPersonReset);
        ImGui::TextDisabled("END = features on/off (DLL stays). Hooks removed only on real FreeLibrary.");
        ImGui::Spacing();
        if (ImGui::Button("Save", ImVec2(110, 32))) SaveConfig();
        ImGui::SameLine();
        if (ImGui::Button("Load", ImVec2(110, 32))) LoadConfig();
        ImGui::SameLine();
        if (ImGui::Button("Reset defaults", ImVec2(130, 32))) {
            g_config = Config();
            SaveConfig();
        }
        ImGui::Spacing();
        ImGui::TextDisabled("%s", GetConfigPath().c_str());
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.95f, 0.40f, 0.38f, 1.f), "Legit tips:");
        ImGui::BulletText("Smooth 0.70–0.85 · FOV 20–35");
        ImGui::BulletText("Humanize ~0.3 · RCS strength ~0.5");
        ImGui::BulletText("Trigger delay 25–60 ms random");
        ImGui::BulletText("Visible-only on aim + ESP for safer look");
    }

    ImGui::EndGroup();
    ImGui::EndChild();
    ImGui::End();
    ImGui::PopStyleVar();
}

// -------------------- MAIN FRAME --------------------

// -------------------- WATERMARK / FOV / BOMB ESP / NADE / SNIPER CH --------------------


// -------------------- ENVIRONMENT CHANGER (schema offsets) --------------------
// C_EnvSky — client_dll schema build 14178
namespace EnvSky {
    constexpr std::ptrdiff_t m_vTintColor = 0xFC1;
    constexpr std::ptrdiff_t m_vTintColorLightingOnly = 0xFC5;
    constexpr std::ptrdiff_t m_flBrightnessScale = 0xFCC;
}
// C_FogController::m_fog (fogparams_t)
namespace EnvFog {
    constexpr std::ptrdiff_t m_fog = 0x600;
    constexpr std::ptrdiff_t colorPrimary = 0x14;   // relative to m_fog
    constexpr std::ptrdiff_t colorSecondary = 0x18;
    constexpr std::ptrdiff_t start = 0x24;
    constexpr std::ptrdiff_t end = 0x28;
    constexpr std::ptrdiff_t maxdensity = 0x30;
    constexpr std::ptrdiff_t HDRColorScale = 0x38;
    constexpr std::ptrdiff_t enable = 0x64;
}
// C_PostProcessingVolume exposure
namespace EnvPP {
    constexpr std::ptrdiff_t m_flMinExposure = 0x10BC;
    constexpr std::ptrdiff_t m_flMaxExposure = 0x10C0;
    constexpr std::ptrdiff_t m_flExposureCompensation = 0x10C4;
    constexpr std::ptrdiff_t m_bExposureControl = 0x10D5;
}

static void WriteSkyTint(uintptr_t sky, uint8_t r, uint8_t g, uint8_t b, uint8_t a, float brightness) {
    if (!IsValid(sky)) return;
    __try {
        uint8_t* c = (uint8_t*)(sky + EnvSky::m_vTintColor);
        c[0] = r; c[1] = g; c[2] = b; c[3] = a;
        uint8_t* c2 = (uint8_t*)(sky + EnvSky::m_vTintColorLightingOnly);
        c2[0] = r; c2[1] = g; c2[2] = b; c2[3] = a;
        *(float*)(sky + EnvSky::m_flBrightnessScale) = brightness;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

static void WriteFogTint(uintptr_t fogEnt, uint8_t r, uint8_t g, uint8_t b, float density, float startD, float endD) {
    if (!IsValid(fogEnt)) return;
    __try {
        uintptr_t fog = fogEnt + EnvFog::m_fog;
        uint8_t* cp = (uint8_t*)(fog + EnvFog::colorPrimary);
        uint8_t* cs = (uint8_t*)(fog + EnvFog::colorSecondary);
        cp[0] = r; cp[1] = g; cp[2] = b; cp[3] = 255;
        cs[0] = r; cs[1] = g; cs[2] = b; cs[3] = 255;
        *(float*)(fog + EnvFog::start) = startD;
        *(float*)(fog + EnvFog::end) = endD;
        *(float*)(fog + EnvFog::maxdensity) = density;
        *(float*)(fog + EnvFog::HDRColorScale) = 1.f;
        *(uint8_t*)(fog + EnvFog::enable) = 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

static void WritePPExposure(uintptr_t vol, float minE, float maxE, float comp) {
    if (!IsValid(vol)) return;
    __try {
        *(float*)(vol + EnvPP::m_flMinExposure) = minE;
        *(float*)(vol + EnvPP::m_flMaxExposure) = maxE;
        *(float*)(vol + EnvPP::m_flExposureCompensation) = comp;
        *(uint8_t*)(vol + EnvPP::m_bExposureControl) = 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// Cached environment entity handles — avoid full entity scans every call
static uintptr_t g_envSky[8] = {};
static int g_envSkyCount = 0;
static uintptr_t g_envFog[4] = {};
static int g_envFogCount = 0;
static uintptr_t g_envPP[4] = {};
static int g_envPPCount = 0;
static int g_envCacheTick = 0;

static void RefreshEnvEntityCache() {
    g_envSkyCount = g_envFogCount = g_envPPCount = 0;
    if (!g_pES || !IsValid(g_pES)) return;
    int highest = SafeRead<int>(g_pES + O::dwGameEntitySystem_highestEntityIndex, 512);
    if (highest < 64) highest = 512;
    if (highest > 2048) highest = 2048;
    for (int i = 64; i <= highest; i++) {
        uintptr_t id = GetIdentityPtr(i);
        if (!IsValid(id)) continue;
        uintptr_t ent = SafeRead<uintptr_t>(id, 0);
        if (!IsValid(ent)) continue;
        if (DesignerNameEquals(id, "env_sky")) {
            if (g_envSkyCount < 8) g_envSky[g_envSkyCount++] = ent;
        } else if (DesignerNameEquals(id, "fog_controller") || DesignerNameEquals(id, "env_fog_controller")) {
            if (g_envFogCount < 4) g_envFog[g_envFogCount++] = ent;
        } else if (DesignerNameEquals(id, "post_processing_volume") || DesignerNameEquals(id, "env_post_processing")) {
            if (g_envPPCount < 4) g_envPP[g_envPPCount++] = ent;
        }
        if (g_envSkyCount >= 8 && g_envFogCount >= 4 && g_envPPCount >= 4) break;
    }
}

void DoEnvironmentSky() {
    if (!g_config.envEnabled || !g_pES || !IsInGame()) return;

    // Refresh entity cache every ~2s (or first run)
    if ((++g_envCacheTick % 128) == 1 || (g_envSkyCount + g_envFogCount + g_envPPCount) == 0)
        RefreshEnvEntityCache();

    // Apply tint every ~8 frames (cheap writes)
    static int applyTick = 0;
    if ((++applyTick % 8) != 0) return;

    uint8_t r = 255, g = 255, b = 255, a = 255;
    float bright = 1.f;
    float fogDensity = 0.f;
    float fogStart = 500.f, fogEnd = 4000.f;
    float expMin = 0.2f, expMax = 2.f, expComp = 0.f;
    float s = g_config.envStrength;
    if (s < 0.05f) s = 0.05f;
    if (s > 1.f) s = 1.f;

    switch (g_config.envPreset) {
    case 1: // Night
        r = (uint8_t)(20 + 30 * (1.f - s)); g = (uint8_t)(30 + 40 * (1.f - s)); b = (uint8_t)(70 + 50 * (1.f - s));
        bright = 1.f - 0.75f * s;
        fogDensity = 0.35f * s; fogStart = 200.f; fogEnd = 2500.f;
        expMin = 0.05f; expMax = 0.6f; expComp = -0.4f * s;
        break;
    case 2: // Warm
        r = 255; g = (uint8_t)(170 - 50 * s); b = (uint8_t)(110 - 70 * s);
        bright = 1.f + 0.15f * s;
        fogDensity = 0.12f * s; fogStart = 400.f; fogEnd = 3500.f;
        expMin = 0.25f; expMax = 1.8f; expComp = 0.15f * s;
        break;
    case 3: // Cold
        r = (uint8_t)(110 - 40 * s); g = (uint8_t)(150 - 20 * s); b = 255;
        bright = 1.f;
        fogDensity = 0.2f * s; fogStart = 300.f; fogEnd = 3000.f;
        expMin = 0.15f; expMax = 1.4f; expComp = -0.1f * s;
        break;
    case 4: // Dark
        r = g = b = (uint8_t)(255 * (1.f - 0.9f * s));
        bright = 1.f - 0.85f * s;
        fogDensity = 0.5f * s; fogStart = 100.f; fogEnd = 1800.f;
        expMin = 0.02f; expMax = 0.4f; expComp = -0.6f * s;
        break;
    case 5: // Bright
        r = g = b = 255;
        bright = 1.f + 1.8f * s;
        fogDensity = 0.f;
        expMin = 0.5f; expMax = 3.f; expComp = 0.5f * s;
        break;
    default: // Custom = mild neutral boost
        r = g = b = 255;
        bright = 1.f + 0.3f * s;
        break;
    }

    for (int i = 0; i < g_envSkyCount; i++)
        WriteSkyTint(g_envSky[i], r, g, b, a, bright);
    for (int i = 0; i < g_envFogCount; i++)
        WriteFogTint(g_envFog[i], r, g, b, fogDensity, fogStart, fogEnd);
    for (int i = 0; i < g_envPPCount; i++)
        WritePPExposure(g_envPP[i], expMin, expMax, expComp);
}

void DrawEnvironmentGrade_UNUSED(ImDrawList* dl) {
    if (!g_config.envEnabled || !dl) return;
    float a = g_config.envStrength;
    if (a < 0.f) a = 0.f;
    if (a > 0.85f) a = 0.85f;
    int ai = (int)(a * 255.f);
    ImU32 col = IM_COL32(0, 0, 0, 0);
    switch (g_config.envPreset) {
    case 1: col = IM_COL32(10, 20, 50, ai); break;      // Night blue
    case 2: col = IM_COL32(60, 30, 10, ai); break;      // Warm
    case 3: col = IM_COL32(10, 40, 60, ai); break;      // Cold
    case 4: col = IM_COL32(0, 0, 0, ai); break;         // Dark
    case 5: col = IM_COL32(255, 255, 230, (int)(a * 40.f)); break; // Bright wash
    default: return;
    }
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    dl->AddRectFilled(ImVec2(0, 0), ds, col);
}

void DrawWatermark(ImDrawList* dl) {
    if (!g_config.watermark || !dl) return;
    float fps = ImGui::GetIO().Framerate;
    int ok = 0, total = 7;
    if (Pat::g_res.viewMatrix) ok++;
    if (Pat::g_res.gameEntitySystemPtr) ok++;
    if (Pat::g_res.localPlayerControllerPtr) ok++;
    if (Pat::g_res.globalVarsPtr) ok++;
    if (Pat::g_res.drawSmokeArray) ok++;
    if (Pat::g_res.csgoInputPtr) ok++;
    if (Pat::g_res.thirdPersonReset) ok++;
    char buf[128];
    sprintf_s(buf, g_featuresEnabled
        ? "rakhus  |  %.0f fps  |  patterns %d/%d"
        : "rakhus  |  FEATURES OFF (END)  |  %.0f fps  |  %d/%d",
        fps, ok, total);
    ImVec2 ts = ImGui::CalcTextSize(buf);
    float x = 12.f, y = 10.f;
    dl->AddRectFilled(ImVec2(x - 6, y - 4), ImVec2(x + ts.x + 6, y + ts.y + 4), IM_COL32(12, 14, 20, 200), 6.f);
    ImU32 col = (ok >= 5) ? IM_COL32(120, 220, 160, 255) : IM_COL32(255, 160, 80, 255);
    dl->AddText(ImVec2(x, y), col, buf);
}

void DrawSniperCrosshair(ImDrawList* dl, uintptr_t localPawn) {
    if (!g_config.sniperCrosshair || !IsValid(localPawn) || !dl) return;
    uintptr_t wep = GetActiveWeapon(localPawn);
    if (!IsValid(wep)) return;
    uint16_t def = SafeRead<uint16_t>(wep + O::m_AttributeManager + O::m_Item + O::m_iItemDefinitionIndex, 0);
    // AWP, SSG, SCAR, G3
    if (!(def == 9 || def == 40 || def == 38 || def == 11)) return;
    if (SafeRead<uint8_t>(localPawn + O::m_bIsScoped, 0)) return;
    ImVec2 c(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f);
    ImU32 col = IM_COL32(0, 255, 100, 220);
    float s = 4.f;
    dl->AddLine(ImVec2(c.x - s, c.y), ImVec2(c.x + s, c.y), col, 1.2f);
    dl->AddLine(ImVec2(c.x, c.y - s), ImVec2(c.x, c.y + s), col, 1.2f);
}

// Shared planted C4 resolve
static uintptr_t ResolvePlantedC4() {
    if (!hClient) return 0;
    uintptr_t slot = SafeRead<uintptr_t>(hClient + O::dwPlantedC4, 0);
    uintptr_t c4 = 0;
    if (IsValid(slot)) {
        c4 = SafeRead<uintptr_t>(slot, 0);
        if (!IsValid(c4)) c4 = slot;
    }
    return IsValid(c4) ? c4 : 0;
}

void DrawBombWorldEsp(ImDrawList* dl, int sw, int sh) {
    if (!g_config.bombEsp || !dl) return;
    uintptr_t c4 = ResolvePlantedC4();
    if (!IsValid(c4)) return;
    uint8_t ticking = SafeRead<uint8_t>(c4 + O::C4::m_bBombTicking, 0);
    if (!ticking) return;
    Vector3 o = GetOrigin(c4);
    if (!OriginSane(o)) return;
    Vector2 s;
    if (!WorldToScreen(o, s, sw, sh)) return;
    float blow = SafeRead<float>(c4 + O::C4::m_flC4Blow, 0.f);
    float cur = GetCurTime();
    float remain = (blow > 1.f && cur > 1.f) ? (blow - cur) : -1.f;
    char buf[48];
    if (remain >= 0.f && remain < 60.f) sprintf_s(buf, "C4  %.1fs", remain);
    else sprintf_s(buf, "C4");
    ImU32 col = remain >= 0.f && remain < 10.f ? IM_COL32(255, 60, 60, 255) : IM_COL32(255, 200, 60, 255);
    ImVec2 ts = ImGui::CalcTextSize(buf);
    dl->AddRectFilled(ImVec2(s.x - ts.x * 0.5f - 6, s.y - 18), ImVec2(s.x + ts.x * 0.5f + 6, s.y + 4), IM_COL32(0, 0, 0, 160), 4.f);
    dl->AddText(ImVec2(s.x - ts.x * 0.5f, s.y - 16), col, buf);
    dl->AddCircle(ImVec2(s.x, s.y), 5.f, col, 12, 1.5f);
}

void DrawNadePrediction(ImDrawList* dl, uintptr_t localPawn, int sw, int sh) {
    if (!g_config.nadePred || !IsValid(localPawn) || !dl) return;
    uintptr_t wep = GetActiveWeapon(localPawn);
    if (!IsValid(wep)) return;
    uint16_t def = SafeRead<uint16_t>(wep + O::m_AttributeManager + O::m_Item + O::m_iItemDefinitionIndex, 0);
    // HE 44, flash 43, smoke 45, molly 46/48, decoy 47
    bool isNade = (def == 43 || def == 44 || def == 45 || def == 46 || def == 47 || def == 48);
    if (!isNade) return;

    Vector3 eye = GetOrigin(localPawn);
    Vector3 vo = GetViewOffset(localPawn);
    eye.x += vo.x; eye.y += vo.y; eye.z += vo.z;
    uintptr_t va = hClient + O::dwViewAngles;
    Vector3 ang = SafeRead<Vector3>(va, {});
    float pitch = ang.x * (3.14159265f / 180.f);
    float yaw = ang.y * (3.14159265f / 180.f);
    Vector3 fwd{
        cosf(pitch) * cosf(yaw),
        cosf(pitch) * sinf(yaw),
        -sinf(pitch)
    };
    // Approximate throw velocity
    float speed = 750.f;
    Vector3 vel{ fwd.x * speed, fwd.y * speed, fwd.z * speed + 200.f };
    Vector3 pos = eye;
    pos.x += fwd.x * 16.f; pos.y += fwd.y * 16.f; pos.z += fwd.z * 16.f;
    float dt = 0.05f;
    Vector2 prev{};
    bool hasPrev = false;
    int steps = g_config.nadePredSteps;
    if (steps < 10) steps = 10;
    if (steps > 80) steps = 80;
    for (int i = 0; i < steps; i++) {
        pos.x += vel.x * dt;
        pos.y += vel.y * dt;
        pos.z += vel.z * dt;
        vel.z -= 800.f * dt; // gravity approx
        Vector2 scr;
        if (!WorldToScreen(pos, scr, sw, sh)) { hasPrev = false; continue; }
        if (hasPrev)
            dl->AddLine(ImVec2(prev.x, prev.y), ImVec2(scr.x, scr.y), IM_COL32(100, 200, 255, 180), 1.5f);
        prev = scr; hasPrev = true;
        if (pos.z < eye.z - 300.f) break;
    }
}

// OverrideView hook — FOV + third person camera
typedef void(__fastcall* OverrideViewFn)(void*, void*);
static OverrideViewFn oOverrideView = nullptr;
static bool g_ovHooked = false;

// CViewSetup-ish offsets (community layout; may drift by build)

void __fastcall hkOverrideView(void* a1, void* setup) {
    if (oOverrideView) oOverrideView(a1, setup);
    if (!setup || !g_running || !g_featuresEnabled || g_unloadRequested) return;
    if (!g_config.fovChanger) return;
    // Single common FOV slot only (multi-offset writes caused camera glitches)
    __try {
        float* fov = (float*)((uintptr_t)setup + 0x4B8);
        float cur = *fov;
        if (cur >= 60.f && cur <= 130.f)
            *fov = g_config.fovValue;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}


void DrawFrame() {
    if (!g_running) return;

    // NOTE: no C++ try/catch here — MSVC forbids mixing with __try in callees/helpers
    if (!g_imGuiInitialized || !hClient) return;
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    // Pattern-resolved globals first (from pattern_scan.h), offsets as fallback
    if (Pat::g_res.gameEntitySystemPtr)
        g_pES = Pat::ReadPtr(Pat::g_res.gameEntitySystemPtr);
    else
        g_pES = SafeRead<uintptr_t>(hClient + O::dwGameEntitySystem, 0);

    // View matrix: prefer dumper RVA, fall back to pattern
    if (Pat::g_res.viewMatrix)
        SafeMemcpy(viewMatrix, (void*)Pat::g_res.viewMatrix, sizeof(viewMatrix));
    else
        SafeMemcpy(viewMatrix, (void*)(hClient + O::dwViewMatrix), sizeof(viewMatrix));
    if (viewMatrix[0] == 0.f && viewMatrix[5] == 0.f && viewMatrix[10] == 0.f)
        SafeMemcpy(viewMatrix, (void*)(hClient + O::dwViewMatrix), sizeof(viewMatrix));

    int sw = (int)ImGui::GetIO().DisplaySize.x;
    int sh = (int)ImGui::GetIO().DisplaySize.y;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    RefreshEntityCache();

    // Not in a match → only menu, no feature memory access (prevents leave-crash)
    uintptr_t pLocal = 0;
    if (IsInGame())
        pLocal = SafeRead<uintptr_t>(hClient + O::dwLocalPlayerPawn, 0);

    // END toggles features; menu/watermark still work
    if (g_featuresEnabled && IsValid(pLocal) && IsAlive(pLocal) && IsInGame()) {
        int localTeam = Team(pLocal);
        DoNoFlash(pLocal);
        DoNoSmoke(pLocal);
        DoThirdPerson(pLocal);
        UpdateSmokeNearby(pLocal);
        DoEnvironmentSky();
        // Early punch path (CreateMove-style ordering before ESP)
        if (g_config.earlyPunchPath) {
            DoNoVisualRecoil(pLocal);
            DoSoftRCS(pLocal);
        } else {
            DoSoftRCS(pLocal);
            DoNoVisualRecoil(pLocal);
        }
        DoTriggerbot(pLocal, localTeam);
        UpdateHitmarker(pLocal, localTeam);
        DoGlow(pLocal, localTeam);

        if (g_config.espEnabled && g_cacheCount > 0) {
            // Use entity cache (no second 1..64 entity walk — major lag fix)
            static int s_espFrame = 0;
            s_espFrame++;
            bool doSkeletonThisFrame = !g_config.espSkeletonEveryOther || (s_espFrame & 1);

            Vector3 eye = GetOrigin(pLocal);
            eye.z += GetViewOffset(pLocal).z;

            for (int ci = 0; ci < g_cacheCount; ci++) {
                auto& c = g_cache[ci];
                uintptr_t pawn = c.pawn;
                uintptr_t ctrl = c.ctrl;
                if (!IsValid(pawn) || pawn == pLocal || !c.alive) continue;
                if (g_config.espTeamCheck && c.team == localTeam) continue;

                Vector3 feet = GetOrigin(pawn);
                if (!OriginSane(feet)) continue;

                bool vis = IsSpotted(pawn);
                if (g_config.espVisibleOnly && !vis) continue;

                // Cheap distance from feet first (skip bone if too far)
                float dx = feet.x - eye.x, dy = feet.y - eye.y, dz = feet.z - eye.z;
                float distM = sqrtf(dx * dx + dy * dy + dz * dz) * 0.01905f;
                if (distM > g_config.espMaxDistance) continue;

                feet.z += 4.f;
                Vector3 head;
                if (!GetBonePos(pawn, O::Bone::head, head)) continue;
                head.z += 2.f;
                if (!OriginSane(head)) continue;

                Vector2 sf, shs;
                if (!WorldToScreen(feet, sf, sw, sh, true) || !WorldToScreen(head, shs, sw, sh, true)) continue;

                // Off-screen cull with small margin
                if (shs.x < -80 || shs.x > sw + 80 || sf.y < -80 || shs.y > sh + 80) continue;

                float h = sf.y - shs.y;
                if (h < 6.f || h > sh * 1.5f) continue;
                float w = h * 0.42f;
                float x = shs.x - w * 0.5f, y = shs.y;

                ImU32 col = vis
                    ? IM_COL32((int)(g_config.espVisColorR * 255), (int)(g_config.espVisColorG * 255), (int)(g_config.espVisColorB * 255), 210)
                    : IM_COL32((int)(g_config.espColorR * 255), (int)(g_config.espColorG * 255), (int)(g_config.espColorB * 255), 200);

                if (g_config.espBox) {
                    if (g_config.espBoxOutline)
                        dl->AddRect(ImVec2(x - 1, y - 1), ImVec2(x + w + 1, y + h + 1), IM_COL32(0, 0, 0, 140), 0.f, 0, 1.f);
                    dl->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), col, 0.f, 0, g_config.espBoxThickness);
                }
                if (g_config.espHeadDot) {
                    // reuse head screen pos — no second GetBonePos/W2S
                    dl->AddCircleFilled(ImVec2(shs.x, shs.y), 2.2f, col);
                }
                if (g_config.espHealth) {
                    int hp = (std::clamp)(c.hp > 0 ? c.hp : HP(pawn), 0, 100);
                    float bh = h * (hp / 100.f);
                    ImU32 hc = hp > 60 ? IM_COL32(50, 210, 90, 255) : hp > 30 ? IM_COL32(230, 190, 40, 255) : IM_COL32(230, 55, 55, 255);
                    dl->AddRectFilled(ImVec2(x - 5, y + h - bh), ImVec2(x - 2, y + h), hc);
                    dl->AddRect(ImVec2(x - 5, y), ImVec2(x - 2, y + h), IM_COL32(0, 0, 0, 160));
                }
                if (g_config.espArmor) {
                    int ar = Armor(pawn);
                    if (ar > 0) {
                        float bh = h * ((std::clamp)(ar, 0, 100) / 100.f);
                        dl->AddRectFilled(ImVec2(x + w + 2, y + h - bh), ImVec2(x + w + 5, y + h), IM_COL32(70, 140, 255, 230));
                    }
                }
                float ty = y - 13.f;
                UpdatePlayerTextCache(c);
                if (g_config.espName && c.name[0]) {
                    ImVec2 ts = ImGui::CalcTextSize(c.name);
                    dl->AddText(ImVec2(shs.x - ts.x * 0.5f, ty), IM_COL32(240, 240, 245, 235), c.name);
                    ty -= 13.f;
                }
                if (g_config.espWeapon && c.weapon[0]) {
                    ImVec2 ts = ImGui::CalcTextSize(c.weapon);
                    dl->AddText(ImVec2(shs.x - ts.x * 0.5f, y + h + 2), IM_COL32(190, 190, 200, 210), c.weapon);
                }
                if (g_config.espDistance) {
                    char dt[16]; sprintf_s(dt, "%.0fm", distM);
                    dl->AddText(ImVec2(x, y + h + (g_config.espWeapon ? 15.f : 2.f)), IM_COL32(170, 170, 180, 190), dt);
                }
                // Skeleton: optional every-other-frame + distance gate
                if (g_config.espSkeleton && doSkeletonThisFrame && distM < 45.f)
                    DrawSkeleton(dl, pawn, sw, sh, col);
            }
        }

        DrawSpectatorList(pLocal);
        DrawHitmarker(dl);
        DrawFovCircle(dl);
        DrawCrosshair(dl);
        DrawBombTimer(dl);
        DrawBombWorldEsp(dl, sw, sh);
        DrawWatermark(dl);
        DrawSniperCrosshair(dl, pLocal);
        DrawNadePrediction(dl, pLocal, sw, sh);
        DrawHitlog(dl);
        DrawSoundEsp(dl, pLocal, localTeam, sw, sh);
        DoLegitAim(pLocal, localTeam, sw, sh);
    }
    else if (IsValid(pLocal)) {
        DrawSpectatorList(pLocal);
    }

    // Always: status watermark + menu (works while features OFF)
    if (!g_featuresEnabled && dl)
        DrawWatermark(dl);
    DrawMenu();

    ImGui::EndFrame();
    ImGui::Render();
    if (g_mainRenderTargetView && g_pd3dDeviceContext) {
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }
}

// -------------------- HOOKS --------------------
typedef HRESULT(__stdcall* Present)(IDXGISwapChain*, UINT, UINT);
static Present oPresent = nullptr;

HRESULT __stdcall hkPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags) {
    if (!g_running || g_unloadRequested)
        return oPresent ? oPresent(pSwapChain, SyncInterval, Flags) : S_OK;
    InterlockedIncrement(&g_presentBusy);
    try {
        if (!g_imGuiInitialized) {
            if (SUCCEEDED(pSwapChain->GetDevice(__uuidof(ID3D11Device), (void**)&g_pd3dDevice))) {
                g_pd3dDevice->GetImmediateContext(&g_pd3dDeviceContext);
                DXGI_SWAP_CHAIN_DESC desc; pSwapChain->GetDesc(&desc);
                g_gameHwnd = desc.OutputWindow;
                ID3D11Texture2D* bb = nullptr;
                pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb);
                if (bb) { g_pd3dDevice->CreateRenderTargetView(bb, nullptr, &g_mainRenderTargetView); bb->Release(); }
                ImGui::CreateContext();
                ImGuiIO& io = ImGui::GetIO();
                io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
                io.IniFilename = nullptr; // no imgui.ini spam

                // Premium fonts – try modern Windows fonts first
                ImFontConfig cfg;
                cfg.OversampleH = 3;
                cfg.OversampleV = 2;
                cfg.PixelSnapH = true;
                cfg.RasterizerMultiply = 1.1f;

                // Primary UI font (Segoe UI Semibold looks clean & modern)
                if (!io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\seguisb.ttf", 16.0f, &cfg)) {
                    if (!io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 16.0f, &cfg)) {
                        io.Fonts->AddFontDefault(); // fallback
                    }
                }
                // Slightly larger for titles / headers if needed later
                io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\seguisb.ttf", 18.5f, &cfg);

                SetupImGuiStyle();
                ImGui_ImplWin32_Init(g_gameHwnd);
                if (ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext)) {
                    g_imGuiInitialized = true;
                    g_OriginalWndProc = (WNDPROC)SetWindowLongPtr(g_gameHwnd, GWLP_WNDPROC, (LONG_PTR)HookedWndProc);
                    LoadConfig();
                    LOG("[+] rakhus-legit UI ready");
                }
            }
        }
        if (g_imGuiInitialized && !g_mainRenderTargetView) {
            ID3D11Texture2D* bb = nullptr;
            pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb);
            if (bb) { g_pd3dDevice->CreateRenderTargetView(bb, nullptr, &g_mainRenderTargetView); bb->Release(); }
        }
        if (g_imGuiInitialized && g_mainRenderTargetView) DrawFrame();
    }
    catch (...) {}
    InterlockedDecrement(&g_presentBusy);
    return oPresent(pSwapChain, SyncInterval, Flags);
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK HookedWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (g_imGuiInitialized) ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
    return CallWindowProc(g_OriginalWndProc, hWnd, msg, wParam, lParam);
}

DWORD WINAPI MainLoop(LPVOID) {
    srand((unsigned)time(nullptr));
    LOG("[*] rakhus-legit main");
    for (int i = 0; i < 80; i++) {
        Sleep(100);
        if (HMODULE h = GetModuleHandleA("client.dll")) {
            hClient = (uintptr_t)h;
            LOG_FMT("[+] client.dll 0x%llX\n", (unsigned long long)hClient);
            break;
        }
    }
    if (!hClient) { LOG("[-] no client.dll"); return 0; }

    // Pattern scan (SDK patterns) — prefer over static RVAs
    if (Pat::ResolveAll()) {
        g_patternsOk = true;
        LOG("[+] patterns resolved");
        LOG_FMT("    viewMatrix        @ 0x%llX\n", (unsigned long long)Pat::g_res.viewMatrix);
        LOG_FMT("    gameEntitySystem  @ 0x%llX\n", (unsigned long long)Pat::g_res.gameEntitySystemPtr);
        LOG_FMT("    localController   @ 0x%llX\n", (unsigned long long)Pat::g_res.localPlayerControllerPtr);
        LOG_FMT("    globalVars        @ 0x%llX\n", (unsigned long long)Pat::g_res.globalVarsPtr);
        LOG_FMT("    DrawSmokeArray    @ 0x%llX\n", (unsigned long long)Pat::g_res.drawSmokeArray);
        LOG_FMT("    ApplyEconCustom   @ 0x%llX\n", (unsigned long long)Pat::g_res.applyEconCustomization);
        LOG_FMT("    ThirdPersonReset  @ 0x%llX\n", (unsigned long long)Pat::g_res.thirdPersonReset);
        LOG_FMT("    CSGOInput         @ 0x%llX\n", (unsigned long long)Pat::g_res.csgoInputPtr);
    }
    else {
        LOG("[-] pattern resolve incomplete — using static offsets");
    }

    // OverrideView — FOV changer (single offset, default off)
    if (Pat::g_res.overrideView) {
        MH_STATUS mh = MH_Initialize();
        if (mh == MH_OK || mh == MH_ERROR_ALREADY_INITIALIZED) {
            if (MH_CreateHook((LPVOID)Pat::g_res.overrideView, (LPVOID)&hkOverrideView, (LPVOID*)&oOverrideView) == MH_OK
                && MH_EnableHook((LPVOID)Pat::g_res.overrideView) == MH_OK) {
                g_ovHooked = true;
                LOG("[+] OverrideView hooked (FOV)");
            } else LOG("[-] OverrideView hook failed");
        }
    } else LOG("[-] OverrideView pattern miss");

    // Hook DrawSmokeArray for crash-free NoSmoke
    if (Pat::g_res.drawSmokeArray) {
        MH_STATUS mhInit = MH_Initialize();
        if (mhInit == MH_OK || mhInit == MH_ERROR_ALREADY_INITIALIZED) {
            if (MH_CreateHook((LPVOID)Pat::g_res.drawSmokeArray, (LPVOID)&hkSmokeDrawArray, (LPVOID*)&oSmokeDrawArray) == MH_OK) {
                if (MH_EnableHook((LPVOID)Pat::g_res.drawSmokeArray) == MH_OK)
                    LOG("[+] DrawSmokeArray hooked (NoSmoke)");
                else
                    LOG("[-] DrawSmokeArray enable failed");
            }
            else {
                LOG("[-] DrawSmokeArray create hook failed");
            }
        }
    }
    else {
        LOG("[-] DrawSmokeArray pattern not found — NoSmoke uses memory fallback");
    }

    bool ok = false;
    do {
        if (kiero::init(kiero::RenderType::D3D11) == kiero::Status::Success)
            if (kiero::bind(8, (void**)&oPresent, hkPresent) == kiero::Status::Success) {
                ok = true; LOG("[+] Present hooked");
            }
        Sleep(100);
    } while (!ok);

    bool lastIns = false;
    bool lastEnd = false;
    while (g_running) {
        bool ins = (GetAsyncKeyState(VK_INSERT) & 0x8000) != 0;
        if (ins && !lastIns) g_config.showMenu = !g_config.showMenu;
        lastIns = ins;

        // END = toggle features only — DLL stays loaded, hooks stay, no FreeLibrary
        bool endKey = (GetAsyncKeyState(VK_END) & 0x8000) != 0;
        if (endKey && !lastEnd) {
            g_featuresEnabled = !g_featuresEnabled;
            if (!g_featuresEnabled) {
                SetThirdPersonResetPatch(false);
                LOG("[*] END — features OFF (DLL still loaded). Press END again to re-enable.");
            } else {
                LOG("[*] END — features ON");
            }
        }
        lastEnd = endKey;
        Sleep(16);
    }
}


// Unhook everything — call only when DLL is really leaving (DETACH), after Present is idle
static void SafeUnhookAll() {
    g_featuresEnabled = false;
    g_unloadRequested = true;
    g_running = false;

    for (int i = 0; i < 150 && InterlockedCompareExchange(&g_presentBusy, 0, 0) != 0; i++)
        Sleep(10);

    SetThirdPersonResetPatch(false);

    if (g_gameHwnd && g_OriginalWndProc) {
        SetWindowLongPtr(g_gameHwnd, GWLP_WNDPROC, (LONG_PTR)g_OriginalWndProc);
        g_OriginalWndProc = nullptr;
    }

    if (g_ovHooked && Pat::g_res.overrideView) {
        MH_DisableHook((LPVOID)Pat::g_res.overrideView);
        MH_RemoveHook((LPVOID)Pat::g_res.overrideView);
        g_ovHooked = false;
    }
    if (Pat::g_res.drawSmokeArray) {
        MH_DisableHook((LPVOID)Pat::g_res.drawSmokeArray);
        MH_RemoveHook((LPVOID)Pat::g_res.drawSmokeArray);
    }

    // Present unbind — kiero shutdown removes index 8 hook
    __try {
        kiero::shutdown();
    } __except (EXCEPTION_EXECUTE_HANDLER) {}

    oPresent = nullptr;

    if (g_imGuiInitialized) {
        __try {
            ImGui_ImplDX11_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
        g_imGuiInitialized = false;
    }
    // Do NOT Release game device/swapchain — game owns them
    g_mainRenderTargetView = nullptr;
    g_pd3dDeviceContext = nullptr;
    g_pd3dDevice = nullptr;

    FreeConsole();
}


BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_hModule = hModule;
        DisableThreadLibraryCalls(hModule);
        InitConsole();
        LOG("[+] rakhus-legit loaded");
        CreateThread(NULL, 0, MainLoop, NULL, 0, NULL);
    }
    else if (reason == DLL_PROCESS_DETACH) {
        // Real unload (injector FreeLibrary) — remove hooks safely
        SafeUnhookAll();
    }
    return TRUE;
}
