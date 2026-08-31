#pragma once
// Runtime pattern scan + RIP-relative resolve
// Patterns from cs2-sdk dump (build 14178)

#include <Windows.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <cstdlib>
#include <Psapi.h>

namespace Pat {
    // ---- client.dll patterns (IDA-style) from SDK ----
    inline constexpr const char* pViewMatrix =
        "48 8D 0D ? ? ? ? 48 C1 E0 06";
    inline constexpr const char* pGameEntitySystem =
        "48 8B 1D ? ? ? ? 48 89 1D ? ? ? ?";
    inline constexpr const char* pLocalPlayerController =
        "48 8B 05 ? ? ? ? 41 89 BE";
    inline constexpr const char* pGlowManager =
        "48 8B 05 ? ? ? ? C3 CC CC CC CC CC CC CC CC 8B 41";
    inline constexpr const char* UpdateGlobalVars =
        "48 8B 0D ? ? ? ? 4C 8D 05 ? ? ? ? 48 85 D2 48 8D 05";
    inline constexpr const char* pCSGOInput =
        "48 8B 0D ? ? ? ? 4C 8B C6 8B 10 E8";
    inline constexpr const char* DrawSmokeArray =
        "48 89 54 24 10 55 41 54 48 8D AC 24 38 F9 FF FF";
    inline constexpr const char* DrawSmokeVertex =
        "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 41 56 41 57 48 83 EC 40 48 8B 9C 24 88 00 00 00 4D 8B F8 48 8B FA 48 8B";
    inline constexpr const char* ApplyEconCustomization =
        "48 89 5C 24 08 57 48 83 EC 20 8B FA 48 8B D9 E8 ? ? ? ? 48 8B CB E8 ? ? ? ? 48 85 C0 74";
    inline constexpr const char* AnimGraphRebuild =
        "40 55 57 48 83 EC 28 4C 89 74 24 58 48 8B F9 80";
    inline constexpr const char* FlashOverlay =
        "85 D2 0F 88 ? ? ? ? 48 89 4C 24 08 55 53 41";
    inline constexpr const char* GetViewAngles =
        "4C 8B C1 85 D2 74 08 48 8D 05 ? ? ? ? C3 8B";
    inline constexpr const char* ThirdPersonReset =
        "48 8B 40 08 44 38 38 75 10 44 88 7F 01 44 89 BF";
    inline constexpr const char* OverrideView =
        "40 57 48 83 EC 60 48 8B FA E8 ? ? ? ? BA FF";
    inline constexpr const char* SetViewAngles =
        "85 D2 75 3D 48 63 81 50 0B 00 00 F2 41 0F 10 00";

    struct ModuleInfo {
        uintptr_t base = 0;
        size_t    size = 0;
    };

    inline ModuleInfo GetModuleInfo(const char* name) {
        ModuleInfo mi{};
        HMODULE mod = GetModuleHandleA(name);
        if (!mod) return mi;
        MODULEINFO info{};
        if (!GetModuleInformation(GetCurrentProcess(), mod, &info, sizeof(info))) return mi;
        mi.base = (uintptr_t)info.lpBaseOfDll;
        mi.size = (size_t)info.SizeOfImage;
        return mi;
    }

    inline bool ParsePattern(const char* pattern, std::vector<int>& out) {
        out.clear();
        const char* p = pattern;
        while (*p) {
            while (*p == ' ') ++p;
            if (!*p) break;
            if (*p == '?') {
                out.push_back(-1);
                ++p;
                if (*p == '?') ++p;
            } else {
                char* end = nullptr;
                long v = strtol(p, &end, 16);
                if (end == p) return false;
                out.push_back((int)(v & 0xFF));
                p = end;
            }
        }
        return !out.empty();
    }

    inline uintptr_t FindPattern(uintptr_t base, size_t size, const char* pattern) {
        if (!base || !size || !pattern) return 0;
        std::vector<int> bytes;
        if (!ParsePattern(pattern, bytes)) return 0;
        const size_t len = bytes.size();
        if (len == 0 || len > size) return 0;
        const uint8_t* data = (const uint8_t*)base;
        for (size_t i = 0; i + len <= size; ++i) {
            bool ok = true;
            for (size_t j = 0; j < len; ++j) {
                if (bytes[j] >= 0 && data[i + j] != (uint8_t)bytes[j]) {
                    ok = false;
                    break;
                }
            }
            if (ok) return base + i;
        }
        return 0;
    }

    inline uintptr_t FindInModule(const char* module, const char* pattern) {
        auto mi = GetModuleInfo(module);
        return FindPattern(mi.base, mi.size, pattern);
    }

    // instruction: 48 8B/8D 05/0D xx xx xx xx  → disp at +3, size 7
    inline uintptr_t ResolveRip(uintptr_t instr, int dispOffset = 3, int instrSize = 7) {
        if (!instr) return 0;
        __try {
            int32_t rel = *(int32_t*)(instr + dispOffset);
            return instr + instrSize + rel;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    }

    // Resolved runtime addresses (filled by ResolveAll)
    struct Resolved {
        uintptr_t viewMatrix = 0;          // direct address of float[16]
        uintptr_t gameEntitySystemPtr = 0; // address OF the pointer (deref each frame)
        uintptr_t localPlayerControllerPtr = 0;
        uintptr_t globalVarsPtr = 0;       // address OF the pointer
        uintptr_t glowManagerPtr = 0;
        uintptr_t csgoInputPtr = 0;
        uintptr_t drawSmokeArray = 0;      // function
        uintptr_t drawSmokeVertex = 0;
        uintptr_t applyEconCustomization = 0;
        uintptr_t animGraphRebuild = 0;
        uintptr_t flashOverlay = 0;
        uintptr_t getViewAngles = 0;
        uintptr_t setViewAngles = 0;
        uintptr_t thirdPersonReset = 0;
        uintptr_t overrideView = 0;
        bool ok = false;
    };

    inline Resolved g_res{};

    inline bool ResolveAll() {
        g_res = {};
        auto client = GetModuleInfo("client.dll");
        if (!client.base) return false;

        auto scan = [&](const char* pat) {
            return FindPattern(client.base, client.size, pat);
        };

        // View matrix: LEA → absolute address of matrix
        if (uintptr_t m = scan(pViewMatrix))
            g_res.viewMatrix = ResolveRip(m);

        // Game entity system: MOV reg, [rip+disp] → slot holding pointer
        if (uintptr_t m = scan(pGameEntitySystem))
            g_res.gameEntitySystemPtr = ResolveRip(m);

        if (uintptr_t m = scan(pLocalPlayerController))
            g_res.localPlayerControllerPtr = ResolveRip(m);

        // Global vars pointer inside UpdateGlobalVars
        if (uintptr_t m = scan(UpdateGlobalVars))
            g_res.globalVarsPtr = ResolveRip(m);

        if (uintptr_t m = scan(pGlowManager))
            g_res.glowManagerPtr = ResolveRip(m);

        if (uintptr_t m = scan(pCSGOInput))
            g_res.csgoInputPtr = ResolveRip(m);

        g_res.drawSmokeArray = scan(DrawSmokeArray);
        g_res.drawSmokeVertex = scan(DrawSmokeVertex);
        g_res.applyEconCustomization = scan(ApplyEconCustomization);
        g_res.animGraphRebuild = scan(AnimGraphRebuild);
        g_res.flashOverlay = scan(FlashOverlay);
        g_res.getViewAngles = scan(GetViewAngles);
        g_res.setViewAngles = scan(SetViewAngles);
        g_res.thirdPersonReset = scan(ThirdPersonReset);
        g_res.overrideView = scan(OverrideView);

        g_res.ok = g_res.gameEntitySystemPtr && g_res.viewMatrix;
        return g_res.ok;
    }

    inline uintptr_t ReadPtr(uintptr_t slot) {
        if (!slot) return 0;
        __try { return *(uintptr_t*)slot; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    }
}
