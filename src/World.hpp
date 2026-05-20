#pragma once

#include <unordered_map>
#include <string>
#include <vector>

#include "Constants.hpp"
#include "Types.hpp"

struct RuntimeMap {
    std::string id = "overworld";
    std::string name = "Overworld";
    int widthScreens = kDefaultWorldScreensWide;
    int heightScreens = kDefaultWorldScreensHigh;
    int defaultStartScreenX = 0;
    int defaultStartScreenY = 0;
    std::vector<Screen> screens;
    std::vector<std::string> dungeonIds;
};

class World {
public:
    World();

    bool LoadFromJsonOrDefault(const std::string& preferredPath);

    bool HasMap(const std::string& mapId) const;
    bool InBounds(const std::string& mapId, int sx, int sy) const;
    bool IsTileSolid(const std::string& mapId, int sx, int sy, int tx, int ty) const;
    // Returns the hitbox rect for a tile in world pixels, or nullopt if no solid tile
    SDL_FRect GetTileHitbox(const std::string& mapId, int sx, int sy, int tx, int ty) const;
    std::vector<SDL_FRect> GetTileHitboxes(const std::string& mapId, int sx, int sy, int tx, int ty) const;
    int TileIdAt(const std::string& mapId, int sx, int sy, int tx, int ty) const;
    const TileDef* FindTileDef(int tileId) const;

    const Screen& GetScreen(const std::string& mapId, int sx, int sy) const;

    int WidthScreens(const std::string& mapId) const;
    int HeightScreens(const std::string& mapId) const;
    int MapStartScreenX(const std::string& mapId) const;
    int MapStartScreenY(const std::string& mapId) const;
    const std::string& DefaultMapId() const { return defaultMapId_; }
    int DefaultStartScreenX() const { return defaultStartScreenX_; }
    int DefaultStartScreenY() const { return defaultStartScreenY_; }

    const std::vector<Item>& Items() const { return items_; }
    std::vector<Item>& Items() { return items_; }

    const std::vector<Enemy>& Enemies() const { return enemies_; }
    std::vector<Enemy>& Enemies() { return enemies_; }

    const std::vector<ScreenTransition>& Transitions() const { return transitions_; }
    const std::vector<WarpPoint>& Warps() const { return warps_; }
    const std::vector<PowerupDef>& Powerups() const { return powerups_; }
    const std::vector<ItemDefinition>& ItemDefinitions() const { return itemDefinitions_; }
    const std::vector<EnemyDropTable>& DropTables() const { return dropTables_; }
    const std::vector<WeaponDefinition>& WeaponDefinitions() const { return weaponDefinitions_; }
    const std::vector<ProjectileDefinition>& ProjectileDefinitions() const { return projectileDefinitions_; }
    const GlobalSettings& Settings() const { return globalSettings_; }
    const std::vector<TileCollection>& TileCollections() const { return tileCollections_; }
    const std::vector<CharacterSpriteset>& CharacterSpritesets() const { return characterSpritesets_; }
    std::vector<CharacterSpriteset>& CharacterSpritesets() { return characterSpritesets_; }
    const CharacterSpriteset* ActiveCharacterSpriteset() const;

    const ScreenTransition* FindTransition(const std::string& fromMapId, int fromSX, int fromSY, const std::string& edge) const;
    const WarpPoint* FindWarpAt(const std::string& mapId, int screenX, int screenY, const SDL_FRect& rect) const;
    const PowerupDef* FindPowerupById(const std::string& id) const;
    std::string DungeonIdForScreen(const std::string& mapId, int sx, int sy) const;

private:
    int ToIndex(const RuntimeMap& map, int sx, int sy) const;
    const RuntimeMap* FindMap(const std::string& mapId) const;
    RuntimeMap* FindMap(const std::string& mapId);
    void CarveExits(RuntimeMap& map, Screen& screen, int sx, int sy);
    void GenerateDefaultWorld();

private:
    std::string defaultMapId_ = "overworld";
    int defaultStartScreenX_ = 0;
    int defaultStartScreenY_ = 0;
    std::vector<RuntimeMap> maps_;
    std::vector<Item> items_;
    std::vector<Enemy> enemies_;
    std::vector<ScreenTransition> transitions_;
    std::vector<WarpPoint> warps_;
    std::vector<PowerupDef> powerups_;
    std::vector<ItemDefinition> itemDefinitions_;
    std::vector<EnemyDropTable> dropTables_;
    std::vector<WeaponDefinition> weaponDefinitions_;
    std::vector<ProjectileDefinition> projectileDefinitions_;
    GlobalSettings globalSettings_{};
    std::vector<TileCollection> tileCollections_;
    std::vector<CharacterSpriteset> characterSpritesets_;
    std::string activeCharacterSpritesetId_;
    std::unordered_map<int, bool> tileSolidById_;
};
