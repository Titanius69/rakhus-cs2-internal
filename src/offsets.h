#pragma once
#include <cstddef>
#include <cstdint>

namespace O {
    // client.dll
    constexpr std::ptrdiff_t dwEntityList = 0x2554050;
    constexpr std::ptrdiff_t dwGameEntitySystem = 0x2554050;
    constexpr std::ptrdiff_t dwLocalPlayerPawn = 0x23A9118;
    constexpr std::ptrdiff_t dwViewMatrix = 0x23AE550;
    constexpr std::ptrdiff_t dwViewAngles = 0x23BF1A8;
    constexpr std::ptrdiff_t attack = 0x2099000;

    // pawn
    constexpr std::ptrdiff_t m_hPlayerPawn = 0x914;
    constexpr std::ptrdiff_t m_iHealth = 0x34C;
    constexpr std::ptrdiff_t m_lifeState = 0x354;
    constexpr std::ptrdiff_t m_iTeamNum = 0x3E7;
    constexpr std::ptrdiff_t m_pGameSceneNode = 0x330;
    constexpr std::ptrdiff_t m_vecOrigin = 0x80;          // CGameSceneNode::m_vecOrigin
    constexpr std::ptrdiff_t m_vecViewOffset = 0xE78;     // C_BaseModelEntity::m_vecViewOffset
    constexpr std::ptrdiff_t m_angEyeAngles = 0x3350;     // C_CSPlayerPawn::m_angEyeAngles

    // entity list
    constexpr std::ptrdiff_t kListOffset = 0x10;
    constexpr std::ptrdiff_t kStride = 0x70;
    constexpr int kChunk = 512;
}