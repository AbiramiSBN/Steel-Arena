# Steel Arena Local v15

Made by y8tireu

A fully local/offline no-engine C++17 + raylib arena shooter prototype.

## New in v15

- Added **LAN Multiplayer** for two devices on the same Wi-Fi/LAN.
- Host auto-generates a **4-digit LAN code**.
- Joiner enters that code to discover and connect to the host over local UDP broadcast.
- Added a LAN loadout screen before the match starts.
- LAN mode keeps everything local/offline: no internet, no server, no database, no real login.
- Keeps 50 levels, practice mode, shop/loadouts, fullscreen, first-person/third-person camera toggle.

## LAN Multiplayer How To

1. Put both devices on the same Wi-Fi or wired LAN.
2. On device 1: Main Menu → **LAN Multiplayer** → **Host LAN Match**.
3. The game shows a 4-digit code.
4. On device 2: Main Menu → **LAN Multiplayer** → **Join LAN Match**.
5. Type the same 4-digit code and press Enter.
6. If your OS asks, allow the game through the firewall for private/local networks.

The code maps to a local UDP port and uses LAN broadcast discovery. It is not an internet invite code.

## Default Loadout

- Primary: Assault Rifle
- Secondary: Handgun
- Melee: Fists
- Utility: Grenade

## Controls

### Menus / Shop / Loadout

- `W/S` or `Up/Down`: move selection
- `A/D` or `Left/Right`: switch tab/section
- `Enter`: select, buy, equip, or confirm
- `F`: start from the loadout screen
- Number keys: enter LAN join code
- `Backspace` or `Esc`: go back
- `F11`: fullscreen

### Gameplay

- `W A S D`: move
- Mouse: aim/camera
- Left Mouse: use selected slot
- `1`: Primary
- `2`: Secondary
- `3`: Melee
- `4`: Utility
- `C`: toggle first-person/third-person camera
- `Space`: jump
- `Q`: dash
- `Left Shift`: sprint
- `E`: reload selected gun
- `P`: pause
- `R`: restart while paused
- `F11`: fullscreen

## Ubuntu/Debian Build

Install raylib first. If your distro does not have `libraylib-dev`, build raylib from source and install it to `/usr/local`.

```bash
cd SteelArenaShooterLocal
cmake -S . -B build -DCMAKE_PREFIX_PATH=/usr/local
cmake --build build -j"$(nproc)"
./build/SteelArenaShooterLocal
```

Or run:

```bash
./scripts/build_ubuntu.sh
```

## Windows with vcpkg

```powershell
vcpkg install raylib
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build
.\build\Debug\SteelArenaShooterLocal.exe
```

## Save Data

The game stores local save data in `save/account.txt`. There is no internet, server, database, real login, or personal data collection.


## v15 Visual Polish Update

This version improves the look and feel of the game without adding a full game engine:

- Cleaner neon/glass-style menu screens
- Better loading screen with animated backdrop and progress card
- More readable keyboard-only shop/loadout UI
- Polished HUD panels, rounded health/stamina bars, and clearer weapon slot icons
- Better arena visuals: tiled floor, neon grid lines, glowing arena walls, decorative pillars, ramps/platforms, hazard pads, and richer player/bot placeholder models
- MSAA enabled through raylib config for smoother edges where supported

The game is still fully local/offline and made with C++17 + raylib.


## v14 compile fix
Fixed raylib 6 DrawCircleGradient signature by using Vector2 center arguments.


## v15 GUI polish notes
- Main menu and common menus are centered.
- Shop/loadout screen was rebuilt as a clean keyboard table.
- Level names no longer include duplicate numbers.
- Level picker shows one number column only.

## v17 GUI rebuild

This version specifically fixes the shop text-overlap problem by replacing the old side-panel shop with a cleaner centered full-width table.

- Rebuilt Shop and Loadout screens with a centered layout.
- Removed the cramped side panel that caused text collision.
- Added automatic text trimming so long weapon names/status text cannot overlap other columns.
- Added cleaner pill-style tabs for Primary, Secondary, Melee, and Utility.
- Added a cleaner current-loadout panel under the item table.
- Kept keyboard-only navigation to avoid mouse click/hitbox issues.


## v17 GUI Patch
Removed subtitle/tip text from Shop, Practice Loadout, and Loadout screens to prevent overlap with the tab bar. The shop remains keyboard-only.
