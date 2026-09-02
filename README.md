# rakhus-cs2-internal

Internal **legit** cheat for Counter-Strike 2 (schema target: **build 14178**).

> Educational and private use only. Online play risks VAC, Overwatch, and account bans.

---

## Controls

| Key | Action |
|-----|--------|
| **INSERT** | Toggle menu |
| **END** | Unload the cheat (waits for Present to finish, then restores hooks) |
| Aim / Trigger / third-person keys | Hold keys; rebindable in the menu |

---

## Features

### Aimbot
- Smooth FOV aim, bone priority, humanize, visible and team filters
- **Recoil-aware aim** — aim punch is applied to the target angles
- **Unified punch** — soft RCS yields while aiming (single punch authority)
- **Early punch path** — no-visual-recoil and RCS run before ESP (CreateMove-style ordering on Present)

### Soft RCS
- Partial recoil compensation; automatically disabled when No Visual Recoil is on

### Triggerbot
- Threadless state machine
- **Weapon delay profiles** (AWP slower, pistols faster)
- **Flash and smoke checks** (smoke uses a cheap proximity flag from a periodic scan)
- Optional trigger RCS and bone FOV

### ESP (optimized)
- Boxes, health, armor, distance, skeleton, head dot
- **Name and weapon text cache** on `CachedPlayer` (refreshed about every 20 ticks)
- Entity cache path (no second pass over slots 1–64 every frame)
- **ESP Y bias**, skeleton every other frame, off-screen culling

### Visual extras
- **Glow** (types 0–3)
- **Sound ESP** arrows
- **Bomb timer** (HUD) and **bomb world ESP** (C4 on the map)
- **Grenade prediction** line (approximate ballistics, no collision)
- **Sniper crosshair** when AWP or scout is unscoped
- **Hitmarker** and **hitlog** (floating damage numbers)
- **Watermark** — FPS and pattern OK count
- **FOV changer** via an **OverrideView** hook (when the pattern resolves)

### Misc
- No Flash / No Smoke (`DrawSmokeArray` hook preferred)
- No Visual Recoil
- Spectator list
- **Third person** (hold key): engine path via CSGOInput flag and ThirdPersonReset patch (not CViewSetup origin)
- Config: **pattern health**, **reset this tab** buttons, save/load `legit.ini`

---

## Architecture notes

| Item | Implementation |
|------|----------------|
| ESP cache | `CachedPlayer` plus lazy text cache |
| Entity listener style | Lite cache with stale text; full VMT listener still optional |
| OverrideView | MinHook on the SDK pattern — **FOV only** (values already in the 60–120 range) |
| Third person | Separate engine path: CSGOInput + ThirdPersonReset (not OverrideView origin) |
| CreateMove | Not a separate hook; **early punch path** approximates ordering |
| Unload | END → wait for `g_presentBusy` → disable OverrideView and smoke hooks → kiero → free the DLL |

---

## Build

```bat
cmake --preset x64-debug
cmake --build --preset x64-debug
```

Inject `rakhus.dll` into `cs2.exe`. Delete any old `legit.ini` after major updates so new defaults load.

---

## Performance (casual, about 10 players)

- Keep the text cache and skeleton every-other-frame options enabled
- Lower ESP max distance if needed
- Disable grenade prediction or glow if the frame rate drops

---

## Limitations

- FOV depends on OverrideView and on FOV slots that already look valid (60–120)
- **Third person does not use OverrideView origin**
- Grenade trajectory is approximate (no world collision)
- Smoke check is proximity-based, not a full engine volume query
- Full `IEntityListener` VMT is not hooked yet (cache-lite only)

---

## License

See [LICENSE](LICENSE). Provided as-is, without warranty.
