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
    int editorPaletteColumns = 0;
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
    std::string id;     // "standing", "walking", "sword_slash", "projectile_fire"
    std::string name;
    float animationSpeed = 1.0f;  // Frames per second
    // Character-space hitboxes for this action, in sprite pixel coordinates.
    std::vector<TileHitbox> hitboxes;
    // Optional direction-specific hitboxes (South, West, East, North) used by weapon actions.
    std::array<std::vector<TileHitbox>, 4> directionalHitboxes{};
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

enum class ItemTriggerFunction {
    None,
    IncreaseCoins,
    IncreaseHealth,
    IncreaseMaxHealth,
    ApplySpeedBoost,
};

enum class EnemyMoveType {
    MoveRandomDirection,
    StandStill,
    Disappear,
    FireProjectile,
};

enum class EnemyReappearMode {
    SamePlace,
    RandomPosition,
};

enum class ProjectileMovementType {
    FixedFunction,
    StraightLimitedDistance,
    TrackPlayer,
    Homing,
};

struct ItemAnimationFrame {
    std::string sourceImagePath;
    std::string sourceLabel;
    int sourceX = 0;
    int sourceY = 0;
    int sourceW = 16;
    int sourceH = 16;
};

struct ItemTriggerParam {
    std::string key;
    std::string value;
};

struct EnemyMoveDefinition {
    struct AnimationTile {
        std::string sourceImagePath;
        std::string sourceLabel;
        int sourceX = 0;
        int sourceY = 0;
        int sourceW = 16;
        int sourceH = 16;
        int tileX = 0;
        int tileY = 0;
    };

    struct AnimationFrame {
        int frameWidth = 1;
        int frameHeight = 1;
        std::vector<AnimationTile> tiles;
    };

    EnemyMoveType type = EnemyMoveType::StandStill;
    float minSeconds = 1.0f;
    float maxSeconds = 1.0f;
    float speedTilesPerSecond = 1.0f;
    EnemyReappearMode reappearMode = EnemyReappearMode::SamePlace;
    std::string projectileDefinitionId;
    // Direction order: South, West, East, North (matches Direction enum Down, Left, Right, Up)
    std::array<std::vector<AnimationFrame>, 4> directionalFrames{};
    float animationSpeed = 0.0f;
    std::vector<TileHitbox> hitboxes;
};

struct EnemyReactionAnimation {
    std::array<std::vector<EnemyMoveDefinition::AnimationFrame>, 4> directionalFrames{};
    float animationSpeed = 0.0f;
};

struct EnemyDefinition {
    std::string id = "enemy_1";
    std::string name = "enemy";
    int hitpoints = 2;
    int baseDamage = 1;
    bool immuneToKnockback = false;
    std::vector<EnemyMoveDefinition> moves;
    EnemyReactionAnimation knockbackAnimation{};
    EnemyReactionAnimation deathAnimation{};
};

struct ProjectileDefinition {
    std::string id = "projectile_1";
    std::string name = "projectile";
    std::vector<ItemAnimationFrame> startFrames;
    std::vector<ItemAnimationFrame> flightFrames;
    std::vector<ItemAnimationFrame> impactFrames;
    float startAnimationSpeed = 0.0f;
    float flightAnimationSpeed = 0.0f;
    float impactAnimationSpeed = 0.0f;
    std::vector<TileHitbox> hitboxes;
    ProjectileMovementType movementType = ProjectileMovementType::TrackPlayer;
    float speedTilesPerSecond = 1.0f;
    float fixedFunctionA = 0.0f;
    float limitedDistanceTiles = 4.0f;
    float limitedDurationSeconds = 0.5f;
    bool moveThroughSolid = false;
    int baseDamage = 1;
};

struct WeaponDefinition {
    std::string id = "weapon_1";
    std::string name = "weapon";
    int damage = 1;
    ItemAnimationFrame hudSprite{};
    bool isProjectile = false;
    std::string projectileDefinitionId;
};

struct EnemyPlacement {
    std::string enemyId;
    float x = 0.0f;
    float y = 0.0f;
    std::string mapId = "overworld";
    int screenX = 0;
    int screenY = 0;
};

enum class Direction {
    Down = 0,
    Left = 1,
    Right = 2,
    Up = 3,
};

struct Screen {
    std::array<std::array<int, kTilesPerScreen>, kTileLayers> tileLayerIds{};
    bool displayTextEnabled = false;
    std::string displayText;

        Screen() {
        for (int layer = 0; layer < kTileLayers; ++layer) {
                tileLayerIds[static_cast<size_t>(layer)].fill(layer == 0 ? 0 : -1);
            }
        }
};

struct ItemDefinition {
    std::string id = "item";
    std::string name = "item";
    std::vector<ItemAnimationFrame> frames;
    float animationSpeed = 0.0f;
    std::vector<TileHitbox> hitboxes;
    ItemTriggerFunction triggerFunction = ItemTriggerFunction::None;
    std::vector<ItemTriggerParam> triggerParams;

    // Legacy pickup fields kept for backward compatibility with older world files.
    ItemType type = ItemType::Coin;
    std::string powerupId{};
    bool legacyPickup = false;
};

struct ItemPlacement {
    std::string itemId;
    float x = 0.0f;
    float y = 0.0f;
    std::string mapId = "overworld";
    int screenX = 0;
    int screenY = 0;

    bool collected = false;
};

struct Item {
    std::string itemId;
    SDL_FRect bounds{0.0f, 0.0f, 16.0f, 16.0f};
    std::string name = "item";
    std::string mapId = "overworld";
    int screenX = 0;
    int screenY = 0;
    std::vector<ItemAnimationFrame> frames;
    float animationSpeed = 0.0f;
    std::vector<TileHitbox> hitboxes;
    ItemTriggerFunction triggerFunction = ItemTriggerFunction::None;
    std::vector<ItemTriggerParam> triggerParams;

    // Legacy pickup fields kept for backward compatibility with older world files.
    ItemType type = ItemType::Coin;
    std::string powerupId{};
    bool legacyPickup = false;
    bool collected = false;
};

struct Enemy {
    SDL_FRect bounds{};
    std::string enemyId;
    std::string name = "enemy";
    std::string mapId = "overworld";
    int screenX = 0;
    int screenY = 0;
    bool alive = true;

    SDL_FPoint velocity{20.0f, 0.0f};
    float directionTimer = 1.2f;

    int health = 2;
    int baseDamage = 1;
    bool immuneToKnockback = false;
    float invulnTimer = 0.0f;
    EnemyReactionAnimation knockbackAnimation{};
    SDL_FPoint knockbackVelocity{0.0f, 0.0f};
    float knockbackTimer = 0.0f;
    float knockbackAnimationTimer = 0.0f;
    int knockbackAnimationFrame = 0;
    int knockbackDirection = 0;

    EnemyReactionAnimation deathAnimation{};

    std::vector<EnemyMoveDefinition> moves;
    int currentMoveIndex = 0;
    float moveTimer = 0.0f;
    float moveDuration = 0.0f;
    bool moveInitialized = false;
    bool disappeared = false;
    SDL_FPoint disappearOrigin{0.0f, 0.0f};
    enum class DisappearPhase {
        None,
        PreDisappear,
        Hidden,
        Reappear,
    };
    DisappearPhase disappearPhase = DisappearPhase::None;
    float animationTimer = 0.0f;
    int animationFrame = 0;
    int moveDirection = 0;
    bool moveProjectileSpawned = false;
    SDL_FPoint fireDirection{0.0f, 0.0f};
    bool deathAnimationPlaying = false;
    float deathAnimationTimer = 0.0f;
    int deathAnimationFrame = 0;

    // Legacy behavior fields kept for backward compatibility with older world files.
    std::string behavior = "wander";
    float speed = 24.0f;
    float projectileCooldownTimer = 1.2f;
};

enum class ProjectileOwner {
    Player,
    Enemy,
};

struct Projectile {
    enum class Phase {
        Start,
        Flight,
        Impact,
        Done,
    };

    std::string projectileId;
    std::string name = "projectile";
    std::string mapId = "overworld";
    int screenX = 0;
    int screenY = 0;
    SDL_FRect bounds{0.0f, 0.0f, 8.0f, 8.0f};
    SDL_FPoint velocity{0.0f, 0.0f};
    ProjectileOwner owner = ProjectileOwner::Enemy;
    ProjectileMovementType movementType = ProjectileMovementType::TrackPlayer;
    float speedPixelsPerSecond = 16.0f;
    float fixedFunctionA = 0.0f;
    float limitedDistancePixels = 64.0f;
    float limitedDurationSeconds = 0.5f;
    float lifetimeTimer = 0.0f;
    float traveledDistancePixels = 0.0f;
    float trackCorrectionDistanceAccumulator = 0.0f;
    bool movementStopped = false;
    bool damageConsumed = false;
    bool moveThroughSolid = false;
    int baseDamage = 1;
    std::vector<TileHitbox> hitboxes;

    std::vector<ItemAnimationFrame> startFrames;
    std::vector<ItemAnimationFrame> flightFrames;
    std::vector<ItemAnimationFrame> impactFrames;
    float startAnimationSpeed = 0.0f;
    float flightAnimationSpeed = 0.0f;
    float impactAnimationSpeed = 0.0f;

    Phase phase = Phase::Flight;
    float animationTimer = 0.0f;
    int animationFrame = 0;
    bool impactAnimationFinished = false;
    bool alive = true;
};

enum class TransitionKind {
    Fade,
    Instant,
};

enum class WarpSpawnOffset {
    OnTop,
    Above,
    Below,
    Left,
    Right,
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

struct WarpEndpointDefinition {
    int tileId = 0;
    std::vector<TileHitbox> hitboxes;
    WarpSpawnOffset spawnOffset = WarpSpawnOffset::OnTop;
};

struct WarpDefinition {
    std::string id = "warp_1";
    std::string name = "warp";
    std::array<WarpEndpointDefinition, 2> endpoints{};
    TransitionKind kind = TransitionKind::Instant;
};

struct WarpPlacement {
    std::string warpId;
    int endpointIndex = 0;
    float x = 0.0f;
    float y = 0.0f;
    std::string mapId = "overworld";
    int screenX = 0;
    int screenY = 0;
};

struct WarpPoint {
    std::string warpId;
    int endpointIndex = 0;
    SDL_FRect trigger{112.0f, 72.0f, 32.0f, 32.0f};
    std::vector<SDL_FRect> triggers;
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

struct GlobalSettings {
    float knockbackDistanceTiles = 0.5f;
    float invulnerabilitySeconds = 1.5f;
    float textLettersPerSecond = 28.0f;
    std::string textGlyphMap;
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

    int maxHealth = 8;
    int health = 8;
    float invulnTimer = 0.0f;
    SDL_FPoint knockbackVelocity{0.0f, 0.0f};
    float knockbackTimer = 0.0f;

    Direction facing = Direction::Down;
    bool moving = false;
    float animTimer = 0.0f;
    int animFrame = 0;

    PlayerAttack attack{};
};
