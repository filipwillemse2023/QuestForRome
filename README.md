# Quest for Rome - SDL3 Engine + wxWidgets Editor

This is a lightweight starting point for a top-down, Zelda-like screen-based adventure game themed around the Roman era.

This version now includes:

- A split engine-style code structure (`Game`, `World`, `MapLoader`, entity types)
- Directional sprite rendering with walk/idle animation states
- JSON world loading (`data/world.json`) with procedural fallback
- Combat scaffolding (attack hitbox, enemy HP, invulnerability frames)
- A dedicated desktop world editor (`quest_for_rome_editor`) built with wxWidgets

## What this prototype includes

- Tile grid per screen: 16x11 tiles
- Tile size: 16x16 pixels
- Screen transitions between a world made of multiple screens
- Player movement with tile collision
- Scaffolding for enemies (basic wander update + damage interaction)
- Scaffolding for collectible items (coin/wheat examples)
- Basic combat loop (player attack and enemy defeat)

## Controls

- Move: WASD or arrow keys
- Attack: Space or Enter

## Editor (wxWidgets)

The workspace includes an editor app for creating/editing the same world assets used by the game:

- Tile painting for 16x11 rooms/screens
- Screen metadata (`dungeonId`)
- Item placement (coin, wheat, powerup)
- Enemy placement (`wander`, `chase`, `static`, `patrol`)
- Screen edge transitions (fade/instant)
- Powerup definitions and effects

Editor source: `editor/EditorMain.cpp`

### Build and run editor

The CMake project prefers wxWidgets at `C:/Tools/wxWidgets` and falls back to dependency resolution via package manager setup.

```powershell
$toolchain = Join-Path (Get-Location) '.vcpkg\scripts\buildsystems\vcpkg.cmake'
cmake -S . -B build-editor "-DCMAKE_TOOLCHAIN_FILE=$toolchain" -DQUEST_BUILD_EDITOR=ON
cmake --build build-editor --config Release --target quest_for_rome_editor
.\build-editor\Release\quest_for_rome_editor.exe
```

The editor reads/writes `data/world.json` using the same schema loader (`src/MapLoader.*`) used by the game.

## Engine + Editor workflow contract

From this point forward, any gameplay/engine feature that changes world asset schema or semantics should be updated in both places:

- Game runtime and loaders (`src/`)
- Editor UI and save/load behavior (`editor/`)

This keeps authoring parity so new features are immediately editable.

## Build (Windows + CMake)

You need SDL3 available to CMake (`find_package(SDL3 CONFIG REQUIRED)`).

Example with a preinstalled SDL3 package:

```powershell
cmake -S . -B build
cmake --build build --config Release
```

Run:

```powershell
.\build\Release\quest_for_rome.exe
```

## Build with local vcpkg (recommended)

If you do not have SDL3 globally installed, this project now supports local vcpkg.

```powershell
git clone https://github.com/microsoft/vcpkg.git .vcpkg
.\.vcpkg\bootstrap-vcpkg.bat
```

First-version mode (original prototype behavior):

```powershell
$toolchain = Join-Path (Get-Location) '.vcpkg\scripts\buildsystems\vcpkg.cmake'
cmake -S . -B build-first "-DCMAKE_TOOLCHAIN_FILE=$toolchain" -DQUEST_FIRST_VERSION_MODE=ON
cmake --build build-first --config Release
.\build-first\Release\quest_for_rome.exe
```

Current engine mode (all upgrades):

```powershell
$toolchain = Join-Path (Get-Location) '.vcpkg\scripts\buildsystems\vcpkg.cmake'
cmake -S . -B build-modern "-DCMAKE_TOOLCHAIN_FILE=$toolchain" -DQUEST_FIRST_VERSION_MODE=OFF
cmake --build build-modern --config Release
.\build-modern\Release\quest_for_rome.exe
```

## Where to extend next

Files are now split and ready for incremental growth:

- `src/Game.cpp`: game loop, input, movement, transitions, combat, rendering
- `src/World.cpp`: world data ownership, collision, procedural fallback generation
- `src/MapLoader.cpp`: minimal JSON loader for world/screens/items/enemies
- `src/Types.hpp`: entity structs and shared enums

## JSON map format

The loader currently expects:

- `formatVersion` (current: `2`)
- `worldWidthScreens`, `worldHeightScreens`
- `powerups`: array of `{ id, name, effect, magnitude, durationSeconds }`
- `screens`: array of objects with:
	- `x`, `y`
	- `dungeonId` (optional)
	- `tiles`: 176 integers (16x11), where tile IDs are:
		- `0` grass
		- `1` stone wall (solid)
		- `2` water (solid)
		- `3` sand
	- `items`: array of `{ "x": int, "y": int, "w"?: int, "h"?: int, "type": "coin"|"wheat"|"powerup", "powerupId"?: string }`
	- `enemies`: array of `{ "x": int, "y": int, "hp": int, "behavior"?: string, "speed"?: number }`
	- `transitions`: array of `{ "edge": "left"|"right"|"up"|"down", "toX": int, "toY": int, "spawnX": int, "spawnY": int, "kind": "fade"|"instant" }`

See `data/world.json` for a sample.

## Suggested next additions

- Replace generated placeholder character atlas with external PNG spritesheets.
- Move enemy AI and combat into dedicated systems/classes.
- Add melee weapon types and stamina/cooldown balancing.
- Add save/load for inventory and defeated enemies.
