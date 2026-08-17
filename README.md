# rakhus-cs2-internal

A small, internal "legit" cheat for Counter-Strike 2 (CS2) built with ImGui and Kiero.  
Features include ESP (wallhack), smooth aim assist, and NoFlash – all configurable via an in-game menu.

---

## Features

- **ESP (Wallhack)**  
  - Box ESP with health bar  
  - Head indicator  
  - Distance display (optional)  
  - Customizable ESP color  

- **Aim Assist**  
  - Smooth aiming (adjustable smoothness)  
  - Configurable aim radius (in pixels)  
  - Selectable activation key (mouse or keyboard)  

- **NoFlash**  
  - Disables flashbang effect entirely  

- **In-Game Menu**  
  - Toggle with `INSERT` key  
  - Adjust all settings on the fly  
  - Save/Load configuration to/from `config.ini`  

- **Configuration**  
  - Settings persist between game sessions  
  - Auto‑loads config on injection  
---

## Installation

1. **Download** You need to build it yourself, i should update the offsets now and then.  
2. **Inject** the DLL into `cs2.exe` using your preferred injector (e.g., [Anarchy Injector](https://github.com/AnarchyLoader/AnarchyInjector)).  
3. **Launch CS2** (the cheat will automatically hook D3D11 and initialise).  
4. Press **`INSERT`** to open the settings menu.

> **Note:** The cheat is designed for **internal use only**. It requires a working D3D11 renderer (CS2 uses D3D11 by default).

---

## Usage

| Key | Action |
|-----|--------|
| `INSERT` | Toggle settings menu |
| *(configurable)* | Aim assist activation key (default: `F1`) |

- **Aim assist** works while holding the assigned key. It will smoothly move your crosshair toward the nearest enemy’s head within the aim radius.  
- **ESP** is always active when the cheat is enabled (toggleable via menu).  
- **NoFlash** is a checkbox in the menu – enable to ignore flashbangs.

---

## Configuration

Settings are stored in `config.ini`, located in the same folder as the injected DLL.  

- The file is **auto‑loaded** on injection.  
- Use the **Save Config** / **Load Config** buttons in the menu to manage settings.  
- **Reset Defaults** restores all values to their default state.

### Default Settings

| Setting | Default |
|---------|---------|
| Cheat enabled | `true` |
| Aim radius | `20.0` px |
| Aim key | `F1` |
| Smoothness | `0.8` |
| NoFlash | `false` |
| Show distance | `true` |
| ESP color | `(0.0, 0.75, 1.0)` – blue |

---

## Building from Source

### Prerequisites

- Visual Studio 2019 or newer (with C++ development tools)  
- [CMake](https://cmake.org/) (optional – or use the provided `.sln`)  
- Git (to clone submodules)  

### Steps

1. **Clone the repository** with submodules:
   ```bash
   git clone --recursive https://github.com/Titanius69/rakhus-cs2-internal.git
   cd rakhus-cs2-internal
   ```

2. **Open the solution** (`rakhus-cs2-internal.sln`) in Visual Studio.  
   *(If you prefer CMake, generate project files with `cmake -B build`)*

3. **Build** the project in `Release` or `Debug` configuration.  
   The output DLL will be placed in the `out/` folder.

4. **Update offsets** – the cheat relies on `offsets.h`. You may need to update them after each CS2 game update.  
   *(Offsets are not included in this repository – please obtain them from a reliable source.)*

---

## Credits

- [ImGui](https://github.com/ocornut/imgui) – GUI library  
- [Kiero](https://github.com/rdbo/ImGui-DirectX-11-Kiero-Hook) – D3D11 hooking  
- [CS2-Dumper](https://github.com/a2x/cs2-dumper) - Dumping CS2 offsets
- [CS2-SDK](https://www.cs2-sdk.com/) – CS2 SDK, some offsets come from there
---

## Disclaimer

**This project is for educational purposes only.**  
The author does not condone or encourage cheating in online multiplayer games.  
Using this cheat may result in a permanent ban from CS2. Use at your own risk.

---

## License

This project is licensed under the MIT License – see the [LICENSE](LICENSE) file for details.

---
