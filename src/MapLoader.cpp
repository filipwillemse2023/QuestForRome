#include "MapLoader.hpp"

#include <fstream>
#include <iomanip>

#include <nlohmann/json.hpp>

namespace {

using json = nlohmann::json;

ItemType ItemTypeFromString(const std::string& value) {
    if (value == "wheat") {
        return ItemType::Wheat;
    }
    if (value == "powerup") {
        return ItemType::Powerup;
    }
    return ItemType::Coin;
}

std::string ItemTypeToString(ItemType value) {
    switch (value) {
        case ItemType::Wheat:
            return "wheat";
        case ItemType::Powerup:
            return "powerup";
        case ItemType::Coin:
        default:
            return "coin";
    }
}

TransitionKind TransitionKindFromString(const std::string& value) {
    if (value == "instant") {
        return TransitionKind::Instant;
    }
    return TransitionKind::Fade;
}

std::string TransitionKindToString(TransitionKind value) {
    return value == TransitionKind::Instant ? "instant" : "fade";
}

std::vector<TileCollection> BuildLegacyTileCollections() {
    TileCollection collection;
    collection.id = "legacy";
    collection.name = "Legacy";
    collection.description = "Compatibility tiles";
    collection.imagePath = "";

    collection.tiles.push_back(TileDef{0, "Grass", "Walkable grass", 0, 0, false});
    collection.tiles.push_back(TileDef{1, "Stone Wall", "Solid wall", 0, 0, true});
    collection.tiles.push_back(TileDef{2, "Water", "Solid water", 0, 0, true});
    collection.tiles.push_back(TileDef{3, "Sand", "Walkable sand", 0, 0, false});
    return {collection};
}

TileHitbox MakeFullTileHitbox() {
    return TileHitbox{0, 0, 16, 16};
}

void SyncLegacyTileHitboxFields(TileDef& tile) {
    if (!tile.hitboxes.empty()) {
        tile.hitboxX = tile.hitboxes.front().x;
        tile.hitboxY = tile.hitboxes.front().y;
        tile.hitboxW = tile.hitboxes.front().w;
        tile.hitboxH = tile.hitboxes.front().h;
    } else {
        tile.hitboxX = 0;
        tile.hitboxY = 0;
        tile.hitboxW = 16;
        tile.hitboxH = 16;
    }
}

void LoadTileCollections(const json& root, WorldLoadData& out) {
    const json collectionsJson = root.value("tileCollections", json::array());
    if (!collectionsJson.is_array() || collectionsJson.empty()) {
        out.tileCollections = BuildLegacyTileCollections();
        out.activeTileCollectionId = out.tileCollections.front().id;
        return;
    }

    out.tileCollections.clear();
    for (const json& collectionJson : collectionsJson) {
        TileCollection collection;
        collection.id = collectionJson.value("id", "");
        collection.name = collectionJson.value("name", collection.id);
        collection.description = collectionJson.value("description", "");
        collection.imagePath = collectionJson.value("imagePath", "");
        collection.tileWidth = collectionJson.value("tileWidth", 16);
        collection.tileHeight = collectionJson.value("tileHeight", 16);
        collection.imageWidth = collectionJson.value("imageWidth", 0);
        collection.imageHeight = collectionJson.value("imageHeight", 0);
        if (collection.id.empty()) {
            continue;
        }

        for (const json& tileJson : collectionJson.value("tiles", json::array())) {
            TileDef tile;
            tile.id = tileJson.value("id", 0);
            tile.name = tileJson.value("name", "tile");
            tile.description = tileJson.value("description", "");
            tile.sourceX = tileJson.value("sourceX", 0);
            tile.sourceY = tileJson.value("sourceY", 0);
            tile.solid = tileJson.value("solid", false);
            const json hitboxesJson = tileJson.value("hitboxes", json::array());
            if (hitboxesJson.is_array() && !hitboxesJson.empty()) {
                for (const json& hitboxJson : hitboxesJson) {
                    TileHitbox hitbox;
                    hitbox.x = hitboxJson.value("x", 0);
                    hitbox.y = hitboxJson.value("y", 0);
                    hitbox.w = hitboxJson.value("w", 16);
                    hitbox.h = hitboxJson.value("h", 16);
                    tile.hitboxes.push_back(hitbox);
                }
            } else if (tile.solid) {
                tile.hitboxes.push_back(MakeFullTileHitbox());
            }
            if (tile.hitboxes.empty() && tile.solid) {
                tile.hitboxes.push_back(TileHitbox{tileJson.value("hitboxX", 0), tileJson.value("hitboxY", 0), tileJson.value("hitboxW", 16), tileJson.value("hitboxH", 16)});
            }
            SyncLegacyTileHitboxFields(tile);
            collection.tiles.push_back(tile);
        }

        out.tileCollections.push_back(collection);
    }

    if (out.tileCollections.empty()) {
        out.tileCollections = BuildLegacyTileCollections();
    }

    out.activeTileCollectionId = root.value("activeTileCollectionId", out.tileCollections.front().id);
}

void SaveTileCollections(json& root, const WorldLoadData& data) {
    root["activeTileCollectionId"] = data.activeTileCollectionId;
    root["tileCollections"] = json::array();
    for (const TileCollection& collection : data.tileCollections) {
        json collectionJson;
        collectionJson["id"] = collection.id;
        collectionJson["name"] = collection.name;
        collectionJson["description"] = collection.description;
        collectionJson["imagePath"] = collection.imagePath;
        collectionJson["tileWidth"] = collection.tileWidth;
        collectionJson["tileHeight"] = collection.tileHeight;
        collectionJson["imageWidth"] = collection.imageWidth;
        collectionJson["imageHeight"] = collection.imageHeight;
        collectionJson["tiles"] = json::array();
        for (const TileDef& tile : collection.tiles) {
            json tileJson;
            tileJson["id"] = tile.id;
            tileJson["name"] = tile.name;
            tileJson["description"] = tile.description;
            tileJson["sourceX"] = tile.sourceX;
            tileJson["sourceY"] = tile.sourceY;
            tileJson["solid"] = tile.solid;
            tileJson["hitboxX"] = tile.hitboxes.empty() ? tile.hitboxX : tile.hitboxes.front().x;
            tileJson["hitboxY"] = tile.hitboxes.empty() ? tile.hitboxY : tile.hitboxes.front().y;
            tileJson["hitboxW"] = tile.hitboxes.empty() ? tile.hitboxW : tile.hitboxes.front().w;
            tileJson["hitboxH"] = tile.hitboxes.empty() ? tile.hitboxH : tile.hitboxes.front().h;
            tileJson["hitboxes"] = json::array();
            for (const TileHitbox& hitbox : tile.hitboxes) {
                json hitboxJson;
                hitboxJson["x"] = hitbox.x;
                hitboxJson["y"] = hitbox.y;
                hitboxJson["w"] = hitbox.w;
                hitboxJson["h"] = hitbox.h;
                tileJson["hitboxes"].push_back(hitboxJson);
            }
            collectionJson["tiles"].push_back(tileJson);
        }
        root["tileCollections"].push_back(collectionJson);
    }
}

void LoadCharacterSpritesets(const json& root, WorldLoadData& out) {
    out.activeCharacterSpritesetId = root.value("activeCharacterSpritesetId", "");
    const json spritesets = root.value("characterSpritesets", json::array());
    for (const json& spritesetJson : spritesets) {
        CharacterSpriteset spriteset;
        spriteset.id = spritesetJson.value("id", "");
        spriteset.name = spritesetJson.value("name", "character");
        spriteset.description = spritesetJson.value("description", "");
        spriteset.imagePath = spritesetJson.value("imagePath", "");
        spriteset.tileWidth = spritesetJson.value("tileWidth", 16);
        spriteset.tileHeight = spritesetJson.value("tileHeight", 16);
        spriteset.imageWidth = spritesetJson.value("imageWidth", 0);
        spriteset.imageHeight = spritesetJson.value("imageHeight", 0);

        if (spriteset.id.empty()) {
            continue;
        }

        for (const json& actionJson : spritesetJson.value("actions", json::array())) {
            CharacterAction action;
            action.id = actionJson.value("id", "");
            action.name = actionJson.value("name", "action");
            action.animationSpeed = actionJson.value("animationSpeed", 1.0f);
            for (const json& hitboxJson : actionJson.value("hitboxes", json::array())) {
                TileHitbox hitbox;
                hitbox.x = hitboxJson.value("x", 0);
                hitbox.y = hitboxJson.value("y", 0);
                hitbox.w = hitboxJson.value("w", 12);
                hitbox.h = hitboxJson.value("h", 12);
                action.hitboxes.push_back(hitbox);
            }

            const json directional = actionJson.value("directionalFrames", json::object());
            if (directional.is_object() && !directional.empty()) {
                const std::array<std::string, 4> keys = {"south", "west", "east", "north"};
                for (int dir = 0; dir < 4; ++dir) {
                    for (const json& frameJson : directional.value(keys[static_cast<size_t>(dir)], json::array())) {
                        CharacterFrame frame;
                        frame.tileX = frameJson.value("tileX", 0);
                        frame.tileY = frameJson.value("tileY", 0);
                        frame.frameWidth = frameJson.value("frameWidth", 1);
                        frame.frameHeight = frameJson.value("frameHeight", 2);
                        action.directionalFrames[static_cast<size_t>(dir)].push_back(frame);
                    }
                }
            } else {
                // Backward compatibility with older single-frame-list format.
                std::vector<CharacterFrame> legacyFrames;
                for (const json& frameJson : actionJson.value("frames", json::array())) {
                    CharacterFrame frame;
                    frame.tileX = frameJson.value("tileX", 0);
                    frame.tileY = frameJson.value("tileY", 0);
                    frame.frameWidth = frameJson.value("frameWidth", 1);
                    frame.frameHeight = frameJson.value("frameHeight", 2);
                    legacyFrames.push_back(frame);
                }
                if (legacyFrames.empty()) {
                    legacyFrames.push_back(CharacterFrame{});
                }
                for (int dir = 0; dir < 4; ++dir) {
                    action.directionalFrames[static_cast<size_t>(dir)] = legacyFrames;
                }
            }

            for (int dir = 0; dir < 4; ++dir) {
                if (action.directionalFrames[static_cast<size_t>(dir)].empty()) {
                    action.directionalFrames[static_cast<size_t>(dir)].push_back(CharacterFrame{});
                }
            }

            spriteset.actions.push_back(action);
        }

        out.characterSpritesets.push_back(spriteset);
    }
}

void SaveCharacterSpritesets(json& root, const WorldLoadData& data) {
    root["activeCharacterSpritesetId"] = data.activeCharacterSpritesetId;
    root["characterSpritesets"] = json::array();
    for (const CharacterSpriteset& spriteset : data.characterSpritesets) {
        json spritesetJson;
        spritesetJson["id"] = spriteset.id;
        spritesetJson["name"] = spriteset.name;
        spritesetJson["description"] = spriteset.description;
        spritesetJson["imagePath"] = spriteset.imagePath;
        spritesetJson["tileWidth"] = spriteset.tileWidth;
        spritesetJson["tileHeight"] = spriteset.tileHeight;
        spritesetJson["imageWidth"] = spriteset.imageWidth;
        spritesetJson["imageHeight"] = spriteset.imageHeight;
        spritesetJson["actions"] = json::array();

        for (const CharacterAction& action : spriteset.actions) {
            json actionJson;
            actionJson["id"] = action.id;
            actionJson["name"] = action.name;
            actionJson["animationSpeed"] = action.animationSpeed;
            actionJson["hitboxes"] = json::array();
            for (const TileHitbox& hitbox : action.hitboxes) {
                json hitboxJson;
                hitboxJson["x"] = hitbox.x;
                hitboxJson["y"] = hitbox.y;
                hitboxJson["w"] = hitbox.w;
                hitboxJson["h"] = hitbox.h;
                actionJson["hitboxes"].push_back(hitboxJson);
            }
            actionJson["directionalFrames"] = json::object();

            const std::array<std::string, 4> keys = {"south", "west", "east", "north"};
            for (int dir = 0; dir < 4; ++dir) {
                json arr = json::array();
                for (const CharacterFrame& frame : action.directionalFrames[static_cast<size_t>(dir)]) {
                    json frameJson;
                    frameJson["tileX"] = frame.tileX;
                    frameJson["tileY"] = frame.tileY;
                    frameJson["frameWidth"] = frame.frameWidth;
                    frameJson["frameHeight"] = frame.frameHeight;
                    arr.push_back(frameJson);
                }
                actionJson["directionalFrames"][keys[static_cast<size_t>(dir)]] = arr;
            }

            spritesetJson["actions"].push_back(actionJson);
        }

        root["characterSpritesets"].push_back(spritesetJson);
    }
}

void LoadTiles(const json& source, Screen& screen) {
    const json tileLayers = source.value("tileLayers", json::array());
    if (tileLayers.is_array() && !tileLayers.empty()) {
        for (int layer = 0; layer < kTileLayers; ++layer) {
            if (layer >= static_cast<int>(tileLayers.size())) {
                continue;
            }
            const json& layerTiles = tileLayers[static_cast<size_t>(layer)];
            if (!layerTiles.is_array() || layerTiles.size() != static_cast<size_t>(kTilesPerScreen)) {
                continue;
            }
            for (int i = 0; i < kTilesPerScreen; ++i) {
                screen.tileLayerIds[static_cast<size_t>(layer)][static_cast<size_t>(i)] = layerTiles[static_cast<size_t>(i)].get<int>();
            }
        }
        return;
    }

    const json tiles = source.value("tiles", json::array());
    if (tiles.is_array() && tiles.size() == static_cast<size_t>(kTilesPerScreen)) {
        for (int i = 0; i < kTilesPerScreen; ++i) {
            screen.tileLayerIds[0][static_cast<size_t>(i)] = tiles[static_cast<size_t>(i)].get<int>();
        }
    }
}

void SaveTiles(json& screenJson, const Screen& screen) {
    screenJson["tileLayers"] = json::array();
    for (int layer = 0; layer < kTileLayers; ++layer) {
        json layerTiles = json::array();
        for (int tileId : screen.tileLayerIds[static_cast<size_t>(layer)]) {
            layerTiles.push_back(tileId);
        }
        screenJson["tileLayers"].push_back(layerTiles);
    }
}

void LoadItems(const json& source, const std::string& mapId, int screenX, int screenY, std::vector<Item>& out) {
    for (const json& itemJson : source.value("items", json::array())) {
        Item item;
        item.mapId = mapId;
        item.screenX = screenX;
        item.screenY = screenY;
        item.bounds.x = static_cast<float>(itemJson.value("x", 0));
        item.bounds.y = static_cast<float>(itemJson.value("y", 0));
        item.bounds.w = static_cast<float>(itemJson.value("w", 10));
        item.bounds.h = static_cast<float>(itemJson.value("h", 10));
        item.type = ItemTypeFromString(itemJson.value("type", "coin"));
        item.powerupId = itemJson.value("powerupId", "");
        out.push_back(item);
    }
}

void SaveItems(json& screenJson, const std::vector<Item>& items) {
    screenJson["items"] = json::array();
    for (const Item& item : items) {
        json itemJson;
        itemJson["x"] = static_cast<int>(item.bounds.x);
        itemJson["y"] = static_cast<int>(item.bounds.y);
        itemJson["w"] = static_cast<int>(item.bounds.w);
        itemJson["h"] = static_cast<int>(item.bounds.h);
        itemJson["type"] = ItemTypeToString(item.type);
        if (!item.powerupId.empty()) {
            itemJson["powerupId"] = item.powerupId;
        }
        screenJson["items"].push_back(itemJson);
    }
}

void LoadEnemies(const json& source, const std::string& mapId, int screenX, int screenY, std::vector<Enemy>& out) {
    for (const json& enemyJson : source.value("enemies", json::array())) {
        Enemy enemy;
        enemy.mapId = mapId;
        enemy.screenX = screenX;
        enemy.screenY = screenY;
        enemy.bounds.x = static_cast<float>(enemyJson.value("x", 0));
        enemy.bounds.y = static_cast<float>(enemyJson.value("y", 0));
        enemy.bounds.w = static_cast<float>(enemyJson.value("w", 12));
        enemy.bounds.h = static_cast<float>(enemyJson.value("h", 12));
        enemy.health = enemyJson.value("hp", 2);
        enemy.behavior = enemyJson.value("behavior", "wander");
        enemy.speed = enemyJson.value("speed", 24.0f);
        enemy.velocity.x = enemyJson.value("vx", enemy.speed);
        enemy.velocity.y = enemyJson.value("vy", 0.0f);
        out.push_back(enemy);
    }
}

void SaveEnemies(json& screenJson, const std::vector<Enemy>& enemies) {
    screenJson["enemies"] = json::array();
    for (const Enemy& enemy : enemies) {
        json enemyJson;
        enemyJson["x"] = static_cast<int>(enemy.bounds.x);
        enemyJson["y"] = static_cast<int>(enemy.bounds.y);
        enemyJson["w"] = static_cast<int>(enemy.bounds.w);
        enemyJson["h"] = static_cast<int>(enemy.bounds.h);
        enemyJson["hp"] = enemy.health;
        enemyJson["behavior"] = enemy.behavior;
        enemyJson["speed"] = enemy.speed;
        enemyJson["vx"] = enemy.velocity.x;
        enemyJson["vy"] = enemy.velocity.y;
        screenJson["enemies"].push_back(enemyJson);
    }
}

void LoadTransitions(const json& source, const std::string& mapId, int screenX, int screenY, std::vector<ScreenTransition>& out) {
    for (const json& trJson : source.value("transitions", json::array())) {
        ScreenTransition tr;
        tr.fromMapId = mapId;
        tr.fromScreenX = screenX;
        tr.fromScreenY = screenY;
        tr.edge = trJson.value("edge", "right");
        tr.toMapId = trJson.value("toMapId", mapId);
        tr.toScreenX = trJson.value("toX", screenX);
        tr.toScreenY = trJson.value("toY", screenY);
        tr.spawnX = trJson.value("spawnX", 8);
        tr.spawnY = trJson.value("spawnY", 8);
        tr.kind = TransitionKindFromString(trJson.value("kind", "fade"));
        out.push_back(tr);
    }
}

void SaveTransitions(json& screenJson, const std::vector<ScreenTransition>& transitions, const std::string& mapId) {
    screenJson["transitions"] = json::array();
    for (const ScreenTransition& tr : transitions) {
        json trJson;
        trJson["edge"] = tr.edge;
        if (tr.toMapId != mapId) {
            trJson["toMapId"] = tr.toMapId;
        }
        trJson["toX"] = tr.toScreenX;
        trJson["toY"] = tr.toScreenY;
        trJson["spawnX"] = tr.spawnX;
        trJson["spawnY"] = tr.spawnY;
        trJson["kind"] = TransitionKindToString(tr.kind);
        screenJson["transitions"].push_back(trJson);
    }
}

void LoadWarps(const json& source, const std::string& mapId, int screenX, int screenY, std::vector<WarpPoint>& out) {
    for (const json& warpJson : source.value("warps", json::array())) {
        WarpPoint warp;
        warp.fromMapId = mapId;
        warp.fromScreenX = screenX;
        warp.fromScreenY = screenY;
        warp.label = warpJson.value("label", "warp");
        warp.trigger.x = static_cast<float>(warpJson.value("x", 112));
        warp.trigger.y = static_cast<float>(warpJson.value("y", 72));
        warp.trigger.w = static_cast<float>(warpJson.value("w", 32));
        warp.trigger.h = static_cast<float>(warpJson.value("h", 32));
        warp.targetMapId = warpJson.value("toMapId", mapId);
        warp.targetScreenX = warpJson.value("toX", screenX);
        warp.targetScreenY = warpJson.value("toY", screenY);
        warp.spawnX = warpJson.value("spawnX", 120);
        warp.spawnY = warpJson.value("spawnY", 80);
        warp.kind = TransitionKindFromString(warpJson.value("kind", "instant"));
        out.push_back(warp);
    }
}

void SaveWarps(json& screenJson, const std::vector<WarpPoint>& warps, const std::string& mapId) {
    screenJson["warps"] = json::array();
    for (const WarpPoint& warp : warps) {
        json warpJson;
        warpJson["label"] = warp.label;
        warpJson["x"] = static_cast<int>(warp.trigger.x);
        warpJson["y"] = static_cast<int>(warp.trigger.y);
        warpJson["w"] = static_cast<int>(warp.trigger.w);
        warpJson["h"] = static_cast<int>(warp.trigger.h);
        if (warp.targetMapId != mapId) {
            warpJson["toMapId"] = warp.targetMapId;
        }
        warpJson["toX"] = warp.targetScreenX;
        warpJson["toY"] = warp.targetScreenY;
        warpJson["spawnX"] = warp.spawnX;
        warpJson["spawnY"] = warp.spawnY;
        warpJson["kind"] = TransitionKindToString(warp.kind);
        screenJson["warps"].push_back(warpJson);
    }
}

ScreenLoadData LoadScreen(const json& source, const std::string& mapId) {
    ScreenLoadData screenData;
    screenData.x = source.value("x", -1);
    screenData.y = source.value("y", -1);
    screenData.dungeonId = source.value("dungeonId", "");
    LoadTiles(source, screenData.screen);
    if (screenData.x < 0 || screenData.y < 0) {
        return screenData;
    }
    LoadItems(source, mapId, screenData.x, screenData.y, screenData.items);
    LoadEnemies(source, mapId, screenData.x, screenData.y, screenData.enemies);
    LoadTransitions(source, mapId, screenData.x, screenData.y, screenData.transitions);
    LoadWarps(source, mapId, screenData.x, screenData.y, screenData.warps);
    return screenData;
}

json SaveScreen(const ScreenLoadData& screenData, const std::string& mapId) {
    json screenJson;
    screenJson["x"] = screenData.x;
    screenJson["y"] = screenData.y;
    if (!screenData.dungeonId.empty()) {
        screenJson["dungeonId"] = screenData.dungeonId;
    }
    SaveTiles(screenJson, screenData.screen);
    SaveItems(screenJson, screenData.items);
    SaveEnemies(screenJson, screenData.enemies);
    SaveTransitions(screenJson, screenData.transitions, mapId);
    SaveWarps(screenJson, screenData.warps, mapId);
    return screenJson;
}

bool LoadSingleMapFormat(const json& root, WorldLoadData& out) {
    MapLoadData map;
    map.id = root.value("defaultMapId", "overworld");
    map.name = root.value("name", "Overworld");
    map.widthScreens = root.value("worldWidthScreens", 0);
    map.heightScreens = root.value("worldHeightScreens", 0);
    map.defaultStartScreenX = root.value("defaultStartScreenX", 0);
    map.defaultStartScreenY = root.value("defaultStartScreenY", 0);

    if (map.widthScreens <= 0 || map.heightScreens <= 0) {
        return false;
    }

    out.defaultMapId = map.id;
    out.defaultStartScreenX = map.defaultStartScreenX;
    out.defaultStartScreenY = map.defaultStartScreenY;

    for (const json& s : root.value("screens", json::array())) {
        ScreenLoadData screenData = LoadScreen(s, map.id);
        if (screenData.x < 0 || screenData.y < 0) {
            continue;
        }
        map.screens.push_back(screenData);
    }

    out.maps.push_back(map);
    return true;
}

bool LoadMultiMapFormat(const json& root, WorldLoadData& out) {
    for (const json& mapJson : root.value("maps", json::array())) {
        MapLoadData map;
        map.id = mapJson.value("id", "map");
        map.name = mapJson.value("name", map.id);
        map.widthScreens = mapJson.value("widthScreens", 0);
        map.heightScreens = mapJson.value("heightScreens", 0);
        map.defaultStartScreenX = mapJson.value("defaultStartScreenX", 0);
        map.defaultStartScreenY = mapJson.value("defaultStartScreenY", 0);
        if (map.id.empty() || map.widthScreens <= 0 || map.heightScreens <= 0) {
            continue;
        }

        for (const json& screenJson : mapJson.value("screens", json::array())) {
            ScreenLoadData screenData = LoadScreen(screenJson, map.id);
            if (screenData.x < 0 || screenData.y < 0) {
                continue;
            }
            map.screens.push_back(screenData);
        }

        out.maps.push_back(map);
    }

    if (out.maps.empty()) {
        return false;
    }

    out.defaultMapId = root.value("defaultMapId", out.maps.front().id);
    out.defaultStartScreenX = root.value("defaultStartScreenX", out.maps.front().defaultStartScreenX);
    out.defaultStartScreenY = root.value("defaultStartScreenY", out.maps.front().defaultStartScreenY);
    return true;
}

}  // namespace

bool MapLoader::LoadWorldJson(const std::string& filePath, WorldLoadData& out) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        return false;
    }

    json root;
    try {
        file >> root;
    } catch (...) {
        return false;
    }

    out = WorldLoadData{};
    out.formatVersion = root.value("formatVersion", 1);
    LoadTileCollections(root, out);
    LoadCharacterSpritesets(root, out);

    for (const json& p : root.value("powerups", json::array())) {
        PowerupDef def;
        def.id = p.value("id", "");
        def.name = p.value("name", def.id);
        def.effect = p.value("effect", "none");
        def.magnitude = p.value("magnitude", 0);
        def.durationSeconds = p.value("durationSeconds", 0.0f);
        if (!def.id.empty()) {
            out.powerups.push_back(def);
        }
    }

    if (root.contains("maps")) {
        return LoadMultiMapFormat(root, out);
    }
    return LoadSingleMapFormat(root, out);
}

bool MapLoader::SaveWorldJson(const std::string& filePath, const WorldLoadData& data) {
    json root;
    root["formatVersion"] = 5;
    root["defaultMapId"] = data.defaultMapId;
    root["defaultStartScreenX"] = data.defaultStartScreenX;
    root["defaultStartScreenY"] = data.defaultStartScreenY;
    SaveTileCollections(root, data);
    SaveCharacterSpritesets(root, data);

    root["powerups"] = json::array();
    for (const PowerupDef& def : data.powerups) {
        json p;
        p["id"] = def.id;
        p["name"] = def.name;
        p["effect"] = def.effect;
        p["magnitude"] = def.magnitude;
        p["durationSeconds"] = def.durationSeconds;
        root["powerups"].push_back(p);
    }

    root["maps"] = json::array();
    for (const MapLoadData& map : data.maps) {
        json mapJson;
        mapJson["id"] = map.id;
        mapJson["name"] = map.name;
        mapJson["widthScreens"] = map.widthScreens;
        mapJson["heightScreens"] = map.heightScreens;
        mapJson["defaultStartScreenX"] = map.defaultStartScreenX;
        mapJson["defaultStartScreenY"] = map.defaultStartScreenY;
        mapJson["screens"] = json::array();
        for (const ScreenLoadData& screen : map.screens) {
            mapJson["screens"].push_back(SaveScreen(screen, map.id));
        }
        root["maps"].push_back(mapJson);
    }

    std::ofstream out(filePath);
    if (!out.is_open()) {
        return false;
    }

    out << std::setw(2) << root << '\n';
    return out.good();
}
