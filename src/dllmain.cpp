// ========================================================================
// Rakhus CS2 Internal – LEGIT Edition (build 14178)
// Smooth aim, soft RCS, triggerbot, subtle ESP, bomb timer
// ========================================================================

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
    SetConsoleTitleA("rakhus-legit");
    printf("[+] rakhus legit console ready\n");
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
    int triggerDelayMin = 90;
    int triggerDelayMax = 160;
    bool triggerTeamCheck = true;
    bool triggerVisibleOnly = false;

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
        } catch (...) {}
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
    } __except (EXCEPTION_EXECUTE_HANDLER) { buffer[0] = 0; }
}
static bool IsAlive(uintptr_t pawn) {
    return IsValid(pawn) && SafeRead<uint8_t>(pawn + O::m_lifeState, 1) == 0;
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
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
static uintptr_t HandleToEnt(uint32_t h) {
    if (!h || h == 0xFFFFFFFF) return 0;
    return GetEntity(h & 0x7FFF);
}
static int HP(uintptr_t e) { return SafeRead<int>(e + O::m_iHealth, 0); }
static int Team(uintptr_t e) { return SafeRead<uint8_t>(e + O::m_iTeamNum, 0); }
static int Armor(uintptr_t e) { return SafeRead<int>(e + O::m_ArmorValue, 0); }

void RefreshEntityCache() {
    g_cacheTick++;
    if ((g_cacheTick % 2) != 0 && g_cacheCount > 0) return;
    int n = 0;
    for (int i = 1; i <= 64 && n < 64; i++) {
        uintptr_t ctrl = GetEntity(i);
        if (!IsValid(ctrl)) continue;
        uintptr_t pawn = HandleToEnt(SafeRead<uint32_t>(ctrl + O::m_hPlayerPawn, 0));
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
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return o;
}
static Vector3 GetViewOffset(uintptr_t pawn) {
    Vector3 o{};
    if (!IsValid(pawn)) return o;
    __try {
        o.x = *(float*)(pawn + O::m_vecViewOffset);
        o.y = *(float*)(pawn + O::m_vecViewOffset + 4);
        o.z = *(float*)(pawn + O::m_vecViewOffset + 8);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
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
    Vector3 approx = origin;
    if (boneIdx == O::Bone::head || boneIdx == 6) {
        approx.z = origin.z + headZ + 4.f; // slight lift so ESP sits on skull not forehead-down
    } else if (boneIdx == O::Bone::neck || boneIdx == 5) {
        approx.z = origin.z + headZ * 0.82f;
    } else if (boneIdx == O::Bone::spine || boneIdx == 4) {
        approx.z = origin.z + headZ * 0.55f;
    } else if (boneIdx == O::Bone::pelvis || boneIdx == 0) {
        approx.z = origin.z + 12.f;
    } else {
        approx.z = origin.z + headZ * 0.4f;
    }

    uintptr_t sn = SafeRead<uintptr_t>(pawn + O::m_pGameSceneNode, 0);
    if (!IsValid(sn)) { out = approx; return true; }

    uintptr_t boneArray = SafeRead<uintptr_t>(sn + O::m_modelState + O::m_boneArray, 0);
    if (!IsValid(boneArray))
        boneArray = SafeRead<uintptr_t>(sn + 0x1E0, 0);
    if (!IsValid(boneArray)) { out = approx; return true; }

    __try {
        // CS2 bone_data: 32 bytes, world position at +0
        float x = *(float*)(boneArray + boneIdx * 32 + 0);
        float y = *(float*)(boneArray + boneIdx * 32 + 4);
        float z = *(float*)(boneArray + boneIdx * 32 + 8);
        float dx = x - origin.x, dy = y - origin.y, dz = z - origin.z;
        float dist2 = dx*dx + dy*dy + dz*dz;
        // Bone must sit near the body (rejects wrong stride / stale matrices when rotated)
        if (dist2 > 1.f && dist2 < 95.f * 95.f) {
            out.x = x; out.y = y; out.z = z;
            return true;
        }
        // matrix3x4 48-byte fallback
        float* m48 = (float*)(boneArray + boneIdx * 48);
        x = m48[3]; y = m48[7]; z = m48[11];
        dx = x - origin.x; dy = y - origin.y; dz = z - origin.z;
        dist2 = dx*dx + dy*dy + dz*dz;
        if (dist2 > 1.f && dist2 < 95.f * 95.f) {
            out.x = x; out.y = y; out.z = z;
            return true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}

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
        float cx = viewMatrix[0]*world.x + viewMatrix[1]*world.y + viewMatrix[2]*world.z + viewMatrix[3];
        float cy = viewMatrix[4]*world.x + viewMatrix[5]*world.y + viewMatrix[6]*world.z + viewMatrix[7];
        float cw = viewMatrix[12]*world.x + viewMatrix[13]*world.y + viewMatrix[14]*world.z + viewMatrix[15];
        if (cw < 0.001f) return false;
        float inv = 1.f / cw;
        screen.x = (sw * 0.5f) + (cx * inv) * (sw * 0.5f);
        screen.y = (sh * 0.5f) - (cy * inv) * (sh * 0.5f);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static void GetPlayerName(uintptr_t ctrl, char* buf, size_t len) {
    buf[0] = 0;
    if (IsValid(ctrl)) SafeReadArray(ctrl + O::m_iszPlayerName, buf, len);
}
static bool IsSpotted(uintptr_t pawn) {
    return IsValid(pawn) && SafeRead<uint8_t>(pawn + O::m_entitySpottedState + O::Spotted::m_bSpotted, 0) != 0;
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
        } __except (EXCEPTION_EXECUTE_HANDLER) { jeAddr = 0; }
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
        } else if (!enable && patched) {
            DWORD old = 0;
            if (!VirtualProtect((void*)jeAddr, 1, PAGE_EXECUTE_READWRITE, &old)) return;
            *(uint8_t*)jeAddr = original;
            VirtualProtect((void*)jeAddr, 1, old, &old);
            patched = false;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
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
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
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
    if (!g_config.noSmoke) return;

    // Local overlay (screen fog when inside smoke) — safe on player pawn
    if (IsValid(p)) {
        SafeWrite<float>(p + O::m_flLastSmokeOverlayAlpha, 0.f);
        SafeWrite<float>(p + O::m_flLastSmokeAge, 0.f);
        SafeWrite<Vector3>(p + O::m_vLastSmokeOverlayColor, Vector3{ 0.f, 0.f, 0.f });
    }

    // Preferred path: DrawSmokeArray is hooked → no entity writes needed
    if (oSmokeDrawArray)
        return;

    if (!g_pES) return;

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
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

void DoTriggerbot(uintptr_t localPawn, int localTeam) {
    if (!g_config.triggerEnabled || !IsValid(localPawn) || !IsAlive(localPawn)) return;
    if (!IsKeyDown(g_config.triggerKey)) return;

    // m_iIDEntIndex is the entity index under crosshair (pawn/entity)
    int cross = SafeRead<int>(localPawn + O::m_iIDEntIndex, -1);
    if (cross <= 0 || cross > 0x7FFE) return;

    uintptr_t target = GetEntity(cross);
    // Sometimes index points at controller — try pawn handle
    if (!IsValid(target) || !IsAlive(target)) {
        uintptr_t maybePawn = HandleToEnt(SafeRead<uint32_t>(target + O::m_hPlayerPawn, 0));
        if (IsValid(maybePawn) && IsAlive(maybePawn))
            target = maybePawn;
        else
            return;
    }
    if (!IsValid(target) || !IsAlive(target)) return;
    if (g_config.triggerTeamCheck && Team(target) == localTeam) return;
    // visible-only is optional; default off recommended for reliability
    if (g_config.triggerVisibleOnly && !IsSpotted(target)) return;

    // Inter-shot pause so spray/recoil can settle (legit)
    static auto lastShot = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    int delay = g_config.triggerDelayMin;
    if (g_config.triggerDelayMax > g_config.triggerDelayMin)
        delay = g_config.triggerDelayMin + (rand() % (g_config.triggerDelayMax - g_config.triggerDelayMin + 1));
    // Extra pause while already spraying (shots fired)
    int shots = SafeRead<int>(localPawn + O::m_iShotsFired, 0);
    if (shots >= 2)
        delay += 40 + shots * 8; // more pause deeper into spray
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastShot).count() < delay)
        return;
    lastShot = now;

    if (!hClient) return;
    // Short click, then mandatory release — next shot waits for delay above
    SafeWrite<int>(hClient + O::attack, 65537);
    std::thread([]() {
        Sleep(25 + rand() % 20);
        if (hClient) SafeWrite<int>(hClient + O::attack, 256);
    }).detach();
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
    if (!g_config.aimEnabled || !IsValid(localPawn) || !IsAlive(localPawn)) return;
    if (!IsKeyDown(g_config.aimKey)) { g_hasTarget = false; return; }
    if (g_config.aimOnlyWhenScoped && !SafeRead<uint8_t>(localPawn + O::m_bIsScoped, 0)) return;

    uintptr_t va = hClient + O::dwViewAngles;
    if (!IsValid(va)) return;

    Vector3 eye = GetOrigin(localPawn);
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
    } else {
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
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
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
        } __except (EXCEPTION_EXECUTE_HANDLER) { continue; }
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
    ImU32 col = IM_COL32((int)(g_config.chR*255),(int)(g_config.chG*255),(int)(g_config.chB*255),230);
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
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    SafeWrite<uint8_t>(glow + O::Glow::m_bGlowing, 1);
    SafeWrite<uint8_t>(glow + O::Glow::m_bEligibleForScreenHighlight, 1);
}

void DoGlow(uintptr_t localPawn, int localTeam) {
    if (!g_config.glowEnabled) return;
    for (int i = 0; i < g_cacheCount; i++) {
        auto& c = g_cache[i];
        if (!c.alive || c.pawn == localPawn) continue;
        if (g_config.espTeamCheck && c.team == localTeam) continue;
        ApplyGlow(c.pawn, c.team != localTeam);
    }
}

// -------------------- SOUND / MOVEMENT ESP --------------------
void DrawSoundEsp(ImDrawList* dl, uintptr_t localPawn, int localTeam, int sw, int sh) {
    if (!g_config.soundEsp || !IsValid(localPawn)) return;
    Vector3 eye = GetOrigin(localPawn);
    eye.z += GetViewOffset(localPawn).z;
    Vector3 viewAng = SafeRead<Vector3>(hClient + O::dwViewAngles, {});

    for (int i = 0; i < g_cacheCount; i++) {
        auto& c = g_cache[i];
        if (!c.alive || c.pawn == localPawn) continue;
        if (c.team == localTeam) continue;
        if (IsSpotted(c.pawn)) continue; // only non-visible

        Vector3 vel{};
        __try {
            vel.x = *(float*)(c.pawn + O::m_vecVelocity);
            vel.y = *(float*)(c.pawn + O::m_vecVelocity + 4);
            vel.z = *(float*)(c.pawn + O::m_vecVelocity + 8);
        } __except (EXCEPTION_EXECUTE_HANDLER) { continue; }
        float speed = sqrtf(vel.x * vel.x + vel.y * vel.y);
        if (speed < g_config.soundMinSpeed) continue;

        Vector3 pos = GetOrigin(c.pawn);
        float dx = pos.x - eye.x, dy = pos.y - eye.y, dz = pos.z - eye.z;
        float distM = sqrtf(dx*dx + dy*dy + dz*dz) * 0.01905f;
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


// -------------------- MODERN GUI STYLE --------------------
void SetupImGuiStyle() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 10.f;
    s.ChildRounding = 8.f;
    s.FrameRounding = 6.f;
    s.GrabRounding = 6.f;
    s.PopupRounding = 8.f;
    s.ScrollbarRounding = 8.f;
    s.TabRounding = 6.f;
    s.WindowPadding = ImVec2(0, 0);
    s.FramePadding = ImVec2(10, 6);
    s.ItemSpacing = ImVec2(10, 8);
    s.ItemInnerSpacing = ImVec2(8, 4);
    s.ScrollbarSize = 10.f;
    s.GrabMinSize = 10.f;
    s.WindowBorderSize = 0.f;
    s.ChildBorderSize = 0.f;
    s.FrameBorderSize = 0.f;

    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg]        = ImVec4(0.07f, 0.07f, 0.09f, 0.97f);
    c[ImGuiCol_ChildBg]         = ImVec4(0.09f, 0.09f, 0.11f, 1.00f);
    c[ImGuiCol_PopupBg]         = ImVec4(0.10f, 0.10f, 0.13f, 0.98f);
    c[ImGuiCol_Border]          = ImVec4(0.20f, 0.22f, 0.30f, 0.40f);
    c[ImGuiCol_FrameBg]         = ImVec4(0.12f, 0.13f, 0.17f, 1.00f);
    c[ImGuiCol_FrameBgHovered]  = ImVec4(0.16f, 0.18f, 0.24f, 1.00f);
    c[ImGuiCol_FrameBgActive]   = ImVec4(0.18f, 0.20f, 0.28f, 1.00f);
    c[ImGuiCol_TitleBg]         = ImVec4(0.08f, 0.08f, 0.10f, 1.00f);
    c[ImGuiCol_TitleBgActive]   = ImVec4(0.10f, 0.11f, 0.15f, 1.00f);
    c[ImGuiCol_CheckMark]       = ImVec4(0.35f, 0.75f, 1.00f, 1.00f);
    c[ImGuiCol_SliderGrab]      = ImVec4(0.30f, 0.65f, 0.95f, 0.90f);
    c[ImGuiCol_SliderGrabActive]= ImVec4(0.40f, 0.75f, 1.00f, 1.00f);
    c[ImGuiCol_Button]          = ImVec4(0.16f, 0.22f, 0.35f, 0.85f);
    c[ImGuiCol_ButtonHovered]   = ImVec4(0.22f, 0.32f, 0.50f, 1.00f);
    c[ImGuiCol_ButtonActive]    = ImVec4(0.14f, 0.20f, 0.35f, 1.00f);
    c[ImGuiCol_Header]          = ImVec4(0.16f, 0.22f, 0.35f, 0.70f);
    c[ImGuiCol_HeaderHovered]   = ImVec4(0.22f, 0.30f, 0.48f, 0.90f);
    c[ImGuiCol_HeaderActive]    = ImVec4(0.18f, 0.28f, 0.45f, 1.00f);
    c[ImGuiCol_Separator]       = ImVec4(0.20f, 0.22f, 0.28f, 0.60f);
    c[ImGuiCol_Text]            = ImVec4(0.92f, 0.93f, 0.96f, 1.00f);
    c[ImGuiCol_TextDisabled]    = ImVec4(0.45f, 0.48f, 0.55f, 1.00f);
}

static bool SidebarButton(const char* label, int id, int& current) {
    bool active = (current == id);
    if (active) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.35f, 0.60f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.40f, 0.65f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 1.f));
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.f, 0.f, 0.f, 0.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.14f, 0.16f, 0.22f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65f, 0.68f, 0.75f, 1.f));
    }
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.f);
    bool clicked = ImGui::Button(label, ImVec2(-1, 36));
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
    if (clicked) current = id;
    return clicked;
}

static void SectionHeader(const char* title) {
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.40f, 0.70f, 1.00f, 1.f), "%s", title);
    ImGui::Separator();
    ImGui::Spacing();
}

void DrawMenu() {
    static float alpha = 0.f;
    if (g_config.showMenu) alpha = (std::min)(1.f, alpha + 0.08f);
    else alpha = (std::max)(0.f, alpha - 0.08f);
    if (alpha < 0.01f) {
        ImGui::SetNextWindowPos(ImVec2(14, 14), ImGuiCond_Always);
        ImGui::Begin("##hint", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::TextColored(ImVec4(0.45f, 0.55f, 0.75f, 0.55f), "INSERT  ·  rakhus legit");
        ImGui::End();
        return;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
    ImGui::SetNextWindowSize(ImVec2(620, 460), ImGuiCond_Always);
    ImGui::Begin("##rakhus", &g_config.showMenu,
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar);

    // Sidebar
    ImGui::BeginChild("##side", ImVec2(150, 0), false);
    ImGui::SetCursorPos(ImVec2(12, 16));
    ImGui::TextColored(ImVec4(0.35f, 0.75f, 1.f, 1.f), "RAKHUS");
    ImGui::SetCursorPosX(12);
    ImGui::TextColored(ImVec4(0.45f, 0.48f, 0.55f, 1.f), "legit  ·  14178");
    ImGui::Spacing(); ImGui::Spacing();
    ImGui::SetCursorPosX(8);
    ImGui::BeginGroup();
    SidebarButton("  Aimbot", 0, g_menuTab);
    SidebarButton("  Trigger", 1, g_menuTab);
    SidebarButton("  Visuals", 2, g_menuTab);
    SidebarButton("  Misc", 3, g_menuTab);
    SidebarButton("  Glow", 4, g_menuTab);
    SidebarButton("  Config", 5, g_menuTab);
    ImGui::EndGroup();
    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 40);
    ImGui::SetCursorPosX(12);
    ImGui::TextColored(ImVec4(0.35f, 0.38f, 0.45f, 1.f), "INSERT close");
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
            ImGui::TextColored(ImVec4(1.f, 0.7f, 0.3f, 1.f), "press key...");
            for (int vk = 1; vk < 256; vk++) {
                if (vk == VK_INSERT) continue;
                if (GetAsyncKeyState(vk) & 0x8000) { g_config.aimKey = vk; bindAim = false; SaveConfig(); break; }
            }
        } else {
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
        ImGui::SliderInt("Delay min (ms)", &g_config.triggerDelayMin, 30, 250);
        ImGui::SliderInt("Delay max (ms)", &g_config.triggerDelayMax, 50, 350);
        ImGui::TextDisabled("Higher = more pause between shots (recoil).");
        if (g_config.triggerDelayMax < g_config.triggerDelayMin)
            g_config.triggerDelayMax = g_config.triggerDelayMin;
        ImGui::Checkbox("Team check", &g_config.triggerTeamCheck);
        ImGui::Checkbox("Visible only", &g_config.triggerVisibleOnly);

        static bool bindTrig = false;
        ImGui::Text("Trigger key:");
        ImGui::SameLine();
        if (bindTrig) {
            ImGui::TextColored(ImVec4(1.f, 0.7f, 0.3f, 1.f), "press key...");
            for (int vk = 1; vk < 256; vk++) {
                if (vk == VK_INSERT) continue;
                if (GetAsyncKeyState(vk) & 0x8000) { g_config.triggerKey = vk; bindTrig = false; SaveConfig(); break; }
            }
        } else {
            char kb[24]; sprintf_s(kb, "0x%02X", g_config.triggerKey);
            if (ImGui::Button(kb, ImVec2(70, 0))) bindTrig = true;
            ImGui::SameLine();
            if (ImGui::Button("M5")) { g_config.triggerKey = VK_XBUTTON2; SaveConfig(); }
        }
        ImGui::TextDisabled("Random delay between min–max for more natural timing.");
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
                ImGui::TextColored(ImVec4(1.f, 0.7f, 0.3f, 1.f), "press...");
                for (int vk = 1; vk < 256; vk++) {
                    if (vk == VK_INSERT) continue;
                    if (GetAsyncKeyState(vk) & 0x8000) { g_config.thirdPersonKey = vk; bindTp = false; SaveConfig(); break; }
                }
            } else {
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
        ImGui::TextColored(ImVec4(0.5f, 0.7f, 0.5f, 1.f), "Legit preset tips:");
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

    // Pattern-resolved globals first, static offsets as fallback
    if (Pat::g_res.gameEntitySystemPtr)
        g_pES = Pat::ReadPtr(Pat::g_res.gameEntitySystemPtr);
    else
        g_pES = SafeRead<uintptr_t>(hClient + O::dwGameEntitySystem, 0);

    // Prefer dumper RVA for view matrix (more reliable than pattern LEA for W2S)
    SafeMemcpy(viewMatrix, (void*)(hClient + O::dwViewMatrix), sizeof(viewMatrix));
    // If matrix looks zeroed, try pattern-resolved address
    if (viewMatrix[0] == 0.f && viewMatrix[5] == 0.f && viewMatrix[10] == 0.f && Pat::g_res.viewMatrix)
        SafeMemcpy(viewMatrix, (void*)Pat::g_res.viewMatrix, sizeof(viewMatrix));

        int sw = (int)ImGui::GetIO().DisplaySize.x;
        int sh = (int)ImGui::GetIO().DisplaySize.y;
        ImDrawList* dl = ImGui::GetBackgroundDrawList();

        RefreshEntityCache();

        uintptr_t pLocal = SafeRead<uintptr_t>(hClient + O::dwLocalPlayerPawn, 0);
        if (IsValid(pLocal) && IsAlive(pLocal)) {
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
                    if (!ctrl) continue;
                    uintptr_t pawn = HandleToEnt(SafeRead<uint32_t>(ctrl + O::m_hPlayerPawn, 0));
                    if (!IsValid(pawn) || pawn == pLocal || !IsAlive(pawn)) continue;
                    if (g_config.espTeamCheck && Team(pawn) == localTeam) continue;
                    bool vis = IsSpotted(pawn);
                    if (g_config.espVisibleOnly && !vis) continue;

                    Vector3 feet = GetOrigin(pawn);
                    feet.z += 4.f; // visual sole lift — abs origin sits slightly under model
                    Vector3 head;
                    GetBonePos(pawn, O::Bone::head, head);
                    // head already includes crouch-aware height; small extra so box clears helmet
                    head.z += 2.f;

                    Vector3 eye = GetOrigin(pLocal);
                    eye.z += GetViewOffset(pLocal).z;
                    float distM = sqrtf((head.x-eye.x)*(head.x-eye.x)+(head.y-eye.y)*(head.y-eye.y)+(head.z-eye.z)*(head.z-eye.z)) * 0.01905f;
                    if (distM > g_config.espMaxDistance) continue;

                    Vector2 sf, shs;
                    if (!WorldToScreen(feet, sf, sw, sh) || !WorldToScreen(head, shs, sw, sh)) continue;
                    float h = sf.y - shs.y;
                    if (h < 6.f) continue;
                    float w = h * 0.42f;
                    float x = shs.x - w * 0.5f, y = shs.y;

                    ImU32 col = vis
                        ? IM_COL32((int)(g_config.espVisColorR*255),(int)(g_config.espVisColorG*255),(int)(g_config.espVisColorB*255),210)
                        : IM_COL32((int)(g_config.espColorR*255),(int)(g_config.espColorG*255),(int)(g_config.espColorB*255),200);

                    if (g_config.espBox) {
                        if (g_config.espBoxOutline)
                            dl->AddRect(ImVec2(x-1, y-1), ImVec2(x+w+1, y+h+1), IM_COL32(0,0,0,140), 0.f, 0, 1.f);
                        dl->AddRect(ImVec2(x, y), ImVec2(x+w, y+h), col, 0.f, 0, g_config.espBoxThickness);
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
                        ImU32 hc = hp > 60 ? IM_COL32(50,210,90,255) : hp > 30 ? IM_COL32(230,190,40,255) : IM_COL32(230,55,55,255);
                        dl->AddRectFilled(ImVec2(x - 5, y + h - bh), ImVec2(x - 2, y + h), hc);
                        dl->AddRect(ImVec2(x - 5, y), ImVec2(x - 2, y + h), IM_COL32(0,0,0,160));
                    }
                    if (g_config.espArmor && Armor(pawn) > 0) {
                        float bh = h * ((std::clamp)(Armor(pawn), 0, 100) / 100.f);
                        dl->AddRectFilled(ImVec2(x + w + 2, y + h - bh), ImVec2(x + w + 5, y + h), IM_COL32(70,140,255,230));
                    }
                    float ty = y - 13.f;
                    if (g_config.espName) {
                        char name[128]; GetPlayerName(ctrl, name, sizeof(name));
                        if (name[0]) {
                            ImVec2 ts = ImGui::CalcTextSize(name);
                            dl->AddText(ImVec2(shs.x - ts.x * 0.5f, ty), IM_COL32(240,240,245,235), name);
                            ty -= 13.f;
                        }
                    }
                    if (g_config.espWeapon) {
                        char wn[64]; GetWeaponName(pawn, wn, sizeof(wn));
                        if (wn[0]) {
                            ImVec2 ts = ImGui::CalcTextSize(wn);
                            dl->AddText(ImVec2(shs.x - ts.x * 0.5f, y + h + 2), IM_COL32(190,190,200,210), wn);
                        }
                    }
                    if (g_config.espDistance) {
                        char dt[16]; sprintf_s(dt, "%.0fm", distM);
                        dl->AddText(ImVec2(x, y + h + (g_config.espWeapon ? 15.f : 2.f)), IM_COL32(170,170,180,190), dt);
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
        } else if (IsValid(pLocal)) {
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
                SetupImGuiStyle();
                ImGui_ImplWin32_Init(g_gameHwnd);
                if (ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext)) {
                    g_imGuiInitialized = true;
                    g_OriginalWndProc = (WNDPROC)SetWindowLongPtr(g_gameHwnd, GWLP_WNDPROC, (LONG_PTR)HookedWndProc);
                    LoadConfig();
                    LOG("[+] legit UI ready");
                }
            }
        }
        if (g_imGuiInitialized && !g_mainRenderTargetView) {
            ID3D11Texture2D* bb = nullptr;
            pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb);
            if (bb) { g_pd3dDevice->CreateRenderTargetView(bb, nullptr, &g_mainRenderTargetView); bb->Release(); }
        }
        if (g_imGuiInitialized && g_mainRenderTargetView) DrawFrame();
    } catch (...) {}
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
    } else {
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
            } else {
                LOG("[-] DrawSmokeArray create hook failed");
            }
        }
    } else {
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
        LOG("[+] rakhus legit loaded");
        CreateThread(NULL, 0, MainLoop, NULL, 0, NULL);
    }
    return TRUE;
}
