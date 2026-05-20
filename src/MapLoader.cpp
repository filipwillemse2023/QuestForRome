#include "MapLoader.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <unordered_set>

#include <nlohmann/json.hpp>

namespace {

using json = nlohmann::json;

TransitionKind TransitionKindFromString(const std::string& value);
std::string TransitionKindToString(TransitionKind value);
WarpSpawnOffset WarpSpawnOffsetFromString(const std::string& value);
std::string WarpSpawnOffsetToString(WarpSpawnOffset value);
EnemyMoveType EnemyMoveTypeFromString(const std::string& value);
std::string EnemyMoveTypeToString(EnemyMoveType value);
EnemyReappearMode EnemyReappearModeFromString(const std::string& value);
std::string EnemyReappearModeToString(EnemyReappearMode value);
ProjectileMovementType ProjectileMovementTypeFromString(const std::string& value);
std::string ProjectileMovementTypeToString(ProjectileMovementType value);

const std::array<std::string, 4> kDirectionalFrameKeys = {"south", "west", "east", "north"};

std::string NormalizeTextGlyphMapForStorage(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (unsigned char ch : value) {
        if (ch == '\n' || ch == '\r') {
            continue;
        }
        if (ch == '_') {
            out.push_back(' ');
            continue;
        }
        if (ch >= 32 && ch <= 126) {
            out.push_back(static_cast<char>(ch));
        } else {
            out.push_back(' ');
        }
    }
    return out;
}

ItemType ItemTypeFromString(const std::string& value) {
    if (value == "wheat") {
        return ItemType::Wheat;
    }
    if (value == "powerup") {
        return ItemType::Powerup;
    }
    return ItemType::Coin;
}

ContainerContentKind ContainerContentKindFromString(const std::string& value) {
    if (value == "item") {
        return ContainerContentKind::Item;
    }
    if (value == "weapon") {
        return ContainerContentKind::Weapon;
    }
    return ContainerContentKind::None;
}

std::string ContainerContentKindToString(ContainerContentKind value) {
    switch (value) {
        case ContainerContentKind::Item:
            return "item";
        case ContainerContentKind::Weapon:
            return "weapon";
        case ContainerContentKind::None:
        default:
            return "none";
    }
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

ItemTriggerFunction ItemTriggerFunctionFromString(const std::string& value) {
    if (value == "increase_coins") {
        return ItemTriggerFunction::IncreaseCoins;
    }
    if (value == "increase_health") {
        return ItemTriggerFunction::IncreaseHealth;
    }
    if (value == "increase_max_health") {
        return ItemTriggerFunction::IncreaseMaxHealth;
    }
    if (value == "apply_speed_boost") {
        return ItemTriggerFunction::ApplySpeedBoost;
    }
    if (value == "heart_piece") {
        return ItemTriggerFunction::HeartPiece;
    }
    return ItemTriggerFunction::None;
}

std::string ItemTriggerFunctionToString(ItemTriggerFunction value) {
    switch (value) {
        case ItemTriggerFunction::IncreaseCoins:
            return "increase_coins";
        case ItemTriggerFunction::IncreaseHealth:
            return "increase_health";
        case ItemTriggerFunction::IncreaseMaxHealth:
            return "increase_max_health";
        case ItemTriggerFunction::ApplySpeedBoost:
            return "apply_speed_boost";
        case ItemTriggerFunction::HeartPiece:
            return "heart_piece";
        case ItemTriggerFunction::None:
        default:
            return "none";
    }
}

ProjectileMovementType ProjectileMovementTypeFromString(const std::string& value) {
    if (value == "fixed_function") {
        return ProjectileMovementType::FixedFunction;
    }
    if (value == "straight_limited_distance") {
        return ProjectileMovementType::StraightLimitedDistance;
    }
    if (value == "homing") {
        return ProjectileMovementType::Homing;
    }
    return ProjectileMovementType::TrackPlayer;
}

std::string ProjectileMovementTypeToString(ProjectileMovementType value) {
    switch (value) {
        case ProjectileMovementType::FixedFunction:
            return "fixed_function";
        case ProjectileMovementType::StraightLimitedDistance:
            return "straight_limited_distance";
        case ProjectileMovementType::Homing:
            return "homing";
        case ProjectileMovementType::TrackPlayer:
        default:
            return "track_player";
    }
}

void LoadProjectileFrameArray(const json& sourceFrames, std::vector<ItemAnimationFrame>& outFrames) {
    for (const json& frameJson : sourceFrames) {
        ItemAnimationFrame frame;
        frame.sourceImagePath = frameJson.value("sourceImagePath", "");
        frame.sourceLabel = frameJson.value("sourceLabel", "");
        frame.sourceX = frameJson.value("sourceX", 0);
        frame.sourceY = frameJson.value("sourceY", 0);
        frame.sourceW = frameJson.value("sourceW", 16);
        frame.sourceH = frameJson.value("sourceH", 16);
        outFrames.push_back(frame);
    }
}

json SaveProjectileFrameArray(const std::vector<ItemAnimationFrame>& frames) {
    json out = json::array();
    for (const ItemAnimationFrame& frame : frames) {
        json frameJson;
        frameJson["sourceImagePath"] = frame.sourceImagePath;
        frameJson["sourceLabel"] = frame.sourceLabel;
        frameJson["sourceX"] = frame.sourceX;
        frameJson["sourceY"] = frame.sourceY;
        frameJson["sourceW"] = frame.sourceW;
        frameJson["sourceH"] = frame.sourceH;
        out.push_back(frameJson);
    }
    return out;
}

void LoadItemDefinitionFields(const json& itemJson, ItemDefinition& item) {
    item.name = itemJson.value("name", item.name);
    item.animationSpeed = itemJson.value("animationSpeed", 0.0f);

    for (const json& frameJson : itemJson.value("frames", json::array())) {
        ItemAnimationFrame frame;
        frame.sourceImagePath = frameJson.value("sourceImagePath", "");
        frame.sourceLabel = frameJson.value("sourceLabel", "");
        frame.sourceX = frameJson.value("sourceX", 0);
        frame.sourceY = frameJson.value("sourceY", 0);
        frame.sourceW = frameJson.value("sourceW", 16);
        frame.sourceH = frameJson.value("sourceH", 16);
        item.frames.push_back(frame);
    }

    item.emptyFrames.clear();
    for (const json& frameJson : itemJson.value("emptyFrames", json::array())) {
        ItemAnimationFrame frame;
        frame.sourceImagePath = frameJson.value("sourceImagePath", "");
        frame.sourceLabel = frameJson.value("sourceLabel", "");
        frame.sourceX = frameJson.value("sourceX", 0);
        frame.sourceY = frameJson.value("sourceY", 0);
        frame.sourceW = frameJson.value("sourceW", 16);
        frame.sourceH = frameJson.value("sourceH", 16);
        item.emptyFrames.push_back(frame);
    }
    item.emptyAnimationSpeed = itemJson.value("emptyAnimationSpeed", 0.0f);
    item.isContainer = itemJson.value("isContainer", false);
    item.importantItem = itemJson.value("importantItem", false);

    for (const json& hitboxJson : itemJson.value("hitboxes", json::array())) {
        TileHitbox hitbox;
        hitbox.x = hitboxJson.value("x", 0);
        hitbox.y = hitboxJson.value("y", 0);
        hitbox.w = hitboxJson.value("w", 16);
        hitbox.h = hitboxJson.value("h", 16);
        item.hitboxes.push_back(hitbox);
    }
                json frameJson;
    item.triggerFunction = ItemTriggerFunctionFromString(itemJson.value("function", "none"));
    for (const json& paramJson : itemJson.value("params", json::array())) {
        ItemTriggerParam param;
        param.key = paramJson.value("key", "");
        param.value = paramJson.value("value", "");
        if (!param.key.empty()) {
            item.triggerParams.push_back(param);
        }
    }

    if (item.frames.empty() && itemJson.contains("type")) {
        item.legacyPickup = true;
        item.type = ItemTypeFromString(itemJson.value("type", "coin"));
        item.powerupId = itemJson.value("powerupId", "");
        if (item.name == "item") {
            item.name = ItemTypeToString(item.type);
        }
    }
}

void SaveItemDefinitionFields(json& itemJson, const ItemDefinition& item) {
    itemJson["name"] = item.name;
    itemJson["animationSpeed"] = item.animationSpeed;
    itemJson["frames"] = json::array();
    for (const ItemAnimationFrame& frame : item.frames) {
        json frameJson;
        frameJson["sourceImagePath"] = frame.sourceImagePath;
        frameJson["sourceLabel"] = frame.sourceLabel;
        frameJson["sourceX"] = frame.sourceX;
        frameJson["sourceY"] = frame.sourceY;
        frameJson["sourceW"] = frame.sourceW;
        frameJson["sourceH"] = frame.sourceH;
        itemJson["frames"].push_back(frameJson);
    }
    itemJson["isContainer"] = item.isContainer;
    itemJson["importantItem"] = item.importantItem;
    itemJson["emptyAnimationSpeed"] = item.emptyAnimationSpeed;
    itemJson["emptyFrames"] = json::array();
    for (const ItemAnimationFrame& frame : item.emptyFrames) {
        json frameJson;
        frameJson["sourceImagePath"] = frame.sourceImagePath;
        frameJson["sourceLabel"] = frame.sourceLabel;
        frameJson["sourceX"] = frame.sourceX;
        frameJson["sourceY"] = frame.sourceY;
        frameJson["sourceW"] = frame.sourceW;
        frameJson["sourceH"] = frame.sourceH;
        itemJson["emptyFrames"].push_back(frameJson);
    }
    itemJson["hitboxes"] = json::array();
    for (const TileHitbox& hitbox : item.hitboxes) {
        json hitboxJson;
        hitboxJson["x"] = hitbox.x;
        hitboxJson["y"] = hitbox.y;
        hitboxJson["w"] = hitbox.w;
        hitboxJson["h"] = hitbox.h;
        itemJson["hitboxes"].push_back(hitboxJson);
    }
    itemJson["function"] = ItemTriggerFunctionToString(item.triggerFunction);
    itemJson["params"] = json::array();
    for (const ItemTriggerParam& param : item.triggerParams) {
        json paramJson;
        paramJson["key"] = param.key;
        paramJson["value"] = param.value;
        itemJson["params"].push_back(paramJson);
    }
}

void LoadItemDefinitions(const json& root, WorldLoadData& out) {
    std::unordered_set<std::string> seenIds;
    for (const json& itemJson : root.value("itemDefinitions", json::array())) {
        ItemDefinition item;
        item.id = itemJson.value("id", "");
        if (item.id.empty()) {
            item.id = "item_" + std::to_string(out.itemDefinitions.size() + 1);
        }
        LoadItemDefinitionFields(itemJson, item);
        if (seenIds.insert(item.id).second) {
            out.itemDefinitions.push_back(item);
        }
    }
}

void LoadEnemyDefinitions(const json& root, WorldLoadData& out) {
    std::unordered_set<std::string> seenIds;
    const auto loadFrameArray = [](const json& frameArrayJson, std::vector<EnemyMoveDefinition::AnimationFrame>& outFrames) {
        for (const json& frameJson : frameArrayJson) {
            EnemyMoveDefinition::AnimationFrame frame;
            frame.frameWidth = std::max(1, frameJson.value("frameWidth", 1));
            frame.frameHeight = std::max(1, frameJson.value("frameHeight", 1));

            const json tilesJson = frameJson.value("tiles", json::array());
            if (tilesJson.is_array() && !tilesJson.empty()) {
                for (const json& tileJson : tilesJson) {
                    EnemyMoveDefinition::AnimationTile tile;
                    tile.sourceImagePath = tileJson.value("sourceImagePath", "");
                    tile.sourceLabel = tileJson.value("sourceLabel", "");
                    tile.sourceX = tileJson.value("sourceX", 0);
                    tile.sourceY = tileJson.value("sourceY", 0);
                    tile.sourceW = tileJson.value("sourceW", 16);
                    tile.sourceH = tileJson.value("sourceH", 16);
                    tile.tileX = tileJson.value("tileX", 0);
                    tile.tileY = tileJson.value("tileY", 0);
                    frame.tiles.push_back(tile);
                }
            } else {
                EnemyMoveDefinition::AnimationTile tile;
                tile.sourceImagePath = frameJson.value("sourceImagePath", "");
                tile.sourceLabel = frameJson.value("sourceLabel", "");
                tile.sourceX = frameJson.value("sourceX", 0);
                tile.sourceY = frameJson.value("sourceY", 0);
                tile.sourceW = frameJson.value("sourceW", 16);
                tile.sourceH = frameJson.value("sourceH", 16);
                tile.tileX = 0;
                tile.tileY = 0;
                frame.tiles.push_back(tile);
            }

            if (!frame.tiles.empty()) {
                outFrames.push_back(frame);
            }
        }
    };

    for (const json& enemyJson : root.value("enemyDefinitions", json::array())) {
        EnemyDefinition definition;
        definition.id = enemyJson.value("id", "");
        if (definition.id.empty()) {
            definition.id = "enemy_" + std::to_string(out.enemyDefinitions.size() + 1);
        }
        definition.name = enemyJson.value("name", definition.id);
        definition.isNpc = enemyJson.value("isNpc", false);
        definition.npcText = enemyJson.value("npcText", "");
        definition.hitpoints = std::max(1, enemyJson.value("hitpoints", 2));
        definition.baseDamage = std::max(0, enemyJson.value("baseDamage", 1));
        definition.immuneToKnockback = enemyJson.value("immuneToKnockback", false);
        definition.dropTableId = enemyJson.value("dropTableId", "");
        for (const json& wid : enemyJson.value("invulnerableToWeaponIds", json::array())) {
            if (wid.is_string() && !wid.get<std::string>().empty()) {
                definition.invulnerableToWeaponIds.push_back(wid.get<std::string>());
            }
        }
        for (const json& pid : enemyJson.value("invulnerableToProjectileIds", json::array())) {
            if (pid.is_string() && !pid.get<std::string>().empty()) {
                definition.invulnerableToProjectileIds.push_back(pid.get<std::string>());
            }
        }

        for (const json& moveJson : enemyJson.value("moves", json::array())) {
            EnemyMoveDefinition move;
            move.type = EnemyMoveTypeFromString(moveJson.value("type", "stand_still"));
            move.minSeconds = std::max(0.1f, moveJson.value("minSeconds", 1.0f));
            move.maxSeconds = std::max(move.minSeconds, moveJson.value("maxSeconds", move.minSeconds));
            move.speedTilesPerSecond = std::max(0.0f, moveJson.value("speedTilesPerSecond", 1.0f));
            move.reappearMode = EnemyReappearModeFromString(moveJson.value("reappearMode", "same_place"));
            move.projectileDefinitionId = moveJson.value("projectileDefinitionId", "");
            move.animationSpeed = std::max(0.0f, moveJson.value("animationSpeed", 0.0f));

            const json directional = moveJson.value("directionalFrames", json::object());
            if (directional.is_object() && !directional.empty()) {
                for (int dir = 0; dir < 4; ++dir) {
                    loadFrameArray(directional.value(kDirectionalFrameKeys[static_cast<size_t>(dir)], json::array()), move.directionalFrames[static_cast<size_t>(dir)]);
                }
            } else {
                std::vector<EnemyMoveDefinition::AnimationFrame> legacyFrames;
                loadFrameArray(moveJson.value("frames", json::array()), legacyFrames);
                for (int dir = 0; dir < 4; ++dir) {
                    move.directionalFrames[static_cast<size_t>(dir)] = legacyFrames;
                }
            }

            for (const json& hitboxJson : moveJson.value("hitboxes", json::array())) {
                TileHitbox hitbox;
                hitbox.x = hitboxJson.value("x", 0);
                hitbox.y = hitboxJson.value("y", 0);
                hitbox.w = hitboxJson.value("w", 12);
                hitbox.h = hitboxJson.value("h", 12);
                move.hitboxes.push_back(hitbox);
            }
            if (move.hitboxes.empty()) {
                move.hitboxes.push_back(TileHitbox{0, 0, 12, 12});
            }

            definition.moves.push_back(move);
        }

        const json knockbackJson = enemyJson.value("knockbackAnimation", json::object());
        if (knockbackJson.is_object()) {
            definition.knockbackAnimation.animationSpeed = std::max(0.0f, knockbackJson.value("animationSpeed", 0.0f));
            const json knockbackDirectional = knockbackJson.value("directionalFrames", json::object());
            if (knockbackDirectional.is_object()) {
                for (int dir = 0; dir < 4; ++dir) {
                    loadFrameArray(knockbackDirectional.value(kDirectionalFrameKeys[static_cast<size_t>(dir)], json::array()), definition.knockbackAnimation.directionalFrames[static_cast<size_t>(dir)]);
                }
            }
        }

        const json deathJson = enemyJson.value("deathAnimation", json::object());
        if (deathJson.is_object()) {
            definition.deathAnimation.animationSpeed = std::max(0.0f, deathJson.value("animationSpeed", 0.0f));
            const json deathDirectional = deathJson.value("directionalFrames", json::object());
            if (deathDirectional.is_object()) {
                for (int dir = 0; dir < 4; ++dir) {
                    loadFrameArray(deathDirectional.value(kDirectionalFrameKeys[static_cast<size_t>(dir)], json::array()), definition.deathAnimation.directionalFrames[static_cast<size_t>(dir)]);
                }
            }
        }

        if (definition.moves.empty()) {
            EnemyMoveDefinition fallback;
            fallback.type = EnemyMoveType::StandStill;
            fallback.minSeconds = 1.0f;
            fallback.maxSeconds = 1.0f;
            fallback.hitboxes.push_back(TileHitbox{0, 0, 12, 12});
            definition.moves.push_back(fallback);
        }

        if (seenIds.insert(definition.id).second) {
            out.enemyDefinitions.push_back(definition);
        }
    }
}

void LoadProjectileDefinitions(const json& root, WorldLoadData& out) {
    std::unordered_set<std::string> seenIds;
    for (const json& projectileJson : root.value("projectileDefinitions", json::array())) {
        ProjectileDefinition definition;
        definition.id = projectileJson.value("id", "");
        if (definition.id.empty()) {
            definition.id = "projectile_" + std::to_string(out.projectileDefinitions.size() + 1);
        }
        definition.name = projectileJson.value("name", definition.id);
        definition.startAnimationSpeed = std::max(0.0f, projectileJson.value("startAnimationSpeed", 0.0f));
        definition.flightAnimationSpeed = std::max(0.0f, projectileJson.value("flightAnimationSpeed", 0.0f));
        definition.impactAnimationSpeed = std::max(0.0f, projectileJson.value("impactAnimationSpeed", 0.0f));
        definition.movementType = ProjectileMovementTypeFromString(projectileJson.value("movementType", "track_player"));
        definition.speedTilesPerSecond = std::max(0.0f, projectileJson.value("speedTilesPerSecond", 1.0f));
        definition.fixedFunctionA = projectileJson.value("fixedFunctionA", 0.0f);
        definition.limitedDistanceTiles = std::max(0.0f, projectileJson.value("limitedDistanceTiles", 4.0f));
        definition.limitedDurationSeconds = std::max(0.0f, projectileJson.value("limitedDurationSeconds", 0.5f));
        definition.moveThroughSolid = projectileJson.value("moveThroughSolid", false);
        definition.baseDamage = std::max(0, projectileJson.value("baseDamage", 1));

        LoadProjectileFrameArray(projectileJson.value("startFrames", json::array()), definition.startFrames);
        LoadProjectileFrameArray(projectileJson.value("flightFrames", json::array()), definition.flightFrames);
        LoadProjectileFrameArray(projectileJson.value("impactFrames", json::array()), definition.impactFrames);

        for (const json& hitboxJson : projectileJson.value("hitboxes", json::array())) {
            TileHitbox hitbox;
            hitbox.x = hitboxJson.value("x", 0);
            hitbox.y = hitboxJson.value("y", 0);
            hitbox.w = hitboxJson.value("w", 8);
            hitbox.h = hitboxJson.value("h", 8);
            definition.hitboxes.push_back(hitbox);
        }
        if (definition.hitboxes.empty()) {
            definition.hitboxes.push_back(TileHitbox{0, 0, 8, 8});
        }

        if (seenIds.insert(definition.id).second) {
            out.projectileDefinitions.push_back(definition);
        }
    }
}

void LoadWeaponDefinitions(const json& root, WorldLoadData& out) {
    std::unordered_set<std::string> seenIds;
    for (const json& weaponJson : root.value("weaponDefinitions", json::array())) {
        WeaponDefinition definition;
        definition.id = weaponJson.value("id", "");
        if (definition.id.empty()) {
            definition.id = "weapon_" + std::to_string(out.weaponDefinitions.size() + 1);
        }
        definition.name = weaponJson.value("name", definition.id);
        definition.damage = std::max(0, weaponJson.value("damage", 1));
        definition.isProjectile = weaponJson.value("isProjectile", false);
        definition.projectileDefinitionId = weaponJson.value("projectileDefinitionId", "");

        const json spriteJson = weaponJson.value("hudSprite", json::object());
        if (spriteJson.is_object()) {
            definition.hudSprite.sourceImagePath = spriteJson.value("sourceImagePath", "");
            definition.hudSprite.sourceLabel = spriteJson.value("sourceLabel", "");
            definition.hudSprite.sourceX = spriteJson.value("sourceX", 0);
            definition.hudSprite.sourceY = spriteJson.value("sourceY", 0);
            definition.hudSprite.sourceW = std::max(1, spriteJson.value("sourceW", 16));
            definition.hudSprite.sourceH = std::max(1, spriteJson.value("sourceH", 16));
        }

        if (seenIds.insert(definition.id).second) {
            out.weaponDefinitions.push_back(definition);
        }
    }
}

void SaveItemDefinitions(json& root, const WorldLoadData& data) {
    root["itemDefinitions"] = json::array();
    for (const ItemDefinition& item : data.itemDefinitions) {
        json itemJson;
        itemJson["id"] = item.id;
        SaveItemDefinitionFields(itemJson, item);
        root["itemDefinitions"].push_back(itemJson);
    }
}

void SaveEnemyDefinitions(json& root, const WorldLoadData& data) {
    root["enemyDefinitions"] = json::array();
    for (const EnemyDefinition& definition : data.enemyDefinitions) {
        json enemyJson;
        enemyJson["id"] = definition.id;
        enemyJson["name"] = definition.name;
        enemyJson["isNpc"] = definition.isNpc;
        enemyJson["npcText"] = definition.npcText;
        enemyJson["hitpoints"] = std::max(1, definition.hitpoints);
        enemyJson["baseDamage"] = std::max(0, definition.baseDamage);
        enemyJson["immuneToKnockback"] = definition.immuneToKnockback;
        enemyJson["dropTableId"] = definition.dropTableId;
        enemyJson["invulnerableToWeaponIds"] = json::array();
        for (const std::string& wid : definition.invulnerableToWeaponIds) {
            enemyJson["invulnerableToWeaponIds"].push_back(wid);
        }
        enemyJson["invulnerableToProjectileIds"] = json::array();
        for (const std::string& pid : definition.invulnerableToProjectileIds) {
            enemyJson["invulnerableToProjectileIds"].push_back(pid);
        }
        enemyJson["moves"] = json::array();

        for (const EnemyMoveDefinition& move : definition.moves) {
            json moveJson;
            moveJson["type"] = EnemyMoveTypeToString(move.type);
            moveJson["minSeconds"] = move.minSeconds;
            moveJson["maxSeconds"] = move.maxSeconds;
            moveJson["speedTilesPerSecond"] = move.speedTilesPerSecond;
            moveJson["reappearMode"] = EnemyReappearModeToString(move.reappearMode);
            moveJson["projectileDefinitionId"] = move.projectileDefinitionId;
            moveJson["animationSpeed"] = move.animationSpeed;
            moveJson["directionalFrames"] = json::object();
            moveJson["hitboxes"] = json::array();

            for (int dir = 0; dir < 4; ++dir) {
                json frameArray = json::array();
                for (const EnemyMoveDefinition::AnimationFrame& frame : move.directionalFrames[static_cast<size_t>(dir)]) {
                    json frameJson;
                    frameJson["frameWidth"] = std::max(1, frame.frameWidth);
                    frameJson["frameHeight"] = std::max(1, frame.frameHeight);
                    frameJson["tiles"] = json::array();
                    for (const EnemyMoveDefinition::AnimationTile& tile : frame.tiles) {
                        json tileJson;
                        tileJson["sourceImagePath"] = tile.sourceImagePath;
                        tileJson["sourceLabel"] = tile.sourceLabel;
                        tileJson["sourceX"] = tile.sourceX;
                        tileJson["sourceY"] = tile.sourceY;
                        tileJson["sourceW"] = tile.sourceW;
                        tileJson["sourceH"] = tile.sourceH;
                        tileJson["tileX"] = tile.tileX;
                        tileJson["tileY"] = tile.tileY;
                        frameJson["tiles"].push_back(tileJson);
                    }
                    frameArray.push_back(frameJson);
                }
                moveJson["directionalFrames"][kDirectionalFrameKeys[static_cast<size_t>(dir)]] = frameArray;
            }

            for (const TileHitbox& hitbox : move.hitboxes) {
                json hitboxJson;
                hitboxJson["x"] = hitbox.x;
                hitboxJson["y"] = hitbox.y;
                hitboxJson["w"] = hitbox.w;
                hitboxJson["h"] = hitbox.h;
                moveJson["hitboxes"].push_back(hitboxJson);
            }

            enemyJson["moves"].push_back(moveJson);
        }

        enemyJson["knockbackAnimation"] = json::object();
        enemyJson["knockbackAnimation"]["animationSpeed"] = definition.knockbackAnimation.animationSpeed;
        enemyJson["knockbackAnimation"]["directionalFrames"] = json::object();
        for (int dir = 0; dir < 4; ++dir) {
            json frameArray = json::array();
            for (const EnemyMoveDefinition::AnimationFrame& frame : definition.knockbackAnimation.directionalFrames[static_cast<size_t>(dir)]) {
                json frameJson;
                frameJson["frameWidth"] = std::max(1, frame.frameWidth);
                frameJson["frameHeight"] = std::max(1, frame.frameHeight);
                frameJson["tiles"] = json::array();
                for (const EnemyMoveDefinition::AnimationTile& tile : frame.tiles) {
                    json tileJson;
                    tileJson["sourceImagePath"] = tile.sourceImagePath;
                    tileJson["sourceLabel"] = tile.sourceLabel;
                    tileJson["sourceX"] = tile.sourceX;
                    tileJson["sourceY"] = tile.sourceY;
                    tileJson["sourceW"] = tile.sourceW;
                    tileJson["sourceH"] = tile.sourceH;
                    tileJson["tileX"] = tile.tileX;
                    tileJson["tileY"] = tile.tileY;
                    frameJson["tiles"].push_back(tileJson);
                }
                frameArray.push_back(frameJson);
            }
            enemyJson["knockbackAnimation"]["directionalFrames"][kDirectionalFrameKeys[static_cast<size_t>(dir)]] = frameArray;
        }

        enemyJson["deathAnimation"] = json::object();
        enemyJson["deathAnimation"]["animationSpeed"] = definition.deathAnimation.animationSpeed;
        enemyJson["deathAnimation"]["directionalFrames"] = json::object();
        for (int dir = 0; dir < 4; ++dir) {
            json frameArray = json::array();
            for (const EnemyMoveDefinition::AnimationFrame& frame : definition.deathAnimation.directionalFrames[static_cast<size_t>(dir)]) {
                json frameJson;
                frameJson["frameWidth"] = std::max(1, frame.frameWidth);
                frameJson["frameHeight"] = std::max(1, frame.frameHeight);
                frameJson["tiles"] = json::array();
                for (const EnemyMoveDefinition::AnimationTile& tile : frame.tiles) {
                    json tileJson;
                    tileJson["sourceImagePath"] = tile.sourceImagePath;
                    tileJson["sourceLabel"] = tile.sourceLabel;
                    tileJson["sourceX"] = tile.sourceX;
                    tileJson["sourceY"] = tile.sourceY;
                    tileJson["sourceW"] = tile.sourceW;
                    tileJson["sourceH"] = tile.sourceH;
                    tileJson["tileX"] = tile.tileX;
                    tileJson["tileY"] = tile.tileY;
                    frameJson["tiles"].push_back(tileJson);
                }
                frameArray.push_back(frameJson);
            }
            enemyJson["deathAnimation"]["directionalFrames"][kDirectionalFrameKeys[static_cast<size_t>(dir)]] = frameArray;
        }

        root["enemyDefinitions"].push_back(enemyJson);
    }
}

void SaveProjectileDefinitions(json& root, const WorldLoadData& data) {
    root["projectileDefinitions"] = json::array();
    for (const ProjectileDefinition& definition : data.projectileDefinitions) {
        json projectileJson;
        projectileJson["id"] = definition.id;
        projectileJson["name"] = definition.name;
        projectileJson["startAnimationSpeed"] = definition.startAnimationSpeed;
        projectileJson["flightAnimationSpeed"] = definition.flightAnimationSpeed;
        projectileJson["impactAnimationSpeed"] = definition.impactAnimationSpeed;
        projectileJson["movementType"] = ProjectileMovementTypeToString(definition.movementType);
        projectileJson["speedTilesPerSecond"] = definition.speedTilesPerSecond;
        projectileJson["fixedFunctionA"] = definition.fixedFunctionA;
        projectileJson["limitedDistanceTiles"] = definition.limitedDistanceTiles;
        projectileJson["limitedDurationSeconds"] = definition.limitedDurationSeconds;
        projectileJson["moveThroughSolid"] = definition.moveThroughSolid;
        projectileJson["baseDamage"] = std::max(0, definition.baseDamage);
        projectileJson["startFrames"] = SaveProjectileFrameArray(definition.startFrames);
        projectileJson["flightFrames"] = SaveProjectileFrameArray(definition.flightFrames);
        projectileJson["impactFrames"] = SaveProjectileFrameArray(definition.impactFrames);
        projectileJson["hitboxes"] = json::array();
        for (const TileHitbox& hitbox : definition.hitboxes) {
            json hitboxJson;
            hitboxJson["x"] = hitbox.x;
            hitboxJson["y"] = hitbox.y;
            hitboxJson["w"] = hitbox.w;
            hitboxJson["h"] = hitbox.h;
            projectileJson["hitboxes"].push_back(hitboxJson);
        }
        root["projectileDefinitions"].push_back(projectileJson);
    }
}

void SaveWeaponDefinitions(json& root, const WorldLoadData& data) {
    root["weaponDefinitions"] = json::array();
    for (const WeaponDefinition& definition : data.weaponDefinitions) {
        json weaponJson;
        weaponJson["id"] = definition.id;
        weaponJson["name"] = definition.name;
        weaponJson["damage"] = std::max(0, definition.damage);
        weaponJson["isProjectile"] = definition.isProjectile;
        weaponJson["projectileDefinitionId"] = definition.projectileDefinitionId;
        weaponJson["hudSprite"] = {
            {"sourceImagePath", definition.hudSprite.sourceImagePath},
            {"sourceLabel", definition.hudSprite.sourceLabel},
            {"sourceX", definition.hudSprite.sourceX},
            {"sourceY", definition.hudSprite.sourceY},
            {"sourceW", std::max(1, definition.hudSprite.sourceW)},
            {"sourceH", std::max(1, definition.hudSprite.sourceH)}
        };
        root["weaponDefinitions"].push_back(weaponJson);
    }
}

void LoadWarpDefinitions(const json& root, WorldLoadData& out) {
    std::unordered_set<std::string> seenIds;
    for (const json& warpJson : root.value("warpDefinitions", json::array())) {
        WarpDefinition definition;
        definition.id = warpJson.value("id", "");
        if (definition.id.empty()) {
            definition.id = "warp_" + std::to_string(out.warpDefinitions.size() + 1);
        }
        definition.name = warpJson.value("name", definition.id);
        definition.kind = TransitionKindFromString(warpJson.value("kind", "instant"));

        const json endpointsJson = warpJson.value("endpoints", json::array());
        for (int endpointIndex = 0; endpointIndex < 2; ++endpointIndex) {
            const json endpointJson = endpointIndex < static_cast<int>(endpointsJson.size())
                ? endpointsJson[static_cast<size_t>(endpointIndex)]
                : json::object();

            WarpEndpointDefinition endpoint;
            endpoint.tileId = endpointJson.value("tileId", 0);
            endpoint.spawnOffset = WarpSpawnOffsetFromString(endpointJson.value("spawnOffset", "on_top"));
            for (const json& hitboxJson : endpointJson.value("hitboxes", json::array())) {
                TileHitbox hitbox;
                hitbox.x = hitboxJson.value("x", 0);
                hitbox.y = hitboxJson.value("y", 0);
                hitbox.w = hitboxJson.value("w", 16);
                hitbox.h = hitboxJson.value("h", 16);
                endpoint.hitboxes.push_back(hitbox);
            }
            if (endpoint.hitboxes.empty()) {
                endpoint.hitboxes.push_back(TileHitbox{0, 0, 16, 16});
            }
            definition.endpoints[static_cast<size_t>(endpointIndex)] = endpoint;
        }

        if (seenIds.insert(definition.id).second) {
            out.warpDefinitions.push_back(definition);
        }
    }
}

void SaveWarpDefinitions(json& root, const WorldLoadData& data) {
    root["warpDefinitions"] = json::array();
    for (const WarpDefinition& definition : data.warpDefinitions) {
        json warpJson;
        warpJson["id"] = definition.id;
        warpJson["name"] = definition.name;
        warpJson["kind"] = TransitionKindToString(definition.kind);
        warpJson["endpoints"] = json::array();
        for (int endpointIndex = 0; endpointIndex < 2; ++endpointIndex) {
            const WarpEndpointDefinition& endpoint = definition.endpoints[static_cast<size_t>(endpointIndex)];
            json endpointJson;
            endpointJson["tileId"] = endpoint.tileId;
            endpointJson["spawnOffset"] = WarpSpawnOffsetToString(endpoint.spawnOffset);
            endpointJson["hitboxes"] = json::array();
            for (const TileHitbox& hitbox : endpoint.hitboxes) {
                json hitboxJson;
                hitboxJson["x"] = hitbox.x;
                hitboxJson["y"] = hitbox.y;
                hitboxJson["w"] = hitbox.w;
                hitboxJson["h"] = hitbox.h;
                endpointJson["hitboxes"].push_back(hitboxJson);
            }
            warpJson["endpoints"].push_back(endpointJson);
        }
        root["warpDefinitions"].push_back(warpJson);
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

WarpSpawnOffset WarpSpawnOffsetFromString(const std::string& value) {
    if (value == "above") {
        return WarpSpawnOffset::Above;
    }
    if (value == "below") {
        return WarpSpawnOffset::Below;
    }
    if (value == "left") {
        return WarpSpawnOffset::Left;
    }
    if (value == "right") {
        return WarpSpawnOffset::Right;
    }
    return WarpSpawnOffset::OnTop;
}

std::string WarpSpawnOffsetToString(WarpSpawnOffset value) {
    switch (value) {
        case WarpSpawnOffset::Above:
            return "above";
        case WarpSpawnOffset::Below:
            return "below";
        case WarpSpawnOffset::Left:
            return "left";
        case WarpSpawnOffset::Right:
            return "right";
        case WarpSpawnOffset::OnTop:
        default:
            return "on_top";
    }
}

EnemyMoveType EnemyMoveTypeFromString(const std::string& value) {
    if (value == "move_random_direction") {
        return EnemyMoveType::MoveRandomDirection;
    }
    if (value == "disappear") {
        return EnemyMoveType::Disappear;
    }
    if (value == "fire_projectile") {
        return EnemyMoveType::FireProjectile;
    }
    return EnemyMoveType::StandStill;
}

std::string EnemyMoveTypeToString(EnemyMoveType value) {
    switch (value) {
        case EnemyMoveType::MoveRandomDirection:
            return "move_random_direction";
        case EnemyMoveType::Disappear:
            return "disappear";
        case EnemyMoveType::FireProjectile:
            return "fire_projectile";
        case EnemyMoveType::StandStill:
        default:
            return "stand_still";
    }
}

EnemyReappearMode EnemyReappearModeFromString(const std::string& value) {
    if (value == "random_position") {
        return EnemyReappearMode::RandomPosition;
    }
    return EnemyReappearMode::SamePlace;
}

std::string EnemyReappearModeToString(EnemyReappearMode value) {
    return value == EnemyReappearMode::RandomPosition ? "random_position" : "same_place";
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
        collection.editorPaletteColumns = std::max(0, collectionJson.value("editorPaletteColumns", 0));
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
        collectionJson["editorPaletteColumns"] = std::max(0, collection.editorPaletteColumns);
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

            const json directionalHitboxesJson = actionJson.value("directionalHitboxes", json::object());
            if (directionalHitboxesJson.is_object() && !directionalHitboxesJson.empty()) {
                for (int dir = 0; dir < 4; ++dir) {
                    for (const json& hitboxJson : directionalHitboxesJson.value(kDirectionalFrameKeys[static_cast<size_t>(dir)], json::array())) {
                        TileHitbox hitbox;
                        hitbox.x = hitboxJson.value("x", 0);
                        hitbox.y = hitboxJson.value("y", 0);
                        hitbox.w = hitboxJson.value("w", 12);
                        hitbox.h = hitboxJson.value("h", 12);
                        action.directionalHitboxes[static_cast<size_t>(dir)].push_back(hitbox);
                    }
                }
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
            actionJson["directionalHitboxes"] = json::object();
            for (int dir = 0; dir < 4; ++dir) {
                json hitboxArray = json::array();
                for (const TileHitbox& hitbox : action.directionalHitboxes[static_cast<size_t>(dir)]) {
                    json hitboxJson;
                    hitboxJson["x"] = hitbox.x;
                    hitboxJson["y"] = hitbox.y;
                    hitboxJson["w"] = hitbox.w;
                    hitboxJson["h"] = hitbox.h;
                    hitboxArray.push_back(hitboxJson);
                }
                actionJson["directionalHitboxes"][kDirectionalFrameKeys[static_cast<size_t>(dir)]] = hitboxArray;
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

void LoadItemPlacements(const json& source, const std::string& mapId, int screenX, int screenY, std::vector<ItemPlacement>& out) {
    for (const json& placementJson : source.value("itemPlacements", json::array())) {
        ItemPlacement placement;
        placement.itemId = placementJson.value("itemId", "");
        placement.x = static_cast<float>(placementJson.value("x", 0));
        placement.y = static_cast<float>(placementJson.value("y", 0));
        placement.mapId = mapId;
        placement.screenX = screenX;
        placement.screenY = screenY;
        placement.collected = placementJson.value("collected", false);
        placement.opened = placementJson.value("opened", false);
        placement.containerContentKind = ContainerContentKindFromString(placementJson.value("containerContentKind", "none"));
        placement.containerContentId = placementJson.value("containerContentId", "");
        out.push_back(placement);
    }
}

void SaveItemPlacements(json& screenJson, const std::vector<ItemPlacement>& placements) {
    screenJson["itemPlacements"] = json::array();
    for (const ItemPlacement& placement : placements) {
        json placementJson;
        placementJson["itemId"] = placement.itemId;
        placementJson["x"] = static_cast<int>(placement.x);
        placementJson["y"] = static_cast<int>(placement.y);
        placementJson["collected"] = placement.collected;
        placementJson["opened"] = placement.opened;
        placementJson["containerContentKind"] = ContainerContentKindToString(placement.containerContentKind);
        placementJson["containerContentId"] = placement.containerContentId;
        screenJson["itemPlacements"].push_back(placementJson);
    }
}

void LoadLegacyScreenItems(
    const json& source,
    const std::string& mapId,
    int screenX,
    int screenY,
    WorldLoadData& out,
    std::vector<ItemPlacement>& placements,
    int& legacyCounter
) {
    for (const json& itemJson : source.value("items", json::array())) {
        ItemDefinition item;
        item.id = "legacy_item_" + std::to_string(++legacyCounter);
        LoadItemDefinitionFields(itemJson, item);
        out.itemDefinitions.push_back(item);

        ItemPlacement placement;
        placement.itemId = item.id;
        placement.x = static_cast<float>(itemJson.value("x", 0));
        placement.y = static_cast<float>(itemJson.value("y", 0));
        placement.mapId = mapId;
        placement.screenX = screenX;
        placement.screenY = screenY;
        placements.push_back(placement);
    }
}

void LoadEnemyPlacements(const json& source, const std::string& mapId, int screenX, int screenY, std::vector<EnemyPlacement>& out) {
    for (const json& placementJson : source.value("enemyPlacements", json::array())) {
        EnemyPlacement placement;
        placement.enemyId = placementJson.value("enemyId", "");
        placement.x = static_cast<float>(placementJson.value("x", 0));
        placement.y = static_cast<float>(placementJson.value("y", 0));
        placement.mapId = mapId;
        placement.screenX = screenX;
        placement.screenY = screenY;
        if (!placement.enemyId.empty()) {
            out.push_back(placement);
        }
    }
}

void SaveEnemyPlacements(json& screenJson, const std::vector<EnemyPlacement>& placements) {
    screenJson["enemyPlacements"] = json::array();
    for (const EnemyPlacement& placement : placements) {
        json enemyJson;
        enemyJson["enemyId"] = placement.enemyId;
        enemyJson["x"] = static_cast<int>(placement.x);
        enemyJson["y"] = static_cast<int>(placement.y);
        screenJson["enemyPlacements"].push_back(enemyJson);
    }
}

void LoadLegacyScreenEnemies(
    const json& source,
    const std::string& mapId,
    int screenX,
    int screenY,
    WorldLoadData& out,
    std::vector<EnemyPlacement>& placements,
    int& legacyCounter
) {
    for (const json& enemyJson : source.value("enemies", json::array())) {
        EnemyDefinition definition;
        definition.id = "legacy_enemy_" + std::to_string(++legacyCounter);
        const std::string behavior = enemyJson.value("behavior", "wander");
        definition.name = behavior;
        definition.hitpoints = std::max(1, enemyJson.value("hp", 2));
        definition.baseDamage = std::max(0, enemyJson.value("damage", 1));

        EnemyMoveDefinition move;
        move.minSeconds = 1.2f;
        move.maxSeconds = 1.2f;
        move.speedTilesPerSecond = std::max(0.0f, enemyJson.value("speed", 24.0f) / 16.0f);
        move.hitboxes.push_back(TileHitbox{0, 0, std::max(1, enemyJson.value("w", 12)), std::max(1, enemyJson.value("h", 12))});

        if (behavior == "static") {
            move.type = EnemyMoveType::StandStill;
            move.speedTilesPerSecond = 0.0f;
        } else {
            move.type = EnemyMoveType::MoveRandomDirection;
        }

        definition.moves.push_back(move);
        out.enemyDefinitions.push_back(definition);

        EnemyPlacement placement;
        placement.enemyId = definition.id;
        placement.mapId = mapId;
        placement.screenX = screenX;
        placement.screenY = screenY;
        placement.x = static_cast<float>(enemyJson.value("x", 0));
        placement.y = static_cast<float>(enemyJson.value("y", 0));
        placements.push_back(placement);
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

void LoadWarpPlacements(const json& source, const std::string& mapId, int screenX, int screenY, std::vector<WarpPlacement>& out) {
    for (const json& placementJson : source.value("warpPlacements", json::array())) {
        WarpPlacement placement;
        placement.warpId = placementJson.value("warpId", "");
        placement.endpointIndex = std::clamp(placementJson.value("endpoint", 0), 0, 1);
        placement.x = static_cast<float>(placementJson.value("x", 0));
        placement.y = static_cast<float>(placementJson.value("y", 0));
        placement.mapId = mapId;
        placement.screenX = screenX;
        placement.screenY = screenY;
        if (!placement.warpId.empty()) {
            out.push_back(placement);
        }
    }
}

void SaveWarpPlacements(json& screenJson, const std::vector<WarpPlacement>& placements) {
    screenJson["warpPlacements"] = json::array();
    for (const WarpPlacement& placement : placements) {
        json placementJson;
        placementJson["warpId"] = placement.warpId;
        placementJson["endpoint"] = std::clamp(placement.endpointIndex, 0, 1);
        placementJson["x"] = static_cast<int>(placement.x);
        placementJson["y"] = static_cast<int>(placement.y);
        screenJson["warpPlacements"].push_back(placementJson);
    }
}

void LoadLegacyWarps(
    const json& source,
    const std::string& mapId,
    int screenX,
    int screenY,
    WorldLoadData& out,
    std::vector<WarpPlacement>& placements,
    int& legacyCounter
) {
    for (const json& warpJson : source.value("warps", json::array())) {
        WarpDefinition definition;
        definition.id = "legacy_warp_" + std::to_string(++legacyCounter);
        definition.name = warpJson.value("label", definition.id);
        definition.kind = TransitionKindFromString(warpJson.value("kind", "instant"));

        const int triggerW = std::max(1, warpJson.value("w", 16));
        const int triggerH = std::max(1, warpJson.value("h", 16));
        definition.endpoints[0].tileId = 0;
        definition.endpoints[0].hitboxes = {TileHitbox{0, 0, triggerW, triggerH}};
        definition.endpoints[1].tileId = 0;
        definition.endpoints[1].hitboxes = {TileHitbox{0, 0, 16, 16}};
        out.warpDefinitions.push_back(definition);

        WarpPlacement pointA;
        pointA.warpId = definition.id;
        pointA.endpointIndex = 0;
        pointA.mapId = mapId;
        pointA.screenX = screenX;
        pointA.screenY = screenY;
        pointA.x = static_cast<float>(warpJson.value("x", 0));
        pointA.y = static_cast<float>(warpJson.value("y", 0));
        placements.push_back(pointA);

        WarpPlacement pointB;
        pointB.warpId = definition.id;
        pointB.endpointIndex = 1;
        pointB.mapId = warpJson.value("toMapId", mapId);
        pointB.screenX = warpJson.value("toX", screenX);
        pointB.screenY = warpJson.value("toY", screenY);
        pointB.x = static_cast<float>(warpJson.value("spawnX", 0));
        pointB.y = static_cast<float>(warpJson.value("spawnY", 0));
        placements.push_back(pointB);
    }
}

ScreenLoadData LoadScreen(const json& source, const std::string& mapId, WorldLoadData& out, int& legacyCounter) {
    ScreenLoadData screenData;
    screenData.x = source.value("x", -1);
    screenData.y = source.value("y", -1);
    screenData.dungeonId = source.value("dungeonId", "");
    screenData.screen.displayText = source.value("displayText", "");
    screenData.screen.displayTextEnabled = source.value("displayTextEnabled", !screenData.screen.displayText.empty());
    screenData.screen.hideFromMap = source.value("hideFromMap", false);
    screenData.hideFromMap = screenData.screen.hideFromMap;
    LoadTiles(source, screenData.screen);
    if (screenData.x < 0 || screenData.y < 0) {
        return screenData;
    }
    LoadItemPlacements(source, mapId, screenData.x, screenData.y, screenData.itemPlacements);
    if (screenData.itemPlacements.empty() && source.contains("items")) {
        LoadLegacyScreenItems(source, mapId, screenData.x, screenData.y, out, screenData.itemPlacements, legacyCounter);
    }
    LoadEnemyPlacements(source, mapId, screenData.x, screenData.y, screenData.enemyPlacements);
    if (screenData.enemyPlacements.empty() && source.contains("enemies")) {
        LoadLegacyScreenEnemies(source, mapId, screenData.x, screenData.y, out, screenData.enemyPlacements, legacyCounter);
    }
    LoadTransitions(source, mapId, screenData.x, screenData.y, screenData.transitions);
    LoadWarpPlacements(source, mapId, screenData.x, screenData.y, screenData.warpPlacements);
    if (screenData.warpPlacements.empty() && source.contains("warps")) {
        LoadLegacyWarps(source, mapId, screenData.x, screenData.y, out, screenData.warpPlacements, legacyCounter);
    }
    return screenData;
}

json SaveScreen(const ScreenLoadData& screenData, const std::string& mapId) {
    json screenJson;
    screenJson["x"] = screenData.x;
    screenJson["y"] = screenData.y;
    if (!screenData.dungeonId.empty()) {
        screenJson["dungeonId"] = screenData.dungeonId;
    }
    screenJson["displayTextEnabled"] = screenData.screen.displayTextEnabled;
    screenJson["displayText"] = screenData.screen.displayText;
    screenJson["hideFromMap"] = screenData.screen.hideFromMap;
    SaveTiles(screenJson, screenData.screen);
    SaveItemPlacements(screenJson, screenData.itemPlacements);
    SaveEnemyPlacements(screenJson, screenData.enemyPlacements);
    SaveTransitions(screenJson, screenData.transitions, mapId);
    SaveWarpPlacements(screenJson, screenData.warpPlacements);
    return screenJson;
}

bool LoadSingleMapFormat(const json& root, WorldLoadData& out) {
    int legacyCounter = 0;
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
        ScreenLoadData screenData = LoadScreen(s, map.id, out, legacyCounter);
        if (screenData.x < 0 || screenData.y < 0) {
            continue;
        }
        map.screens.push_back(screenData);
    }

    out.maps.push_back(map);
    return true;
}

bool LoadMultiMapFormat(const json& root, WorldLoadData& out) {
    int legacyCounter = 0;
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
            ScreenLoadData screenData = LoadScreen(screenJson, map.id, out, legacyCounter);
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
    const json globalSettingsJson = root.value("globalSettings", json::object());
    out.globalSettings.knockbackDistanceTiles = std::max(0.0f, globalSettingsJson.value("knockbackDistanceTiles", 0.5f));
    out.globalSettings.invulnerabilitySeconds = std::max(0.0f, globalSettingsJson.value("invulnerabilitySeconds", 1.5f));
    out.globalSettings.textLettersPerSecond = std::max(1.0f, globalSettingsJson.value("textLettersPerSecond", 28.0f));
    out.globalSettings.textGlyphMap = NormalizeTextGlyphMapForStorage(globalSettingsJson.value("textGlyphMap", std::string()));
    out.globalSettings.dropItemLifetimeSec = std::max(1.0f, globalSettingsJson.value("dropItemLifetimeSec", 6.0f));
    out.globalSettings.itemPickupDurationSec = std::max(0.1f, globalSettingsJson.value("itemPickupDurationSec", 2.5f));
    LoadTileCollections(root, out);
    LoadCharacterSpritesets(root, out);
    LoadItemDefinitions(root, out);
    LoadEnemyDefinitions(root, out);
    LoadWeaponDefinitions(root, out);
    LoadProjectileDefinitions(root, out);
    LoadWarpDefinitions(root, out);

    // Load drop tables
    for (const json& dtJson : root.value("dropTables", json::array())) {
        EnemyDropTable table;
        table.id = dtJson.value("id", "");
        if (table.id.empty()) {
            table.id = "drop_table_" + std::to_string(out.dropTables.size() + 1);
        }
        table.name = dtJson.value("name", table.id);
        for (const json& entryJson : dtJson.value("entries", json::array())) {
            EnemyDropEntry entry;
            entry.itemId = entryJson.value("itemId", "");
            entry.weight = std::clamp(entryJson.value("weight", 10), 0, 100);
            if (!entry.itemId.empty()) {
                table.entries.push_back(entry);
            }
        }
        out.dropTables.push_back(table);
    }

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
    try {
        json root;
        root["formatVersion"] = 16;
        root["defaultMapId"] = data.defaultMapId;
        root["defaultStartScreenX"] = data.defaultStartScreenX;
        root["defaultStartScreenY"] = data.defaultStartScreenY;
        root["globalSettings"] = {
            {"knockbackDistanceTiles", std::max(0.0f, data.globalSettings.knockbackDistanceTiles)},
            {"invulnerabilitySeconds", std::max(0.0f, data.globalSettings.invulnerabilitySeconds)},
            {"textLettersPerSecond", std::max(1.0f, data.globalSettings.textLettersPerSecond)},
            {"textGlyphMap", NormalizeTextGlyphMapForStorage(data.globalSettings.textGlyphMap)},
            {"dropItemLifetimeSec", std::max(1.0f, data.globalSettings.dropItemLifetimeSec)},
            {"itemPickupDurationSec", std::max(0.1f, data.globalSettings.itemPickupDurationSec)}
        };
        SaveTileCollections(root, data);
        SaveCharacterSpritesets(root, data);
        SaveItemDefinitions(root, data);
        SaveEnemyDefinitions(root, data);
        SaveWeaponDefinitions(root, data);
        SaveProjectileDefinitions(root, data);
        SaveWarpDefinitions(root, data);

        root["dropTables"] = json::array();
        for (const EnemyDropTable& table : data.dropTables) {
            json tableJson;
            tableJson["id"] = table.id;
            tableJson["name"] = table.name;
            tableJson["entries"] = json::array();
            for (const EnemyDropEntry& entry : table.entries) {
                json entryJson;
                entryJson["itemId"] = entry.itemId;
                entryJson["weight"] = std::clamp(entry.weight, 0, 100);
                tableJson["entries"].push_back(entryJson);
            }
            root["dropTables"].push_back(tableJson);
        }

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

        const std::string serialized = root.dump(2);
        const std::filesystem::path outPath(filePath);
        const std::filesystem::path tmpPath = outPath.string() + ".tmp";

        std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            return false;
        }
        out << serialized << '\n';
        out.flush();
        if (!out.good()) {
            return false;
        }
        out.close();

        std::error_code ec;
        std::filesystem::remove(outPath, ec);
        ec.clear();
        std::filesystem::rename(tmpPath, outPath, ec);
        if (ec) {
            std::filesystem::remove(tmpPath, ec);
            return false;
        }
        return true;
    } catch (...) {
        return false;
    }
}
