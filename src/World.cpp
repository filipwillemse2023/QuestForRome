#include "World.hpp"

#include <algorithm>
#include <random>

#include "MapLoader.hpp"

namespace {

RuntimeMap MakeBlankMap(const std::string& id, const std::string& name, int widthScreens, int heightScreens) {
    RuntimeMap map;
    map.id = id;
    map.name = name;
    map.widthScreens = widthScreens;
    map.heightScreens = heightScreens;
    map.defaultStartScreenX = 0;
    map.defaultStartScreenY = 0;
    map.screens.assign(static_cast<size_t>(widthScreens * heightScreens), Screen{});
    map.dungeonIds.assign(static_cast<size_t>(widthScreens * heightScreens), "");
    return map;
}

}  // namespace

World::World() {
    GenerateDefaultWorld();
}

bool World::LoadFromJsonOrDefault(const std::string& preferredPath) {
    WorldLoadData loaded;
    if (!MapLoader::LoadWorldJson(preferredPath, loaded)) {
        GenerateDefaultWorld();
        return false;
    }

    defaultMapId_ = loaded.defaultMapId;
    defaultStartScreenX_ = loaded.defaultStartScreenX;
    defaultStartScreenY_ = loaded.defaultStartScreenY;
    maps_.clear();
    items_.clear();
    enemies_.clear();
    transitions_.clear();
    warps_.clear();
    powerups_ = loaded.powerups;
    tileCollections_ = loaded.tileCollections;
    characterSpritesets_ = loaded.characterSpritesets;
    activeCharacterSpritesetId_ = loaded.activeCharacterSpritesetId;
    if (activeCharacterSpritesetId_.empty() && !characterSpritesets_.empty()) {
        activeCharacterSpritesetId_ = characterSpritesets_.front().id;
    }
    tileSolidById_.clear();
    for (const TileCollection& collection : tileCollections_) {
        for (const TileDef& tile : collection.tiles) {
            tileSolidById_[tile.id] = tile.solid;
        }
    }

    for (const MapLoadData& mapData : loaded.maps) {
        RuntimeMap map = MakeBlankMap(mapData.id, mapData.name, mapData.widthScreens, mapData.heightScreens);
        map.defaultStartScreenX = mapData.defaultStartScreenX;
        map.defaultStartScreenY = mapData.defaultStartScreenY;

        for (const ScreenLoadData& screenData : mapData.screens) {
            if (screenData.x < 0 || screenData.x >= map.widthScreens || screenData.y < 0 || screenData.y >= map.heightScreens) {
                continue;
            }

            map.screens[static_cast<size_t>(ToIndex(map, screenData.x, screenData.y))] = screenData.screen;
            map.dungeonIds[static_cast<size_t>(ToIndex(map, screenData.x, screenData.y))] = screenData.dungeonId;
            items_.insert(items_.end(), screenData.items.begin(), screenData.items.end());
            enemies_.insert(enemies_.end(), screenData.enemies.begin(), screenData.enemies.end());
            transitions_.insert(transitions_.end(), screenData.transitions.begin(), screenData.transitions.end());
            warps_.insert(warps_.end(), screenData.warps.begin(), screenData.warps.end());
        }

        maps_.push_back(std::move(map));
    }

    if (!HasMap(defaultMapId_) && !maps_.empty()) {
        defaultMapId_ = maps_.front().id;
        defaultStartScreenX_ = maps_.front().defaultStartScreenX;
        defaultStartScreenY_ = maps_.front().defaultStartScreenY;
    }

    return !maps_.empty();
}

bool World::HasMap(const std::string& mapId) const {
    return FindMap(mapId) != nullptr;
}

const CharacterSpriteset* World::ActiveCharacterSpriteset() const {
    if (!activeCharacterSpritesetId_.empty()) {
        for (const CharacterSpriteset& spriteset : characterSpritesets_) {
            if (spriteset.id == activeCharacterSpritesetId_) {
                return &spriteset;
            }
        }
    }
    if (!characterSpritesets_.empty()) {
        return &characterSpritesets_.front();
    }
    return nullptr;
}

bool World::InBounds(const std::string& mapId, int sx, int sy) const {
    const RuntimeMap* map = FindMap(mapId);
    if (!map) {
        return false;
    }
    return sx >= 0 && sx < map->widthScreens && sy >= 0 && sy < map->heightScreens;
}

bool World::IsTileSolid(const std::string& mapId, int sx, int sy, int tx, int ty) const {
    if (!InBounds(mapId, sx, sy)) {
        return true;
    }
    if (tx < 0 || tx >= kTilesWide || ty < 0 || ty >= kTilesHigh) {
        return true;
    }

    const size_t tileIndex = static_cast<size_t>(ty * kTilesWide + tx);
    const Screen& screen = GetScreen(mapId, sx, sy);
    for (int layer = 0; layer < kTileLayers; ++layer) {
        const int tileId = screen.tileLayerIds[static_cast<size_t>(layer)][tileIndex];
        if (tileId < 0) {
            continue;
        }
        const auto it = tileSolidById_.find(tileId);
        if (it != tileSolidById_.end() && it->second) {
            return true;
        }
    }
    return false;
}

SDL_FRect World::GetTileHitbox(const std::string& mapId, int sx, int sy, int tx, int ty) const {
    const std::vector<SDL_FRect> hitboxes = GetTileHitboxes(mapId, sx, sy, tx, ty);
    if (!hitboxes.empty()) {
        return hitboxes.front();
    }
    return SDL_FRect{0, 0, 0, 0};
}

std::vector<SDL_FRect> World::GetTileHitboxes(const std::string& mapId, int sx, int sy, int tx, int ty) const {
    if (!InBounds(mapId, sx, sy)) {
        return {};
    }
    if (tx < 0 || tx >= kTilesWide || ty < 0 || ty >= kTilesHigh) {
        return {};
    }

    const size_t tileIndex = static_cast<size_t>(ty * kTilesWide + tx);
    const Screen& screen = GetScreen(mapId, sx, sy);
    for (int layer = 0; layer < kTileLayers; ++layer) {
        const int tileId = screen.tileLayerIds[static_cast<size_t>(layer)][tileIndex];
        if (tileId < 0) {
            continue;
        }
        const TileDef* tileDef = FindTileDef(tileId);
        if (tileDef && tileDef->solid) {
            std::vector<SDL_FRect> hitboxes;
            if (!tileDef->hitboxes.empty()) {
                hitboxes.reserve(tileDef->hitboxes.size());
                for (const TileHitbox& hitbox : tileDef->hitboxes) {
                    const float worldX = static_cast<float>(tx * kTileSize + hitbox.x);
                    const float worldY = static_cast<float>(ty * kTileSize + hitbox.y);
                    const float worldW = static_cast<float>(hitbox.w);
                    const float worldH = static_cast<float>(hitbox.h);
                    hitboxes.push_back(SDL_FRect{worldX, worldY, worldW, worldH});
                }
            } else {
                const float worldX = static_cast<float>(tx * kTileSize + tileDef->hitboxX);
                const float worldY = static_cast<float>(ty * kTileSize + tileDef->hitboxY);
                const float worldW = static_cast<float>(tileDef->hitboxW);
                const float worldH = static_cast<float>(tileDef->hitboxH);
                hitboxes.push_back(SDL_FRect{worldX, worldY, worldW, worldH});
            }
            return hitboxes;
        }
    }
    return {};
}

int World::TileIdAt(const std::string& mapId, int sx, int sy, int tx, int ty) const {
    if (!InBounds(mapId, sx, sy) || tx < 0 || tx >= kTilesWide || ty < 0 || ty >= kTilesHigh) {
        return 0;
    }
    const Screen& screen = GetScreen(mapId, sx, sy);
    const size_t tileIndex = static_cast<size_t>(ty * kTilesWide + tx);
    for (int layer = kTileLayers - 1; layer >= 0; --layer) {
        const int tileId = screen.tileLayerIds[static_cast<size_t>(layer)][tileIndex];
        if (tileId >= 0) {
            return tileId;
        }
    }
    return 0;
}

const TileDef* World::FindTileDef(int tileId) const {
    for (const TileCollection& collection : tileCollections_) {
        for (const TileDef& tile : collection.tiles) {
            if (tile.id == tileId) {
                return &tile;
            }
        }
    }
    return nullptr;
}

const Screen& World::GetScreen(const std::string& mapId, int sx, int sy) const {
    const RuntimeMap* map = FindMap(mapId);
    return map->screens[static_cast<size_t>(ToIndex(*map, sx, sy))];
}

int World::WidthScreens(const std::string& mapId) const {
    const RuntimeMap* map = FindMap(mapId);
    return map ? map->widthScreens : 0;
}

int World::HeightScreens(const std::string& mapId) const {
    const RuntimeMap* map = FindMap(mapId);
    return map ? map->heightScreens : 0;
}

int World::MapStartScreenX(const std::string& mapId) const {
    const RuntimeMap* map = FindMap(mapId);
    return map ? map->defaultStartScreenX : 0;
}

int World::MapStartScreenY(const std::string& mapId) const {
    const RuntimeMap* map = FindMap(mapId);
    return map ? map->defaultStartScreenY : 0;
}

int World::ToIndex(const RuntimeMap& map, int sx, int sy) const {
    return sy * map.widthScreens + sx;
}

const RuntimeMap* World::FindMap(const std::string& mapId) const {
    for (const RuntimeMap& map : maps_) {
        if (map.id == mapId) {
            return &map;
        }
    }
    return nullptr;
}

RuntimeMap* World::FindMap(const std::string& mapId) {
    for (RuntimeMap& map : maps_) {
        if (map.id == mapId) {
            return &map;
        }
    }
    return nullptr;
}

const ScreenTransition* World::FindTransition(const std::string& fromMapId, int fromSX, int fromSY, const std::string& edge) const {
    for (const ScreenTransition& tr : transitions_) {
        if (tr.fromMapId == fromMapId && tr.fromScreenX == fromSX && tr.fromScreenY == fromSY && tr.edge == edge) {
            return &tr;
        }
    }
    return nullptr;
}

const WarpPoint* World::FindWarpAt(const std::string& mapId, int screenX, int screenY, const SDL_FRect& rect) const {
    for (const WarpPoint& warp : warps_) {
        if (warp.fromMapId != mapId || warp.fromScreenX != screenX || warp.fromScreenY != screenY) {
            continue;
        }

        const bool intersects = !(rect.x + rect.w <= warp.trigger.x ||
                                  warp.trigger.x + warp.trigger.w <= rect.x ||
                                  rect.y + rect.h <= warp.trigger.y ||
                                  warp.trigger.y + warp.trigger.h <= rect.y);
        if (intersects) {
            return &warp;
        }
    }
    return nullptr;
}

const PowerupDef* World::FindPowerupById(const std::string& id) const {
    for (const PowerupDef& p : powerups_) {
        if (p.id == id) {
            return &p;
        }
    }
    return nullptr;
}

std::string World::DungeonIdForScreen(const std::string& mapId, int sx, int sy) const {
    const RuntimeMap* map = FindMap(mapId);
    if (!map || !InBounds(mapId, sx, sy)) {
        return "";
    }
    return map->dungeonIds[static_cast<size_t>(ToIndex(*map, sx, sy))];
}

void World::CarveExits(RuntimeMap& map, Screen& screen, int sx, int sy) {
    const int midY = kTilesHigh / 2;
    const int midX = kTilesWide / 2;

    if (sx > 0) {
        screen.tileLayerIds[0][static_cast<size_t>(midY * kTilesWide)] = 0;
    }
    if (sx < map.widthScreens - 1) {
        screen.tileLayerIds[0][static_cast<size_t>(midY * kTilesWide + (kTilesWide - 1))] = 0;
    }
    if (sy > 0) {
        screen.tileLayerIds[0][static_cast<size_t>(midX)] = 0;
    }
    if (sy < map.heightScreens - 1) {
        screen.tileLayerIds[0][static_cast<size_t>((kTilesHigh - 1) * kTilesWide + midX)] = 0;
    }
}

void World::GenerateDefaultWorld() {
    defaultMapId_ = "overworld";
    defaultStartScreenX_ = 0;
    defaultStartScreenY_ = 0;
    maps_.clear();
    items_.clear();
    enemies_.clear();
    transitions_.clear();
    warps_.clear();
    powerups_.clear();
    tileCollections_.clear();
    tileSolidById_.clear();

    TileCollection legacy;
    legacy.id = "legacy";
    legacy.name = "Legacy";
    legacy.description = "Fallback tiles";
    legacy.tiles.push_back(TileDef{0, "Grass", "Walkable grass", 0, 0, false});
    legacy.tiles.push_back(TileDef{1, "Stone Wall", "Solid wall", 0, 0, true});
    legacy.tiles.push_back(TileDef{2, "Water", "Solid water", 0, 0, true});
    legacy.tiles.push_back(TileDef{3, "Sand", "Walkable sand", 0, 0, false});
    tileCollections_.push_back(legacy);
    for (const TileDef& tile : legacy.tiles) {
        tileSolidById_[tile.id] = tile.solid;
    }

    RuntimeMap overworld = MakeBlankMap("overworld", "Overworld", kDefaultWorldScreensWide, kDefaultWorldScreensHigh);

    std::mt19937 rng(1337);
    std::uniform_int_distribution<int> featureDist(0, 3);

    for (int sy = 0; sy < overworld.heightScreens; ++sy) {
        for (int sx = 0; sx < overworld.widthScreens; ++sx) {
            Screen& screen = overworld.screens[static_cast<size_t>(ToIndex(overworld, sx, sy))];

            for (int ty = 0; ty < kTilesHigh; ++ty) {
                for (int tx = 0; tx < kTilesWide; ++tx) {
                    const bool border = tx == 0 || tx == kTilesWide - 1 || ty == 0 || ty == kTilesHigh - 1;
                    screen.tileLayerIds[0][static_cast<size_t>(ty * kTilesWide + tx)] = border ? 1 : 0;
                }
            }

            CarveExits(overworld, screen, sx, sy);

            for (int i = 0; i < 8; ++i) {
                const int tx = 1 + (rng() % (kTilesWide - 2));
                const int ty = 1 + (rng() % (kTilesHigh - 2));
                const int feature = featureDist(rng);

                if (feature == 0) {
                    screen.tileLayerIds[0][static_cast<size_t>(ty * kTilesWide + tx)] = 1;
                } else if (feature == 1) {
                    screen.tileLayerIds[0][static_cast<size_t>(ty * kTilesWide + tx)] = 2;
                } else if (feature == 2) {
                    screen.tileLayerIds[0][static_cast<size_t>(ty * kTilesWide + tx)] = 3;
                }
            }

            CarveExits(overworld, screen, sx, sy);
        }
    }

    maps_.push_back(std::move(overworld));

    items_.push_back(Item{SDL_FRect{96.0f, 64.0f, 10.0f, 10.0f}, "overworld", 0, 0, ItemType::Coin, "", false});
    items_.push_back(Item{SDL_FRect{144.0f, 120.0f, 10.0f, 10.0f}, "overworld", 2, 1, ItemType::Wheat, "", false});

    enemies_.push_back(Enemy{SDL_FRect{120.0f, 80.0f, 12.0f, 12.0f}, "overworld", 1, 0, true, SDL_FPoint{22.0f, 0.0f}, 1.0f, 2, 0.0f, "wander", 24.0f});
    enemies_.push_back(Enemy{SDL_FRect{64.0f, 128.0f, 12.0f, 12.0f}, "overworld", 2, 2, true, SDL_FPoint{0.0f, -20.0f}, 1.2f, 3, 0.0f, "wander", 24.0f});

    powerups_.push_back(PowerupDef{"speed_tonic", "Speed Tonic", "speed", 40, 8.0f});
    powerups_.push_back(PowerupDef{"legion_crest", "Legion Crest", "max_health", 1, 0.0f});

    items_.push_back(Item{SDL_FRect{180.0f, 48.0f, 10.0f, 10.0f}, "overworld", 1, 1, ItemType::Powerup, "speed_tonic", false});

    transitions_.push_back(ScreenTransition{"overworld", 0, 0, "right", "overworld", 1, 0, 6, 88, TransitionKind::Fade});
    transitions_.push_back(ScreenTransition{"overworld", 1, 0, "left", "overworld", 0, 0, kScreenPixelWidth - 20, 88, TransitionKind::Fade});
}
