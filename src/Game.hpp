#pragma once

#include <SDL3/SDL.h>

#include <unordered_map>
#include <vector>

#include "Types.hpp"
#include "World.hpp"

enum class TransitionPhase {
    None,
    Scrolling,
};

enum class TransitionDirection {
    Left,
    Right,
    Up,
    Down,
};

class Game {
public:
    bool Initialize();
    void Run();
    ~Game();

private:
    bool IsFirstVersionMode() const;
    void ApplyPowerup(const PowerupDef& def);
    void ApplyItemTrigger(const Item& item);
    static float Lerp(float start, float end, float t);
    SDL_FPoint DefaultArrivalPosition(TransitionDirection direction) const;

    void HandleEvents(bool& running);

    bool IsRectCollidingWithSolidTiles(const SDL_FRect& rect, const std::string& mapId, int screenX, int screenY) const;
    bool IsPlayerHitboxCollidingAt(const SDL_FRect& candidateBounds, const std::string& mapId, int screenX, int screenY) const;
    bool AreEnemyHitboxesCollidingAfterDelta(const Enemy& enemy, float dx, float dy, const std::string& mapId, int screenX, int screenY) const;
    void ResolveAxisMovement(float dx, float dy);
    void ResolveEnemyAxisMovement(Enemy& enemy, float dx, float dy);
     void ResolveKnockbackMovement(float dx, float dy);
     void ResolveEnemyKnockbackMovement(Enemy& enemy, float dx, float dy);
     float ApplyPlayerAxisDeltaClamped(float delta, bool xAxis);
     float ApplyEnemyAxisDeltaClamped(Enemy& enemy, float delta, bool xAxis);
    std::vector<SDL_FRect> ActivePlayerHitboxesAt(const SDL_FRect& candidateBounds) const;
    std::vector<SDL_FRect> ActiveEnemyHitboxesAt(const Enemy& enemy) const;
    SDL_FRect PlayerSpriteRectForBounds(const SDL_FRect& bounds) const;
    bool PlayerIntersects(const SDL_FRect& other) const;

    void BeginScreenTransition(const std::string& nextMapId, int nextScreenX, int nextScreenY, TransitionDirection direction, SDL_FPoint destinationPos, bool scrolling);
    SDL_FPoint FindNearbyFreePosition(SDL_FPoint candidate, const std::string& mapId, int screenX, int screenY) const;
    void TryDetectEdgeTrigger(float intendedDx, float intendedDy);
    void TryUseWarpPoint();
    void TryStartScreenTransition();
    void UpdateTransition(float dt);

    void UpdatePlayerInputAndAnimation(float dt);
    void UpdateCharacterAnimation(float dt);
    void UpdateCombat(float dt);
    void UpdateProjectiles(float dt);
    void UpdateEnemies(float dt);
    void UpdateItems();
    void UpdateRoomText(float dt);
    void Update(float dt);
    void ApplyPlayerDamage(int damage, const SDL_FPoint& knockbackDirection);
    void ApplyEnemyDamage(Enemy& enemy, int damage, const SDL_FPoint& knockbackDirection);

    bool BuildTileTextureAtlas();
    bool BuildSpriteAtlas();
    SDL_Texture* TextureForItemFrame(const ItemAnimationFrame& frame);
    std::string ResolveAssetPath(const std::string& sourcePath) const;
    SDL_Surface* LoadPngSurface(const std::string& path) const;
    void DrawTilesForScreen(const std::string& mapId, int screenX, int screenY, float offsetX, float offsetY, const SDL_FRect* playerBoundsOverride);
    void DrawItemsForScreen(const std::string& mapId, int screenX, int screenY, float offsetX, float offsetY);
    void DrawProjectilesForScreen(const std::string& mapId, int screenX, int screenY, float offsetX, float offsetY);
    void DrawEnemiesForScreen(const std::string& mapId, int screenX, int screenY, float offsetX, float offsetY);
    void DrawForegroundOcclusionTilesForScreen(const std::string& mapId, int screenX, int screenY, float offsetX, float offsetY, const SDL_FRect* playerBoundsOverride);
    void DrawPlayer();
    void DrawPlayerAt(const SDL_FRect& bounds);
    void DrawPlayerClassic();
    void DrawAttackHitbox();
    void DrawDebugHitboxesForScreen(const std::string& mapId, int screenX, int screenY, float offsetX, float offsetY);
    void DrawPlayerDebugHitboxesAt(const SDL_FRect& bounds);
    void DrawHUD();
    void DrawRoomText();
    void DrawTransitionOverlay();
    void DrawScreenLayer(const std::string& mapId, int screenX, int screenY, float offsetX, float offsetY, const SDL_FRect* playerBoundsOverride);
    const CharacterAction* ActiveCharacterAction() const;
    const CharacterFrame* ActiveCharacterFrame() const;
    const WeaponDefinition* FindWeaponDefinitionById(const std::string& weaponId) const;
    const ProjectileDefinition* FindProjectileDefinitionById(const std::string& projectileId) const;
    const WeaponDefinition* EquippedWeaponForSlotA() const;
    const WeaponDefinition* EquippedWeaponForSlotB() const;
    void UseWeapon(const WeaponDefinition& weapon);
    void SpawnProjectile(const ProjectileDefinition& definition, ProjectileOwner owner, const SDL_FPoint& spawnPos, const SDL_FPoint& initialDirection);
    void Draw();

private:
    struct TileRenderInfo {
        SDL_Texture* texture = nullptr;
        SDL_FRect source{};
    };

    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* characterAtlas_ = nullptr;
    SDL_Texture* textAtlas_ = nullptr;
    std::vector<SDL_Texture*> ownedTileAtlases_;
    std::unordered_map<int, TileRenderInfo> tileRenderById_;
    std::unordered_map<std::string, SDL_Texture*> itemTextureByPath_;

    World world_{};
    Player player_{};

    std::string currentMapId_ = "overworld";
    int currentScreenX_ = 0;
    int currentScreenY_ = 0;

    TransitionPhase transitionPhase_ = TransitionPhase::None;
    TransitionDirection transitionDirection_ = TransitionDirection::Down;
    float transitionTimer_ = 0.0f;
    float transitionDuration_ = 0.5f;
    float transitionCooldownTimer_ = 0.0f;
    std::string transitionSourceMapId_ = "overworld";
    int transitionSourceScreenX_ = 0;
    int transitionSourceScreenY_ = 0;
    std::string transitionTargetMapId_ = "overworld";
    int transitionTargetScreenX_ = 0;
    int transitionTargetScreenY_ = 0;
    SDL_FPoint transitionStartPos_{};
    SDL_FPoint transitionEndPos_{};

    bool previousWeaponAPressed_ = false;
    bool previousWeaponBPressed_ = false;
    std::string activeActionId_ = "standing";
    int activeActionFrame_ = 0;
    float activeActionTimer_ = 0.0f;
    std::string activeWeaponActionId_;
    float weaponVisualTimer_ = 0.0f;

    std::string equippedWeaponAId_;
    std::string equippedWeaponBId_;

    float speedBuffTimer_ = 0.0f;
    int speedBuffMagnitude_ = 0;

    std::vector<Projectile> projectiles_;

    std::string roomTextMapId_;
    int roomTextScreenX_ = -1;
    int roomTextScreenY_ = -1;
    std::string roomTextContent_;
    float roomTextVisibleCharacters_ = 0.0f;

    std::string previousScreenMapId_;
    int previousScreenX_ = -1;
    int previousScreenY_ = -1;

    int coins_ = 0;
    int wheat_ = 0;
    bool debugShowHitboxes_ = false;
    bool debugShowOcclusion_ = false;
};
