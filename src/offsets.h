#pragma once
#include <cstddef>
#include <cstdint>

namespace O {
    // ---------- client.dll globals (build 14178) ----------
    constexpr std::ptrdiff_t dwGameEntitySystem = 0x2571220;
    constexpr std::ptrdiff_t dwLocalPlayerPawn = 0x23C6268;
    constexpr std::ptrdiff_t dwLocalPlayerController = 0x23A0F30;
    constexpr std::ptrdiff_t dwViewMatrix = 0x23CB830;
    constexpr std::ptrdiff_t dwViewAngles = 0x23DC2F8;
    constexpr std::ptrdiff_t dwCSGOInput = 0x23DBC70;
    constexpr std::ptrdiff_t dwEntityList = 0x2571220;
    constexpr std::ptrdiff_t dwGameEntitySystem_highestEntityIndex = 0x2090;
    constexpr std::ptrdiff_t dwGlobalVars = 0x20AF5F0;
    constexpr std::ptrdiff_t dwPlantedC4 = 0x2390A14; // pPlantedC4s from sdk 14178
    constexpr std::ptrdiff_t dwSensitivity = 0x23C3578;
    constexpr std::ptrdiff_t dwSensitivity_sensitivity = 0x58;
    constexpr std::ptrdiff_t dwGlowManager = 0x23C2A58;

    // CGlobalVarsBase (after deref of dwGlobalVars) – curtime candidates
    namespace GV {
        constexpr std::ptrdiff_t realtime = 0x0;
        constexpr std::ptrdiff_t framecount = 0x4;
        constexpr std::ptrdiff_t absoluteframetime = 0x8;
        constexpr std::ptrdiff_t curtime = 0x34;   // common Source2 layout; fallbacks in code
        constexpr std::ptrdiff_t curtime_alt = 0x30;
    }

    // Entity system listener list
    constexpr std::ptrdiff_t m_entityListeners = 0x30;

    // ---------- C_BaseEntity / C_CSPlayerPawn ----------
    constexpr std::ptrdiff_t m_iHealth = 0x34C;
    constexpr std::ptrdiff_t m_lifeState = 0x354;
    constexpr std::ptrdiff_t m_iTeamNum = 0x3E7;
    constexpr std::ptrdiff_t m_fFlags = 0x3F4;
    constexpr std::ptrdiff_t m_pGameSceneNode = 0x330;
    constexpr std::ptrdiff_t m_vecVelocity = 0x430;
    constexpr std::ptrdiff_t m_hOwnerEntity = 0x520;
    constexpr std::ptrdiff_t m_nSubclassID = 0x380;
    constexpr std::ptrdiff_t m_flSimulationTime = 0x3B8;
    constexpr std::ptrdiff_t m_hController = 0x13D0;
    constexpr std::ptrdiff_t m_pWeaponServices = 0x1208;
    constexpr std::ptrdiff_t m_pMovementServices = 0x1248;
    constexpr std::ptrdiff_t m_pObserverServices = 0x1220;
    constexpr std::ptrdiff_t m_pAimPunchServices = 0x14B8;
    constexpr std::ptrdiff_t m_vOldOrigin = 0x13B8;
    constexpr std::ptrdiff_t m_vecViewOffset = 0xE78;
    constexpr std::ptrdiff_t m_angEyeAngles = 0x3350;
    constexpr std::ptrdiff_t m_iIDEntIndex = 0x342C;
    constexpr std::ptrdiff_t m_bIsScoped = 0x1C78;
    constexpr std::ptrdiff_t m_iShotsFired = 0x1C8C;
    constexpr std::ptrdiff_t m_flVelocityModifier = 0x1C94;
    // C_CSWeaponBase
    constexpr std::ptrdiff_t m_fAccuracyPenalty = 0x17F0;
    constexpr std::ptrdiff_t m_ArmorValue = 0x1CA4;
    constexpr std::ptrdiff_t m_entitySpottedState = 0x1C60;
    constexpr std::ptrdiff_t m_Glow = 0xDE0; // CGlowProperty on C_BaseModelEntity

    namespace Glow {
        constexpr std::ptrdiff_t m_fGlowColor = 0x8;
        constexpr std::ptrdiff_t m_iGlowType = 0x30;
        constexpr std::ptrdiff_t m_iGlowTeam = 0x34;
        constexpr std::ptrdiff_t m_nGlowRange = 0x38;
        constexpr std::ptrdiff_t m_nGlowRangeMin = 0x3C;
        constexpr std::ptrdiff_t m_glowColorOverride = 0x40; // Color {r,g,b,a}
        constexpr std::ptrdiff_t m_bFlashing = 0x44;
        constexpr std::ptrdiff_t m_bEligibleForScreenHighlight = 0x50;
        constexpr std::ptrdiff_t m_bGlowing = 0x51;
    }

    constexpr std::ptrdiff_t m_vecOrigin = 0x80;
    constexpr std::ptrdiff_t m_vecAbsOrigin = 0xC8;
    constexpr std::ptrdiff_t m_bDormant = 0x103;
    constexpr std::ptrdiff_t m_modelState = 0x140;
    constexpr std::ptrdiff_t m_boneArray = 0x80;
    constexpr std::ptrdiff_t m_MeshGroupMask = 0x208; // inside CModelState

    namespace Spotted {
        constexpr std::ptrdiff_t m_bSpotted = 0x8;
        constexpr std::ptrdiff_t m_bSpottedByMask = 0xC;
    }

    constexpr std::ptrdiff_t m_iszPlayerName = 0x6F4;
    constexpr std::ptrdiff_t m_pDamageServices = 0x828;
    constexpr std::ptrdiff_t m_hPlayerPawn = 0x914;
    constexpr std::ptrdiff_t m_hObserverPawn = 0x918;
    constexpr std::ptrdiff_t m_bPawnIsAlive = 0x91C;
    constexpr std::ptrdiff_t m_iDesiredFOV = 0x78C;

    namespace WeaponServices {
        constexpr std::ptrdiff_t m_hMyWeapons = 0x48;
        constexpr std::ptrdiff_t m_hActiveWeapon = 0x60;
        constexpr std::ptrdiff_t m_hLastWeapon = 0x64;
        constexpr std::ptrdiff_t m_iAmmo = 0x68;
    }

    // Skin / econ (on C_EconEntity / weapon)
    constexpr std::ptrdiff_t m_AttributeManager = 0x11A8; // C_EconEntity (embedded, NOT a pointer)
    constexpr std::ptrdiff_t m_Item = 0x50;
    constexpr std::ptrdiff_t m_iItemDefinitionIndex = 0x1BA;
    constexpr std::ptrdiff_t m_iEntityQuality = 0x1BC;
    constexpr std::ptrdiff_t m_iItemIDHigh = 0x1D0;
    constexpr std::ptrdiff_t m_iItemIDLow = 0x1D4;
    constexpr std::ptrdiff_t m_bInitialized = 0x1E8;
    constexpr std::ptrdiff_t m_iAccountID = 0x1D8;
    constexpr std::ptrdiff_t m_iClip1 = 0x1700;
    constexpr std::ptrdiff_t m_szName = 0x720;
    constexpr std::ptrdiff_t m_OriginalOwnerXuidLow = 0x1678;
    constexpr std::ptrdiff_t m_OriginalOwnerXuidHigh = 0x167C;
    constexpr std::ptrdiff_t m_nFallbackPaintKit = 0x1680;
    constexpr std::ptrdiff_t m_nFallbackSeed = 0x1684;
    constexpr std::ptrdiff_t m_flFallbackWear = 0x1688;
    constexpr std::ptrdiff_t m_nFallbackStatTrak = 0x168C;

    namespace Observer {
        constexpr std::ptrdiff_t m_iObserverMode = 0x48;
        constexpr std::ptrdiff_t m_hObserverTarget = 0x4C;
    }

    namespace Damage {
        constexpr std::ptrdiff_t m_DamageList = 0x48;
    }

    namespace AimPunch {
        constexpr std::ptrdiff_t m_predictableBaseAngle = 0x50;
        constexpr std::ptrdiff_t m_predictableBaseAngleVel = 0x5C;
        constexpr std::ptrdiff_t m_unpredictableBaseAngle = 0xA4;
    }

    namespace C4 {
        constexpr std::ptrdiff_t m_bBombTicking = 0x11A0;
        constexpr std::ptrdiff_t m_nBombSite = 0x11A4;
        constexpr std::ptrdiff_t m_flC4Blow = 0x11D0;
        constexpr std::ptrdiff_t m_flTimerLength = 0x11D8;
    }

    constexpr std::ptrdiff_t m_flFlashBangTime = 0x1414;
    constexpr std::ptrdiff_t m_flFlashOverlayAlpha = 0x141C;
    constexpr std::ptrdiff_t m_flFlashMaxAlpha = 0x1424;
    constexpr std::ptrdiff_t m_flFlashDuration = 0x1428;
    constexpr std::ptrdiff_t m_flLastSmokeOverlayAlpha = 0x1448;
    constexpr std::ptrdiff_t m_flLastSmokeAge = 0x144C;
    constexpr std::ptrdiff_t m_vLastSmokeOverlayColor = 0x1450;

    constexpr std::ptrdiff_t m_bSmokeEffectSpawned = 0x12C1;
    constexpr std::ptrdiff_t m_bDidSmokeEffect = 0x127C;
    constexpr std::ptrdiff_t m_nSmokeEffectTickBegin = 0x1278;
    constexpr std::ptrdiff_t m_vSmokeColor = 0x1284;
    constexpr std::ptrdiff_t m_vSmokeDetonationPos = 0x1290;
    constexpr std::ptrdiff_t m_bSmokeVolumeDataReceived = 0x12C0;

    constexpr std::ptrdiff_t kListOffset = 0x10;
    constexpr std::ptrdiff_t kStride = 0x70;
    constexpr int            kChunk = 512;

    namespace Identity {
        constexpr std::ptrdiff_t m_pEntity = 0x0;
        constexpr std::ptrdiff_t m_designerName = 0x20;
        constexpr std::ptrdiff_t m_flags = 0x30;
        constexpr std::ptrdiff_t m_pNext = 0x58;
    }

    constexpr std::ptrdiff_t attack = 0x20B38F0;
    constexpr std::ptrdiff_t attack2 = 0x20B3980;
    constexpr std::ptrdiff_t jump   = 0x20B3E00;
    constexpr std::ptrdiff_t duck   = 0x20B3E90;
    constexpr std::ptrdiff_t forward = 0x20B3B30;
    constexpr std::ptrdiff_t back    = 0x20B3BC0;
    constexpr std::ptrdiff_t left    = 0x20B3C50;
    constexpr std::ptrdiff_t right   = 0x20B3CE0;

    namespace Bone {
        constexpr int head = 6;
        constexpr int neck = 5;
        constexpr int spine = 4;
        constexpr int pelvis = 0;
        constexpr int left_shoulder = 8;
        constexpr int left_elbow = 9;
        constexpr int left_hand = 10;
        constexpr int right_shoulder = 13;
        constexpr int right_elbow = 14;
        constexpr int right_hand = 15;
        constexpr int left_hip = 22;
        constexpr int left_knee = 23;
        constexpr int left_foot = 24;
        constexpr int right_hip = 25;
        constexpr int right_knee = 26;
        constexpr int right_foot = 27;
    }

    // Flags
    constexpr uint32_t FL_ONGROUND = (1 << 0);
    constexpr uint32_t FL_DUCKING  = (1 << 1);
}
