# rakhus-cs2-internal

Internal **legit** cheat for [Counter-Strike 2](https://store.steampowered.com/app/730).  
Schema / offsets target: **build 14178** (cs2-sdk dump).

Focus: smooth aim, soft RCS, natural triggerbot, ESP, bomb timer, modern ImGui menu.  
**No rage features** (no spinbot, no silent aim, no shoot-through-walls).  
**No skin / knife changer** (removed — was unreliable clientside).

> **Educational / private use only.** Online matchmaking can result in VAC, Overwatch, or account bans. You are solely responsible for how you use this software.

---

## Quick start

1. Build the project (see [Build](#build)).
2. Start CS2 and reach the main menu (or a match).
3. Inject `rakhus.dll` into `cs2.exe` (LoadLibrary or manual-map injector).
4. A console window titled **`rakhus-legit`** should open (pattern resolve log).
5. Press **INSERT** to open / close the menu.
6. Enable the features you want; settings save to **`legit.ini`**.

**First inject tip:** delete any old `legit.ini` so new defaults load cleanly.

---

## Controls

| Key | Action |
|-----|--------|
| **INSERT** | Toggle menu |
| Aim key (default **LMB**) | Hold to aim assist |
| Trigger key (default **Mouse 5**) | Hold to triggerbot |
| Third-person key (default **Mouse 4**) | **Hold** for third person (release = first person) |

All feature keys are rebindable in the menu (click the hex key button, then press a key).

---

## Features

### Aimbot
| Option | Description |
|--------|-------------|
| Enable | Smooth aim assist toward selected bone |
| FOV | Pixel FOV radius |
| Smooth | Higher = slower / more legit |
| Humanize | Small random offset on the aim point |
| Bone | Head / Neck / Chest |
| Bone priority | Head → Neck → Chest (first bone inside FOV wins) |
| Team check | Enemies only |
| Visible only | Spotted targets only |
| Only when scoped | Aim only while zoomed |
| Draw FOV circle | On-screen FOV overlay |
| Aim key | Hold key (default LMB) |

### Soft RCS
- Partial aim-punch compensation (strength 0–1)
- Configurable start bullet
- **Auto-disabled while No Visual Recoil is on** (avoids camera glitches)

### Triggerbot
- Crosshair entity via `m_iIDEntIndex`
- Inter-shot delay (min/max ms) for recoil settle
- Extra pause while spraying
- Team check; visible-only optional (default off)
- Rebindable hold key

### ESP
- Box + outline, name, health, armor
- Weapon name, distance (m)
- Head dot, optional skeleton
- Visible vs hidden colors
- Max distance, team / visible filters

Positions use `m_vecAbsOrigin` + crouch-aware view offset. Bones only when near the body.

### Glow
- `CGlowProperty` on enemies
- RGBA + type **0–3** (0 = outline … 3 = strongest)

### Sound ESP
- Edge arrows for non-visible moving enemies + distance (velocity-based)

### Bomb timer
- Centered `BOMB A · 12.3s` after plant
- `m_flC4Blow` − curtime, or local countdown fallback

### Misc
| Feature | Description |
|---------|-------------|
| No Flash | Clears flash overlay |
| No Smoke | `smokegrenade_projectile` filter + optional DrawSmokeArray hook |
| No Visual Recoil | Zeros aim-punch services only (no viewangle writes) |
| Spectator list | Observer pawn / mode / target |
| Hitmarker | Enemy HP drop + fade |
| Custom crosshair | Size / gap / thickness / color |
| Third person | **Hold key** (off by default). Safe CSGOInput + optional reset patch |

---

## Menu tabs

| Tab | Contents |
|-----|----------|
| Aimbot | Aim + soft RCS |
| Trigger | Triggerbot |
| Visuals | ESP |
| Misc | Flash / smoke / NVR / spectators / hitmarker / bomb / third person / sound / crosshair |
| Glow | Enemy glow |
| Config | Save / load |

Config file: **`legit.ini`**.

---

## Build

**Requirements:** Windows x64, VS 2022 (MSVC), CMake ≥ 3.20

```bat
cmake --preset x64-debug
cmake --build --preset x64-debug
```

Output: `rakhus.dll`.

---

## Injection

1. Run CS2  
2. Inject `rakhus.dll` into `cs2.exe`  
3. Check console `rakhus-legit` for pattern logs  
4. **INSERT** → menu  

If the game crashes: leave **Third person** and **No Smoke** off, re-enable one by one.

---

## Pattern scan

| Pattern | Use |
|---------|-----|
| `pViewMatrix` | View matrix (static RVA preferred for W2S) |
| `pGameEntitySystem` | Entity system |
| `pLocalPlayerController` | Local controller |
| `UpdateGlobalVars` | Global vars / bomb timer |
| `pGlowManager` | Glow |
| `pCSGOInput` | Input + third person |
| `DrawSmokeArray` | Optional NoSmoke hook |
| `ThirdPersonReset` | Optional third-person patch |

Fallbacks: static RVAs in `src/offsets.h`.

---

## Project layout

```
rakhus/
├── CMakeLists.txt
├── CMakePresets.json
├── LICENSE
├── README.md
└── src/
    ├── dllmain.cpp
    ├── offsets.h
    ├── pattern_scan.h
    ├── imgui/
    └── kiero/
```

---

## After a game update

Refresh at least:

- `dwGameEntitySystem`, `dwLocalPlayerPawn`, `dwViewMatrix`, `dwViewAngles`
- `dwGlobalVars`, `dwPlantedC4`, `dwGlowManager`
- Pawn fields: health, team, scene node, weapon services, glow, flash/smoke, aim punch, `m_iIDEntIndex`

If class layouts stay the same and only globals move, updating `dw*` is often enough.

---

## Known limitations

1. **No Visual Recoil** — punch zeroed on Present path; tiny residual possible without CreateMove. Do not combine with Soft RCS.  
2. **Third person** — hold-key path only; full distance needs OverrideView (not included).  
3. **Sound ESP** — velocity-based, not real footsteps.  
4. **Bomb timer** — curtime can drift; local countdown is fallback.  
5. **Skeleton** — bone layout varies; falls back to origin + view offset.  
6. **NoSmoke** — entity writes + optional draw hook; map-dependent.

---

## Troubleshooting

| Problem | Try |
|---------|-----|
| No menu | Inject after main menu; INSERT; check console |
| ESP low / shifted | Delete `legit.ini`; rebuild; check ViewMatrix log |
| Trigger never fires | Visible only **off**; check key; raise delay |
| NVR glitches | Soft RCS **off**; only NVR |
| Crash on third person | Leave TP **off** |
| Crash on smoke | Disable No Smoke |

---

## License

See [LICENSE](LICENSE).

Provided as-is, without warranty. Use at your own risk.
