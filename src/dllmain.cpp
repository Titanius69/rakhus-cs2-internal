// ========================================================================
// Rakhus CS2 Internal Cheat – Professional Edition
// Features: ESP, Smooth Aim Assist, NoFlash, NoSmoke, NoVisualRecoil,
// Spectator List, Hitmarker, FOV Circle, Modern GUI
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
#include <unordered_set>
#include <climits>
#include <d3d11.h>
#include <dxgi.h>
#include "imgui/imgui.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_dx11.h"
#include "offsets.h"
#include "kiero/kiero.h"

#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

// -------------------- FONT AWESOME ICONS --------------------
#define ICON_MIN_FA 0xf000
#define ICON_MAX_FA 0xf8ff

#define ICON_FA_CROSSHAIRS "\uf05b"
#define ICON_FA_EYE        "\uf06e"
#define ICON_FA_COG        "\uf013"
#define ICON_FA_SAVE       "\uf0c7"
#define ICON_FA_PLUS       "\uf067"

// -------------------- CONSOLE --------------------
void InitConsole() {
    AllocConsole();
    FILE* f;
    freopen_s(&f, "CONOUT$", "w", stdout);
    freopen_s(&f, "CONOUT$", "w", stderr);
    SetConsoleTitleA("rakhus-cs2-internal");
    printf("[+] Console initialized.\n");
}
#define LOG(m)          printf("[+] %s\n", m)
#define LOG_FMT(f, ...) printf(f, __VA_ARGS__)

// -------------------- GLOBALS --------------------
static uintptr_t g_pES = 0;
static uintptr_t hClient = 0;
HMODULE g_hModule = nullptr;
HWND g_gameHwnd = nullptr;

ID3D11Device* g_pd3dDevice = nullptr;
ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
IDXGISwapChain* g_pSwapChain = nullptr;
ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;
bool g_imGuiInitialized = false;
WNDPROC g_OriginalWndProc = nullptr;

struct Vector3 { float x, y, z; };
struct Vector2 { float x, y; };

static Vector3 g_targetAngles = { 0, 0, 0 };
static bool g_hasTarget = false;

static bool g_hitMarkerActive = false;
static std::chrono::steady_clock::time_point g_hitMarkerTime;

// -------------------- CONFIG --------------------
#define AIM_KEY_DEFAULT VK_F1
#define AIM_RADIUS_DEFAULT 20.0f

struct Config {
    bool enabled = true;
    float aimRadius = AIM_RADIUS_DEFAULT;
    int aimKey = AIM_KEY_DEFAULT;
    float espColorR = 0.0f;
    float espColorG = 0.75f;
    float espColorB = 1.0f;
    bool showMenu = false;
    float smoothness = 0.8f;
    bool noFlash = false;
    bool noSmoke = false;
    bool showDistance = true;
    bool noVisualRecoil = false;
    bool spectatorList = false;
    bool hitmarker = false;
    bool fovCircle = false;
} g_config;

// -------------------- CONFIG I/O --------------------
std::string GetConfigPath() {
    char path[MAX_PATH];
    GetModuleFileNameA(g_hModule, path, MAX_PATH);
    char* lastSlash = strrchr(path, '\\');
    if (lastSlash) *(lastSlash + 1) = '\0';
    return std::string(path) + "config.ini";
}

void SaveConfig() {
    std::string path = GetConfigPath();
    std::ofstream file(path);
    if (!file.is_open()) return;
    file << "enabled=" << (g_config.enabled ? 1 : 0) << "\n";
    file << "aimRadius=" << g_config.aimRadius << "\n";
    file << "aimKey=" << g_config.aimKey << "\n";
    file << "espColorR=" << g_config.espColorR << "\n";
    file << "espColorG=" << g_config.espColorG << "\n";
    file << "espColorB=" << g_config.espColorB << "\n";
    file << "smoothness=" << g_config.smoothness << "\n";
    file << "noFlash=" << (g_config.noFlash ? 1 : 0) << "\n";
    file << "noSmoke=" << (g_config.noSmoke ? 1 : 0) << "\n";
    file << "showDistance=" << (g_config.showDistance ? 1 : 0) << "\n";
    file << "noVisualRecoil=" << (g_config.noVisualRecoil ? 1 : 0) << "\n";
    file << "spectatorList=" << (g_config.spectatorList ? 1 : 0) << "\n";
    file << "hitmarker=" << (g_config.hitmarker ? 1 : 0) << "\n";
    file << "fovCircle=" << (g_config.fovCircle ? 1 : 0) << "\n";
    file.close();
}

void LoadConfig() {
    std::string path = GetConfigPath();
    std::ifstream file(path);
    if (!file.is_open()) { SaveConfig(); return; }
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);
        if (key == "enabled") g_config.enabled = (std::stoi(value) != 0);
        else if (key == "aimRadius") g_config.aimRadius = std::stof(value);
        else if (key == "aimKey") g_config.aimKey = std::stoi(value);
        else if (key == "espColorR") g_config.espColorR = std::stof(value);
        else if (key == "espColorG") g_config.espColorG = std::stof(value);
        else if (key == "espColorB") g_config.espColorB = std::stof(value);
        else if (key == "smoothness") g_config.smoothness = std::stof(value);
        else if (key == "noFlash") g_config.noFlash = (std::stoi(value) != 0);
        else if (key == "noSmoke") g_config.noSmoke = (std::stoi(value) != 0);
        else if (key == "showDistance") g_config.showDistance = (std::stoi(value) != 0);
        else if (key == "noVisualRecoil") g_config.noVisualRecoil = (std::stoi(value) != 0);
        else if (key == "spectatorList") g_config.spectatorList = (std::stoi(value) != 0);
        else if (key == "hitmarker") g_config.hitmarker = (std::stoi(value) != 0);
        else if (key == "fovCircle") g_config.fovCircle = (std::stoi(value) != 0);
    }
    file.close();
}

// -------------------- SAFE MEMORY HELPERS --------------------
static bool IsValid(uintptr_t a) {
    return a > 0x10000 && a < 0x7FFFFFFFFFFF;
}

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

static void SafeReadArray(uintptr_t address, char* buffer, size_t maxLen) {
    __try {
        const char* ptr = (const char*)address;
        for (size_t i = 0; i < maxLen - 1; i++) {
            buffer[i] = ptr[i];
            if (ptr[i] == '\0') break;
        }
        buffer[maxLen - 1] = '\0';
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        buffer[0] = '\0';
    }
}

static bool IsAlive(uintptr_t pawn) {
    if (!IsValid(pawn)) return false;
    int life = SafeRead<uint8_t>(pawn + O::m_lifeState, 1);
    return life == 0;
}

// -------------------- ENTITY LIST (chunk-based) --------------------
static uintptr_t GetEntity(int idx) {
    if (idx < 0 || idx > 4096) return 0;
    if (!g_pES || !IsValid(g_pES)) return 0;

    __try {
        uintptr_t listPtr = g_pES + O::kListOffset;
        if (!IsValid(listPtr)) return 0;

        uintptr_t chunkPtr = listPtr + (idx / O::kChunk) * 8;
        if (!IsValid(chunkPtr)) return 0;

        uintptr_t chunk = SafeRead<uintptr_t>(chunkPtr, 0);
        if (!IsValid(chunk)) return 0;

        uintptr_t identity = chunk + (idx % O::kChunk) * O::kStride;
        if (!IsValid(identity)) return 0;

        uintptr_t ent = SafeRead<uintptr_t>(identity, 0);
        if (IsValid(ent)) return ent;
        return 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

static uintptr_t HandleToEnt(uint32_t h) {
    if (!h || h == 0xFFFFFFFF) return 0;
    return GetEntity(h & 0x7FFF);
}

static int HP(uintptr_t e) { return SafeRead<int>(e + O::m_iHealth, 0); }
static int Team(uintptr_t e) { return SafeRead<uint8_t>(e + O::m_iTeamNum, 0); }
static int Life(uintptr_t e) { return SafeRead<uint8_t>(e + O::m_lifeState, 0); }

// -------------------- POSITION HELPERS --------------------
static Vector3 GetOrigin(uintptr_t pawn) {
    Vector3 org{ 0,0,0 };
    if (!IsValid(pawn)) return org;
    uintptr_t sceneNode = SafeRead<uintptr_t>(pawn + O::m_pGameSceneNode, 0);
    if (!IsValid(sceneNode)) return org;
    __try {
        org.x = *(float*)(sceneNode + O::m_vecOrigin);
        org.y = *(float*)(sceneNode + O::m_vecOrigin + 4);
        org.z = *(float*)(sceneNode + O::m_vecOrigin + 8);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
    return org;
}

static Vector3 GetViewOffset(uintptr_t pawn) {
    Vector3 off{ 0,0,0 };
    if (!IsValid(pawn)) return off;
    __try {
        off.x = *(float*)(pawn + O::m_vecViewOffset);
        off.y = *(float*)(pawn + O::m_vecViewOffset + 4);
        off.z = *(float*)(pawn + O::m_vecViewOffset + 8);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
    return off;
}

static Vector3 GetEyeAngles(uintptr_t pawn) {
    Vector3 angles{ 0,0,0 };
    if (!IsValid(pawn)) return angles;
    __try {
        angles.x = *(float*)(pawn + O::m_angEyeAngles);
        angles.y = *(float*)(pawn + O::m_angEyeAngles + 4);
        angles.z = *(float*)(pawn + O::m_angEyeAngles + 8);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
    return angles;
}

static float viewMatrix[16];

bool WorldToScreen(const Vector3& world, Vector2& screen, int screenW, int screenH) {
    __try {
        float clipX = viewMatrix[0] * world.x + viewMatrix[1] * world.y + viewMatrix[2] * world.z + viewMatrix[3];
        float clipY = viewMatrix[4] * world.x + viewMatrix[5] * world.y + viewMatrix[6] * world.z + viewMatrix[7];
        float clipZ = viewMatrix[8] * world.x + viewMatrix[9] * world.y + viewMatrix[10] * world.z + viewMatrix[11];
        float clipW = viewMatrix[12] * world.x + viewMatrix[13] * world.y + viewMatrix[14] * world.z + viewMatrix[15];
        if (clipW < 0.001f) return false;
        float ndcX = clipX / clipW;
        float ndcY = clipY / clipW;
        screen.x = (screenW / 2.0f) * ndcX + (screenW / 2.0f);
        screen.y = -(screenH / 2.0f) * ndcY + (screenH / 2.0f);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// -------------------- KEY STATE --------------------
bool IsKeyDown(int key) {
    return (GetAsyncKeyState(key) & 0x8000) != 0;
}

// -------------------- FEATURES --------------------
void DoNoFlash(uintptr_t localPawn) {
    if (!g_config.noFlash || !localPawn) return;
    SafeWrite<float>(localPawn + O::m_flFlashOverlayAlpha, 0.0f);
    SafeWrite<float>(localPawn + O::m_flFlashMaxAlpha, 0.0f);
    SafeWrite<float>(localPawn + O::m_flFlashDuration, 0.0f);
    SafeWrite<float>(localPawn + O::m_flFlashBangTime, 0.0f);
}

void DoNoSmoke(uintptr_t localPawn) {
    if (!g_config.noSmoke || !localPawn) return;
    if (!IsAlive(localPawn)) return;

    SafeWrite<float>(localPawn + O::m_flLastSmokeOverlayAlpha, 0.0f);
    SafeWrite<float>(localPawn + O::m_flLastSmokeAge, 0.0f);
    SafeWrite<Vector3>(localPawn + O::m_vLastSmokeOverlayColor, Vector3{ 1.0f, 1.0f, 1.0f });

    g_pES = SafeRead<uintptr_t>(hClient + O::dwGameEntitySystem, 0);
    if (!g_pES) return;

    for (int i = 1; i <= 4096; i++) {
        uintptr_t ent = GetEntity(i);
        if (!ent || !IsValid(ent)) continue;

        uint8_t spawned = SafeRead<uint8_t>(ent + O::m_bSmokeEffectSpawned, 0);
        if (spawned == 1) {
            SafeWrite<uint8_t>(ent + O::m_bSmokeEffectSpawned, 0);
            SafeWrite<uint8_t>(ent + O::m_bDidSmokeEffect, 0);
            SafeWrite<uint32_t>(ent + O::m_nSmokeEffectTickBegin, 0);
        }
    }
}

// NoVisualRecoil – zero out all punch components to kill both recoil and shake
void DoNoVisualRecoil(uintptr_t localPawn) {
    if (!g_config.noVisualRecoil || !localPawn) return;
    uintptr_t punchServices = SafeRead<uintptr_t>(localPawn + O::m_pAimPunchServices, 0);
    if (!punchServices || !IsValid(punchServices)) return;

    // Zero predictable and unpredictable punch – this kills the view shake
    SafeWrite<Vector3>(punchServices + O::AimPunch::m_predictableBaseAngle, Vector3{ 0,0,0 });
    SafeWrite<Vector3>(punchServices + O::AimPunch::m_predictableBaseAngleVel, Vector3{ 0,0,0 });
    SafeWrite<Vector3>(punchServices + O::AimPunch::m_unpredictableBaseAngle, Vector3{ 0,0,0 });
}

// -------------------- SPECTATOR LIST (FIXED: top‑right corner, 15px from edge) --------------------
void DrawSpectatorList(uintptr_t localPawn) {
    if (!g_config.spectatorList) return;
    if (!localPawn) return;

    static char specNames[64][128];
    static int specCount = 0;
    specCount = 0;

    for (int i = 1; i <= 64 && specCount < 64; i++) {
        uintptr_t pCtrl = GetEntity(i);
        if (!pCtrl) continue;
        uintptr_t pPawn = HandleToEnt(SafeRead<uint32_t>(pCtrl + O::m_hPlayerPawn, 0));
        if (!pPawn || pPawn == localPawn) continue;
        uintptr_t obsServices = SafeRead<uintptr_t>(pPawn + O::m_pObserverServices, 0);
        if (!obsServices || !IsValid(obsServices)) continue;
        uint32_t targetHandle = SafeRead<uint32_t>(obsServices + O::Observer::m_hObserverTarget, 0);
        uintptr_t targetEnt = HandleToEnt(targetHandle);
        if (targetEnt == localPawn) {
            char name[128] = { 0 };
            SafeReadArray(pCtrl + O::m_iszPlayerName, name, 128);
            if (name[0] != '\0') {
                strcpy_s(specNames[specCount], 128, name);
                specCount++;
            }
        }
    }

    if (specCount > 0) {
        float screenW = ImGui::GetIO().DisplaySize.x;
        float padding = 15.0f;
        float windowWidth = 200.0f;
        // Bal felső sarok referencia, a jobb széltől padding távolságra
        ImGui::SetNextWindowPos(ImVec2(screenW - padding - windowWidth, padding), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(windowWidth, 0), ImGuiCond_Always);
        ImGui::Begin("Spectators", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Text("Spectators (%d):", specCount);
        for (int i = 0; i < specCount; i++) {
            ImGui::Text("- %s", specNames[i]);
        }
        ImGui::End();
    }
}

// -------------------- HITMARKER (only triggers when YOU receive damage) --------------------
void UpdateHitmarker(uintptr_t localPawn, uintptr_t localController) {
    if (!g_config.hitmarker || !localPawn || !localController) return;
    static size_t lastDamageCount = 0;
    uintptr_t damageServices = SafeRead<uintptr_t>(localController + O::m_pDamageServices, 0);
    if (!damageServices || !IsValid(damageServices)) return;
    uintptr_t damageList = damageServices + O::Damage::m_DamageList;
    if (!IsValid(damageList)) return;

    // CUtlVector: [0] = m_pElements (8 bytes), [8] = m_nSize (int)
    int count = SafeRead<int>(damageList + 8, 0);
    if (count > 0 && count > (int)lastDamageCount) {
        g_hitMarkerActive = true;
        g_hitMarkerTime = std::chrono::steady_clock::now();
    }
    lastDamageCount = count;
}

void DrawHitmarker(ImDrawList* draw) {
    if (!g_config.hitmarker || !g_hitMarkerActive) return;
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - g_hitMarkerTime).count() > 300) {
        g_hitMarkerActive = false;
        return;
    }
    ImVec2 center(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f);
    float size = 10.0f, thickness = 2.0f;
    draw->AddLine(ImVec2(center.x - size, center.y), ImVec2(center.x - size / 2, center.y), IM_COL32(255, 255, 255, 255), thickness);
    draw->AddLine(ImVec2(center.x + size / 2, center.y), ImVec2(center.x + size, center.y), IM_COL32(255, 255, 255, 255), thickness);
    draw->AddLine(ImVec2(center.x, center.y - size), ImVec2(center.x, center.y - size / 2), IM_COL32(255, 255, 255, 255), thickness);
    draw->AddLine(ImVec2(center.x, center.y + size / 2), ImVec2(center.x, center.y + size), IM_COL32(255, 255, 255, 255), thickness);
}

// -------------------- FOV CIRCLE --------------------
void DrawFovCircle(ImDrawList* draw) {
    if (!g_config.fovCircle) return;
    ImVec2 center(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f);
    draw->AddCircle(center, g_config.aimRadius, IM_COL32(255, 255, 255, 80), 64, 1.5f);
}

// -------------------- AIM ASSIST --------------------
static Vector3 CalculateAngles(const Vector3& source, const Vector3& target) {
    Vector3 delta = { target.x - source.x, target.y - source.y, target.z - source.z };
    float dist = sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
    if (dist < 1.0f) return { 0,0,0 };
    Vector3 angles;
    angles.x = -atan2(delta.z, sqrt(delta.x * delta.x + delta.y * delta.y)) * (180.0f / 3.14159265359f);
    angles.y = atan2(delta.y, delta.x) * (180.0f / 3.14159265359f);
    angles.z = 0.0f;
    return angles;
}

static void NormalizeAngles(Vector3& angles) {
    if (angles.x > 89.0f) angles.x = 89.0f;
    if (angles.x < -89.0f) angles.x = -89.0f;
    while (angles.y > 180.0f) angles.y -= 360.0f;
    while (angles.y < -180.0f) angles.y += 360.0f;
    angles.z = 0.0f;
}

void DoAimAssist(uintptr_t localPawn, int localTeam, int screenW, int screenH) {
    if (!g_config.enabled || !localPawn || !IsAlive(localPawn)) return;
    if (!IsKeyDown(g_config.aimKey)) {
        g_hasTarget = false;
        return;
    }

    uintptr_t viewAnglesAddr = hClient + O::dwViewAngles;
    if (!IsValid(viewAnglesAddr)) return;

    Vector3 localEye = GetOrigin(localPawn);
    Vector3 viewOff = GetViewOffset(localPawn);
    localEye.z += viewOff.z;

    const float fov = 90.0f;
    float maxAngle = g_config.aimRadius * (fov / screenW);

    float bestAngle = maxAngle + 1.0f;
    float bestDist = FLT_MAX;
    uintptr_t bestTarget = 0;
    Vector3 bestHead = { 0,0,0 };

    for (int i = 1; i <= 64; i++) {
        uintptr_t pCtrl = GetEntity(i);
        if (!pCtrl) continue;

        uint32_t hPawn = 0;
        __try { hPawn = *(uint32_t*)(pCtrl + O::m_hPlayerPawn); }
        __except (EXCEPTION_EXECUTE_HANDLER) { continue; }
        if (!hPawn || hPawn == 0xFFFFFFFF) continue;

        uintptr_t pPawn = HandleToEnt(hPawn);
        if (!pPawn || pPawn == localPawn) continue;

        int hp = HP(pPawn);
        int team = Team(pPawn);
        int life = Life(pPawn);
        if (hp <= 0 || hp > 100 || life != 0 || (team != 2 && team != 3) || team == localTeam) continue;

        Vector3 head = GetOrigin(pPawn);
        Vector3 off = GetViewOffset(pPawn);
        head.z += off.z + 2.5f;

        Vector3 delta = { head.x - localEye.x, head.y - localEye.y, head.z - localEye.z };
        float dist3D = sqrtf(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
        if (dist3D < 1.0f) continue;

        Vector3 neededAngles = CalculateAngles(localEye, head);
        NormalizeAngles(neededAngles);

        Vector3 currentAngles;
        __try {
            currentAngles = *(Vector3*)viewAnglesAddr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            continue;
        }

        float deltaPitch = neededAngles.x - currentAngles.x;
        float deltaYaw = neededAngles.y - currentAngles.y;
        if (deltaYaw > 180.0f) deltaYaw -= 360.0f;
        if (deltaYaw < -180.0f) deltaYaw += 360.0f;
        if (deltaPitch > 180.0f) deltaPitch -= 360.0f;
        if (deltaPitch < -180.0f) deltaPitch += 360.0f;

        float angleDiff = sqrtf(deltaPitch * deltaPitch + deltaYaw * deltaYaw);

        if (angleDiff <= maxAngle && angleDiff < bestAngle) {
            bestAngle = angleDiff;
            bestDist = dist3D;
            bestTarget = pPawn;
            bestHead = head;
        }
        else if (fabsf(angleDiff - bestAngle) < 0.001f && dist3D < bestDist) {
            bestDist = dist3D;
            bestTarget = pPawn;
            bestHead = head;
        }
    }

    if (bestTarget) {
        Vector3 targetAngles = CalculateAngles(localEye, bestHead);
        NormalizeAngles(targetAngles);
        g_targetAngles = targetAngles;
        g_hasTarget = true;
    }
    else {
        g_hasTarget = false;
        return;
    }

    if (g_hasTarget) {
        __try {
            Vector3 currentAngles = *(Vector3*)viewAnglesAddr;
            float deltaPitch = g_targetAngles.x - currentAngles.x;
            float deltaYaw = g_targetAngles.y - currentAngles.y;
            if (deltaYaw > 180.0f) deltaYaw -= 360.0f;
            if (deltaYaw < -180.0f) deltaYaw += 360.0f;

            float smoothFactor = 1.0f - g_config.smoothness;
            Vector3 newAngles = currentAngles;

            if (fabsf(deltaPitch) < 0.5f && fabsf(deltaYaw) < 0.5f) {
                newAngles.x = g_targetAngles.x;
                newAngles.y = g_targetAngles.y;
            }
            else {
                newAngles.x += deltaPitch * smoothFactor;
                newAngles.y += deltaYaw * smoothFactor;
            }
            newAngles.z = 0.0f;

            if (isnan(newAngles.x) || isnan(newAngles.y) || isnan(newAngles.z)) {
                return;
            }

            *(Vector3*)viewAnglesAddr = newAngles;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
}

// -------------------- MODERN GUI STYLE --------------------
void SetupImGuiStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 8.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.ChildRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.WindowPadding = ImVec2(12, 12);
    style.FramePadding = ImVec2(8, 6);
    style.ItemSpacing = ImVec2(8, 8);
    style.ItemInnerSpacing = ImVec2(4, 4);
    style.IndentSpacing = 12.0f;
    style.ScrollbarSize = 12.0f;
    style.GrabMinSize = 8.0f;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.08f, 0.12f, 0.92f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.10f, 0.10f, 0.14f, 0.80f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.08f, 0.08f, 0.12f, 0.95f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.15f, 0.15f, 0.25f, 0.90f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.20f, 0.25f, 0.40f, 0.95f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.10f, 0.10f, 0.15f, 0.50f);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.10f, 0.10f, 0.15f, 0.90f);
    colors[ImGuiCol_Border] = ImVec4(0.25f, 0.30f, 0.45f, 0.60f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_Separator] = ImVec4(0.20f, 0.25f, 0.40f, 0.50f);
    colors[ImGuiCol_SeparatorHovered] = ImVec4(0.30f, 0.40f, 0.60f, 0.80f);
    colors[ImGuiCol_SeparatorActive] = ImVec4(0.40f, 0.50f, 0.70f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.20f, 0.30f, 0.50f, 0.70f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.30f, 0.40f, 0.65f, 0.90f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.15f, 0.25f, 0.45f, 1.00f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.30f, 0.70f, 1.00f, 1.00f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.30f, 0.70f, 1.00f, 0.80f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.30f, 0.70f, 1.00f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.20f, 0.35f, 0.60f, 0.60f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.25f, 0.40f, 0.70f, 0.80f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.15f, 0.30f, 0.55f, 0.90f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.15f, 0.15f, 0.25f, 0.60f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.25f, 0.40f, 0.80f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.25f, 0.30f, 0.50f, 1.00f);
    colors[ImGuiCol_Text] = ImVec4(1.00f, 1.00f, 1.00f, 0.95f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.60f, 0.60f, 0.70f, 0.80f);
    colors[ImGuiCol_Tab] = ImVec4(0.12f, 0.12f, 0.20f, 0.80f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.25f, 0.35f, 0.55f, 1.00f);
    colors[ImGuiCol_TabActive] = ImVec4(0.20f, 0.30f, 0.50f, 1.00f);
    colors[ImGuiCol_TabUnfocused] = ImVec4(0.10f, 0.10f, 0.15f, 0.80f);
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.15f, 0.20f, 0.30f, 1.00f);
}

bool LoadFontFromDisk() {
    char modulePath[MAX_PATH];
    GetModuleFileNameA(g_hModule, modulePath, MAX_PATH);
    char* lastSlash = strrchr(modulePath, '\\');
    if (lastSlash) *(lastSlash + 1) = '\0';
    char fontPath[MAX_PATH];
    snprintf(fontPath, MAX_PATH, "%sfont.ttf", modulePath);
    LOG_FMT("[*] Loading font from disk: %s\n", fontPath);

    FILE* file = fopen(fontPath, "rb");
    if (!file) {
        LOG_FMT("[-] Cannot open font file: %s\n", fontPath);
        return false;
    }
    fseek(file, 0, SEEK_END);
    long fileSize = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (fileSize <= 0) {
        LOG("[-] Invalid font file size");
        fclose(file);
        return false;
    }
    void* pData = malloc(fileSize);
    if (!pData) {
        LOG("[-] Failed to allocate memory for font");
        fclose(file);
        return false;
    }
    size_t bytesRead = fread(pData, 1, fileSize, file);
    fclose(file);
    if (bytesRead != fileSize) {
        LOG_FMT("[-] Failed to read complete font file (got %zu of %ld bytes)\n", bytesRead, fileSize);
        free(pData);
        return false;
    }
    LOG_FMT("[+] Font loaded from disk: %ld bytes\n", fileSize);

    ImGuiIO& io = ImGui::GetIO();
    static const ImWchar faRange[] = { 0xF000, 0xF8FF, 0 };
    static ImVector<ImWchar> glyphRanges;
    ImFontGlyphRangesBuilder builder;
    builder.AddRanges(io.Fonts->GetGlyphRangesDefault());
    builder.AddRanges(faRange);
    builder.BuildRanges(&glyphRanges);

    ImFontConfig fontConfig;
    fontConfig.OversampleH = 2;
    fontConfig.OversampleV = 1;
    fontConfig.PixelSnapH = false;
    fontConfig.FontDataOwnedByAtlas = true;

    ImFont* font = io.Fonts->AddFontFromMemoryTTF(
        pData,
        static_cast<int>(fileSize),
        16.0f,
        &fontConfig,
        glyphRanges.Data
    );
    if (!font) {
        LOG("[-] Failed to load font into ImGui");
        free(pData);
        return false;
    }
    LOG("[+] Font Awesome 6 Solid + default glyphs loaded successfully");
    return true;
}

// -------------------- RENDER TARGET --------------------
bool CreateRenderTarget() {
    if (!g_pSwapChain) return false;
    ID3D11Texture2D* pBackBuffer = nullptr;
    HRESULT hr = g_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
    if (FAILED(hr) || !pBackBuffer) return false;
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
    hr = g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
    return SUCCEEDED(hr);
}

void CleanupRenderTarget() {
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

// -------------------- WNDPROC HOOK --------------------
extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK HookedWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// -------------------- MAIN DRAW --------------------
void DrawImGuiESP() {
    try {
        if (!g_imGuiInitialized) return;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // --- Entity System ---
        g_pES = SafeRead<uintptr_t>(hClient + O::dwGameEntitySystem, 0);

        // --- ViewMatrix ---
        try {
            memcpy(viewMatrix, (void*)(hClient + O::dwViewMatrix), sizeof(viewMatrix));
        }
        catch (...) {}

        // --- Local Player ---
        uintptr_t pLocal = SafeRead<uintptr_t>(hClient + O::dwLocalPlayerPawn, 0);
        bool isAlive = IsAlive(pLocal);

        if (isAlive && pLocal) {
            int localTeam = Team(pLocal);
            if (localTeam == 0) localTeam = 3;

            uint32_t hController = SafeRead<uint32_t>(pLocal + O::m_hController, 0);
            uintptr_t localController = HandleToEnt(hController);

            if (localController && IsValid(localController)) {
                int screenW = (int)ImGui::GetIO().DisplaySize.x;
                int screenH = (int)ImGui::GetIO().DisplaySize.y;

                // --- Feature calls ---
                DoNoFlash(pLocal);
                DoNoSmoke(pLocal);
                DoNoVisualRecoil(pLocal);
                UpdateHitmarker(pLocal, localController);

                ImDrawList* draw = ImGui::GetForegroundDrawList();
                if (draw) {
                    // ---- ESP: enemies ----
                    uintptr_t enemies[64];
                    int enemyCount = 0;
                    for (int i = 1; i <= 64; i++) {
                        uintptr_t pCtrl = GetEntity(i);
                        if (!pCtrl) continue;
                        uint32_t hPawn = SafeRead<uint32_t>(pCtrl + O::m_hPlayerPawn, 0);
                        if (!hPawn || hPawn == 0xFFFFFFFF) continue;
                        uintptr_t pPawn = HandleToEnt(hPawn);
                        if (!pPawn || pPawn == pLocal) continue;
                        int hp = HP(pPawn);
                        int team = Team(pPawn);
                        int life = Life(pPawn);
                        if (hp <= 0 || hp > 100 || life != 0 || (team != 2 && team != 3) || team == localTeam) continue;
                        if (enemyCount < 64) enemies[enemyCount++] = pPawn;
                    }

                    // ---- ESP: draw boxes, HP bars, etc. ----
                    for (int idx = 0; idx < enemyCount; idx++) {
                        uintptr_t pPawn = enemies[idx];
                        int hp = HP(pPawn);
                        Vector3 origin = GetOrigin(pPawn);
                        Vector3 viewOffset = GetViewOffset(pPawn);
                        Vector3 headPos = origin;
                        headPos.z += viewOffset.z + 2.5f;
                        Vector3 eyeAngles = GetEyeAngles(pPawn);
                        float yaw = eyeAngles.y * (3.14159265359f / 180.0f);
                        Vector3 forward = { cosf(yaw), sinf(yaw), 0.0f };
                        headPos.x += forward.x * 5.0f;
                        headPos.y += forward.y * 5.0f;
                        Vector3 footPos = origin;
                        Vector2 screenHead, screenFoot;
                        if (!WorldToScreen(headPos, screenHead, screenW, screenH)) continue;
                        if (!WorldToScreen(footPos, screenFoot, screenW, screenH)) continue;
                        float height = abs(screenHead.y - screenFoot.y);
                        if (height < 1.0f) continue;
                        float topOffset = height * 0.15f;
                        float topY = screenHead.y - topOffset;
                        float boxHeight = screenFoot.y - topY;
                        float boxWidth = boxHeight * 0.5f;
                        float x = screenFoot.x - boxWidth / 2;
                        float y = topY;
                        ImU32 color = IM_COL32((int)(g_config.espColorR * 255), (int)(g_config.espColorG * 255), (int)(g_config.espColorB * 255), 255);
                        ImU32 colorBg = IM_COL32(0, 0, 0, 180);
                        draw->AddRect(ImVec2(x, y), ImVec2(x + boxWidth, y + boxHeight), color, 0.0f, 0, 2.0f);
                        float barWidth = 5.0f, barX = x - barWidth - 4.0f;
                        draw->AddRectFilled(ImVec2(barX, y + 1), ImVec2(barX + barWidth, y + boxHeight - 1), colorBg);
                        float fillHeight = boxHeight - 2.0f;
                        if (hp > 0) {
                            float hpPercent = hp / 100.0f;
                            float fill = fillHeight * hpPercent;
                            ImU32 hpColor = IM_COL32(255 - (int)(255 * hpPercent), (int)(255 * hpPercent), 0, 255);
                            draw->AddRectFilled(ImVec2(barX, y + 1 + fillHeight - fill), ImVec2(barX + barWidth, y + boxHeight - 1), hpColor);
                        }
                        draw->AddCircleFilled(ImVec2(screenHead.x, screenHead.y), 5.0f, IM_COL32(255, 255, 255, 255));
                        draw->AddCircle(ImVec2(screenHead.x, screenHead.y), 5.0f, IM_COL32(0, 0, 0, 200), 12, 1.5f);
                        char text[16]; sprintf_s(text, "%d HP", hp);
                        draw->AddText(ImVec2(x, y + boxHeight + 3), IM_COL32(255, 255, 255, 255), text);
                        if (g_config.showDistance) {
                            Vector3 localEye = GetOrigin(pLocal);
                            Vector3 viewOffLocal = GetViewOffset(pLocal);
                            localEye.z += viewOffLocal.z;
                            float dx3D = headPos.x - localEye.x;
                            float dy3D = headPos.y - localEye.y;
                            float dz3D = headPos.z - localEye.z;
                            float dist3D = sqrt(dx3D * dx3D + dy3D * dy3D + dz3D * dz3D);
                            float distMeters = dist3D * 0.01905f;
                            char distText[32]; sprintf_s(distText, "%.0fm", distMeters);
                            draw->AddText(ImVec2(x, y + boxHeight + 3 + 15), IM_COL32(255, 255, 255, 200), distText);
                        }
                    }

                    DrawSpectatorList(pLocal);
                    DrawHitmarker(draw);
                    DrawFovCircle(draw);
                    DoAimAssist(pLocal, localTeam, screenW, screenH);
                }
            }
        }
        else {
            // If local is dead, we can still show spectator list
            if (pLocal) DrawSpectatorList(pLocal);
        }

        // -------------------- MODERN MENU --------------------
        static float menuAlpha = 0.0f;
        static int currentTab = 0;
        static bool aimBindingActive = false;

        if (g_config.showMenu && menuAlpha < 1.0f) {
            menuAlpha += 0.05f;
            if (menuAlpha > 1.0f) menuAlpha = 1.0f;
        }
        else if (!g_config.showMenu && menuAlpha > 0.0f) {
            menuAlpha -= 0.05f;
            if (menuAlpha < 0.0f) menuAlpha = 0.0f;
        }

        if (menuAlpha > 0.01f) {
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, menuAlpha);

            const char* tabs[] = { ICON_FA_CROSSHAIRS " Aimbot", ICON_FA_EYE " Visuals", ICON_FA_SAVE " Config" };

            ImGui::SetNextWindowSize(ImVec2(620, 480), ImGuiCond_FirstUseEver);
            ImGui::Begin("rakhus-cs2", &g_config.showMenu,
                ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize);

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.7f, 1.0f, 1.0f));
            ImGui::Text("RAKHUS CS2");
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "| internal cheat");
            ImGui::Separator();

            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2, 0));
            for (int i = 0; i < IM_ARRAYSIZE(tabs); i++) {
                if (i > 0) ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Button, (i == currentTab) ?
                    ImVec4(0.2f, 0.3f, 0.5f, 0.8f) : ImVec4(0.1f, 0.1f, 0.15f, 0.6f));
                if (ImGui::Button(tabs[i], ImVec2(120, 30))) {
                    currentTab = i;
                }
                ImGui::PopStyleColor();
            }
            ImGui::PopStyleVar();
            ImGui::Separator();

            switch (currentTab) {
            case 0: // Aimbot
                ImGui::BeginChild("AimbotContent", ImVec2(0, 0), true);
                ImGui::Checkbox("Enable Aimbot", &g_config.enabled);
                ImGui::SliderFloat("Aim Radius", &g_config.aimRadius, 0.0f, 100.0f, "%.1f px");
                ImGui::SliderFloat("Smoothness", &g_config.smoothness, 0.0f, 1.0f, "%.2f");
                if (aimBindingActive) {
                    ImGui::Text("Press any key or mouse button...");
                    for (int key = 0x01; key <= 0xFF; key++) {
                        if (GetAsyncKeyState(key) & 0x8000) { g_config.aimKey = key; aimBindingActive = false; SaveConfig(); break; }
                    }
                    if (GetAsyncKeyState(VK_RBUTTON) & 0x8000) { g_config.aimKey = VK_RBUTTON; aimBindingActive = false; SaveConfig(); }
                    if (GetAsyncKeyState(VK_MBUTTON) & 0x8000) { g_config.aimKey = VK_MBUTTON; aimBindingActive = false; SaveConfig(); }
                    if (GetAsyncKeyState(VK_XBUTTON1) & 0x8000) { g_config.aimKey = VK_XBUTTON1; aimBindingActive = false; SaveConfig(); }
                    if (GetAsyncKeyState(VK_XBUTTON2) & 0x8000) { g_config.aimKey = VK_XBUTTON2; aimBindingActive = false; SaveConfig(); }
                }
                else {
                    char keyName[64];
                    if (g_config.aimKey >= 0x01 && g_config.aimKey <= 0x07) {
                        const char* mouseNames[] = { "Left","Right","Middle","X1","X2" };
                        int idx = g_config.aimKey - 1;
                        if (idx >= 0 && idx < 5) sprintf_s(keyName, "Mouse %s", mouseNames[idx]);
                        else sprintf_s(keyName, "Mouse %d", g_config.aimKey);
                    }
                    else {
                        UINT scanCode = MapVirtualKey(g_config.aimKey, MAPVK_VK_TO_VSC);
                        char name[64];
                        if (GetKeyNameTextA(scanCode << 16, name, sizeof(name)) > 0) sprintf_s(keyName, "%s", name);
                        else sprintf_s(keyName, "VK_%02X", g_config.aimKey);
                    }
                    ImGui::Text("Aim Key: %s", keyName);
                    if (ImGui::Button("Change Key")) { aimBindingActive = true; }
                    ImGui::SameLine();
                    if (ImGui::Button("Reset to F1")) { g_config.aimKey = VK_F1; SaveConfig(); }
                }
                ImGui::EndChild();
                break;

            case 1: // Visuals
                ImGui::BeginChild("VisualsContent", ImVec2(0, 0), true);
                ImGui::Checkbox("NoFlash", &g_config.noFlash);
                ImGui::Checkbox("NoSmoke", &g_config.noSmoke);
                ImGui::Checkbox("No Visual Recoil", &g_config.noVisualRecoil);
                ImGui::Checkbox("Show Distance", &g_config.showDistance);
                ImGui::Checkbox("Spectator List", &g_config.spectatorList);
                ImGui::Checkbox("Hitmarker", &g_config.hitmarker);
                ImGui::Checkbox("FOV Circle", &g_config.fovCircle);
                ImGui::ColorEdit3("ESP Color", &g_config.espColorR);
                ImGui::EndChild();
                break;

            case 2: // Config
                ImGui::BeginChild("ConfigContent", ImVec2(0, 0), true);
                if (ImGui::Button(ICON_FA_SAVE " Save Config", ImVec2(120, 30))) SaveConfig();
                ImGui::SameLine();
                if (ImGui::Button(ICON_FA_PLUS " Load Config", ImVec2(120, 30))) LoadConfig();
                ImGui::SameLine();
                if (ImGui::Button("Reset Defaults", ImVec2(120, 30))) {
                    g_config = Config();
                    SaveConfig();
                }
                ImGui::EndChild();
                break;
            }

            ImGui::End();
            ImGui::PopStyleVar();
        }
        else {
            // Minimal hint when menu is hidden
            ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(0, 0), ImGuiCond_Always);
            ImGui::Begin("MenuHint", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground);
            ImGui::Text("Press INSERT to open menu");
            ImGui::End();
        }

        ImGui::EndFrame();
        ImGui::Render();

        if (g_mainRenderTargetView && g_pd3dDeviceContext) {
            g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        }
    }
    catch (...) {
        // Catch-all for C++ exceptions
    }
}

// -------------------- D3D11 HOOK --------------------
typedef HRESULT(__stdcall* Present)(IDXGISwapChain*, UINT, UINT);
Present oPresent = nullptr;

HRESULT __stdcall hkPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags) {
    __try {
        if (!g_imGuiInitialized) {
            if (SUCCEEDED(pSwapChain->GetDevice(__uuidof(ID3D11Device), (void**)&g_pd3dDevice))) {
                g_pd3dDevice->GetImmediateContext(&g_pd3dDeviceContext);
                g_pSwapChain = pSwapChain;
                DXGI_SWAP_CHAIN_DESC sd;
                pSwapChain->GetDesc(&sd);
                g_gameHwnd = sd.OutputWindow;

                if (!CreateRenderTarget()) {
                    LOG("[-] Failed to create render target view");
                    return oPresent(pSwapChain, SyncInterval, Flags);
                }

                IMGUI_CHECKVERSION();
                ImGui::CreateContext();
                ImGuiIO& io = ImGui::GetIO();
                io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

                if (!LoadFontFromDisk()) {
                    LOG("[-] Custom font not loaded, using default");
                    io.Fonts->AddFontDefault();
                }

                if (!io.Fonts->Build()) {
                    LOG("[-] Font atlas Build() failed, forcing default font");
                    io.Fonts->Clear();
                    io.Fonts->AddFontDefault();
                    io.Fonts->Build();
                }

                unsigned char* pixels;
                int width, height;
                io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
                if (pixels) {
                    LOG_FMT("[+] Font texture data generated: %dx%d", width, height);
                }
                else {
                    LOG("[-] GetTexDataAsRGBA32 returned null!");
                }

                SetupImGuiStyle();

                ImGui_ImplWin32_Init(g_gameHwnd);
                if (!ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext)) {
                    LOG("[-] ImGui DX11 init failed");
                }

                g_OriginalWndProc = (WNDPROC)SetWindowLongPtr(g_gameHwnd, GWLP_WNDPROC, (LONG_PTR)HookedWndProc);

                LoadConfig();

                g_imGuiInitialized = true;
                LOG("[+] ImGui and WndProc hook initialized");
            }
            else {
                LOG("[-] Failed to get D3D11 device");
                return oPresent(pSwapChain, SyncInterval, Flags);
            }
        }
        else {
            if (g_pSwapChain != pSwapChain) {
                g_pSwapChain = pSwapChain;
                CleanupRenderTarget();
                if (!CreateRenderTarget()) {
                    LOG("[-] Failed to recreate render target after swapchain change");
                    return oPresent(pSwapChain, SyncInterval, Flags);
                }
            }
            else {
                ID3D11Texture2D* pBackBuffer = nullptr;
                HRESULT hr = pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
                if (FAILED(hr) || !pBackBuffer) {
                    CleanupRenderTarget();
                    if (!CreateRenderTarget()) {
                        LOG("[-] Failed to recreate render target after backbuffer loss");
                        return oPresent(pSwapChain, SyncInterval, Flags);
                    }
                }
                else {
                    pBackBuffer->Release();
                }
            }
        }

        if (g_imGuiInitialized && g_mainRenderTargetView && g_pd3dDeviceContext) {
            DrawImGuiESP();
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        // Never crash
    }

    return oPresent(pSwapChain, SyncInterval, Flags);
}

// -------------------- WNDPROC --------------------
extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK HookedWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (g_imGuiInitialized) {
        ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
    }
    return CallWindowProc(g_OriginalWndProc, hWnd, msg, wParam, lParam);
}

// -------------------- MAIN THREAD --------------------
DWORD WINAPI MainLoop(LPVOID) {
    LOG("[*] Main loop started");

    for (int i = 0; i < 50; i++) {
        Sleep(100);
        if (HMODULE h = GetModuleHandleA("client.dll")) {
            hClient = (uintptr_t)h;
            LOG_FMT("[+] client.dll found @ 0x%llX\n", hClient);
            break;
        }
    }
    if (!hClient) {
        LOG("[-] client.dll not found, exiting...");
        return 0;
    }

    bool init_hook = false;
    do {
        if (kiero::init(kiero::RenderType::D3D11) == kiero::Status::Success) {
            if (kiero::bind(8, (void**)&oPresent, hkPresent) == kiero::Status::Success) {
                init_hook = true;
                LOG("[+] D3D11 Present hook installed");
            }
        }
        Sleep(100);
    } while (!init_hook);

    static bool lastInsertState = false;
    while (true) {
        bool insertState = GetAsyncKeyState(VK_INSERT) & 0x8000;
        if (insertState && !lastInsertState) {
            g_config.showMenu = !g_config.showMenu;
        }
        lastInsertState = insertState;
        Sleep(16);
    }
}

// -------------------- DLL ENTRY --------------------
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_hModule = hModule;
        DisableThreadLibraryCalls(hModule);
        InitConsole();
        LOG("[+] rakhus-cs2-internal loaded");
        CreateThread(NULL, 0, MainLoop, NULL, 0, NULL);
    }
    return TRUE;
}