#pragma once
#include <cstddef>
#include <cstdint>

namespace O {
    // ---------- client.dll globals ----------
    constexpr std::ptrdiff_t dwGameEntitySystem = 0x2571230;
    constexpr std::ptrdiff_t dwLocalPlayerPawn = 0x23C6268;
    constexpr std::ptrdiff_t dwViewMatrix = 0x23CB830;
    constexpr std::ptrdiff_t dwViewAngles = 0x23DC308;

    // ---------- C_CSPlayerPawn ----------
    constexpr std::ptrdiff_t m_iHealth = 0x34C;
    constexpr std::ptrdiff_t m_lifeState = 0x354;
    constexpr std::ptrdiff_t m_iTeamNum = 0x3E7;
    constexpr std::ptrdiff_t m_pGameSceneNode = 0x330;
    constexpr std::ptrdiff_t m_vecOrigin = 0x80;      // CGameSceneNode::m_vecOrigin
    constexpr std::ptrdiff_t m_vecViewOffset = 0xE78; // C_BaseModelEntity
    constexpr std::ptrdiff_t m_angEyeAngles = 0x3350; // C_CSPlayerPawn
    constexpr std::ptrdiff_t m_pAimPunchServices = 0x14B8;
    constexpr std::ptrdiff_t m_hController = 0x13D0;  // CHandle<CBasePlayerController>

    // ---------- CCSPlayerController ----------
    constexpr std::ptrdiff_t m_iszPlayerName = 0x6F4;
    constexpr std::ptrdiff_t m_pDamageServices = 0x828;
    constexpr std::ptrdiff_t m_hPlayerPawn = 0x914;   // in CCSPlayerController

    // ---------- Observer Services ----------
    namespace Observer {
        constexpr std::ptrdiff_t m_hObserverTarget = 0x4C;
    }

    // ---------- Damage Services ----------
    namespace Damage {
        constexpr std::ptrdiff_t m_DamageList = 0x48;  // CUtlVector<CDamageRecord>
    }

    // ---------- AimPunch Services ----------
    namespace AimPunch {
        constexpr std::ptrdiff_t m_predictableBaseAngle = 0x50;
        constexpr std::ptrdiff_t m_predictableBaseAngleVel = 0x5C;
        constexpr std::ptrdiff_t m_unpredictableBaseAngle = 0xA4;
    }

    // ---------- NoFlash ----------
    constexpr std::ptrdiff_t m_flFlashBangTime = 0x1414;
    constexpr std::ptrdiff_t m_flFlashOverlayAlpha = 0x141C;
    constexpr std::ptrdiff_t m_flFlashMaxAlpha = 0x1424;
    constexpr std::ptrdiff_t m_flFlashDuration = 0x1428;

    // ---------- NoSmoke ----------
    constexpr std::ptrdiff_t m_bSmokeEffectSpawned = 0x12C1;
    constexpr std::ptrdiff_t m_bDidSmokeEffect = 0x127C;
    constexpr std::ptrdiff_t m_nSmokeEffectTickBegin = 0x1278;

    // ---------- Entity list helpers ----------
    constexpr std::ptrdiff_t kListOffset = 0x10;
    constexpr std::ptrdiff_t kStride = 0x70;
    constexpr int            kChunk = 512;

    // ---------- Other ----------
    constexpr std::ptrdiff_t m_pObserverServices = 0x1220;
    constexpr std::ptrdiff_t m_flLastSmokeOverlayAlpha = 0x1448;
    constexpr std::ptrdiff_t m_flLastSmokeAge = 0x144C;
    constexpr std::ptrdiff_t m_vLastSmokeOverlayColor = 0x1450;

    constexpr std::ptrdiff_t m_vecVelocity = 0x430;   // CNetworkVelocityVector
    constexpr std::ptrdiff_t m_fFlags = 0x3F4;        // uint32

    // ---------- Force commands (buttons) ----------
    constexpr std::ptrdiff_t jump = 0x20B3E00;
    constexpr std::ptrdiff_t left = 0x20B3C50;
    constexpr std::ptrdiff_t right = 0x20B3CE0;
}