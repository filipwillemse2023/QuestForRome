#pragma once

#include <string>
#include <vector>

#include "Types.hpp"

struct ScreenLoadData {
    int x = 0;
    int y = 0;
    std::string dungeonId;
    Screen screen{};
    std::vector<ItemPlacement> itemPlacements;
    std::vector<EnemyPlacement> enemyPlacements;
    std::vector<ScreenTransition> transitions;
    std::vector<WarpPlacement> warpPlacements;
};

struct MapLoadData {
    std::string id = "overworld";
    std::string name = "Overworld";
    int widthScreens = 0;
    int heightScreens = 0;
    int defaultStartScreenX = 0;
    int defaultStartScreenY = 0;
    std::vector<ScreenLoadData> screens;
};

struct WorldLoadData {
    int formatVersion = 10;
    std::string defaultMapId = "overworld";
    int defaultStartScreenX = 0;
    int defaultStartScreenY = 0;
    std::string activeTileCollectionId;
    std::string activeCharacterSpritesetId;
    std::vector<TileCollection> tileCollections;
    std::vector<CharacterSpriteset> characterSpritesets;
    std::vector<ItemDefinition> itemDefinitions;
    std::vector<EnemyDefinition> enemyDefinitions;
    std::vector<ProjectileDefinition> projectileDefinitions;
    std::vector<WarpDefinition> warpDefinitions;
    std::vector<PowerupDef> powerups;
    std::vector<MapLoadData> maps;
};

class MapLoader {
public:
    static bool LoadWorldJson(const std::string& filePath, WorldLoadData& out);
    static bool SaveWorldJson(const std::string& filePath, const WorldLoadData& data);
};
