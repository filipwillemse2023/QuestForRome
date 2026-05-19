#include "World.hpp"

#include <algorithm>
#include <cmath>
#include <random>
#include <unordered_map>

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

std::pair<int, int> TileSizeForTileId(const std::vector<TileCollection>& collections, int tileId) {
    for (const TileCollection& collection : collections) {
        for (const TileDef& tile : collection.tiles) {
            if (tile.id == tileId) {
                return std::pair<int, int>{std::max(1, collection.tileWidth), std::max(1, collection.tileHeight)};
            }
        }
    }
    return std::pair<int, int>{16, 16};
}

std::pair<int, int> ApplyWarpSpawnOffset(int x, int y, WarpSpawnOffset offset, int tileW, int tileH) {
    switch (offset) {
        case WarpSpawnOffset::Above:
            return std::pair<int, int>{x, y - tileH};
        case WarpSpawnOffset::Below:
            return std::pair<int, int>{x, y + tileH};
        case WarpSpawnOffset::Left:
            return std::pair<int, int>{x - tileW, y};
        case WarpSpawnOffset::Right:
            return std::pair<int, int>{x + tileW, y};
        case WarpSpawnOffset::OnTop:
        default:
            return std::pair<int, int>{x, y};
    }
}

std::pair<float, float> EnemySizeForDefinition(const EnemyDefinition& definition) {
    if (!definition.moves.empty()) {
        const EnemyMoveDefinition& move = definition.moves.front();
        for (int dir = 0; dir < 4; ++dir) {
            const auto& frames = move.directionalFrames[static_cast<size_t>(dir)];
            if (!frames.empty()) {
                const EnemyMoveDefinition::AnimationFrame& frame = frames.front();
                float maxW = 12.0f;
                float maxH = 12.0f;
                for (const EnemyMoveDefinition::AnimationTile& tile : frame.tiles) {
                    const float right = static_cast<float>(tile.tileX * 16 + std::max(1, tile.sourceW));
                    const float bottom = static_cast<float>(tile.tileY * 16 + std::max(1, tile.sourceH));
                    maxW = std::max(maxW, right);
                    maxH = std::max(maxH, bottom);
                }
                return std::pair<float, float>{
                    std::max(1.0f, maxW),
                    std::max(1.0f, maxH)
                };
            }
        }
        if (!move.hitboxes.empty()) {
            const TileHitbox& hitbox = move.hitboxes.front();
            return std::pair<float, float>{static_cast<float>(std::max(1, hitbox.w)), static_cast<float>(std::max(1, hitbox.h))};
        }
    }
    return std::pair<float, float>{12.0f, 12.0f};
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
    weaponDefinitions_ = loaded.weaponDefinitions;
    projectileDefinitions_ = loaded.projectileDefinitions;
    globalSettings_ = loaded.globalSettings;
    tileCollections_ = loaded.tileCollections;
    characterSpritesets_ = loaded.characterSpritesets;
    activeCharacterSpritesetId_ = loaded.activeCharacterSpritesetId;
    if (activeCharacterSpritesetId_.empty() && !characterSpritesets_.empty()) {
        activeCharacterSpritesetId_ = characterSpritesets_.front().id;
    }
    tileSolidById_.clear();
    std::unordered_map<std::string, const ItemDefinition*> itemDefinitionsById;
    std::unordered_map<std::string, const EnemyDefinition*> enemyDefinitionsById;
    std::unordered_map<std::string, const WarpDefinition*> warpDefinitionsById;
    for (const ItemDefinition& itemDefinition : loaded.itemDefinitions) {
        itemDefinitionsById[itemDefinition.id] = &itemDefinition;
    }
    for (const WarpDefinition& warpDefinition : loaded.warpDefinitions) {
        warpDefinitionsById[warpDefinition.id] = &warpDefinition;
    }
    for (const EnemyDefinition& enemyDefinition : loaded.enemyDefinitions) {
        enemyDefinitionsById[enemyDefinition.id] = &enemyDefinition;
    }

    struct LoadedWarpPlacement {
        WarpPlacement placement;
    };
    std::vector<LoadedWarpPlacement> allWarpPlacements;
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
            for (const ItemPlacement& placement : screenData.itemPlacements) {
                auto definitionIt = itemDefinitionsById.find(placement.itemId);
                if (definitionIt == itemDefinitionsById.end() || definitionIt->second == nullptr) {
                    continue;
                }

                const ItemDefinition& definition = *definitionIt->second;
                Item item;
                item.itemId = definition.id;
                item.name = definition.name;
                item.mapId = placement.mapId;
                item.screenX = placement.screenX;
                item.screenY = placement.screenY;
                item.frames = definition.frames;
                item.animationSpeed = definition.animationSpeed;
                item.hitboxes = definition.hitboxes;
                item.triggerFunction = definition.triggerFunction;
                item.triggerParams = definition.triggerParams;
                item.type = definition.type;
                item.powerupId = definition.powerupId;
                item.legacyPickup = definition.legacyPickup;
                item.collected = placement.collected;
                item.bounds.x = placement.x;
                item.bounds.y = placement.y;
                if (!item.frames.empty()) {
                    item.bounds.w = static_cast<float>(std::max(1, item.frames.front().sourceW));
                    item.bounds.h = static_cast<float>(std::max(1, item.frames.front().sourceH));
                }
                items_.push_back(item);
            }
            for (const EnemyPlacement& placement : screenData.enemyPlacements) {
                auto definitionIt = enemyDefinitionsById.find(placement.enemyId);
                if (definitionIt == enemyDefinitionsById.end() || definitionIt->second == nullptr) {
                    continue;
                }

                const EnemyDefinition& definition = *definitionIt->second;
                Enemy enemy;
                enemy.enemyId = definition.id;
                enemy.name = definition.name;
                enemy.mapId = placement.mapId;
                enemy.screenX = placement.screenX;
                enemy.screenY = placement.screenY;
                enemy.health = std::max(1, definition.hitpoints);
                enemy.baseDamage = std::max(0, definition.baseDamage);
                enemy.immuneToKnockback = definition.immuneToKnockback;
                enemy.moves = definition.moves;
                enemy.knockbackAnimation = definition.knockbackAnimation;
                enemy.deathAnimation = definition.deathAnimation;
                const auto [enemyW, enemyH] = EnemySizeForDefinition(definition);
                enemy.bounds = SDL_FRect{placement.x, placement.y, enemyW, enemyH};

                if (!enemy.moves.empty()) {
                    const EnemyMoveDefinition& firstMove = enemy.moves.front();
                    enemy.speed = firstMove.speedTilesPerSecond * 16.0f;
                    enemy.behavior = firstMove.type == EnemyMoveType::StandStill ? "static" : "wander";
                    if (!firstMove.hitboxes.empty()) {
                        enemy.bounds.w = static_cast<float>(std::max(1, firstMove.hitboxes.front().w));
                        enemy.bounds.h = static_cast<float>(std::max(1, firstMove.hitboxes.front().h));
                    }
                }

                enemies_.push_back(enemy);
            }
            transitions_.insert(transitions_.end(), screenData.transitions.begin(), screenData.transitions.end());
            for (const WarpPlacement& placement : screenData.warpPlacements) {
                allWarpPlacements.push_back(LoadedWarpPlacement{placement});
            }
        }

        maps_.push_back(std::move(map));
    }

    std::unordered_map<std::string, std::array<const WarpPlacement*, 2>> placementsByWarp;
    for (const LoadedWarpPlacement& loadedPlacement : allWarpPlacements) {
        if (!InBounds(loadedPlacement.placement.mapId, loadedPlacement.placement.screenX, loadedPlacement.placement.screenY)) {
            continue;
        }
        auto& pair = placementsByWarp[loadedPlacement.placement.warpId];
        const int endpointIndex = std::clamp(loadedPlacement.placement.endpointIndex, 0, 1);
        pair[static_cast<size_t>(endpointIndex)] = &loadedPlacement.placement;
    }

    for (const auto& [warpId, pair] : placementsByWarp) {
        auto definitionIt = warpDefinitionsById.find(warpId);
        if (definitionIt == warpDefinitionsById.end() || definitionIt->second == nullptr) {
            continue;
        }
        const WarpDefinition& definition = *definitionIt->second;
        if (pair[0] == nullptr || pair[1] == nullptr) {
            continue;
        }

        for (int endpoint = 0; endpoint < 2; ++endpoint) {
            const WarpPlacement& source = *pair[static_cast<size_t>(endpoint)];
            const WarpPlacement& target = *pair[static_cast<size_t>(1 - endpoint)];
            const WarpEndpointDefinition& endpointDef = definition.endpoints[static_cast<size_t>(endpoint)];
            const WarpEndpointDefinition& targetEndpointDef = definition.endpoints[static_cast<size_t>(1 - endpoint)];

            WarpPoint runtimeWarp;
            runtimeWarp.warpId = definition.id;
            runtimeWarp.endpointIndex = endpoint;
            runtimeWarp.label = definition.name;
            runtimeWarp.fromMapId = source.mapId;
            runtimeWarp.fromScreenX = source.screenX;
            runtimeWarp.fromScreenY = source.screenY;
            runtimeWarp.targetMapId = target.mapId;
            runtimeWarp.targetScreenX = target.screenX;
            runtimeWarp.targetScreenY = target.screenY;
            const int baseSpawnX = static_cast<int>(std::lround(target.x));
            const int baseSpawnY = static_cast<int>(std::lround(target.y));
            const auto [tileW, tileH] = TileSizeForTileId(loaded.tileCollections, targetEndpointDef.tileId);
            const auto [spawnX, spawnY] = ApplyWarpSpawnOffset(baseSpawnX, baseSpawnY, targetEndpointDef.spawnOffset, tileW, tileH);
            runtimeWarp.spawnX = spawnX;
            runtimeWarp.spawnY = spawnY;
            runtimeWarp.kind = definition.kind;

            float minX = source.x;
            float minY = source.y;
            float maxX = source.x + 16.0f;
            float maxY = source.y + 16.0f;
            for (const TileHitbox& hitbox : endpointDef.hitboxes) {
                const SDL_FRect trigger{
                    source.x + static_cast<float>(hitbox.x),
                    source.y + static_cast<float>(hitbox.y),
                    static_cast<float>(std::max(1, hitbox.w)),
                    static_cast<float>(std::max(1, hitbox.h))
                };
                runtimeWarp.triggers.push_back(trigger);
                minX = std::min(minX, trigger.x);
                minY = std::min(minY, trigger.y);
                maxX = std::max(maxX, trigger.x + trigger.w);
                maxY = std::max(maxY, trigger.y + trigger.h);
            }
            runtimeWarp.trigger = SDL_FRect{minX, minY, std::max(1.0f, maxX - minX), std::max(1.0f, maxY - minY)};

            warps_.push_back(runtimeWarp);
        }
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

        bool intersects = false;
        if (!warp.triggers.empty()) {
            for (const SDL_FRect& trigger : warp.triggers) {
                intersects = !(rect.x + rect.w <= trigger.x ||
                               trigger.x + trigger.w <= rect.x ||
                               rect.y + rect.h <= trigger.y ||
                               trigger.y + trigger.h <= rect.y);
                if (intersects) {
                    break;
                }
            }
        } else {
            intersects = !(rect.x + rect.w <= warp.trigger.x ||
                           warp.trigger.x + warp.trigger.w <= rect.x ||
                           rect.y + rect.h <= warp.trigger.y ||
                           warp.trigger.y + warp.trigger.h <= rect.y);
        }
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
    weaponDefinitions_.clear();
    projectileDefinitions_.clear();
    globalSettings_ = GlobalSettings{};
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

    Enemy enemyA;
    enemyA.bounds = SDL_FRect{120.0f, 80.0f, 12.0f, 12.0f};
    enemyA.mapId = "overworld";
    enemyA.screenX = 1;
    enemyA.screenY = 0;
    enemyA.velocity = SDL_FPoint{22.0f, 0.0f};
    enemyA.directionTimer = 1.0f;
    enemyA.health = 2;
    enemyA.behavior = "wander";
    enemyA.speed = 24.0f;
    enemies_.push_back(enemyA);

    Enemy enemyB;
    enemyB.bounds = SDL_FRect{64.0f, 128.0f, 12.0f, 12.0f};
    enemyB.mapId = "overworld";
    enemyB.screenX = 2;
    enemyB.screenY = 2;
    enemyB.velocity = SDL_FPoint{0.0f, -20.0f};
    enemyB.directionTimer = 1.2f;
    enemyB.health = 3;
    enemyB.behavior = "wander";
    enemyB.speed = 24.0f;
    enemies_.push_back(enemyB);

    powerups_.push_back(PowerupDef{"speed_tonic", "Speed Tonic", "speed", 40, 8.0f});
    powerups_.push_back(PowerupDef{"legion_crest", "Legion Crest", "max_health", 1, 0.0f});

    transitions_.push_back(ScreenTransition{"overworld", 0, 0, "right", "overworld", 1, 0, 6, 88, TransitionKind::Fade});
    transitions_.push_back(ScreenTransition{"overworld", 1, 0, "left", "overworld", 0, 0, kScreenPixelWidth - 20, 88, TransitionKind::Fade});
}
