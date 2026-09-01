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
#define AIM_KEY_DEFAULT VK_LBUTTON
#define TRIGGER_KEY_DEFAULT VK_XBUTTON2

struct Config {
    // Aimbot (legit)
    bool aimEnabled = true;
    float aimFov = 28.0f;
    float aimSmooth = 0.78f;       // higher = slower / more human
    int aimBone = 0;               // 0 head 1 neck 2 chest
    int aimKey = AIM_KEY_DEFAULT;
    bool aimTeamCheck = true;
    bool aimVisibleOnly = true;
    bool aimOnlyWhenScoped = false;
    float aimHumanize = 0.35f;     // micro randomness 0-1
    bool aimDrawFov = true;

    // Soft RCS
    bool rcsEnabled = true;
    float rcsStrength = 0.55f;     // 0-1 partial compensation
    int rcsStartBullet = 2;        // start after N shots

    // Triggerbot
    bool triggerEnabled = false;
    int triggerKey = TRIGGER_KEY_DEFAULT;
    int triggerDelayMin = 45;   // ms between shots (min)
    int triggerDelayMax = 95;   // ms between shots (max, randomized)
    bool triggerTeamCheck = true;
    bool triggerVisibleOnly = false;
    int  triggerBone = 0;            // 0 head, 1 neck, 2 chest — only fire on this bone
    bool triggerRcs = true;          // anti-recoil while trigger fires
    float triggerRcsStrength = 1.0f; // 0–1 full punch compensation
    float triggerBoneFov = 12.f;     // max pixels: bone must be this close to crosshair

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
    float espMaxDistance = 80.f;   // meters

    // Misc legit
    bool noFlash = true;
    bool noSmoke = false;          // often considered less legit
    bool noVisualRecoil = false;
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


    // Third person (hold key)
    bool thirdPerson = false;      // off by default (safer)
    int  thirdPersonKey = 0x05;  // VK_XBUTTON1 default (Mouse 4)
    float thirdPersonDist = 120.f;

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
    w("noFlash", g_config.noFlash ? 1 : 0);
    w("noSmoke", g_config.noSmoke ? 1 : 0);
    w("noVisualRecoil", g_config.noVisualRecoil ? 1 : 0);
    w("spectatorList", g_config.spectatorList ? 1 : 0);
    w("hitmarker", g_config.hitmarker ? 1 : 0);
    w("bombTimer", g_config.bombTimer ? 1 : 0);
    w("thirdPerson", g_config.thirdPerson ? 1 : 0);
    w("thirdPersonKey", g_config.thirdPersonKey);
    w("thirdPersonDist", g_config.thirdPersonDist);
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
            else if (k == "noFlash") g_config.noFlash = std::stoi(v) != 0;
            else if (k == "noSmoke") g_config.noSmoke = std::stoi(v) != 0;
            else if (k == "noVisualRecoil") g_config.noVisualRecoil = std::stoi(v) != 0;
            else if (k == "spectatorList") g_config.spectatorList = std::stoi(v) != 0;
            else if (k == "hitmarker") g_config.hitmarker = std::stoi(v) != 0;
            else if (k == "bombTimer") g_config.bombTimer = std::stoi(v) != 0;
            else if (k == "thirdPerson") g_config.thirdPerson = std::stoi(v) != 0;
            else if (k == "thirdPersonKey") g_config.thirdPersonKey = std::stoi(v);
            else if (k == "thirdPersonDist") g_config.thirdPersonDist = std::stof(v);
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

void RefreshEntityCache() {
    g_cacheTick++;

    if (!IsInGame()) {
        g_cacheCount = 0;
        g_localCtrlIndex = -1;
        memset(g_cache, 0, sizeof(g_cache));
        return;
    }

    // Every-frame refresh (visibility index must not lag)
    uintptr_t localCtrl = SafeRead<uintptr_t>(hClient + O::dwLocalPlayerController, 0);
    uintptr_t localPawn = SafeRead<uintptr_t>(hClient + O::dwLocalPlayerPawn, 0);

    // Prefer handle-based slot (matches SpottedByMask bit indexing)
    g_localCtrlIndex = ResolveLocalControllerIndex(localPawn);
    if (g_localCtrlIndex < 1 && IsValid(localCtrl)) {
        for (int i = 1; i <= 64; i++) {
            if (GetEntity(i) == localCtrl) { g_localCtrlIndex = i; break; }
        }
    }

    int n = 0;
    for (int i = 1; i <= 64 && n < 64; i++) {
        uintptr_t ctrl = GetEntity(i);
        if (!IsValid(ctrl)) continue;
        if (localCtrl && ctrl == localCtrl && g_localCtrlIndex < 1)
            g_localCtrlIndex = i;

        // Drop disconnected controllers (ghost ESP source)
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
bool WorldToScreen(const Vector3& world, Vector2& screen, int sw, int sh) {
    __try {
        // Row-major view-projection (cs2-dumper dwViewMatrix)
        float cx = viewMatrix[0] * world.x + viewMatrix[1] * world.y + viewMatrix[2] * world.z + viewMatrix[3];
        float cy = viewMatrix[4] * world.x + viewMatrix[5] * world.y + viewMatrix[6] * world.z + viewMatrix[7];
        float cw = viewMatrix[12] * world.x + viewMatrix[13] * world.y + viewMatrix[14] * world.z + viewMatrix[15];
        if (cw < 0.001f) return false;
        float inv = 1.f / cw;
        screen.x = (sw * 0.5f) + (cx * inv) * (sw * 0.5f);
        screen.y = (sh * 0.5f) - (cy * inv) * (sh * 0.5f);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
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
    // Always restore patch if feature off / dead
    if (!g_config.thirdPerson || !IsValid(localPawn) || !IsAlive(localPawn) || !hClient) {
        SetThirdPersonResetPatch(false);
        return;
    }

    bool hold = (g_config.thirdPersonKey != 0) && IsKeyDown(g_config.thirdPersonKey);

    // Code patch only when holding (and only if safe JE found)
    SetThirdPersonResetPatch(hold);

    // Single CSGOInput flag write — no shotgun offsets, no float sprays
    uintptr_t input = 0;
    if (Pat::g_res.csgoInputPtr)
        input = Pat::ReadPtr(Pat::g_res.csgoInputPtr);
    if (!IsValid(input)) return;

    // Only the commonly reported thirdperson bool; SEH guarded
    __try {
        SafeWrite<uint8_t>(input + 0x229, hold ? 1 : 0);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
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
    uintptr_t punch = SafeRead<uintptr_t>(p + O::m_pAimPunchServices, 0);
    if (!IsValid(punch)) return;

    // Zero QAngle fields as raw floats (avoid struct layout surprises)
    // NEVER write viewangles here — that causes camera glitches with aim/RCS.
    auto zero_qangle = [&](std::ptrdiff_t off) {
        SafeWrite<float>(punch + off + 0, 0.f);
        SafeWrite<float>(punch + off + 4, 0.f);
        SafeWrite<float>(punch + off + 8, 0.f);
        };
    zero_qangle(O::AimPunch::m_predictableBaseAngle);     // 0x50
    zero_qangle(O::AimPunch::m_predictableBaseAngleVel);  // 0x5C
    zero_qangle(O::AimPunch::m_unpredictableBaseAngle);   // 0xA4

    // Clear soft-RCS delta cache so toggling NVR/RCS does not snap
    g_rcsPunchX = 0.f;
    g_rcsPunchY = 0.f;
}

// Soft RCS – compensates a fraction of aim punch (legit style)
// Disabled automatically while NoVisualRecoil is active (avoids camera glitch)
void DoSoftRCS(uintptr_t localPawn) {
    // NVR owns punch fully — soft RCS must not touch angles or punch cache
    if (g_config.noVisualRecoil) {
        g_rcsPunchX = 0.f;
        g_rcsPunchY = 0.f;
        return;
    }
    if (!g_config.rcsEnabled || !IsValid(localPawn) || !IsAlive(localPawn)) {
        g_rcsPunchX = g_rcsPunchY = 0.f;
        return;
    }
    int shots = SafeRead<int>(localPawn + O::m_iShotsFired, 0);
    if (shots < g_config.rcsStartBullet) {
        g_rcsPunchX = g_rcsPunchY = 0.f;
        return;
    }
    uintptr_t punchSvc = SafeRead<uintptr_t>(localPawn + O::m_pAimPunchServices, 0);
    if (!IsValid(punchSvc)) return;
    Vector3 punch = SafeRead<Vector3>(punchSvc + O::AimPunch::m_predictableBaseAngle, {});
    uintptr_t va = hClient + O::dwViewAngles;
    if (!IsValid(va)) return;

    // delta from previous punch * strength
    float dx = (punch.x - g_rcsPunchX) * g_config.rcsStrength;
    float dy = (punch.y - g_rcsPunchY) * g_config.rcsStrength;
    g_rcsPunchX = punch.x;
    g_rcsPunchY = punch.y;

    __try {
        Vector3 cur = *(Vector3*)va;
        cur.x -= dx;
        cur.y -= dy;
        if (cur.x > 89.f) cur.x = 89.f;
        if (cur.x < -89.f) cur.x = -89.f;
        *(Vector3*)va = cur;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// Trigger bone index (same mapping as aimbot)
static int TriggerBoneIndex() {
    switch (g_config.triggerBone) {
    case 1: return O::Bone::neck;
    case 2: return O::Bone::spine;
    default: return O::Bone::head;
    }
}

// Apply punch compensation so the bullet path hits bonePos (CS2: visual punch ≈ *2)
static void TriggerApplyRcsToBone(uintptr_t localPawn, const Vector3& bonePos) {
    if (!g_config.triggerRcs || !hClient) return;
    if (g_config.noVisualRecoil) return; // NVR already zeros punch

    Vector3 eye = GetOrigin(localPawn);
    Vector3 vo = GetViewOffset(localPawn);
    eye.x += vo.x; eye.y += vo.y; eye.z += vo.z;
    if (!OriginSane(eye) || !OriginSane(bonePos)) return;

    Vector3 punch{};
    uintptr_t punchSvc = SafeRead<uintptr_t>(localPawn + O::m_pAimPunchServices, 0);
    if (IsValid(punchSvc))
        punch = SafeRead<Vector3>(punchSvc + O::AimPunch::m_predictableBaseAngle, {});

    // Angle to bone
    Vector3 delta{ bonePos.x - eye.x, bonePos.y - eye.y, bonePos.z - eye.z };
    float hyp = sqrtf(delta.x * delta.x + delta.y * delta.y);
    if (hyp < 0.001f) return;
    Vector3 targetAng;
    targetAng.x = -atan2f(delta.z, hyp) * (180.f / 3.14159265f);
    targetAng.y = atan2f(delta.y, delta.x) * (180.f / 3.14159265f);
    targetAng.z = 0.f;

    // Bullet travels along view + punch*2 → aim at bone - punch*2 * strength
    float s = (std::clamp)(g_config.triggerRcsStrength, 0.f, 1.f);
    targetAng.x -= punch.x * 2.f * s;
    targetAng.y -= punch.y * 2.f * s;

    if (targetAng.x > 89.f) targetAng.x = 89.f;
    if (targetAng.x < -89.f) targetAng.x = -89.f;
    while (targetAng.y > 180.f) targetAng.y -= 360.f;
    while (targetAng.y < -180.f) targetAng.y += 360.f;

    uintptr_t va = hClient + O::dwViewAngles;
    if (!IsValid(va)) return;
    __try {
        if (!std::isnan(targetAng.x) && !std::isnan(targetAng.y))
            *(Vector3*)va = targetAng;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
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
        if (g_config.triggerRcs && IsValid(holdTarget) && IsAlive(holdTarget) && IsValid(localPawn)) {
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

    float flash = SafeRead<float>(localPawn + O::m_flFlashDuration, 0.f);
    if (flash > 0.4f) return;
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
    nextDelayMs = delay;
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
    if (!g_config.aimEnabled || !IsInGame()) return;
    if (!IsValid(localPawn) || !IsAlive(localPawn)) return;
    if (!IsKeyDown(g_config.aimKey)) { g_hasTarget = false; return; }
    if (g_config.aimOnlyWhenScoped && !SafeRead<uint8_t>(localPawn + O::m_bIsScoped, 0)) return;

    uintptr_t va = hClient + O::dwViewAngles;
    if (!IsValid(va)) return;

    Vector3 eye = GetOrigin(localPawn);
    if (!OriginSane(eye)) return;
    Vector3 vo = GetViewOffset(localPawn);
    eye.x += vo.x; eye.y += vo.y; eye.z += vo.z;

    float bestFov = g_config.aimFov;
    Vector3 best{};
    bool found = false;
    // Bone priority order when enabled: preferred bone first, then head->neck->chest
    int bonesTry[3];
    int boneCount = 1;
    bonesTry[0] = AimBoneIndex();
    if (g_config.aimBonePriority) {
        bonesTry[0] = O::Bone::head;
        bonesTry[1] = O::Bone::neck;
        bonesTry[2] = O::Bone::spine;
        boneCount = 3;
    }

    auto consider = [&](uintptr_t pawn) {
        for (int bi = 0; bi < boneCount; bi++) {
            Vector3 bp;
            if (!GetBonePos(pawn, bonesTry[bi], bp)) continue;
            if (g_config.aimHumanize > 0.01f) {
                float h = g_config.aimHumanize * 2.5f;
                bp.x += RandF(-h, h); bp.y += RandF(-h, h); bp.z += RandF(-h * 0.5f, h * 0.5f);
            }
            Vector2 scr;
            if (!WorldToScreen(bp, scr, sw, sh)) continue;
            float dx = scr.x - sw * 0.5f, dy = scr.y - sh * 0.5f;
            float fov = sqrtf(dx * dx + dy * dy);
            if (fov >= bestFov) continue;
            Vector3 ang = CalcAngles(eye, bp);
            NormAngles(ang);
            bestFov = fov; best = ang; found = true;
            return; // first bone in priority that is inside FOV wins
        }
        };

    if (g_cacheCount > 0) {
        for (int i = 0; i < g_cacheCount; i++) {
            auto& c = g_cache[i];
            if (!c.alive || c.pawn == localPawn) continue;
            if (g_config.aimTeamCheck && c.team == localTeam) continue;
            if (g_config.aimVisibleOnly && !IsSpotted(c.pawn)) continue;
            consider(c.pawn);
        }
    }
    else {
        for (int i = 1; i <= 64; i++) {
            uintptr_t ctrl = GetEntity(i);
            if (!ctrl) continue;
            uintptr_t pawn = HandleToEnt(SafeRead<uint32_t>(ctrl + O::m_hPlayerPawn, 0));
            if (!IsValid(pawn) || pawn == localPawn || !IsAlive(pawn)) continue;
            if (g_config.aimTeamCheck && Team(pawn) == localTeam) continue;
            if (g_config.aimVisibleOnly && !IsSpotted(pawn)) continue;
            consider(pawn);
        }
    }

    if (!found) { g_hasTarget = false; return; }
    g_hasTarget = true;
    g_targetAngles = best;

    __try {
        Vector3 cur = SafeRead<Vector3>(va, {});
        float dp = best.x - cur.x;
        float dy = best.y - cur.y;
        while (dy > 180.f) dy -= 360.f;
        while (dy < -180.f) dy += 360.f;

        // smooth: high value = slower movement (legit)
        float factor = 1.f - (std::max)(0.05f, (std::min)(0.98f, g_config.aimSmooth));
        // extra humanize on delta
        if (g_config.aimHumanize > 0.01f) {
            factor *= RandF(0.85f, 1.05f);
        }

        Vector3 n;
        n.x = cur.x + dp * factor;
        n.y = cur.y + dy * factor;
        n.z = 0.f;
        NormAngles(n);
        if (!std::isnan(n.x) && !std::isnan(n.y))
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
        ImGui::Checkbox("Enable", &g_config.aimEnabled);
        ImGui::SliderFloat("FOV", &g_config.aimFov, 5.f, 120.f, "%.0f px");
        ImGui::SliderFloat("Smooth", &g_config.aimSmooth, 0.15f, 0.95f, "%.2f");
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
            if (ImGui::Button("M4")) { g_config.aimKey = VK_XBUTTON1; SaveConfig(); }
        }

        SectionHeader("SOFT RCS");
        ImGui::Checkbox("Enable RCS", &g_config.rcsEnabled);
        ImGui::SliderFloat("Strength", &g_config.rcsStrength, 0.1f, 1.f, "%.2f");
        ImGui::SliderInt("Start bullet", &g_config.rcsStartBullet, 1, 6);
    }
    else if (g_menuTab == 1) {
        SectionHeader("TRIGGERBOT");
        ImGui::Checkbox("Enable", &g_config.triggerEnabled);
        const char* tbones[] = { "Head", "Neck", "Chest" };
        ImGui::Combo("Bone (only fire on)", &g_config.triggerBone, tbones, 3);
        ImGui::SliderFloat("Bone FOV (px)", &g_config.triggerBoneFov, 3.f, 40.f, "%.0f");
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
        ImGui::Checkbox("Bomb timer (precise)", &g_config.bombTimer);

        SectionHeader("THIRD PERSON");
        ImGui::Checkbox("Enable third person", &g_config.thirdPerson);
        ImGui::SliderFloat("TP distance", &g_config.thirdPersonDist, 40.f, 200.f, "%.0f");
        ImGui::TextDisabled("Hold key to activate (release = first person).");
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
        ImGui::TextDisabled("Arrows for non-visible moving enemies.");

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
        ImGui::TextColored(ImVec4(0.95f, 0.40f, 0.38f, 1.f), "Legit preset tips:");
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
void DrawFrame() {
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

    if (IsValid(pLocal) && IsAlive(pLocal) && IsInGame()) {
        int localTeam = Team(pLocal);
        DoNoFlash(pLocal);
        DoNoSmoke(pLocal);
        DoThirdPerson(pLocal);
        DoSoftRCS(pLocal);
        DoNoVisualRecoil(pLocal);
        DoTriggerbot(pLocal, localTeam);
        UpdateHitmarker(pLocal, localTeam);
        DoGlow(pLocal, localTeam);

        if (g_config.espEnabled) {
            for (int i = 1; i <= 64; i++) {
                uintptr_t ctrl = GetEntity(i);
                if (!IsValid(ctrl)) continue;
                if (!ControllerPawnAlive(ctrl)) continue; // disconnect / leave filter

                uintptr_t pawn = HandleToEnt(SafeRead<uint32_t>(ctrl + O::m_hPlayerPawn, 0));
                if (!IsValid(pawn) || pawn == pLocal || !IsAlive(pawn)) continue;

                Vector3 feet = GetOrigin(pawn);
                if (!OriginSane(feet)) continue; // ghost at 0,0,0 after leave

                if (g_config.espTeamCheck && Team(pawn) == localTeam) continue;
                bool vis = IsSpotted(pawn);
                if (g_config.espVisibleOnly && !vis) continue;

                feet.z += 4.f; // visual sole lift — abs origin sits slightly under model
                Vector3 head;
                if (!GetBonePos(pawn, O::Bone::head, head)) continue;
                // head already includes crouch-aware height; small extra so box clears helmet
                head.z += 2.f;
                if (!OriginSane(head)) continue;

                Vector3 eye = GetOrigin(pLocal);
                eye.z += GetViewOffset(pLocal).z;
                float distM = sqrtf((head.x - eye.x) * (head.x - eye.x) + (head.y - eye.y) * (head.y - eye.y) + (head.z - eye.z) * (head.z - eye.z)) * 0.01905f;
                if (distM > g_config.espMaxDistance) continue;

                Vector2 sf, shs;
                if (!WorldToScreen(feet, sf, sw, sh) || !WorldToScreen(head, shs, sw, sh)) continue;
                float h = sf.y - shs.y;
                if (h < 6.f) continue;
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
                    Vector2 hd;
                    Vector3 hp; GetBonePos(pawn, O::Bone::head, hp);
                    if (WorldToScreen(hp, hd, sw, sh))
                        dl->AddCircleFilled(ImVec2(hd.x, hd.y), 2.2f, col);
                }
                if (g_config.espHealth) {
                    int hp = (std::clamp)(HP(pawn), 0, 100);
                    float bh = h * (hp / 100.f);
                    ImU32 hc = hp > 60 ? IM_COL32(50, 210, 90, 255) : hp > 30 ? IM_COL32(230, 190, 40, 255) : IM_COL32(230, 55, 55, 255);
                    dl->AddRectFilled(ImVec2(x - 5, y + h - bh), ImVec2(x - 2, y + h), hc);
                    dl->AddRect(ImVec2(x - 5, y), ImVec2(x - 2, y + h), IM_COL32(0, 0, 0, 160));
                }
                if (g_config.espArmor && Armor(pawn) > 0) {
                    float bh = h * ((std::clamp)(Armor(pawn), 0, 100) / 100.f);
                    dl->AddRectFilled(ImVec2(x + w + 2, y + h - bh), ImVec2(x + w + 5, y + h), IM_COL32(70, 140, 255, 230));
                }
                float ty = y - 13.f;
                if (g_config.espName) {
                    char name[128]; GetPlayerName(ctrl, name, sizeof(name));
                    if (name[0]) {
                        ImVec2 ts = ImGui::CalcTextSize(name);
                        dl->AddText(ImVec2(shs.x - ts.x * 0.5f, ty), IM_COL32(240, 240, 245, 235), name);
                        ty -= 13.f;
                    }
                }
                if (g_config.espWeapon) {
                    char wn[64]; GetWeaponName(pawn, wn, sizeof(wn));
                    if (wn[0]) {
                        ImVec2 ts = ImGui::CalcTextSize(wn);
                        dl->AddText(ImVec2(shs.x - ts.x * 0.5f, y + h + 2), IM_COL32(190, 190, 200, 210), wn);
                    }
                }
                if (g_config.espDistance) {
                    char dt[16]; sprintf_s(dt, "%.0fm", distM);
                    dl->AddText(ImVec2(x, y + h + (g_config.espWeapon ? 15.f : 2.f)), IM_COL32(170, 170, 180, 190), dt);
                }
                if (g_config.espSkeleton) DrawSkeleton(dl, pawn, sw, sh, col);
            }
        }

        DrawSpectatorList(pLocal);
        DrawHitmarker(dl);
        DrawFovCircle(dl);
        DrawCrosshair(dl);
        DrawBombTimer(dl);
        DrawSoundEsp(dl, pLocal, localTeam, sw, sh);
        DoLegitAim(pLocal, localTeam, sw, sh);
    }
    else if (IsValid(pLocal)) {
        DrawSpectatorList(pLocal);
    }

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
                    LOG("[+] rak-hus-legit premium UI ready");
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
    return oPresent(pSwapChain, SyncInterval, Flags);
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK HookedWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (g_imGuiInitialized) ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
    return CallWindowProc(g_OriginalWndProc, hWnd, msg, wParam, lParam);
}

DWORD WINAPI MainLoop(LPVOID) {
    srand((unsigned)time(nullptr));
    LOG("[*] legit main");
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
    while (true) {
        bool ins = (GetAsyncKeyState(VK_INSERT) & 0x8000) != 0;
        if (ins && !lastIns) g_config.showMenu = !g_config.showMenu;
        lastIns = ins;
        Sleep(16);
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_hModule = hModule;
        DisableThreadLibraryCalls(hModule);
        InitConsole();
        LOG("[+] rak-hus-legit loaded");
        CreateThread(NULL, 0, MainLoop, NULL, 0, NULL);
    }
    return TRUE;
}
