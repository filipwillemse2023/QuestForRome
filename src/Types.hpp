#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <string>
#include <vector>

#include "Constants.hpp"

enum class TileType : int {
    Grass = 0,
    StoneWall = 1,
    Water = 2,
    Sand = 3,
};

struct TileHitbox {
    int x = 0;
    int y = 0;
    int w = 16;
    int h = 16;
};

struct TileDef {
    int id = 0;
    std::string name;
    std::string description;
    int sourceX = 0;
    int sourceY = 0;
    bool solid = false;
    // Legacy single-hitbox fields kept for backwards compatibility.
    int hitboxX = 0;
    int hitboxY = 0;
    int hitboxW = 16;
    int hitboxH = 16;
    std::vector<TileHitbox> hitboxes;
};

struct TileCollection {
    std::string id;
    std::string name;
    std::string description;
    std::string imagePath;
    int tileWidth = 16;
    int tileHeight = 16;
    int imageWidth = 0;
    int imageHeight = 0;
    std::vector<TileDef> tiles;
};

// Character sprite system
struct CharacterFrame {
    int tileX = 0;      // Position in spritesheet (in tiles, 0-based)
    int tileY = 0;
    int frameWidth = 1; // Width in tiles (default 1 wide)
    int frameHeight = 2; // Height in tiles (default 2 high)
};

struct CharacterAction {
    std::string id;     // "standing", "walking", "sword_slash"
    std::string name;
    float animationSpeed = 1.0f;  // Frames per second
    // Character-space hitboxes for this action, in sprite pixel coordinates.
    std::vector<TileHitbox> hitboxes;
    // Direction order: South, West, East, North (matches Direction enum Down, Left, Right, Up)
    std::array<std::vector<CharacterFrame>, 4> directionalFrames{};
};

struct CharacterSpriteset {
    std::string id;
    std::string name;
    std::string description;
    std::string imagePath;  // e.g., "data/sprites/player/player_1.png"
    int tileWidth = 16;
    int tileHeight = 16;
    int imageWidth = 0;
    int imageHeight = 0;
    std::vector<CharacterAction> actions;
};

enum class ItemType {
    Coin,
    Wheat,
    Powerup,
};

enum class Direction {
    Down = 0,
    Left = 1,
    Right = 2,
    Up = 3,
};

struct Screen {
    std::array<std::array<int, kTilesPerScreen>, kTileLayers> tileLayerIds{};

        Screen() {
        for (int layer = 0; layer < kTileLayers; ++layer) {
                tileLayerIds[static_cast<size_t>(layer)].fill(layer == 0 ? 0 : -1);
            }
        }
};

struct Item {
    SDL_FRect bounds{};
    std::string mapId = "overworld";
    int screenX = 0;
    int screenY = 0;
    ItemType type = ItemType::Coin;
    std::string powerupId{};
    bool collected = false;
};

struct Enemy {
    SDL_FRect bounds{};
    std::string mapId = "overworld";
    int screenX = 0;
    int screenY = 0;
    bool alive = true;

    SDL_FPoint velocity{20.0f, 0.0f};
    float directionTimer = 1.2f;

    int health = 2;
    float invulnTimer = 0.0f;

    std::string behavior = "wander";
    float speed = 24.0f;
};

enum class TransitionKind {
    Fade,
    Instant,
};

struct ScreenTransition {
    std::string fromMapId = "overworld";
    int fromScreenX = 0;
    int fromScreenY = 0;
    std::string edge = "right";
    std::string toMapId = "overworld";
    int toScreenX = 0;
    int toScreenY = 0;
    int spawnX = 8;
    int spawnY = 8;
    TransitionKind kind = TransitionKind::Fade;
};

struct WarpPoint {
    SDL_FRect trigger{112.0f, 72.0f, 32.0f, 32.0f};
    std::string fromMapId = "overworld";
    int fromScreenX = 0;
    int fromScreenY = 0;
    std::string targetMapId = "overworld";
    int targetScreenX = 0;
    int targetScreenY = 0;
    int spawnX = 120;
    int spawnY = 80;
    TransitionKind kind = TransitionKind::Instant;
    std::string label = "warp";
};

struct PowerupDef {
    std::string id;
    std::string name;
    std::string effect;
    int magnitude = 0;
    float durationSeconds = 0.0f;
};

struct PlayerAttack {
    bool active = false;
    SDL_FRect hitbox{};
    float activeTimer = 0.0f;
    float cooldownTimer = 0.0f;
    float activeDuration = 0.12f;
    float cooldownDuration = 0.24f;
    int damage = 1;
};

struct Player {
    SDL_FRect bounds{80.0f, 80.0f, 12.0f, 12.0f};
    float speedPixelsPerSecond = 80.0f;
    float baseSpeedPixelsPerSecond = 80.0f;

    int health = 6;
    float invulnTimer = 0.0f;

    Direction facing = Direction::Down;
    bool moving = false;
    float animTimer = 0.0f;
    int animFrame = 0;

    PlayerAttack attack{};
};
