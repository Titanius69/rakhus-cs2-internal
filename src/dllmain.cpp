// ========================================================================
// Rakhus CS2 Internal Cheat
// Features: ESP, Aim Assist, NoFlash, Configurable settings via ImGui
// Dependencies: ImGui, Kiero, D3D11, Windows SDK
// ========================================================================

#include "pch.h"
#include <Windows.h>
#include <Psapi.h>
#include <cstdint>
#include <cstdio>
#include <vector>
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
static uintptr_t g_pES = 0;           // Game Entity System pointer
static uintptr_t hClient = 0;         // client.dll base address
static bool g_bRunning = false;       // Indicates whether the main loop is active
static int  g_frame = 0;              // Frame counter (unused)
HMODULE g_hModule = nullptr;          // DLL module handle
HWND g_gameHwnd = nullptr;            // Game window handle

// D3D11 / ImGui resources
ID3D11Device* g_pd3dDevice = nullptr;
ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
IDXGISwapChain* g_pSwapChain = nullptr;
ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;
bool g_imGuiInitialized = false;

// WndProc hook for ImGui input
WNDPROC g_OriginalWndProc = nullptr;

// -------------------- CONFIGURATION DEFAULTS --------------------
#define AIM_KEY_DEFAULT VK_F1
#define AIM_RADIUS_DEFAULT 20.0f

struct Config {
    bool enabled = true;              // Master toggle for all features
    float aimRadius = AIM_RADIUS_DEFAULT;
    int aimKey = AIM_KEY_DEFAULT;
    float espColorR = 0.0f;
    float espColorG = 0.75f;
    float espColorB = 1.0f;
    bool showMenu = false;            // Whether the ImGui menu is visible
    float smoothness = 0.8f;          // Aim smoothing factor (1.0 = instant, 0.0 = no correction)
    bool noFlash = false;             // Disable flashbang effect
    bool showDistance = true;         // Show distance to enemy
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
// Basic validity check for pointers/addresses
static bool IsValid(uintptr_t a) { return a > 0x10000 && a < 0x7FFFFFFFFFFF; }

// Retrieve an entity's controller (CCSPlayerController) by index
static uintptr_t GetEntity(int idx) {
    if (idx < 0 || !g_pES) return 0;
    uintptr_t chunk = *(uintptr_t*)(g_pES + O::kListOffset + (idx / O::kChunk) * 8);
    if (!IsValid(chunk)) return 0;
    uintptr_t identity = chunk + (idx % O::kChunk) * O::kStride;
    if (!IsValid(identity)) return 0;
    uintptr_t ent = *(uintptr_t*)(identity);
    return IsValid(ent) ? ent : 0;
}

// Convert a handle (e.g., m_hPlayerPawn) to the actual pawn pointer
static uintptr_t HandleToEnt(uint32_t h) {
    if (!h || h == 0xFFFFFFFF) return 0;
    return GetEntity(h & 0x7FFF);
}

// Convenience getters for common entity properties
static int  HP(uintptr_t e) { return IsValid(e) ? *(int*)(e + O::m_iHealth) : 0; }
static int  Team(uintptr_t e) { return IsValid(e) ? *(uint8_t*)(e + O::m_iTeamNum) : 0; }
static int  Life(uintptr_t e) { return IsValid(e) ? *(uint8_t*)(e + O::m_lifeState) : 0; }

struct Vector3 { float x, y, z; };
struct Vector2 { float x, y; };

// Get the absolute origin (feet position) of a pawn
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

// Get the view offset (camera height) of a pawn
static Vector3 GetViewOffset(uintptr_t pawn) {
    Vector3 off{ 0,0,0 };
    if (!IsValid(pawn)) return off;
    off.x = *(float*)(pawn + O::m_vecViewOffset);
    off.y = *(float*)(pawn + O::m_vecViewOffset + 4);
    off.z = *(float*)(pawn + O::m_vecViewOffset + 8);
    return off;
}

static float viewMatrix[16];

// World-to-screen projection using the game's view matrix
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

// -------------------- NOFLASH FEATURE --------------------
// Resets all flashbang related values to zero, effectively nullifying the effect
void DoNoFlash(uintptr_t localPawn) {
    if (!g_config.noFlash || !localPawn) return;
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
// Forward declaration of ImGui's WndProc handler
extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT CALLBACK HookedWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (g_imGuiInitialized) {
        ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
    }
    return CallWindowProc(g_OriginalWndProc, hWnd, msg, wParam, lParam);
}

// -------------------- AIM ASSIST (Smooth Aim) --------------------
// Simple key state check (supports mouse buttons via virtual key codes)
bool IsKeyDown(int key) {
    if (key >= 0x01 && key <= 0x07) {
        return (GetAsyncKeyState(key) & 0x8000) != 0;
    }
    return (GetAsyncKeyState(key) & 0x8000) != 0;
}

// Main aim assist routine: finds the closest enemy to crosshair within the defined radius,
// and smoothly adjusts view angles toward the enemy's head when the aim key is held.
void DoAimAssist(uintptr_t localPawn, int localTeam, int screenW, int screenH) {
    if (!g_config.enabled || !localPawn) return;

    Vector3* viewAngles = (Vector3*)(hClient + O::dwViewAngles);
    if (!viewAngles) return;

    // Calculate local player's eye position
    Vector3 localEye = GetOrigin(localPawn);
    Vector3 viewOff = GetViewOffset(localPawn);
    localEye.z += viewOff.z;

    float bestDist = g_config.aimRadius + 1.0f;
    uintptr_t bestTarget = 0;
    Vector3 bestHead = { 0, 0, 0 };

    // Iterate over all possible player slots (max 64)
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
        if (hp <= 0 || hp > 100) continue;         // dead or invalid
        if (life != 0) continue;                   // alive check (0 = alive)
        if (team != 2 && team != 3) continue;      // not a player team (T=2, CT=3)
        if (team == localTeam) continue;           // skip teammates

        // Get head position (origin + view offset)
        Vector3 head = GetOrigin(pPawn);
        Vector3 off = GetViewOffset(pPawn);
        head.z += off.z;

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

    // If a valid target is found and the aim key is held, apply smooth aim correction
    if (bestTarget && IsKeyDown(g_config.aimKey)) {
        float dx = bestHead.x - localEye.x;
        float dy = bestHead.y - localEye.y;
        float dz = bestHead.z - localEye.z;
        float dist = sqrt(dx * dx + dy * dy + dz * dz);
        if (dist > 1.0f) {
            float targetPitch = -atan2(dz, sqrt(dx * dx + dy * dy)) * (180.0f / 3.14159265359f);
            float targetYaw = atan2(dy, dx) * (180.0f / 3.14159265359f);

            float currentPitch = viewAngles->x;
            float currentYaw = viewAngles->y;

            float deltaPitch = targetPitch - currentPitch;
            float deltaYaw = targetYaw - currentYaw;
            if (deltaYaw > 180) deltaYaw -= 360;
            if (deltaYaw < -180) deltaYaw += 360;

            // Smoothing: lower smoothness = more aggressive correction
            float smoothFactor = 1.0f - g_config.smoothness;
            viewAngles->x += deltaPitch * smoothFactor;
            viewAngles->y += deltaYaw * smoothFactor;
            viewAngles->z = 0.0f;   // roll is always zero
        }
    }
}

// -------------------- IMGUI STYLE SETUP --------------------
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

// -------------------- ESP + OVERLAY RENDERING --------------------
// This function is called every frame from the Present hook.
// It draws the ESP boxes, health bars, distance, and handles the ImGui menu.
void DrawImGuiESP() {
    if (!g_imGuiInitialized) return;

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    // Refresh view matrix and entity system pointers each frame
    memcpy(viewMatrix, (void*)(hClient + O::dwViewMatrix), sizeof(viewMatrix));
    g_pES = *(uintptr_t*)(hClient + O::dwGameEntitySystem);
    uintptr_t pLocal = 0;
    if (IsValid(g_pES)) {
        pLocal = *(uintptr_t*)(hClient + O::dwLocalPlayerPawn);
    }

    if (IsValid(pLocal)) {
        int localTeam = Team(pLocal);
        if (localTeam != 0) {
            int screenW = (int)ImGui::GetIO().DisplaySize.x;
            int screenH = (int)ImGui::GetIO().DisplaySize.y;

            // Apply NoFlash if enabled
            DoNoFlash(pLocal);

            ImDrawList* draw = ImGui::GetForegroundDrawList();
            if (draw) {
                // Collect all valid enemy pawns
                std::vector<uintptr_t> enemies;
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
                    enemies.push_back(pPawn);
                }

                // Draw ESP for each enemy
                for (uintptr_t pPawn : enemies) {
                    int hp = HP(pPawn);
                    Vector3 origin = GetOrigin(pPawn);
                    Vector3 viewOffset = GetViewOffset(pPawn);
                    Vector3 headPos = { origin.x, origin.y, origin.z + viewOffset.z };
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

                    // Main bounding box
                    draw->AddRect(ImVec2(x, y), ImVec2(x + boxWidth, y + boxHeight), color, 0.0f, 0, 2.0f);

                    // Health bar (vertical bar on the left)
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

                    // Head indicator (circle)
                    draw->AddCircleFilled(ImVec2(screenHead.x, screenHead.y), 5.0f, IM_COL32(255, 255, 255, 255));
                    draw->AddCircle(ImVec2(screenHead.x, screenHead.y), 5.0f, IM_COL32(0, 0, 0, 200), 12, 1.5f);

                    // Health text
                    char text[16];
                    sprintf_s(text, "%d HP", hp);
                    draw->AddText(ImVec2(x, y + boxHeight + 3), IM_COL32(255, 255, 255, 255), text);

                    // Distance text (if enabled)
                    if (g_config.showDistance) {
                        Vector3 localEye = GetOrigin(pLocal);
                        Vector3 viewOffLocal = GetViewOffset(pLocal);
                        localEye.z += viewOffLocal.z;
                        float dx3D = headPos.x - localEye.x;
                        float dy3D = headPos.y - localEye.y;
                        float dz3D = headPos.z - localEye.z;
                        float dist3D = sqrt(dx3D * dx3D + dy3D * dy3D + dz3D * dz3D);
                        float distMeters = dist3D * 0.01905f;  // approximate conversion to meters
                        char distText[32];
                        sprintf_s(distText, "%.0fm", distMeters);
                        draw->AddText(ImVec2(x, y + boxHeight + 3 + 15), IM_COL32(255, 255, 255, 200), distText);
                    }
                }

                // Perform aim assist after ESP drawing (uses same enemy list internally)
                DoAimAssist(pLocal, localTeam, screenW, screenH);
            }
        }
    }

    // ImGui menu (toggle with INSERT)
    if (g_config.showMenu) {
        ImGui::SetNextWindowSize(ImVec2(450, 340), ImGuiCond_FirstUseEver);
        ImGui::Begin("rakhus-cs2-internal - Settings", &g_config.showMenu);

        ImGui::Checkbox("Enable Cheat", &g_config.enabled);
        ImGui::Checkbox("NoFlash", &g_config.noFlash);
        ImGui::Checkbox("Show Distance", &g_config.showDistance);

        ImGui::Separator();
        ImGui::SliderFloat("Aim Radius", &g_config.aimRadius, 0.0f, 100.0f, "%.1f px");
        ImGui::SliderFloat("Smoothness", &g_config.smoothness, 0.0f, 1.0f, "%.2f");

        // Aim key binding logic
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

    // Finish ImGui frame and render
    ImGui::EndFrame();
    ImGui::Render();
    g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

// -------------------- D3D11 PRESENT HOOK --------------------
typedef HRESULT(__stdcall* Present) (IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
Present oPresent = nullptr;

HRESULT __stdcall hkPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags) {
    // Initialize ImGui on the first Present call
    if (!g_imGuiInitialized) {
        if (SUCCEEDED(pSwapChain->GetDevice(__uuidof(ID3D11Device), (void**)&g_pd3dDevice))) {
            g_pd3dDevice->GetImmediateContext(&g_pd3dDeviceContext);
            DXGI_SWAP_CHAIN_DESC sd;
            pSwapChain->GetDesc(&sd);
            g_gameHwnd = sd.OutputWindow;

            ID3D11Texture2D* pBackBuffer;
            pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
            g_pd3dDevice->CreateRenderTargetView(pBackBuffer, NULL, &g_mainRenderTargetView);
            pBackBuffer->Release();

            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            ImGuiIO& io = ImGui::GetIO();
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

            SetupImGuiStyle();

            ImGui_ImplWin32_Init(g_gameHwnd);
            ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

            // Hook window procedure for ImGui input
            g_OriginalWndProc = (WNDPROC)SetWindowLongPtr(g_gameHwnd, GWLP_WNDPROC, (LONG_PTR)HookedWndProc);

            LoadConfig();

            g_imGuiInitialized = true;
            LOG("[+] ImGui and WndProc hook initialized");
        }
    }

    // Render our overlay if ImGui is ready
    if (g_imGuiInitialized) {
        DrawImGuiESP();
    }

    // Call original Present
    return oPresent(pSwapChain, SyncInterval, Flags);
}

// -------------------- MAIN THREAD (ENTRY POINT) --------------------
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

    // Install D3D11 Present hook using Kiero
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

    // Main event loop: listen for INSERT key to toggle menu
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