#pragma once

#include <SDL3/SDL.h>

#include <unordered_map>
#include <unordered_set>
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

enum class MenuScreen {
    None,
    Start,
    Map,
};

class Game {
public:
    bool Initialize();
    void Run();
    ~Game();

private:
    bool IsFirstVersionMode() const;
    void ApplyPowerup(const PowerupDef& def);
    void ApplyHeartPiece();
    void ApplyItemTrigger(const Item& item);
    static float Lerp(float start, float end, float t);
    SDL_FPoint DefaultArrivalPosition(TransitionDirection direction) const;

    void HandleEvents(bool& running);

    bool IsRectCollidingWithSolidTiles(const SDL_FRect& rect, const std::string& mapId, int screenX, int screenY) const;
    bool IsRectCollidingWithNpcs(const SDL_FRect& rect, const Enemy* ignoreEnemy) const;
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
    bool TryInteractWithNpc();
    bool TryOpenNearbyContainer();
    void TryAwardContainerContent(const Item& container);
    void StartItemPickupPresentation(const ItemAnimationFrame* frame, SDL_Color fallbackColor);

    void UpdatePlayerInputAndAnimation(float dt);
    void UpdateCharacterAnimation(float dt);
    void UpdateCombat(float dt);
    void UpdateProjectiles(float dt);
    void UpdateEnemies(float dt);
    void UpdateItems();
    void UpdateDroppedItems(float dt);
    void UpdateRoomText(float dt);
    void Update(float dt);
    void ApplyPlayerDamage(int damage, const SDL_FPoint& knockbackDirection);
    void ApplyEnemyDamage(Enemy& enemy, int damage, const SDL_FPoint& knockbackDirection, const std::string& weaponId = {}, const std::string& projectileId = {});
    void SpawnEnemyDrop(const Enemy& enemy);
    void OnEnteredScreen(const std::string& prevMapId, int prevScreenX, int prevScreenY);

    bool BuildTileTextureAtlas();
    bool BuildSpriteAtlas();
    bool IsRectCollidingWithContainerItems(const SDL_FRect& rect, const std::string& mapId, int screenX, int screenY) const;
    SDL_Texture* TextureForItemFrame(const ItemAnimationFrame& frame);
    std::string ResolveAssetPath(const std::string& sourcePath) const;
    SDL_Surface* LoadPngSurface(const std::string& path) const;
    void DrawTilesForScreen(const std::string& mapId, int screenX, int screenY, float offsetX, float offsetY, const SDL_FRect* playerBoundsOverride);
    void DrawItemsForScreen(const std::string& mapId, int screenX, int screenY, float offsetX, float offsetY);
    void DrawDroppedItemsForScreen(const std::string& mapId, int screenX, int screenY, float offsetX, float offsetY);
    void DrawProjectilesForScreen(const std::string& mapId, int screenX, int screenY, float offsetX, float offsetY);
    void DrawEnemiesForScreen(const std::string& mapId, int screenX, int screenY, float offsetX, float offsetY);
    void DrawForegroundOcclusionTilesForScreen(const std::string& mapId, int screenX, int screenY, float offsetX, float offsetY, const SDL_FRect* playerBoundsOverride);
    void DrawPlayer();
    void DrawPlayerAt(const SDL_FRect& bounds);
    void DrawPlayerClassic();
    void DrawAttackHitbox();
    void DrawItemPickupAbovePlayer();
    void DrawDebugHitboxesForScreen(const std::string& mapId, int screenX, int screenY, float offsetX, float offsetY);
    void DrawPlayerDebugHitboxesAt(const SDL_FRect& bounds);
    void DrawHUD();
    void DrawRoomText();
    void DrawStartMenu();
    void DrawMapScreen();
    void UpdateStartMenu(float dt);
    void UpdateMapScreen(float dt);
    void MarkCurrentScreenVisited();
    void DrawTransitionOverlay();
    void DrawScreenLayer(const std::string& mapId, int screenX, int screenY, float offsetX, float offsetY, const SDL_FRect* playerBoundsOverride, bool drawEnemies = true);
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

    MenuScreen menuScreen_ = MenuScreen::None;
    bool previousWeaponAPressed_ = false;
    bool previousWeaponBPressed_ = false;
    bool previousNpcAdvancePressed_ = false;
    std::string activeActionId_ = "standing";
    int activeActionFrame_ = 0;
    float activeActionTimer_ = 0.0f;
    std::string activeWeaponActionId_;
    std::string activeAttackWeaponId_; // weapon id used for current melee attack
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
    std::string npcTextMapId_;
    int npcTextScreenX_ = -1;
    int npcTextScreenY_ = -1;
    std::string npcTextContent_;
    float npcTextVisibleCharacters_ = 0.0f;

    std::string previousScreenMapId_;
    int previousScreenX_ = -1;
    int previousScreenY_ = -1;

    int coins_ = 0;
    int wheat_ = 0;
    int heartPieces_ = 0;
    bool debugShowHitboxes_ = false;
    bool debugShowOcclusion_ = false;

    std::vector<std::string> weaponInventory_;
    std::unordered_set<std::string> visitedScreens_;

    int mapViewCenterScreenX_ = 0;
    int mapViewCenterScreenY_ = 0;
    float menuScreenBlend_ = 0.0f;
    float menuScreenBlendTarget_ = 0.0f;

    float startMenuSlideOffset_ = -200.0f;
    int startMenuCursorRow_ = 0;
    int startMenuCursorCol_ = 0;
    bool previousStartPressed_ = false;
    bool previousSelectPressed_ = false;
    bool previousMenuUpPressed_ = false;
    bool previousMenuDownPressed_ = false;
    bool previousMenuLeftPressed_ = false;
    bool previousMenuRightPressed_ = false;
    bool previousMenuLPressed_ = false;
    bool previousStartMenuAPressed_ = false;
    bool previousStartMenuBPressed_ = false;
    bool previousMapBackPressed_ = false;
    bool previousMapCenterPressed_ = false;

    float itemPickupTimer_ = 0.0f;
    bool itemPickupDisplayHasFrame_ = false;
    ItemAnimationFrame itemPickupDisplayFrame_{};
    SDL_Color itemPickupDisplayColor_{220, 220, 220, 255};

    // Dropped items (spawned on enemy death)
    std::vector<DroppedItem> droppedItems_;

    // Respawn tracking: {mapId, screenX, screenY} -> true if all enemies were killed
    struct ScreenKey {
        std::string mapId;
        int screenX = 0;
        int screenY = 0;
        bool operator==(const ScreenKey& o) const {
            return mapId == o.mapId && screenX == o.screenX && screenY == o.screenY;
        }
    };
    struct ScreenKeyHash {
        std::size_t operator()(const ScreenKey& k) const {
            std::size_t h = std::hash<std::string>{}(k.mapId);
            h ^= std::hash<int>{}(k.screenX) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<int>{}(k.screenY) + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };
    // Respawn rule tracking: screen key -> transition index when all enemies were last cleared.
    std::unordered_map<std::string, int> killedScreenTransitionIndex_;
    int screenTransitionCount_ = 0;
    // Tracks last map the player was on to detect map exits
    std::string lastMapId_;
};
