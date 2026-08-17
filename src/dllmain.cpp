// ========================================================================
// Rakhus CS2 Internal Cheat – Optimized & Stable
// Features: ESP, Smooth Aim Assist, NoFlash, Configurable ImGui Menu
// Dependencies: ImGui, Kiero, D3D11, Windows SDK
// ========================================================================

#include "pch.h"
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

// -------------------- HEAD POSITION OFFSETS --------------------
#define HEAD_OFFSET 2.5f           // Vertical offset above view offset
#define HEAD_FORWARD_OFFSET 5.0f   // Forward offset in enemy's facing direction

// -------------------- CONSOLE INITIALIZATION --------------------
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

// -------------------- GLOBAL VARIABLES --------------------
static uintptr_t g_pES = 0;
static uintptr_t hClient = 0;
static bool g_bRunning = false;
HMODULE g_hModule = nullptr;
HWND g_gameHwnd = nullptr;

ID3D11Device* g_pd3dDevice = nullptr;
ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
IDXGISwapChain* g_pSwapChain = nullptr;
ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;
bool g_imGuiInitialized = false;

WNDPROC g_OriginalWndProc = nullptr;

// -------------------- AIM SMOOTHING STATE --------------------
struct Vector3 { float x, y, z; };
struct Vector2 { float x, y; };

static Vector3 g_targetAngles = { 0, 0, 0 };
static bool g_hasTarget = false;

// -------------------- CONFIGURATION --------------------
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
    bool showDistance = true;
} g_config;

// -------------------- CONFIG FILE I/O --------------------
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
    if (!file.is_open()) {
        LOG("[-] Failed to save config");
        return;
    }
    file << "enabled=" << (g_config.enabled ? 1 : 0) << "\n";
    file << "aimRadius=" << g_config.aimRadius << "\n";
    file << "aimKey=" << g_config.aimKey << "\n";
    file << "espColorR=" << g_config.espColorR << "\n";
    file << "espColorG=" << g_config.espColorG << "\n";
    file << "espColorB=" << g_config.espColorB << "\n";
    file << "smoothness=" << g_config.smoothness << "\n";
    file << "noFlash=" << (g_config.noFlash ? 1 : 0) << "\n";
    file << "showDistance=" << (g_config.showDistance ? 1 : 0) << "\n";
    file.close();
    LOG("[+] Config saved");
}

void LoadConfig() {
    std::string path = GetConfigPath();
    std::ifstream file(path);
    if (!file.is_open()) {
        LOG("[-] No config file found, creating default...");
        SaveConfig();
        return;
    }
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
        else if (key == "showDistance") g_config.showDistance = (std::stoi(value) != 0);
    }
    file.close();
    LOG("[+] Config loaded");
}

// -------------------- HELPER FUNCTIONS --------------------
static bool IsValid(uintptr_t a) {
    // Basic range check – does not guarantee the memory is readable,
    // but avoids obviously invalid pointers.
    return a > 0x10000 && a < 0x7FFFFFFFFFFF;
}

static uintptr_t GetEntity(int idx) {
    if (idx < 0 || !g_pES) return 0;
    uintptr_t chunk = *(uintptr_t*)(g_pES + O::kListOffset + (idx / O::kChunk) * 8);
    if (!IsValid(chunk)) return 0;
    uintptr_t identity = chunk + (idx % O::kChunk) * O::kStride;
    if (!IsValid(identity)) return 0;
    uintptr_t ent = *(uintptr_t*)(identity);
    return IsValid(ent) ? ent : 0;
}

static uintptr_t HandleToEnt(uint32_t h) {
    if (!h || h == 0xFFFFFFFF) return 0;
    return GetEntity(h & 0x7FFF);
}

static int HP(uintptr_t e) {
    if (!IsValid(e)) return 0;
    return *(int*)(e + O::m_iHealth);
}
static int Team(uintptr_t e) {
    if (!IsValid(e)) return 0;
    return *(uint8_t*)(e + O::m_iTeamNum);
}
static int Life(uintptr_t e) {
    if (!IsValid(e)) return 0;
    return *(uint8_t*)(e + O::m_lifeState);
}

static Vector3 GetOrigin(uintptr_t pawn) {
    Vector3 org{ 0,0,0 };
    if (!IsValid(pawn)) return org;
    uintptr_t sceneNode = *(uintptr_t*)(pawn + O::m_pGameSceneNode);
    if (!IsValid(sceneNode)) return org;
    org.x = *(float*)(sceneNode + O::m_vecOrigin);
    org.y = *(float*)(sceneNode + O::m_vecOrigin + 4);
    org.z = *(float*)(sceneNode + O::m_vecOrigin + 8);
    return org;
}

static Vector3 GetViewOffset(uintptr_t pawn) {
    Vector3 off{ 0,0,0 };
    if (!IsValid(pawn)) return off;
    off.x = *(float*)(pawn + O::m_vecViewOffset);
    off.y = *(float*)(pawn + O::m_vecViewOffset + 4);
    off.z = *(float*)(pawn + O::m_vecViewOffset + 8);
    return off;
}

static Vector3 GetEyeAngles(uintptr_t pawn) {
    Vector3 angles{ 0,0,0 };
    if (!IsValid(pawn)) return angles;
    angles.x = *(float*)(pawn + O::m_angEyeAngles);
    angles.y = *(float*)(pawn + O::m_angEyeAngles + 4);
    angles.z = *(float*)(pawn + O::m_angEyeAngles + 8);
    return angles;
}

static float viewMatrix[16];

bool WorldToScreen(const Vector3& world, Vector2& screen, int screenW, int screenH) {
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

// -------------------- NOFLASH --------------------
void DoNoFlash(uintptr_t localPawn) {
    if (!g_config.noFlash || !localPawn) return;
    // Hardcoded offsets – update if game changes
    float* flashOverlayAlpha = (float*)(localPawn + 0x141C);
    float* flashMaxAlpha = (float*)(localPawn + 0x1424);
    float* flashDuration = (float*)(localPawn + 0x1428);
    float* flashBangTime = (float*)(localPawn + 0x1414);
    *flashOverlayAlpha = 0.0f;
    *flashMaxAlpha = 0.0f;
    *flashDuration = 0.0f;
    *flashBangTime = 0.0f;
}

// -------------------- WNDPROC HOOK --------------------
extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT CALLBACK HookedWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (g_imGuiInitialized) {
        ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
    }
    return CallWindowProc(g_OriginalWndProc, hWnd, msg, wParam, lParam);
}

// -------------------- D3D11 RENDER TARGET MANAGEMENT --------------------
bool CreateRenderTarget() {
    if (!g_pSwapChain) return false;
    ID3D11Texture2D* pBackBuffer = nullptr;
    HRESULT hr = g_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
    if (FAILED(hr) || !pBackBuffer) return false;

    // Release old render target view before creating a new one
    if (g_mainRenderTargetView) {
        g_mainRenderTargetView->Release();
        g_mainRenderTargetView = nullptr;
    }

    hr = g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
    return SUCCEEDED(hr);
}

void CleanupRenderTarget() {
    if (g_mainRenderTargetView) {
        g_mainRenderTargetView->Release();
        g_mainRenderTargetView = nullptr;
    }
}

// -------------------- AIM ASSIST (OPTIMIZED) --------------------
bool IsKeyDown(int key) {
    return (GetAsyncKeyState(key) & 0x8000) != 0;
}

// Calculates the angles needed to look from source to target
static Vector3 CalculateAngles(const Vector3& source, const Vector3& target) {
    Vector3 delta = { target.x - source.x, target.y - source.y, target.z - source.z };
    float dist = sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
    if (dist < 1.0f) return { 0, 0, 0 };

    Vector3 angles;
    angles.x = -atan2(delta.z, sqrt(delta.x * delta.x + delta.y * delta.y)) * (180.0f / 3.14159265359f);
    angles.y = atan2(delta.y, delta.x) * (180.0f / 3.14159265359f);
    angles.z = 0.0f;
    return angles;
}

// Normalizes angles to the range [-180, 180] for pitch and yaw
static void NormalizeAngles(Vector3& angles) {
    if (angles.x > 89.0f) angles.x = 89.0f;
    if (angles.x < -89.0f) angles.x = -89.0f;
    while (angles.y > 180.0f) angles.y -= 360.0f;
    while (angles.y < -180.0f) angles.y += 360.0f;
    angles.z = 0.0f;
}

void DoAimAssist(uintptr_t localPawn, int localTeam, int screenW, int screenH) {
    if (!g_config.enabled || !localPawn) return;

    Vector3* viewAngles = (Vector3*)(hClient + O::dwViewAngles);
    if (!viewAngles) return;

    bool keyDown = IsKeyDown(g_config.aimKey);
    if (!keyDown) {
        g_hasTarget = false;
        return;
    }

    // Local player's eye position
    Vector3 localEye = GetOrigin(localPawn);
    Vector3 viewOff = GetViewOffset(localPawn);
    localEye.z += viewOff.z;

    // Find the best target within the aim radius
    float bestDist = g_config.aimRadius + 1.0f;
    uintptr_t bestTarget = 0;
    Vector3 bestHead = { 0, 0, 0 };

    for (int i = 1; i <= 64; i++) {
        uintptr_t pCtrl = GetEntity(i);
        if (!IsValid(pCtrl)) continue;
        uint32_t hPawn = *(uint32_t*)(pCtrl + O::m_hPlayerPawn);
        if (!hPawn || hPawn == 0xFFFFFFFF) continue;
        uintptr_t pPawn = HandleToEnt(hPawn);
        if (!IsValid(pPawn) || pPawn == localPawn) continue;

        int hp = HP(pPawn);
        int team = Team(pPawn);
        int life = Life(pPawn);
        if (hp <= 0 || hp > 100) continue;
        if (life != 0) continue;
        if (team != 2 && team != 3) continue;
        if (team == localTeam) continue;

        // Calculate head position with forward offset
        Vector3 head = GetOrigin(pPawn);
        Vector3 off = GetViewOffset(pPawn);
        head.z += off.z + HEAD_OFFSET;

        Vector3 eyeAngles = GetEyeAngles(pPawn);
        float yaw = eyeAngles.y * (3.14159265359f / 180.0f);
        Vector3 forward = { cosf(yaw), sinf(yaw), 0.0f };
        head.x += forward.x * HEAD_FORWARD_OFFSET;
        head.y += forward.y * HEAD_FORWARD_OFFSET;

        Vector2 screenHead;
        if (!WorldToScreen(head, screenHead, screenW, screenH)) continue;

        float dx = screenHead.x - screenW / 2.0f;
        float dy = screenHead.y - screenH / 2.0f;
        float dist = sqrt(dx * dx + dy * dy);
        if (dist <= g_config.aimRadius && dist < bestDist) {
            bestDist = dist;
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

    // Smoothly interpolate towards target angles
    if (g_hasTarget) {
        float currentPitch = viewAngles->x;
        float currentYaw = viewAngles->y;

        float deltaPitch = g_targetAngles.x - currentPitch;
        float deltaYaw = g_targetAngles.y - currentYaw;
        if (deltaYaw > 180) deltaYaw -= 360;
        if (deltaYaw < -180) deltaYaw += 360;

        float smoothFactor = 1.0f - g_config.smoothness;
        // Snap if the difference is very small to avoid jitter
        if (fabs(deltaPitch) < 0.5f && fabs(deltaYaw) < 0.5f) {
            viewAngles->x = g_targetAngles.x;
            viewAngles->y = g_targetAngles.y;
        }
        else {
            viewAngles->x += deltaPitch * smoothFactor;
            viewAngles->y += deltaYaw * smoothFactor;
        }
        viewAngles->z = 0.0f;
    }
}

// -------------------- IMGUI STYLE --------------------
void SetupImGuiStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.ChildRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.WindowPadding = ImVec2(10, 10);
    style.FramePadding = ImVec2(6, 4);

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
    colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.10f, 0.13f, 0.95f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.10f, 0.10f, 0.13f, 0.50f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.10f, 0.10f, 0.13f, 0.95f);
    colors[ImGuiCol_Border] = ImVec4(0.30f, 0.30f, 0.35f, 0.60f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.20f, 0.20f, 0.25f, 0.60f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.30f, 0.30f, 0.35f, 0.80f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.40f, 0.40f, 0.45f, 1.00f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.10f, 0.10f, 0.13f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.15f, 0.15f, 0.20f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.10f, 0.10f, 0.13f, 0.50f);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.10f, 0.10f, 0.13f, 1.00f);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.10f, 0.10f, 0.13f, 0.50f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.30f, 0.30f, 0.35f, 0.80f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.40f, 0.40f, 0.45f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.50f, 0.50f, 0.55f, 1.00f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.20f, 0.60f, 1.00f, 1.00f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.20f, 0.60f, 1.00f, 0.80f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.20f, 0.60f, 1.00f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.20f, 0.30f, 0.60f, 0.70f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.30f, 0.40f, 0.70f, 0.90f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.10f, 0.20f, 0.50f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.20f, 0.30f, 0.60f, 0.60f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.30f, 0.40f, 0.70f, 0.80f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.10f, 0.20f, 0.50f, 0.90f);
    colors[ImGuiCol_Separator] = ImVec4(0.30f, 0.30f, 0.35f, 0.60f);
    colors[ImGuiCol_SeparatorHovered] = ImVec4(0.40f, 0.40f, 0.45f, 1.00f);
    colors[ImGuiCol_SeparatorActive] = ImVec4(0.50f, 0.50f, 0.55f, 1.00f);
    colors[ImGuiCol_ResizeGrip] = ImVec4(0.30f, 0.30f, 0.35f, 0.60f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.40f, 0.40f, 0.45f, 1.00f);
    colors[ImGuiCol_ResizeGripActive] = ImVec4(0.50f, 0.50f, 0.55f, 1.00f);
    colors[ImGuiCol_Tab] = ImVec4(0.15f, 0.15f, 0.20f, 0.80f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.25f, 0.35f, 0.65f, 1.00f);
    colors[ImGuiCol_TabActive] = ImVec4(0.20f, 0.30f, 0.60f, 1.00f);
    colors[ImGuiCol_TabUnfocused] = ImVec4(0.10f, 0.10f, 0.13f, 0.80f);
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.15f, 0.15f, 0.20f, 1.00f);
    colors[ImGuiCol_PlotLines] = ImVec4(0.20f, 0.60f, 1.00f, 1.00f);
    colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.80f, 0.40f, 0.00f, 1.00f);
    colors[ImGuiCol_PlotHistogram] = ImVec4(0.20f, 0.60f, 1.00f, 1.00f);
    colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.80f, 0.40f, 0.00f, 1.00f);
    colors[ImGuiCol_TextSelectedBg] = ImVec4(0.20f, 0.60f, 1.00f, 0.35f);
    colors[ImGuiCol_DragDropTarget] = ImVec4(0.20f, 0.60f, 1.00f, 0.90f);
    colors[ImGuiCol_NavHighlight] = ImVec4(0.20f, 0.60f, 1.00f, 1.00f);
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(0.20f, 0.60f, 1.00f, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.10f, 0.10f, 0.13f, 0.50f);
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.10f, 0.10f, 0.13f, 0.50f);
}

// -------------------- ESP + OVERLAY RENDERING (OPTIMIZED) --------------------
void DrawImGuiESP() {
    if (!g_imGuiInitialized) return;

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    // Update global view matrix and entity system each frame
    memcpy(viewMatrix, (void*)(hClient + O::dwViewMatrix), sizeof(viewMatrix));
    g_pES = *(uintptr_t*)(hClient + O::dwGameEntitySystem);
    uintptr_t pLocal = 0;
    if (IsValid(g_pES)) {
        pLocal = *(uintptr_t*)(hClient + O::dwLocalPlayerPawn);
    }

    // Only render if we have a valid local player
    if (IsValid(pLocal)) {
        int localTeam = Team(pLocal);
        if (localTeam != 0) {
            int screenW = (int)ImGui::GetIO().DisplaySize.x;
            int screenH = (int)ImGui::GetIO().DisplaySize.y;

            DoNoFlash(pLocal);

            ImDrawList* draw = ImGui::GetForegroundDrawList();
            if (draw) {
                // Gather enemies
                uintptr_t enemies[64];
                int enemyCount = 0;

                for (int i = 1; i <= 64; i++) {
                    uintptr_t pCtrl = GetEntity(i);
                    if (!IsValid(pCtrl)) continue;
                    uint32_t hPawn = *(uint32_t*)(pCtrl + O::m_hPlayerPawn);
                    if (!hPawn || hPawn == 0xFFFFFFFF) continue;
                    uintptr_t pPawn = HandleToEnt(hPawn);
                    if (!IsValid(pPawn) || pPawn == pLocal) continue;

                    int hp = HP(pPawn);
                    int team = Team(pPawn);
                    int life = Life(pPawn);
                    if (hp <= 0 || hp > 100) continue;
                    if (life != 0) continue;
                    if (team != 2 && team != 3) continue;
                    if (team == localTeam) continue;

                    if (enemyCount < 64) {
                        enemies[enemyCount++] = pPawn;
                    }
                }

                // Draw ESP for each enemy
                for (int idx = 0; idx < enemyCount; idx++) {
                    uintptr_t pPawn = enemies[idx];
                    int hp = HP(pPawn);
                    Vector3 origin = GetOrigin(pPawn);
                    Vector3 viewOffset = GetViewOffset(pPawn);

                    // Head position with offset
                    Vector3 headPos = origin;
                    headPos.z += viewOffset.z + HEAD_OFFSET;
                    Vector3 eyeAngles = GetEyeAngles(pPawn);
                    float yaw = eyeAngles.y * (3.14159265359f / 180.0f);
                    Vector3 forward = { cosf(yaw), sinf(yaw), 0.0f };
                    headPos.x += forward.x * HEAD_FORWARD_OFFSET;
                    headPos.y += forward.y * HEAD_FORWARD_OFFSET;

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

                    ImU32 color = IM_COL32(
                        (int)(g_config.espColorR * 255),
                        (int)(g_config.espColorG * 255),
                        (int)(g_config.espColorB * 255),
                        255
                    );
                    ImU32 colorBg = IM_COL32(0, 0, 0, 180);

                    // Box
                    draw->AddRect(ImVec2(x, y), ImVec2(x + boxWidth, y + boxHeight), color, 0.0f, 0, 2.0f);

                    // Health bar
                    float barWidth = 5.0f;
                    float barX = x - barWidth - 4.0f;
                    draw->AddRectFilled(ImVec2(barX, y + 1), ImVec2(barX + barWidth, y + boxHeight - 1), colorBg);
                    float fillHeight = boxHeight - 2.0f;
                    if (hp > 0) {
                        float hpPercent = hp / 100.0f;
                        float fill = fillHeight * hpPercent;
                        ImU32 hpColor = IM_COL32(255 - (int)(255 * hpPercent), (int)(255 * hpPercent), 0, 255);
                        draw->AddRectFilled(ImVec2(barX, y + 1 + fillHeight - fill), ImVec2(barX + barWidth, y + boxHeight - 1), hpColor);
                    }

                    // Head dot
                    draw->AddCircleFilled(ImVec2(screenHead.x, screenHead.y), 5.0f, IM_COL32(255, 255, 255, 255));
                    draw->AddCircle(ImVec2(screenHead.x, screenHead.y), 5.0f, IM_COL32(0, 0, 0, 200), 12, 1.5f);

                    // HP text
                    char text[16];
                    sprintf_s(text, "%d HP", hp);
                    draw->AddText(ImVec2(x, y + boxHeight + 3), IM_COL32(255, 255, 255, 255), text);

                    // Distance
                    if (g_config.showDistance) {
                        Vector3 localEye = GetOrigin(pLocal);
                        Vector3 viewOffLocal = GetViewOffset(pLocal);
                        localEye.z += viewOffLocal.z;
                        float dx3D = headPos.x - localEye.x;
                        float dy3D = headPos.y - localEye.y;
                        float dz3D = headPos.z - localEye.z;
                        float dist3D = sqrt(dx3D * dx3D + dy3D * dy3D + dz3D * dz3D);
                        float distMeters = dist3D * 0.01905f;
                        char distText[32];
                        sprintf_s(distText, "%.0fm", distMeters);
                        draw->AddText(ImVec2(x, y + boxHeight + 3 + 15), IM_COL32(255, 255, 255, 200), distText);
                    }
                }

                // Aim assist (already optimized)
                DoAimAssist(pLocal, localTeam, screenW, screenH);
            }
        }
    }

    // ImGui menu
    if (g_config.showMenu) {
        ImGui::SetNextWindowSize(ImVec2(450, 340), ImGuiCond_FirstUseEver);
        ImGui::Begin("rakhus-cs2-internal - Settings", &g_config.showMenu);

        ImGui::Checkbox("Enable Cheat", &g_config.enabled);
        ImGui::Checkbox("NoFlash", &g_config.noFlash);
        ImGui::Checkbox("Show Distance", &g_config.showDistance);

        ImGui::Separator();
        ImGui::SliderFloat("Aim Radius", &g_config.aimRadius, 0.0f, 100.0f, "%.1f px");
        ImGui::SliderFloat("Smoothness", &g_config.smoothness, 0.0f, 1.0f, "%.2f");

        static bool bindingActive = false;
        if (bindingActive) {
            ImGui::Text("Press any key or mouse button...");
            for (int key = 0x01; key <= 0xFF; key++) {
                if (GetAsyncKeyState(key) & 0x8000) {
                    g_config.aimKey = key;
                    bindingActive = false;
                    SaveConfig();
                    break;
                }
            }
            if (GetAsyncKeyState(VK_RBUTTON) & 0x8000) {
                g_config.aimKey = VK_RBUTTON;
                bindingActive = false;
                SaveConfig();
            }
            if (GetAsyncKeyState(VK_MBUTTON) & 0x8000) {
                g_config.aimKey = VK_MBUTTON;
                bindingActive = false;
                SaveConfig();
            }
            if (GetAsyncKeyState(VK_XBUTTON1) & 0x8000) {
                g_config.aimKey = VK_XBUTTON1;
                bindingActive = false;
                SaveConfig();
            }
            if (GetAsyncKeyState(VK_XBUTTON2) & 0x8000) {
                g_config.aimKey = VK_XBUTTON2;
                bindingActive = false;
                SaveConfig();
            }
        }
        else {
            char keyName[64];
            if (g_config.aimKey >= 0x01 && g_config.aimKey <= 0x07) {
                const char* mouseNames[] = { "Left", "Right", "Middle", "X1", "X2" };
                int idx = g_config.aimKey - 1;
                if (idx >= 0 && idx < 5) sprintf_s(keyName, "Mouse %s", mouseNames[idx]);
                else sprintf_s(keyName, "Mouse %d", g_config.aimKey);
            }
            else {
                UINT scanCode = MapVirtualKey(g_config.aimKey, MAPVK_VK_TO_VSC);
                char name[64];
                if (GetKeyNameTextA(scanCode << 16, name, sizeof(name)) > 0) {
                    sprintf_s(keyName, "%s", name);
                }
                else {
                    sprintf_s(keyName, "VK_%02X", g_config.aimKey);
                }
            }
            ImGui::Text("Aim Key: %s", keyName);
            if (ImGui::Button("Change Key")) {
                bindingActive = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset to F1")) {
                g_config.aimKey = VK_F1;
                SaveConfig();
            }
        }

        ImGui::ColorEdit3("ESP Color", &g_config.espColorR);

        if (ImGui::Button("Save Config")) {
            SaveConfig();
        }
        ImGui::SameLine();
        if (ImGui::Button("Load Config")) {
            LoadConfig();
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset Defaults")) {
            g_config.aimRadius = AIM_RADIUS_DEFAULT;
            g_config.aimKey = AIM_KEY_DEFAULT;
            g_config.espColorR = 0.0f;
            g_config.espColorG = 0.75f;
            g_config.espColorB = 1.0f;
            g_config.smoothness = 0.8f;
            g_config.noFlash = false;
            g_config.showDistance = true;
            SaveConfig();
        }

        ImGui::End();
    }
    else {
        // Small hint when menu is closed
        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::Begin("MenuHint", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground);
        ImGui::Text("Press INSERT to open menu");
        ImGui::End();
    }

    ImGui::EndFrame();
    ImGui::Render();

    // Ensure we have a valid render target before rendering ImGui
    if (g_mainRenderTargetView) {
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }
}

// -------------------- D3D11 PRESENT HOOK --------------------
typedef HRESULT(__stdcall* Present) (IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
Present oPresent = nullptr;

HRESULT __stdcall hkPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags) {
    // First call: initialize ImGui and hook WndProc
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

            SetupImGuiStyle();

            ImGui_ImplWin32_Init(g_gameHwnd);
            ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

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
        // Handle swapchain resizing or recreation
        if (g_pSwapChain != pSwapChain) {
            g_pSwapChain = pSwapChain;
            CleanupRenderTarget();
            if (!CreateRenderTarget()) {
                LOG("[-] Failed to recreate render target after swapchain change");
                return oPresent(pSwapChain, SyncInterval, Flags);
            }
        }
        else {
            // Check if the backbuffer size changed (e.g., resolution change)
            DXGI_SWAP_CHAIN_DESC sd;
            pSwapChain->GetDesc(&sd);
            // We could compare dimensions with previous, but for simplicity we recreate if any error occurs.
            // A more robust method: try to get the backbuffer, if fails, recreate.
            ID3D11Texture2D* pBackBuffer = nullptr;
            HRESULT hr = pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
            if (FAILED(hr) || !pBackBuffer) {
                // Backbuffer lost, recreate render target
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

    // Now render ImGui
    if (g_imGuiInitialized && g_mainRenderTargetView) {
        DrawImGuiESP();
    }

    return oPresent(pSwapChain, SyncInterval, Flags);
}

// -------------------- MAIN THREAD --------------------
DWORD WINAPI MainLoop(LPVOID) {
    LOG("[*] Main loop started");

    // Wait for client.dll to be loaded
    for (int i = 0; i < 50; i++) {
        Sleep(100);
        if (HMODULE h = GetModuleHandleA("client.dll")) {
            hClient = (uintptr_t)h;
            g_bRunning = true;
            LOG_FMT("[+] client.dll found @ 0x%llX\n", hClient);
            break;
        }
    }
    if (!g_bRunning) {
        LOG("[-] client.dll not found, exiting...");
        return 0;
    }

    // Hook D3D11 Present
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

    // Menu toggle loop (INSERT key)
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

// -------------------- DLL ENTRY POINT --------------------
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