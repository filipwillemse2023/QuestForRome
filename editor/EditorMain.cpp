#include <wx/wx.h>
#include <wx/dcbuffer.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/notebook.h>
#include <wx/numdlg.h>
#include <wx/listctrl.h>
#include <wx/scrolwin.h>
#include <wx/spinctrl.h>
#include <wx/splitter.h>
#include <wx/textdlg.h>
#include <wx/artprov.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Constants.hpp"
#include "MapLoader.hpp"
#include "Types.hpp"

namespace {

ScreenLoadData MakeBlankScreen(int x, int y) {
    ScreenLoadData out;
    out.x = x;
    out.y = y;

    for (int layer = 0; layer < kTileLayers; ++layer) {
        out.screen.tileLayerIds[static_cast<size_t>(layer)].fill(-1);
    }

    return out;
}

MapLoadData MakeBlankMap(const std::string& id, const std::string& name, int widthScreens, int heightScreens) {
    MapLoadData map;
    map.id = id;
    map.name = name;
    map.widthScreens = widthScreens;
    map.heightScreens = heightScreens;
    map.defaultStartScreenX = 0;
    map.defaultStartScreenY = 0;

    for (int y = 0; y < heightScreens; ++y) {
        for (int x = 0; x < widthScreens; ++x) {
            map.screens.push_back(MakeBlankScreen(x, y));
        }
    }

    return map;
}

ScreenLoadData* FindScreen(MapLoadData& map, int x, int y) {
    for (ScreenLoadData& screen : map.screens) {
        if (screen.x == x && screen.y == y) {
            return &screen;
        }
    }
    return nullptr;
}

const ScreenLoadData* FindScreen(const MapLoadData& map, int x, int y) {
    for (const ScreenLoadData& screen : map.screens) {
        if (screen.x == x && screen.y == y) {
            return &screen;
        }
    }
    return nullptr;
}

void EnsureMapScreens(MapLoadData& map) {
    for (int y = 0; y < map.heightScreens; ++y) {
        for (int x = 0; x < map.widthScreens; ++x) {
            if (!FindScreen(map, x, y)) {
                map.screens.push_back(MakeBlankScreen(x, y));
            }
        }
    }

    map.screens.erase(
        std::remove_if(
            map.screens.begin(),
            map.screens.end(),
            [&map](const ScreenLoadData& screen) {
                return screen.x < 0 || screen.y < 0 || screen.x >= map.widthScreens || screen.y >= map.heightScreens;
            }
        ),
        map.screens.end()
    );

    std::sort(map.screens.begin(), map.screens.end(), [](const ScreenLoadData& a, const ScreenLoadData& b) {
        if (a.y != b.y) {
            return a.y < b.y;
        }
        return a.x < b.x;
    });

    map.defaultStartScreenX = std::clamp(map.defaultStartScreenX, 0, std::max(0, map.widthScreens - 1));
    map.defaultStartScreenY = std::clamp(map.defaultStartScreenY, 0, std::max(0, map.heightScreens - 1));
}

constexpr int kTextGlyphRows = 6;
constexpr int kTextGlyphColumns = 30;
constexpr int kTextGlyphCount = kTextGlyphRows * kTextGlyphColumns;

constexpr int kLetterColumnsPerRow = 13;
constexpr int kDigitBlockStartCol = 14;

std::string DefaultTextGlyphMap() {
    std::string map(static_cast<size_t>(kTextGlyphCount), ' ');
    auto setGlyph = [&map](int row, int col, char ch) {
        if (row < 0 || row >= kTextGlyphRows || col < 0 || col >= kTextGlyphColumns) {
            return;
        }
        map[static_cast<size_t>(row * kTextGlyphColumns + col)] = ch;
    };

    for (int i = 0; i < 26; ++i) {
        const int letterRowBand = i / kLetterColumnsPerRow;
        const int letterCol = i % kLetterColumnsPerRow;
        setGlyph(letterRowBand * 2, letterCol, static_cast<char>('A' + i));
        setGlyph(letterRowBand * 2 + 1, letterCol, static_cast<char>('a' + i));
    }

    const int digitRows[10] = {0, 0, 0, 1, 1, 1, 2, 2, 2, 3};
    const int digitCols[10] = {0, 1, 2, 0, 1, 2, 0, 1, 2, 1};
    for (int d = 0; d <= 9; ++d) {
        setGlyph(digitRows[d], kDigitBlockStartCol + digitCols[d], static_cast<char>('0' + d));
    }

    return map;
}

std::string NormalizeTextGlyphMap(const std::string& rawMap) {
    std::string map;
    map.reserve(rawMap.size());
    for (unsigned char ch : rawMap) {
        if (ch == '\r' || ch == '\n') {
            continue;
        }
        if (ch == '_') {
            map.push_back(' ');
            continue;
        }
        if (ch >= 32 && ch <= 126) {
            map.push_back(static_cast<char>(ch));
        } else {
            map.push_back(' ');
        }
    }

    if (map.empty()) {
        return DefaultTextGlyphMap();
    }
    if (map.size() < static_cast<size_t>(kTextGlyphCount)) {
        map.append(static_cast<size_t>(kTextGlyphCount) - map.size(), ' ');
    } else if (map.size() > static_cast<size_t>(kTextGlyphCount)) {
        map.resize(static_cast<size_t>(kTextGlyphCount));
    }
    return map;
}

std::string FormatTextGlyphMapForEditor(const std::string& rawMap) {
    std::string map = NormalizeTextGlyphMap(rawMap);
    for (char& ch : map) {
        if (ch == ' ') {
            ch = '_';
        }
    }

    std::string formatted;
    formatted.reserve(static_cast<size_t>(kTextGlyphCount + (kTextGlyphRows - 1)));
    for (int row = 0; row < kTextGlyphRows; ++row) {
        const size_t start = static_cast<size_t>(row * kTextGlyphColumns);
        formatted.append(map.substr(start, static_cast<size_t>(kTextGlyphColumns)));
        if (row + 1 < kTextGlyphRows) {
            formatted.push_back('\n');
        }
    }
    return formatted;
}

std::string ToUtf8String(const wxString& value) {
    const wxScopedCharBuffer utf8 = value.utf8_str();
    if (!utf8) {
        return std::string();
    }
    return std::string(utf8.data(), utf8.length());
}

bool CreateWorldBackupBeforeLoad(const std::string& worldPath, std::string& backupPathOut) {
    const std::filesystem::path sourcePath(worldPath);
    std::error_code ec;
    if (!std::filesystem::exists(sourcePath, ec) || ec) {
        return false;
    }

    std::time_t now = std::time(nullptr);
    std::tm localTm{};
#ifdef _WIN32
    localtime_s(&localTm, &now);
#else
    localtime_r(&now, &localTm);
#endif

    char timestamp[32] = {};
    if (std::strftime(timestamp, sizeof(timestamp), "%d%m%Y_%H%M%S", &localTm) == 0) {
        return false;
    }

    const std::filesystem::path backupDir = sourcePath.parent_path();
    const std::string stamp = timestamp;
    std::filesystem::path backupPath = backupDir / ("world_" + stamp + ".json");
    int suffix = 1;
    while (std::filesystem::exists(backupPath, ec) && !ec) {
        backupPath = backupDir / ("world_" + stamp + "_" + std::to_string(suffix) + ".json");
        ++suffix;
    }
    if (ec) {
        return false;
    }

    std::filesystem::copy_file(sourcePath, backupPath, std::filesystem::copy_options::none, ec);
    if (ec) {
        return false;
    }

    backupPathOut = backupPath.string();
    return true;
}

wxColour TileColorFromId(int id) {
    if (id == 1) {
        return wxColour(125, 117, 104);
    }
    if (id == 2) {
        return wxColour(58, 107, 167);
    }
    if (id == 3) {
        return wxColour(194, 172, 105);
    }
    if (id == 0) {
        return wxColour(99, 145, 71);
    }
    const int r = 60 + ((id * 37) % 140);
    const int g = 60 + ((id * 67) % 140);
    const int b = 60 + ((id * 97) % 140);
    return wxColour(r, g, b);
}

wxBitmap LoadBitmapMaybeRelative(const std::string& pathUtf8) {
    if (pathUtf8.empty()) {
        return wxBitmap();
    }

    wxBitmap bmp;
    wxFileName directPath(wxString::FromUTF8(pathUtf8));
    if (directPath.FileExists() && bmp.LoadFile(directPath.GetFullPath(), wxBITMAP_TYPE_PNG)) {
        return bmp;
    }

    wxFileName relPath(wxString::FromUTF8(pathUtf8));
    relPath.MakeAbsolute(wxGetCwd());
    if (relPath.FileExists()) {
        bmp.LoadFile(relPath.GetFullPath(), wxBITMAP_TYPE_PNG);
    }
    return bmp;
}

wxBitmap BuildItemFramePreviewBitmap(const ItemAnimationFrame* frame, int scale, const wxColour& background = wxColour(24, 29, 36)) {
    const int safeScale = std::max(1, scale);
    if (!frame) {
        wxBitmap fallback(16 * safeScale, 16 * safeScale);
        wxMemoryDC dc;
        dc.SelectObject(fallback);
        dc.SetBackground(wxBrush(background));
        dc.Clear();
        dc.SelectObject(wxNullBitmap);
        return fallback;
    }

    wxBitmap source = LoadBitmapMaybeRelative(frame->sourceImagePath);
    if (!source.IsOk()) {
        wxBitmap fallback(std::max(16, frame->sourceW) * safeScale, std::max(16, frame->sourceH) * safeScale);
        wxMemoryDC dc;
        dc.SelectObject(fallback);
        dc.SetBackground(wxBrush(background));
        dc.Clear();
        dc.SelectObject(wxNullBitmap);
        return fallback;
    }

    wxRect sourceRect(frame->sourceX, frame->sourceY, frame->sourceW, frame->sourceH);
    sourceRect.Intersect(wxRect(0, 0, source.GetWidth(), source.GetHeight()));
    if (sourceRect.width <= 0 || sourceRect.height <= 0) {
        return source;
    }

    wxImage image = source.GetSubBitmap(sourceRect).ConvertToImage();
    image.Rescale(sourceRect.width * safeScale, sourceRect.height * safeScale, wxIMAGE_QUALITY_NEAREST);
    return wxBitmap(image);
}

CharacterSpriteset BuildDefaultPlayerSpriteset() {
    CharacterSpriteset spriteset;
    spriteset.id = "player_1";
    spriteset.name = "Player 1";
    spriteset.description = "Default player spriteset";
    spriteset.imagePath = "data/sprites/player/player_1.png";
    spriteset.tileWidth = 16;
    spriteset.tileHeight = 16;

    CharacterAction standing;
    standing.id = "standing";
    standing.name = "Standing";
    standing.animationSpeed = 1.0f;
    for (int dir = 0; dir < 4; ++dir) {
        standing.directionalFrames[static_cast<size_t>(dir)].push_back(CharacterFrame{0, dir * 2, 1, 2});
    }

    CharacterAction walking;
    walking.id = "walking";
    walking.name = "Walking";
    walking.animationSpeed = 8.0f;
    for (int dir = 0; dir < 4; ++dir) {
        walking.directionalFrames[static_cast<size_t>(dir)].push_back(CharacterFrame{0, dir * 2, 1, 2});
        walking.directionalFrames[static_cast<size_t>(dir)].push_back(CharacterFrame{1, dir * 2, 1, 2});
        walking.directionalFrames[static_cast<size_t>(dir)].push_back(CharacterFrame{2, dir * 2, 1, 2});
    }

    CharacterAction knockback;
    knockback.id = "knockback";
    knockback.name = "Knockback";
    knockback.animationSpeed = 10.0f;
    for (int dir = 0; dir < 4; ++dir) {
        knockback.directionalFrames[static_cast<size_t>(dir)].push_back(CharacterFrame{0, dir * 2, 1, 2});
    }

    CharacterAction itemPickup;
    itemPickup.id = "item pickup";
    itemPickup.name = "Item Pickup";
    itemPickup.animationSpeed = 6.0f;
    for (int dir = 0; dir < 4; ++dir) {
        itemPickup.directionalFrames[static_cast<size_t>(dir)].push_back(CharacterFrame{0, dir * 2, 1, 2});
    }

    spriteset.actions.push_back(standing);
    spriteset.actions.push_back(walking);
    spriteset.actions.push_back(knockback);
    spriteset.actions.push_back(itemPickup);
    return spriteset;
}

TileCollection* FindTileCollection(WorldLoadData& world, const std::string& id) {
    for (TileCollection& collection : world.tileCollections) {
        if (collection.id == id) {
            return &collection;
        }
    }
    return nullptr;
}

const TileCollection* FindTileCollection(const WorldLoadData& world, const std::string& id) {
    for (const TileCollection& collection : world.tileCollections) {
        if (collection.id == id) {
            return &collection;
        }
    }
    return nullptr;
}

ItemDefinition* FindItemDefinition(WorldLoadData& world, const std::string& id) {
    for (ItemDefinition& item : world.itemDefinitions) {
        if (item.id == id) {
            return &item;
        }
    }
    return nullptr;
}

const ItemDefinition* FindItemDefinition(const WorldLoadData& world, const std::string& id) {
    for (const ItemDefinition& item : world.itemDefinitions) {
        if (item.id == id) {
            return &item;
        }
    }
    return nullptr;
}

EnemyDefinition* FindEnemyDefinition(WorldLoadData& world, const std::string& id) {
    for (EnemyDefinition& enemy : world.enemyDefinitions) {
        if (enemy.id == id) {
            return &enemy;
        }
    }
    return nullptr;
}

const EnemyDefinition* FindEnemyDefinition(const WorldLoadData& world, const std::string& id) {
    for (const EnemyDefinition& enemy : world.enemyDefinitions) {
        if (enemy.id == id) {
            return &enemy;
        }
    }
    return nullptr;
}

ProjectileDefinition* FindProjectileDefinition(WorldLoadData& world, const std::string& id) {
    for (ProjectileDefinition& projectile : world.projectileDefinitions) {
        if (projectile.id == id) {
            return &projectile;
        }
    }
    return nullptr;
}

const ProjectileDefinition* FindProjectileDefinition(const WorldLoadData& world, const std::string& id) {
    for (const ProjectileDefinition& projectile : world.projectileDefinitions) {
        if (projectile.id == id) {
            return &projectile;
        }
    }
    return nullptr;
}

WeaponDefinition* FindWeaponDefinition(WorldLoadData& world, const std::string& id) {
    for (WeaponDefinition& weapon : world.weaponDefinitions) {
        if (weapon.id == id) {
            return &weapon;
        }
    }
    return nullptr;
}

const WeaponDefinition* FindWeaponDefinition(const WorldLoadData& world, const std::string& id) {
    for (const WeaponDefinition& weapon : world.weaponDefinitions) {
        if (weapon.id == id) {
            return &weapon;
        }
    }
    return nullptr;
}

WarpDefinition* FindWarpDefinition(WorldLoadData& world, const std::string& id) {
    for (WarpDefinition& warp : world.warpDefinitions) {
        if (warp.id == id) {
            return &warp;
        }
    }
    return nullptr;
}

const WarpDefinition* FindWarpDefinition(const WorldLoadData& world, const std::string& id) {
    for (const WarpDefinition& warp : world.warpDefinitions) {
        if (warp.id == id) {
            return &warp;
        }
    }
    return nullptr;
}

wxSize ItemFrameSize(const ItemDefinition& item) {
    if (!item.frames.empty()) {
        return wxSize(std::max(1, item.frames.front().sourceW), std::max(1, item.frames.front().sourceH));
    }
    return wxSize(16, 16);
}

wxSize EnemyFramePixelSize(const EnemyMoveDefinition::AnimationFrame& frame) {
    int maxW = std::max(1, frame.frameWidth * 16);
    int maxH = std::max(1, frame.frameHeight * 16);
    for (const EnemyMoveDefinition::AnimationTile& tile : frame.tiles) {
        const int right = tile.tileX * 16 + std::max(1, tile.sourceW);
        const int bottom = tile.tileY * 16 + std::max(1, tile.sourceH);
        maxW = std::max(maxW, right);
        maxH = std::max(maxH, bottom);
    }
    return wxSize(maxW, maxH);
}

const std::vector<EnemyMoveDefinition::AnimationFrame>* EnemyFramesForDirection(const EnemyMoveDefinition& move, int directionIndex) {
    const int clamped = std::clamp(directionIndex, 0, 3);
    return &move.directionalFrames[static_cast<size_t>(clamped)];
}

std::vector<EnemyMoveDefinition::AnimationFrame>* EnemyFramesForDirection(EnemyMoveDefinition& move, int directionIndex) {
    const int clamped = std::clamp(directionIndex, 0, 3);
    return &move.directionalFrames[static_cast<size_t>(clamped)];
}

const EnemyMoveDefinition::AnimationFrame* FirstEnemyFrame(const EnemyMoveDefinition& move) {
    for (int dir = 0; dir < 4; ++dir) {
        const auto& frames = move.directionalFrames[static_cast<size_t>(dir)];
        if (!frames.empty()) {
            return &frames.front();
        }
    }
    return nullptr;
}

int EnemyEditorDirectionChoiceToIndex(int selection) {
    switch (selection) {
        case 0:
            return static_cast<int>(Direction::Up);
        case 1:
            return static_cast<int>(Direction::Right);
        case 2:
            return static_cast<int>(Direction::Down);
        case 3:
        default:
            return static_cast<int>(Direction::Left);
    }
}

int EnemyEditorDirectionIndexToChoice(int directionIndex) {
    switch (std::clamp(directionIndex, 0, 3)) {
        case static_cast<int>(Direction::Up):
            return 0;
        case static_cast<int>(Direction::Right):
            return 1;
        case static_cast<int>(Direction::Down):
            return 2;
        case static_cast<int>(Direction::Left):
        default:
            return 3;
    }
}

wxBitmap BuildEnemyFramePreviewBitmap(const EnemyMoveDefinition::AnimationFrame* frame, int scale, const wxColour& background = wxColour(24, 29, 36)) {
    const int safeScale = std::max(1, scale);
    if (!frame) {
        wxBitmap fallback(16 * safeScale, 16 * safeScale);
        wxMemoryDC dc;
        dc.SelectObject(fallback);
        dc.SetBackground(wxBrush(background));
        dc.Clear();
        dc.SelectObject(wxNullBitmap);
        return fallback;
    }

    const wxSize frameSize = EnemyFramePixelSize(*frame);
    wxBitmap composed(std::max(16, frameSize.GetWidth()) * safeScale, std::max(16, frameSize.GetHeight()) * safeScale);
    wxMemoryDC dc;
    dc.SelectObject(composed);
    dc.SetBackground(wxBrush(background));
    dc.Clear();

    for (const EnemyMoveDefinition::AnimationTile& tile : frame->tiles) {
        wxBitmap source = LoadBitmapMaybeRelative(tile.sourceImagePath);
        const int dstX = tile.tileX * 16 * safeScale;
        const int dstY = tile.tileY * 16 * safeScale;
        const int dstW = std::max(1, tile.sourceW) * safeScale;
        const int dstH = std::max(1, tile.sourceH) * safeScale;
        if (source.IsOk()) {
            wxMemoryDC atlasDc;
            atlasDc.SelectObject(source);
            dc.StretchBlit(dstX, dstY, dstW, dstH, &atlasDc, tile.sourceX, tile.sourceY, tile.sourceW, tile.sourceH);
            atlasDc.SelectObject(wxNullBitmap);
        } else {
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.SetBrush(wxBrush(wxColour(140, 60, 60)));
            dc.DrawRectangle(dstX, dstY, dstW, dstH);
        }
    }

    dc.SelectObject(wxNullBitmap);
    return composed;
}

TileDef* FindTileDef(TileCollection& collection, int id) {
    for (TileDef& tile : collection.tiles) {
        if (tile.id == id) {
            return &tile;
        }
    }
    return nullptr;
}

const TileDef* FindTileDef(const TileCollection& collection, int id) {
    for (const TileDef& tile : collection.tiles) {
        if (tile.id == id) {
            return &tile;
        }
    }
    return nullptr;
}

const TileCollection* FindTileCollectionByTileId(const WorldLoadData& world, int tileId) {
    for (const TileCollection& collection : world.tileCollections) {
        for (const TileDef& tile : collection.tiles) {
            if (tile.id == tileId) {
                return &collection;
            }
        }
    }
    return nullptr;
}

bool IsImageFullyTransparent(const wxImage& image);
bool IsImageRectFullyTransparent(const wxImage& image, const wxRect& rect);

TileCollection BuildCollectionFromImageGrid(const std::string& imagePath, const std::string& collectionId, const std::string& collectionName, const std::string& description) {
    TileCollection collection;
    collection.id = collectionId;
    collection.name = collectionName;
    collection.description = description;
    collection.imagePath = imagePath;
    collection.tileWidth = 16;
    collection.tileHeight = 16;

    wxImage image;
    if (!image.LoadFile(wxString::FromUTF8(imagePath), wxBITMAP_TYPE_PNG)) {
        return collection;
    }

    collection.imageWidth = image.GetWidth();
    collection.imageHeight = image.GetHeight();
    const int cols = std::max(1, collection.imageWidth / collection.tileWidth);
    const int rows = std::max(1, collection.imageHeight / collection.tileHeight);
    for (int y = 0; y < rows; ++y) {
        for (int x = 0; x < cols; ++x) {
            const int tileId = y * cols + x;
            const wxRect srcRect(x * collection.tileWidth, y * collection.tileHeight, collection.tileWidth, collection.tileHeight);
            if (IsImageRectFullyTransparent(image, srcRect)) {
                continue;
            }

            TileDef tile;
            tile.id = tileId;
            tile.name = "Tile " + std::to_string(tile.id);
            tile.description = "";
            tile.sourceX = x * collection.tileWidth;
            tile.sourceY = y * collection.tileHeight;
            tile.solid = false;
            collection.tiles.push_back(tile);
        }
    }

    // Keep legacy defaults useful when these known IDs exist.
    if (TileDef* grass = FindTileDef(collection, 0)) {
        grass->name = "Grass";
    }
    if (TileDef* wall = FindTileDef(collection, 1)) {
        wall->name = "Stone Wall";
        wall->solid = true;
    }
    if (TileDef* water = FindTileDef(collection, 2)) {
        water->name = "Water";
        water->solid = true;
    }
    if (TileDef* sand = FindTileDef(collection, 3)) {
        sand->name = "Sand";
    }

    return collection;
}

TileCollection BuildForestCollectionFromImage(const std::string& imagePath) {
    return BuildCollectionFromImageGrid(imagePath, "forest_1", "Forest 1", "Imported from Overworld.png");
}

std::filesystem::path ResolveTilesRootPath() {
    const std::vector<std::filesystem::path> candidates = {
        "data/tiles",
        "../data/tiles",
        "../../data/tiles"
    };
    for (const std::filesystem::path& path : candidates) {
        if (std::filesystem::exists(path)) {
            return path;
        }
    }
    return candidates.front();
}

std::filesystem::path CollectionTilesDirectory(const TileCollection& collection) {
    return ResolveTilesRootPath() / collection.id;
}

std::filesystem::path TileImagePathForId(const TileCollection& collection, int tileId) {
    return CollectionTilesDirectory(collection) / ("tile_" + std::to_string(tileId) + ".png");
}

bool TryParseTileIdFromFilename(const std::filesystem::path& path, int& tileIdOut) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (extension != ".png") {
        return false;
    }

    const std::string stem = path.stem().string();
    const std::string prefix = "tile_";
    if (stem.rfind(prefix, 0) != 0) {
        return false;
    }

    try {
        tileIdOut = std::stoi(stem.substr(prefix.size()));
        return true;
    } catch (...) {
        return false;
    }
}

wxString ResolveExistingPath(const std::string& utf8Path) {
    wxFileName directPath(wxString::FromUTF8(utf8Path));
    if (directPath.FileExists()) {
        return directPath.GetFullPath();
    }

    wxFileName relPath(wxString::FromUTF8(utf8Path));
    relPath.MakeAbsolute(wxGetCwd());
    if (relPath.FileExists()) {
        return relPath.GetFullPath();
    }

    return wxString();
}

bool IsImageFullyTransparent(const wxImage& image) {
    if (!image.IsOk() || !image.HasAlpha()) {
        return false;
    }

    const unsigned char* alpha = image.GetAlpha();
    const int pixelCount = image.GetWidth() * image.GetHeight();
    for (int i = 0; i < pixelCount; ++i) {
        if (alpha[i] != 0) {
            return false;
        }
    }
    return true;
}

bool IsImageRectFullyTransparent(const wxImage& image, const wxRect& rect) {
    if (!image.IsOk() || !image.HasAlpha()) {
        return false;
    }
    if (rect.x < 0 || rect.y < 0 || rect.x + rect.width > image.GetWidth() || rect.y + rect.height > image.GetHeight()) {
        return false;
    }

    const unsigned char* alpha = image.GetAlpha();
    const int imageWidth = image.GetWidth();
    for (int y = rect.y; y < rect.y + rect.height; ++y) {
        const int rowOffset = y * imageWidth;
        for (int x = rect.x; x < rect.x + rect.width; ++x) {
            if (alpha[rowOffset + x] != 0) {
                return false;
            }
        }
    }
    return true;
}

std::string ItemTypeLabel(ItemType t) {
    if (t == ItemType::Wheat) {
        return "wheat";
    }
    if (t == ItemType::Powerup) {
        return "powerup";
    }
    return "coin";
}

wxString ItemTriggerFunctionLabel(ItemTriggerFunction function) {
    switch (function) {
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

int ItemTriggerAmount(const ItemDefinition& item, int fallbackValue = 1) {
    for (const ItemTriggerParam& param : item.triggerParams) {
        if (param.key != "amount") {
            continue;
        }
        long parsed = fallbackValue;
        if (wxString::FromUTF8(param.value).ToLong(&parsed)) {
            return static_cast<int>(parsed);
        }
        return fallbackValue;
    }
    return fallbackValue;
}

wxString EnemyMoveTypeLabel(EnemyMoveType type) {
    switch (type) {
        case EnemyMoveType::MoveRandomDirection:
            return "move random direction";
        case EnemyMoveType::Disappear:
            return "disappear";
        case EnemyMoveType::FireProjectile:
            return "fire projectile";
        case EnemyMoveType::StandStill:
        default:
            return "stand still";
    }
}

int EnemyMoveTypeChoiceIndex(EnemyMoveType type) {
    switch (type) {
        case EnemyMoveType::MoveRandomDirection:
            return 0;
        case EnemyMoveType::StandStill:
            return 1;
        case EnemyMoveType::Disappear:
            return 2;
        case EnemyMoveType::FireProjectile:
            return 3;
        default:
            return 1;
    }
}

EnemyMoveType EnemyMoveTypeFromChoiceIndex(int index) {
    switch (index) {
        case 0:
            return EnemyMoveType::MoveRandomDirection;
        case 2:
            return EnemyMoveType::Disappear;
        case 3:
            return EnemyMoveType::FireProjectile;
        case 1:
        default:
            return EnemyMoveType::StandStill;
    }
}

int EnemyReappearModeChoiceIndex(EnemyReappearMode mode) {
    return mode == EnemyReappearMode::RandomPosition ? 1 : 0;
}

EnemyReappearMode EnemyReappearModeFromChoiceIndex(int index) {
    return index == 1 ? EnemyReappearMode::RandomPosition : EnemyReappearMode::SamePlace;
}

void SetItemTriggerAmount(Item& item, int amount) {
    const std::string amountString = std::to_string(amount);
    for (ItemTriggerParam& param : item.triggerParams) {
        if (param.key == "amount") {
            param.value = amountString;
            return;
        }
    }
    item.triggerParams.push_back(ItemTriggerParam{"amount", amountString});
}

void SetItemTriggerAmount(ItemDefinition& item, int amount) {
    const std::string amountString = std::to_string(amount);
    for (ItemTriggerParam& param : item.triggerParams) {
        if (param.key == "amount") {
            param.value = amountString;
            return;
        }
    }
    item.triggerParams.push_back(ItemTriggerParam{"amount", amountString});
}

float ItemTriggerDurationSeconds(const ItemDefinition& item, float fallbackValue = 0.0f) {
    for (const ItemTriggerParam& param : item.triggerParams) {
        if (param.key != "duration") {
            continue;
        }
        double parsed = fallbackValue;
        if (wxString::FromUTF8(param.value).ToDouble(&parsed)) {
            return static_cast<float>(parsed);
        }
        return fallbackValue;
    }
    return fallbackValue;
}

void SetItemTriggerDurationSeconds(ItemDefinition& item, float durationSeconds) {
    const std::string durationString = wxString::Format("%.2f", std::max(0.0f, durationSeconds)).ToStdString();
    for (ItemTriggerParam& param : item.triggerParams) {
        if (param.key == "duration") {
            param.value = durationString;
            return;
        }
    }
    item.triggerParams.push_back(ItemTriggerParam{"duration", durationString});
}

wxString ItemSummaryLabel(const ItemDefinition& item) {
    wxString label = wxString::Format("function: %s", ItemTriggerFunctionLabel(item.triggerFunction));
    if (item.isContainer) {
        label += "\ncontainer: yes";
    }
    if (item.triggerParams.empty()) {
        return label + "\nparams: -";
    }

    wxString paramValues = "params: ";
    for (size_t i = 0; i < item.triggerParams.size(); ++i) {
        if (i > 0) {
            paramValues += ", ";
        }
        paramValues += wxString::FromUTF8(item.triggerParams[i].value);
    }
    return label + "\n" + paramValues;
}

wxString TransitionKindLabel(TransitionKind kind) {
    return kind == TransitionKind::Instant ? "instant" : "fade";
}

wxString WarpSpawnOffsetLabel(WarpSpawnOffset offset) {
    switch (offset) {
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
            return "on top";
    }
}

int WarpSpawnOffsetChoiceIndex(WarpSpawnOffset offset) {
    switch (offset) {
        case WarpSpawnOffset::Above:
            return 1;
        case WarpSpawnOffset::Below:
            return 2;
        case WarpSpawnOffset::Left:
            return 3;
        case WarpSpawnOffset::Right:
            return 4;
        case WarpSpawnOffset::OnTop:
        default:
            return 0;
    }
}

WarpSpawnOffset WarpSpawnOffsetFromChoiceIndex(int index) {
    switch (index) {
        case 1:
            return WarpSpawnOffset::Above;
        case 2:
            return WarpSpawnOffset::Below;
        case 3:
            return WarpSpawnOffset::Left;
        case 4:
            return WarpSpawnOffset::Right;
        case 0:
        default:
            return WarpSpawnOffset::OnTop;
    }
}

struct SheetSpriteDef {
    int sourceX = 0;
    int sourceY = 0;
};

struct SheetSpriteCollectionDef {
    std::string id;
    std::string name;
    std::string sourceImagePath;
    int tileWidth = 16;
    int tileHeight = 16;
    int imageWidth = 0;
    int imageHeight = 0;
    wxBitmap atlas;
    std::vector<SheetSpriteDef> sprites;
};

struct SheetSpritePick {
    std::string collectionId;
    std::string collectionName;
    std::string sourceImagePath;
    int sourceX = 0;
    int sourceY = 0;
    int width = 16;
    int height = 16;
};

std::string NormalizeCollectionIdFragment(const std::string& text) {
    std::string result;
    result.reserve(text.size());
    bool previousWasUnderscore = false;
    for (unsigned char c : text) {
        if (std::isalnum(c)) {
            result.push_back(static_cast<char>(std::tolower(c)));
            previousWasUnderscore = false;
        } else if (!previousWasUnderscore) {
            result.push_back('_');
            previousWasUnderscore = true;
        }
    }

    while (!result.empty() && result.front() == '_') {
        result.erase(result.begin());
    }
    while (!result.empty() && result.back() == '_') {
        result.pop_back();
    }

    if (result.empty()) {
        return "collection";
    }
    return result;
}

std::string UniqueCollectionId(const std::vector<TileCollection>& collections, const std::string& baseId) {
    std::string candidate = baseId.empty() ? "collection" : baseId;
    std::unordered_set<std::string> usedIds;
    for (const TileCollection& collection : collections) {
        usedIds.insert(collection.id);
    }

    if (usedIds.find(candidate) == usedIds.end()) {
        return candidate;
    }

    for (int suffix = 2;; ++suffix) {
        const std::string nextCandidate = candidate + "_" + std::to_string(suffix);
        if (usedIds.find(nextCandidate) == usedIds.end()) {
            return nextCandidate;
        }
    }
}

TileCollection BuildTileCollectionFromSheetLibrary(const SheetSpriteCollectionDef& source, const std::vector<TileCollection>& existingCollections) {
    const std::string displayName = source.name.empty() ? source.id : source.name;
    const std::string collectionId = UniqueCollectionId(existingCollections, NormalizeCollectionIdFragment(displayName));

    TileCollection collection;
    collection.id = collectionId;
    collection.name = displayName.empty() ? collectionId : displayName;
    collection.description = "Imported from " + (source.id.empty() ? collection.name : source.id);
    collection.imagePath = source.sourceImagePath;
    collection.tileWidth = std::max(1, source.tileWidth);
    collection.tileHeight = std::max(1, source.tileHeight);
    collection.imageWidth = source.imageWidth;
    collection.imageHeight = source.imageHeight;

    int nextTileId = 0;
    for (const TileCollection& existingCollection : existingCollections) {
        for (const TileDef& existingTile : existingCollection.tiles) {
            nextTileId = std::max(nextTileId, existingTile.id + 1);
        }
    }

    wxImage image;
    wxString sourcePath = ResolveExistingPath(source.sourceImagePath);
    if (sourcePath.empty()) {
        sourcePath = wxString::FromUTF8(source.sourceImagePath);
    }
    if (!image.LoadFile(sourcePath, wxBITMAP_TYPE_ANY)) {
        return collection;
    }

    const int cols = std::max(1, image.GetWidth() / collection.tileWidth);
    const int rows = std::max(1, image.GetHeight() / collection.tileHeight);
    const int tileCount = cols * rows;

    int lastNonEmptyIndex = -1;
    for (int index = 0; index < tileCount; ++index) {
        const int col = index % cols;
        const int row = index / cols;
        const wxRect srcRect(col * collection.tileWidth, row * collection.tileHeight, collection.tileWidth, collection.tileHeight);
        if (!IsImageRectFullyTransparent(image, srcRect)) {
            lastNonEmptyIndex = index;
        }
    }

    for (int index = 0; index <= lastNonEmptyIndex; ++index) {
        const int col = index % cols;
        const int row = index / cols;

        TileDef tile;
        tile.id = nextTileId++;
        tile.name = "Tile " + std::to_string(index);
        tile.description = collection.description;
        tile.sourceX = col * collection.tileWidth;
        tile.sourceY = row * collection.tileHeight;
        tile.solid = false;
        collection.tiles.push_back(tile);
    }

    if (!collection.tiles.empty()) {
        collection.tiles[0].name = "Grass";
    }
    if (collection.tiles.size() > 1) {
        collection.tiles[1].name = "Stone Wall";
        collection.tiles[1].solid = true;
    }
    if (collection.tiles.size() > 2) {
        collection.tiles[2].name = "Water";
        collection.tiles[2].solid = true;
    }
    if (collection.tiles.size() > 3) {
        collection.tiles[3].name = "Sand";
    }

    return collection;
}

std::filesystem::path ResolveSheetsRootPath() {
    const std::vector<std::filesystem::path> candidates = {
        "data/sheets",
        "../data/sheets",
        "../../data/sheets"
    };
    for (const std::filesystem::path& path : candidates) {
        if (std::filesystem::exists(path)) {
            return path;
        }
    }
    return candidates.front();
}

bool IsSupportedSheetImageExtension(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return extension == ".png" || extension == ".bmp" || extension == ".jpg" || extension == ".jpeg" || extension == ".webp";
}

std::string g_lastSelectedSpriteLibraryCollectionId;

}  // namespace

enum class CanvasMode { PaintTile, PlaceItem, PlaceEnemy, DrawWarp, EdgeLink };

class TileCanvas final : public wxPanel {
public:
    TileCanvas(wxWindow* parent)
        : wxPanel(parent) {
        SetMinSize(wxSize(620, 430));
        SetBackgroundStyle(wxBG_STYLE_PAINT);

        Bind(wxEVT_PAINT, &TileCanvas::OnPaint, this);
        Bind(wxEVT_LEFT_DOWN, &TileCanvas::OnMouseDown, this);
        Bind(wxEVT_LEFT_UP, &TileCanvas::OnMouseUp, this);
        Bind(wxEVT_MOTION, &TileCanvas::OnMouseMove, this);
        Bind(wxEVT_RIGHT_DOWN, &TileCanvas::OnRightMouseDown, this);
        Bind(wxEVT_RIGHT_UP, &TileCanvas::OnRightMouseUp, this);
        Bind(wxEVT_CONTEXT_MENU, &TileCanvas::OnContextMenu, this);
        Bind(wxEVT_MOUSEWHEEL, &TileCanvas::OnMouseWheel, this);
    }

    void SetScreen(ScreenLoadData* screen) {
        screen_ = screen;
        Refresh();
    }

    void SetTileSelection(int tile) {
        selectedTile_ = tile;
    }

    void SetItemDefinitions(const std::vector<ItemDefinition>* itemDefinitions) {
        itemDefinitions_ = itemDefinitions;
        Refresh();
    }

    void SetItemPlacements(const std::vector<ItemPlacement>* itemPlacements) {
        itemPlacements_ = itemPlacements;
        Refresh();
    }

    void SetEnemyDefinitions(const std::vector<EnemyDefinition>* enemyDefinitions) {
        enemyDefinitions_ = enemyDefinitions;
        Refresh();
    }

    void SetEnemyPlacements(const std::vector<EnemyPlacement>* enemyPlacements) {
        enemyPlacements_ = enemyPlacements;
        Refresh();
    }

    void SetWarpDefinitions(const std::vector<WarpDefinition>* warpDefinitions) {
        warpDefinitions_ = warpDefinitions;
        Refresh();
    }

    void SetWarpPlacements(const std::vector<WarpPlacement>* warpPlacements) {
        warpPlacements_ = warpPlacements;
        Refresh();
    }

    void SetItemSelection(const std::string& itemId) {
        selectedItemId_ = itemId;
        Refresh();
    }

    void SetEnemySelection(const std::string& enemyId) {
        selectedEnemyId_ = enemyId;
        Refresh();
    }

    void SetWarpSelection(const std::string& warpId, int endpointIndex) {
        selectedWarpId_ = warpId;
        selectedWarpEndpointIndex_ = std::clamp(endpointIndex, 0, 1);
        Refresh();
    }

    void SetTileCollection(const TileCollection* collection) {
        collection_ = collection;
        Refresh();
    }

    void SetTileCollections(const std::vector<TileCollection>* collections) {
        allCollections_ = collections;
        collectionAtlasCache_.clear();

        if (!allCollections_) {
            Refresh();
            return;
        }

        for (const TileCollection& collection : *allCollections_) {
            if (collection.id.empty() || collection.imagePath.empty()) {
                continue;
            }

            wxBitmap atlas;
            wxFileName directPath(wxString::FromUTF8(collection.imagePath));
            if (directPath.FileExists()) {
                atlas.LoadFile(directPath.GetFullPath(), wxBITMAP_TYPE_PNG);
            } else {
                wxFileName relPath(wxString::FromUTF8(collection.imagePath));
                relPath.MakeAbsolute(wxGetCwd());
                if (relPath.FileExists()) {
                    atlas.LoadFile(relPath.GetFullPath(), wxBITMAP_TYPE_PNG);
                }
            }

            if (atlas.IsOk()) {
                collectionAtlasCache_[collection.id] = atlas;
            }
        }

        Refresh();
    }

    void SetTileOwnerHints(const std::unordered_map<int, std::string>* ownerHints) {
        tileOwnerHints_ = ownerHints;
        Refresh();
    }

    void SetDirtyCallback(std::function<void()> cb) {
        onDirty_ = std::move(cb);
    }

    void SetMode(CanvasMode mode) {
        mode_ = mode;
        Refresh();
    }

    void SetPaintLayer(int layer) {
        paintLayer_ = std::clamp(layer, 0, kTileLayers - 1);
        Refresh();
    }

    void SetLayerVisibility(int layer, bool visible) {
        if (layer < 0 || layer >= kTileLayers) {
            return;
        }
        layerVisible_[static_cast<size_t>(layer)] = visible;
        Refresh();
    }

    void SetShowHitboxOverlay(bool show) {
        showHitboxOverlay_ = show;
        Refresh();
    }

    void SetWarpPlaceCallback(std::function<void(float, float)> cb) {
        onWarpPlace_ = std::move(cb);
    }

    void SetEdgeClickCallback(std::function<void(const std::string&)> cb) {
        onEdgeClick_ = std::move(cb);
    }

    void SetItemPlaceCallback(std::function<void(float, float)> cb) {
        onItemPlace_ = std::move(cb);
    }

    void SetItemMoveCallback(std::function<void(int, float, float)> cb) {
        onItemMove_ = std::move(cb);
    }

    void SetItemPickCallback(std::function<void(const std::string&)> cb) {
        onItemPick_ = std::move(cb);
    }

    void SetItemPlacementPickCallback(std::function<void(int)> cb) {
        onItemPlacementPick_ = std::move(cb);
    }

    void SetEnemyPlaceCallback(std::function<void(float, float)> cb) {
        onEnemyPlace_ = std::move(cb);
    }

    void SetEnemyPickCallback(std::function<void(const std::string&)> cb) {
        onEnemyPick_ = std::move(cb);
    }

    void SetTilePickCallback(std::function<void(int)> cb) {
        onTilePick_ = std::move(cb);
    }

    void SetLayerChangedCallback(std::function<void(int)> cb) {
        onLayerChanged_ = std::move(cb);
    }

private:
    bool TryGetGridTile(const wxPoint& pt, int& tx, int& ty) const {
        const wxSize size = GetClientSize();
        const int scale = std::max(1, std::min(size.GetWidth() / kTilesWide, size.GetHeight() / kTilesHigh));
        const int drawW = kTilesWide * scale;
        const int drawH = kTilesHigh * scale;
        const int ox = (size.GetWidth() - drawW) / 2;
        const int oy = (size.GetHeight() - drawH) / 2;

        if (pt.x < ox || pt.y < oy || pt.x >= ox + drawW || pt.y >= oy + drawH) {
            return false;
        }

        tx = (pt.x - ox) / scale;
        ty = (pt.y - oy) / scale;
        return tx >= 0 && tx < kTilesWide && ty >= 0 && ty < kTilesHigh;
    }

    bool TryGetScreenPixel(const wxPoint& pt, float& pixelX, float& pixelY) const {
        const wxSize size = GetClientSize();
        const int scale = std::max(1, std::min(size.GetWidth() / kTilesWide, size.GetHeight() / kTilesHigh));
        const int drawW = kTilesWide * scale;
        const int drawH = kTilesHigh * scale;
        const int ox = (size.GetWidth() - drawW) / 2;
        const int oy = (size.GetHeight() - drawH) / 2;
        if (pt.x < ox || pt.y < oy || pt.x >= ox + drawW || pt.y >= oy + drawH) {
            return false;
        }

        pixelX = std::clamp(static_cast<float>(pt.x - ox) * static_cast<float>(kTileSize) / static_cast<float>(scale), 0.0f, static_cast<float>(kScreenPixelWidth));
        pixelY = std::clamp(static_cast<float>(pt.y - oy) * static_cast<float>(kTileSize) / static_cast<float>(scale), 0.0f, static_cast<float>(kScreenPixelHeight));
        return true;
    }

    const ItemDefinition* FindCanvasItemDefinition(const std::string& itemId) const {
        if (!itemDefinitions_) {
            return nullptr;
        }
        for (const ItemDefinition& item : *itemDefinitions_) {
            if (item.id == itemId) {
                return &item;
            }
        }
        return nullptr;
    }

    const WarpDefinition* FindCanvasWarpDefinition(const std::string& warpId) const {
        if (!warpDefinitions_) {
            return nullptr;
        }
        for (const WarpDefinition& definition : *warpDefinitions_) {
            if (definition.id == warpId) {
                return &definition;
            }
        }
        return nullptr;
    }

    const EnemyDefinition* FindCanvasEnemyDefinition(const std::string& enemyId) const {
        if (!enemyDefinitions_) {
            return nullptr;
        }
        for (const EnemyDefinition& enemy : *enemyDefinitions_) {
            if (enemy.id == enemyId) {
                return &enemy;
            }
        }
        return nullptr;
    }

    int FindItemPlacementIndexAtPixel(float pixelX, float pixelY) const {
        if (!itemPlacements_) {
            return -1;
        }

        for (int index = static_cast<int>(itemPlacements_->size()) - 1; index >= 0; --index) {
            const ItemPlacement& placement = (*itemPlacements_)[static_cast<size_t>(index)];
            const ItemDefinition* definition = FindCanvasItemDefinition(placement.itemId);
            const wxSize itemSize = definition ? ItemFrameSize(*definition) : wxSize(16, 16);
            if (pixelX >= placement.x && pixelX <= placement.x + itemSize.GetWidth()
                && pixelY >= placement.y && pixelY <= placement.y + itemSize.GetHeight()) {
                return index;
            }
        }

        return -1;
    }

    int FindEnemyPlacementIndexAtPixel(float pixelX, float pixelY) const {
        if (!enemyPlacements_) {
            return -1;
        }

        for (int index = static_cast<int>(enemyPlacements_->size()) - 1; index >= 0; --index) {
            const EnemyPlacement& placement = (*enemyPlacements_)[static_cast<size_t>(index)];
            const EnemyDefinition* definition = FindCanvasEnemyDefinition(placement.enemyId);
            float w = 12.0f;
            float h = 12.0f;
            if (definition && !definition->moves.empty()) {
                const EnemyMoveDefinition& move = definition->moves.front();
                if (const EnemyMoveDefinition::AnimationFrame* frame = FirstEnemyFrame(move)) {
                    const wxSize frameSize = EnemyFramePixelSize(*frame);
                    w = static_cast<float>(frameSize.GetWidth());
                    h = static_cast<float>(frameSize.GetHeight());
                } else if (!move.hitboxes.empty()) {
                    w = static_cast<float>(std::max(1, move.hitboxes.front().w));
                    h = static_cast<float>(std::max(1, move.hitboxes.front().h));
                }
            }
            if (pixelX >= placement.x && pixelX <= placement.x + w && pixelY >= placement.y && pixelY <= placement.y + h) {
                return index;
            }
        }

        return -1;
    }

    int PickTileAt(const wxPoint& pt) const {
        if (!screen_) {
            return -1;
        }

        int tx = 0;
        int ty = 0;
        if (!TryGetGridTile(pt, tx, ty)) {
            return -1;
        }

        const size_t tileIndex = static_cast<size_t>(ty * kTilesWide + tx);
        const int activeTileId = screen_->screen.tileLayerIds[static_cast<size_t>(paintLayer_)][tileIndex];
        if (activeTileId >= 0) {
            return activeTileId;
        }

        for (int layer = kTileLayers - 1; layer >= 0; --layer) {
            if (!layerVisible_[static_cast<size_t>(layer)]) {
                continue;
            }
            const int tileId = screen_->screen.tileLayerIds[static_cast<size_t>(layer)][tileIndex];
            if (tileId >= 0) {
                return tileId;
            }
        }

        return -1;
    }

    void PaintAt(const wxPoint& pt) {
        if (!screen_) {
            return;
        }

        int tx = 0;
        int ty = 0;
        if (!TryGetGridTile(pt, tx, ty)) {
            return;
        }

        screen_->screen.tileLayerIds[static_cast<size_t>(paintLayer_)][static_cast<size_t>(ty * kTilesWide + tx)] = selectedTile_;
        if (onDirty_) {
            onDirty_();
        }
        Refresh();
    }

    void OnMouseDown(wxMouseEvent& event) {
        const wxPoint pt = event.GetPosition();
        if (mode_ == CanvasMode::PaintTile) {
            dragging_ = true;
            // Check if clicking on a tile that already has the active tile; if so, remove it
            if (!screen_) {
                PaintAt(pt);
            } else {
                int tx = 0, ty = 0;
                if (TryGetGridTile(pt, tx, ty)) {
                    const size_t tileIndex = static_cast<size_t>(ty * kTilesWide + tx);
                    const int currentTile = screen_->screen.tileLayerIds[static_cast<size_t>(paintLayer_)][tileIndex];
                    if (currentTile == selectedTile_) {
                        // Remove the tile by setting it to -1
                        screen_->screen.tileLayerIds[static_cast<size_t>(paintLayer_)][tileIndex] = -1;
                        if (onDirty_) {
                            onDirty_();
                        }
                        Refresh();
                    } else {
                        // Paint the selected tile
                        PaintAt(pt);
                    }
                } else {
                    PaintAt(pt);
                }
            }
        } else if (mode_ == CanvasMode::PlaceItem) {
            if (!screen_ || selectedItemId_.empty() || !onItemPlace_) {
                return;
            }

            float pixelX = 0.0f;
            float pixelY = 0.0f;
            if (!TryGetScreenPixel(pt, pixelX, pixelY)) {
                return;
            }

            const int hitPlacementIndex = FindItemPlacementIndexAtPixel(pixelX, pixelY);
            if (hitPlacementIndex >= 0 && itemPlacements_ && hitPlacementIndex < static_cast<int>(itemPlacements_->size())) {
                const ItemPlacement& placement = (*itemPlacements_)[static_cast<size_t>(hitPlacementIndex)];
                if (const ItemDefinition* definition = FindCanvasItemDefinition(placement.itemId)) {
                    itemDragging_ = true;
                    itemDragMoved_ = false;
                    draggedItemIndex_ = hitPlacementIndex;
                    draggedItemId_ = placement.itemId;
                    dragStartPixelX_ = pixelX;
                    dragStartPixelY_ = pixelY;
                    dragAnchorOffsetX_ = pixelX - placement.x;
                    dragAnchorOffsetY_ = pixelY - placement.y;
                    if (onItemPick_) {
                        onItemPick_(placement.itemId);
                    }
                    Refresh();
                    return;
                }
            }

            onItemPlace_(pixelX, pixelY);
            Refresh();
        } else if (mode_ == CanvasMode::PlaceEnemy) {
            if (!screen_ || selectedEnemyId_.empty() || !onEnemyPlace_) {
                return;
            }

            float pixelX = 0.0f;
            float pixelY = 0.0f;
            if (!TryGetScreenPixel(pt, pixelX, pixelY)) {
                return;
            }

            onEnemyPlace_(pixelX, pixelY);
            Refresh();
        } else if (mode_ == CanvasMode::DrawWarp) {
            if (!screen_ || !onWarpPlace_) {
                return;
            }
            float pixelX = 0.0f;
            float pixelY = 0.0f;
            if (TryGetScreenPixel(pt, pixelX, pixelY)) {
                onWarpPlace_(pixelX, pixelY);
            }
        } else if (mode_ == CanvasMode::EdgeLink) {
            if (screen_) {
                const std::string edge = EdgeFromPoint(pt);
                if (!edge.empty() && onEdgeClick_) {
                    onEdgeClick_(edge);
                }
            }
        }
    }

    void OnMouseUp(wxMouseEvent& event) {
        if (mode_ == CanvasMode::PaintTile) {
            dragging_ = false;
        } else if (mode_ == CanvasMode::PlaceItem) {
            if (itemDragging_) {
                float pixelX = 0.0f;
                float pixelY = 0.0f;
                if (TryGetScreenPixel(event.GetPosition(), pixelX, pixelY)) {
                    if (!itemDragMoved_) {
                        if (draggedItemId_ == selectedItemId_ && onItemPlace_) {
                            onItemPlace_(pixelX, pixelY);
                        }
                    } else if (onItemMove_) {
                        onItemMove_(draggedItemIndex_, pixelX - dragAnchorOffsetX_, pixelY - dragAnchorOffsetY_);
                    }
                }
                itemDragging_ = false;
                itemDragMoved_ = false;
                draggedItemIndex_ = -1;
                draggedItemId_.clear();
                Refresh();
            }
        }
    }

    void OnMouseMove(wxMouseEvent& event) {
        if (mode_ == CanvasMode::PaintTile) {
            if (dragging_ && event.LeftIsDown()) {
                PaintAt(event.GetPosition());
            }
        } else if (mode_ == CanvasMode::PlaceItem) {
            if (itemDragging_ && event.LeftIsDown() && onItemMove_) {
                float pixelX = 0.0f;
                float pixelY = 0.0f;
                if (TryGetScreenPixel(event.GetPosition(), pixelX, pixelY)) {
                    const float dx = std::abs(pixelX - dragStartPixelX_);
                    const float dy = std::abs(pixelY - dragStartPixelY_);
                    if (dx >= 2.0f || dy >= 2.0f) {
                        itemDragMoved_ = true;
                    }
                    if (itemDragMoved_) {
                        onItemMove_(draggedItemIndex_, pixelX - dragAnchorOffsetX_, pixelY - dragAnchorOffsetY_);
                    }
                }
            }
        }
    }

    void OnRightMouseDown(wxMouseEvent& event) {
        if (mode_ == CanvasMode::PlaceItem) {
            TryPickItemAtPoint(event.GetPosition());
            return;
        }
        if (mode_ == CanvasMode::PlaceEnemy) {
            TryPickEnemyAtPoint(event.GetPosition());
            return;
        }
        TryPickTileAtPoint(event.GetPosition());
    }

    void OnRightMouseUp(wxMouseEvent& event) {
        if (mode_ == CanvasMode::PlaceItem) {
            return;
        }
        if (mode_ == CanvasMode::PlaceEnemy) {
            return;
        }
        TryPickTileAtPoint(event.GetPosition());
    }

    void OnContextMenu(wxContextMenuEvent& event) {
        wxPoint p = event.GetPosition();
        if (p.x == -1 && p.y == -1) {
            return;
        }
        p = ScreenToClient(p);
        TryPickTileAtPoint(p);
    }

    void TryPickTileAtPoint(const wxPoint& pt) {
        const int tileId = PickTileAt(pt);
        if (tileId >= 0 && onTilePick_) {
            onTilePick_(tileId);
        }
    }

    void TryPickItemAtPoint(const wxPoint& pt) {
        if (!onItemPick_ && !onItemPlacementPick_) {
            return;
        }

        float pixelX = 0.0f;
        float pixelY = 0.0f;
        if (!TryGetScreenPixel(pt, pixelX, pixelY)) {
            return;
        }

        const int hitPlacementIndex = FindItemPlacementIndexAtPixel(pixelX, pixelY);
        if (hitPlacementIndex < 0 || !itemPlacements_ || hitPlacementIndex >= static_cast<int>(itemPlacements_->size())) {
            return;
        }

        const ItemPlacement& placement = (*itemPlacements_)[static_cast<size_t>(hitPlacementIndex)];
        if (onItemPick_) {
            onItemPick_(placement.itemId);
        }
        if (onItemPlacementPick_) {
            onItemPlacementPick_(hitPlacementIndex);
        }
    }

    void TryPickEnemyAtPoint(const wxPoint& pt) {
        if (!onEnemyPick_) {
            return;
        }

        float pixelX = 0.0f;
        float pixelY = 0.0f;
        if (!TryGetScreenPixel(pt, pixelX, pixelY)) {
            return;
        }

        const int hitPlacementIndex = FindEnemyPlacementIndexAtPixel(pixelX, pixelY);
        if (hitPlacementIndex < 0 || !enemyPlacements_ || hitPlacementIndex >= static_cast<int>(enemyPlacements_->size())) {
            return;
        }

        const EnemyPlacement& placement = (*enemyPlacements_)[static_cast<size_t>(hitPlacementIndex)];
        onEnemyPick_(placement.enemyId);
    }

    void OnMouseWheel(wxMouseEvent& event) {
        const int rotation = event.GetWheelRotation();
        const int delta = std::max(1, event.GetWheelDelta());
        const int steps = rotation / delta;
        if (steps == 0) {
            event.Skip();
            return;
        }

        const int nextLayer = std::clamp(paintLayer_ + steps, 0, kTileLayers - 1);
        if (nextLayer != paintLayer_) {
            paintLayer_ = nextLayer;
            Refresh();
            if (onLayerChanged_) {
                onLayerChanged_(paintLayer_);
            }
        }
    }

    bool IsInCanvas(const wxPoint& pt) const {
        const wxSize size = GetClientSize();
        const int scale = std::max(1, std::min(size.GetWidth() / kTilesWide, size.GetHeight() / kTilesHigh));
        const int ox = (size.GetWidth() - kTilesWide * scale) / 2;
        const int oy = (size.GetHeight() - kTilesHigh * scale) / 2;
        return pt.x >= ox && pt.y >= oy && pt.x < ox + kTilesWide * scale && pt.y < oy + kTilesHigh * scale;
    }

    std::string EdgeFromPoint(const wxPoint& pt) const {
        const wxSize size = GetClientSize();
        const int scale = std::max(1, std::min(size.GetWidth() / kTilesWide, size.GetHeight() / kTilesHigh));
        const int ox = (size.GetWidth() - kTilesWide * scale) / 2;
        const int oy = (size.GetHeight() - kTilesHigh * scale) / 2;
        if (pt.x < ox || pt.y < oy || pt.x >= ox + kTilesWide * scale || pt.y >= oy + kTilesHigh * scale) {
            return "";
        }
        const int tx = (pt.x - ox) / scale;
        const int ty = (pt.y - oy) / scale;
        const int distLeft   = tx;
        const int distRight  = kTilesWide - 1 - tx;
        const int distTop    = ty;
        const int distBottom = kTilesHigh - 1 - ty;
        const int minDist = std::min({distLeft, distRight, distTop, distBottom});
        if (minDist > 2) return "";
        if (distLeft == minDist)   return "left";
        if (distRight == minDist)  return "right";
        if (distTop == minDist)    return "up";
        return "down";
    }

    void OnPaint(wxPaintEvent&) {
        wxAutoBufferedPaintDC dc(this);
        dc.SetBackground(wxBrush(wxColour(20, 24, 30)));
        dc.Clear();

        if (!screen_) {
            dc.SetTextForeground(wxColour(180, 186, 198));
            dc.DrawText("No screen selected", 16, 16);
            return;
        }

        const wxSize size = GetClientSize();
        const int scale = std::max(1, std::min(size.GetWidth() / kTilesWide, size.GetHeight() / kTilesHigh));
        const int drawW = kTilesWide * scale;
        const int drawH = kTilesHigh * scale;
        const int ox = (size.GetWidth() - drawW) / 2;
        const int oy = (size.GetHeight() - drawH) / 2;

        dc.SetPen(*wxTRANSPARENT_PEN);
        for (int ty = 0; ty < kTilesHigh; ++ty) {
            for (int tx = 0; tx < kTilesWide; ++tx) {
                const size_t tileIndex = static_cast<size_t>(ty * kTilesWide + tx);
                for (int layer = 0; layer < kTileLayers; ++layer) {
                    if (!layerVisible_[static_cast<size_t>(layer)]) {
                        continue;
                    }
                    const int tileId = screen_->screen.tileLayerIds[static_cast<size_t>(layer)][tileIndex];
                    if (tileId < 0) {
                        continue;
                    }
                    DrawTile(dc, tileId, ox + tx * scale, oy + ty * scale, scale);
                }
            }
        }

        DrawItemPlacements(dc, ox, oy, scale);
        DrawEnemyPlacements(dc, ox, oy, scale);

        dc.SetPen(wxPen(wxColour(38, 48, 61), 1));
        for (int x = 0; x <= kTilesWide; ++x) {
            dc.DrawLine(ox + x * scale, oy, ox + x * scale, oy + drawH);
        }
        for (int y = 0; y <= kTilesHigh; ++y) {
            dc.DrawLine(ox, oy + y * scale, ox + drawW, oy + y * scale);
        }

        if (showHitboxOverlay_) {
            DrawCollisionHitboxOverlay(dc, ox, oy, scale);
        }

        // Transition edge overlays
        if (screen_ && !screen_->transitions.empty()) {
            for (const ScreenTransition& tr : screen_->transitions) {
                dc.SetPen(wxPen(wxColour(80, 210, 120), 4));
                if (tr.edge == "left") {
                    dc.DrawLine(ox, oy + 2, ox, oy + drawH - 2);
                } else if (tr.edge == "right") {
                    dc.DrawLine(ox + drawW, oy + 2, ox + drawW, oy + drawH - 2);
                } else if (tr.edge == "up") {
                    dc.DrawLine(ox + 2, oy, ox + drawW - 2, oy);
                } else if (tr.edge == "down") {
                    dc.DrawLine(ox + 2, oy + drawH, ox + drawW - 2, oy + drawH);
                }
            }
        }

        DrawWarpPlacements(dc, ox, oy, scale);

        // Edge link mode hover hint
        if (mode_ == CanvasMode::EdgeLink) {
            dc.SetPen(wxPen(wxColour(80, 210, 120, 160), 2, wxPENSTYLE_DOT));
            dc.SetBrush(*wxTRANSPARENT_BRUSH);
            dc.DrawRectangle(ox + 1, oy + 1, drawW - 2, drawH - 2);
        } else if (mode_ == CanvasMode::PlaceItem) {
            dc.SetPen(wxPen(wxColour(255, 210, 96, 180), 2, wxPENSTYLE_DOT));
            dc.SetBrush(*wxTRANSPARENT_BRUSH);
            dc.DrawRectangle(ox + 1, oy + 1, drawW - 2, drawH - 2);
        } else if (mode_ == CanvasMode::PlaceEnemy) {
            dc.SetPen(wxPen(wxColour(240, 120, 120, 180), 2, wxPENSTYLE_DOT));
            dc.SetBrush(*wxTRANSPARENT_BRUSH);
            dc.DrawRectangle(ox + 1, oy + 1, drawW - 2, drawH - 2);
        }

        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(wxColour(10, 14, 20, 220)));
        dc.DrawRoundedRectangle(ox + 10, oy + 10, 164, 30, 6);
        dc.SetTextForeground(wxColour(240, 245, 255));
        dc.SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Segoe UI"));
        dc.DrawText(wxString::Format("Active Layer: L%d", paintLayer_ + 1), ox + 20, oy + 16);

        dc.SetPen(wxPen(wxColour(103, 146, 210), 2));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.DrawRectangle(ox - 1, oy - 1, drawW + 2, drawH + 2);
    }

    const TileDef* FindTileDefAcrossCollections(int tileId, const TileCollection*& outCollection) const {
        outCollection = nullptr;

        if (allCollections_ && tileOwnerHints_) {
            auto hintedOwner = tileOwnerHints_->find(tileId);
            if (hintedOwner != tileOwnerHints_->end()) {
                for (const TileCollection& collection : *allCollections_) {
                    if (collection.id != hintedOwner->second) {
                        continue;
                    }
                    for (const TileDef& tile : collection.tiles) {
                        if (tile.id == tileId) {
                            outCollection = &collection;
                            return &tile;
                        }
                    }
                    break;
                }
            }
        }

        if (allCollections_) {
            for (const TileCollection& collection : *allCollections_) {
                for (const TileDef& tile : collection.tiles) {
                    if (tile.id == tileId) {
                        outCollection = &collection;
                        return &tile;
                    }
                }
            }
        }

        // Fallback for cases where collection list is not yet wired.
        if (collection_) {
            for (const TileDef& tile : collection_->tiles) {
                if (tile.id == tileId) {
                    outCollection = collection_;
                    return &tile;
                }
            }
        }

        return nullptr;
    }

    void DrawTile(wxDC& dc, int tileId, int x, int y, int scale) {
        const TileCollection* owningCollection = nullptr;
        const TileDef* tile = FindTileDefAcrossCollections(tileId, owningCollection);

        wxBitmap atlas;
        if (owningCollection) {
            auto it = collectionAtlasCache_.find(owningCollection->id);
            if (it != collectionAtlasCache_.end()) {
                atlas = it->second;
            }
        }

        if (tile && owningCollection && atlas.IsOk() && owningCollection->tileWidth > 0 && owningCollection->tileHeight > 0) {
            wxMemoryDC atlasDc;
            atlasDc.SelectObject(atlas);
            dc.StretchBlit(
                x,
                y,
                scale,
                scale,
                &atlasDc,
                tile->sourceX,
                tile->sourceY,
                owningCollection->tileWidth,
                owningCollection->tileHeight
            );
            atlasDc.SelectObject(wxNullBitmap);
            return;
        }

        dc.SetBrush(wxBrush(TileColorFromId(tileId)));
        dc.DrawRectangle(x, y, scale, scale);
    }

    void DrawItemPlacements(wxDC& dc, int ox, int oy, int scale) {
        if (!itemPlacements_) {
            return;
        }

        for (const ItemPlacement& placement : *itemPlacements_) {
            const ItemDefinition* definition = FindCanvasItemDefinition(placement.itemId);
            if (!definition) {
                continue;
            }

            const int drawX = ox + static_cast<int>(std::round(placement.x * scale / kTileSize));
            const int drawY = oy + static_cast<int>(std::round(placement.y * scale / kTileSize));
            const wxSize itemSize = ItemFrameSize(*definition);
            const int drawW = std::max(2, static_cast<int>(std::round(static_cast<float>(itemSize.GetWidth()) * scale / kTileSize)));
            const int drawH = std::max(2, static_cast<int>(std::round(static_cast<float>(itemSize.GetHeight()) * scale / kTileSize)));

            if (!definition->frames.empty()) {
                wxBitmap source = LoadBitmapMaybeRelative(definition->frames.front().sourceImagePath);
                if (source.IsOk()) {
                    const ItemAnimationFrame& frame = definition->frames.front();
                    wxMemoryDC atlasDc;
                    atlasDc.SelectObject(source);
                    dc.StretchBlit(drawX, drawY, drawW, drawH, &atlasDc, frame.sourceX, frame.sourceY, frame.sourceW, frame.sourceH);
                    atlasDc.SelectObject(wxNullBitmap);
                } else {
                    dc.SetPen(*wxTRANSPARENT_PEN);
                    dc.SetBrush(wxBrush(wxColour(235, 186, 84)));
                    dc.DrawRectangle(drawX, drawY, drawW, drawH);
                }
            } else {
                dc.SetPen(*wxTRANSPARENT_PEN);
                dc.SetBrush(wxBrush(wxColour(235, 186, 84)));
                dc.DrawRectangle(drawX, drawY, drawW, drawH);
            }

            if (placement.itemId == selectedItemId_) {
                dc.SetPen(wxPen(wxColour(255, 220, 110), 2));
                dc.SetBrush(*wxTRANSPARENT_BRUSH);
                dc.DrawRectangle(drawX - 1, drawY - 1, drawW + 2, drawH + 2);
            }
        }
    }

    void DrawWarpPlacements(wxDC& dc, int ox, int oy, int scale) {
        if (!warpPlacements_ || !warpDefinitions_) {
            return;
        }

        dc.SetFont(wxFont(7, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Segoe UI"));
        for (const WarpPlacement& placement : *warpPlacements_) {
            const WarpDefinition* definition = FindCanvasWarpDefinition(placement.warpId);
            if (!definition) {
                continue;
            }

            const int endpointIndex = std::clamp(placement.endpointIndex, 0, 1);
            const WarpEndpointDefinition& endpoint = definition->endpoints[static_cast<size_t>(endpointIndex)];
            const int drawX = ox + static_cast<int>(std::round(placement.x * scale / kTileSize));
            const int drawY = oy + static_cast<int>(std::round(placement.y * scale / kTileSize));
            const int drawW = std::max(2, static_cast<int>(std::round(static_cast<float>(kTileSize) * scale / kTileSize)));
            const int drawH = std::max(2, static_cast<int>(std::round(static_cast<float>(kTileSize) * scale / kTileSize)));

            DrawTile(dc, endpoint.tileId, drawX, drawY, scale);

            dc.SetPen(wxPen(wxColour(255, 200, 40), 2));
            dc.SetBrush(*wxTRANSPARENT_BRUSH);
            for (const TileHitbox& hitbox : endpoint.hitboxes) {
                const int hx = drawX + static_cast<int>(std::round(static_cast<float>(hitbox.x) * scale / kTileSize));
                const int hy = drawY + static_cast<int>(std::round(static_cast<float>(hitbox.y) * scale / kTileSize));
                const int hw = std::max(1, static_cast<int>(std::round(static_cast<float>(hitbox.w) * scale / kTileSize)));
                const int hh = std::max(1, static_cast<int>(std::round(static_cast<float>(hitbox.h) * scale / kTileSize)));
                dc.DrawRectangle(hx, hy, hw, hh);
            }

            dc.SetTextForeground(wxColour(255, 230, 80));
            dc.DrawText(wxString::Format("%s %c", wxString::FromUTF8(definition->name), endpointIndex == 0 ? 'A' : 'B'), drawX + 2, drawY + 1);

            if (placement.warpId == selectedWarpId_ && endpointIndex == selectedWarpEndpointIndex_) {
                dc.SetPen(wxPen(wxColour(255, 245, 170), 2));
                dc.SetBrush(*wxTRANSPARENT_BRUSH);
                dc.DrawRectangle(drawX - 1, drawY - 1, drawW + 2, drawH + 2);
            }
        }
    }

    void DrawEnemyPlacements(wxDC& dc, int ox, int oy, int scale) {
        if (!enemyPlacements_) {
            return;
        }

        dc.SetFont(wxFont(7, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Segoe UI"));
        for (const EnemyPlacement& placement : *enemyPlacements_) {
            const EnemyDefinition* definition = FindCanvasEnemyDefinition(placement.enemyId);
            const int drawX = ox + static_cast<int>(std::round(placement.x * scale / kTileSize));
            const int drawY = oy + static_cast<int>(std::round(placement.y * scale / kTileSize));
            int drawW = std::max(2, static_cast<int>(std::round(12.0f * scale / kTileSize)));
            int drawH = std::max(2, static_cast<int>(std::round(12.0f * scale / kTileSize)));

            if (definition && !definition->moves.empty()) {
                const EnemyMoveDefinition& move = definition->moves.front();
                if (const EnemyMoveDefinition::AnimationFrame* frame = FirstEnemyFrame(move)) {
                    const wxSize frameSize = EnemyFramePixelSize(*frame);
                    drawW = std::max(2, static_cast<int>(std::round(static_cast<float>(frameSize.GetWidth()) * scale / kTileSize)));
                    drawH = std::max(2, static_cast<int>(std::round(static_cast<float>(frameSize.GetHeight()) * scale / kTileSize)));

                    bool drewAnyTile = false;
                    for (const EnemyMoveDefinition::AnimationTile& tile : frame->tiles) {
                        wxBitmap source = LoadBitmapMaybeRelative(tile.sourceImagePath);
                        const int tileDrawX = drawX + static_cast<int>(std::round(static_cast<float>(tile.tileX * 16) * scale / kTileSize));
                        const int tileDrawY = drawY + static_cast<int>(std::round(static_cast<float>(tile.tileY * 16) * scale / kTileSize));
                        const int tileDrawW = std::max(1, static_cast<int>(std::round(static_cast<float>(std::max(1, tile.sourceW)) * scale / kTileSize)));
                        const int tileDrawH = std::max(1, static_cast<int>(std::round(static_cast<float>(std::max(1, tile.sourceH)) * scale / kTileSize)));
                        if (source.IsOk()) {
                            wxMemoryDC atlasDc;
                            atlasDc.SelectObject(source);
                            dc.StretchBlit(tileDrawX, tileDrawY, tileDrawW, tileDrawH, &atlasDc, tile.sourceX, tile.sourceY, tile.sourceW, tile.sourceH);
                            atlasDc.SelectObject(wxNullBitmap);
                            drewAnyTile = true;
                        }
                    }

                    if (!drewAnyTile) {
                        dc.SetPen(*wxTRANSPARENT_PEN);
                        dc.SetBrush(wxBrush(wxColour(146, 40, 40)));
                        dc.DrawRectangle(drawX, drawY, drawW, drawH);
                    }
                } else {
                    dc.SetPen(*wxTRANSPARENT_PEN);
                    dc.SetBrush(wxBrush(wxColour(146, 40, 40)));
                    dc.DrawRectangle(drawX, drawY, drawW, drawH);
                }
            } else {
                dc.SetPen(*wxTRANSPARENT_PEN);
                dc.SetBrush(wxBrush(wxColour(146, 40, 40)));
                dc.DrawRectangle(drawX, drawY, drawW, drawH);
            }

            if (placement.enemyId == selectedEnemyId_) {
                dc.SetPen(wxPen(wxColour(255, 190, 140), 2));
                dc.SetBrush(*wxTRANSPARENT_BRUSH);
                dc.DrawRectangle(drawX - 1, drawY - 1, drawW + 2, drawH + 2);
            }

            if (definition) {
                dc.SetTextForeground(wxColour(255, 210, 190));
                dc.DrawText(wxString::FromUTF8(definition->name), drawX + 1, drawY + 1);
            }
        }
    }

    void DrawCollisionHitboxOverlay(wxDC& dc, int ox, int oy, int scale) const {
        if (!screen_) {
            return;
        }

        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.SetPen(wxPen(wxColour(64, 220, 255), 2));

        for (int ty = 0; ty < kTilesHigh; ++ty) {
            for (int tx = 0; tx < kTilesWide; ++tx) {
                const size_t tileIndex = static_cast<size_t>(ty * kTilesWide + tx);

                // Match runtime collision behavior: first solid tile from layer 0 upward.
                for (int layer = 0; layer < kTileLayers; ++layer) {
                    const int tileId = screen_->screen.tileLayerIds[static_cast<size_t>(layer)][tileIndex];
                    if (tileId < 0) {
                        continue;
                    }

                    const TileCollection* owningCollection = nullptr;
                    const TileDef* tile = FindTileDefAcrossCollections(tileId, owningCollection);
                    if (!tile || !tile->solid) {
                        continue;
                    }

                    if (!tile->hitboxes.empty()) {
                        for (const TileHitbox& hb : tile->hitboxes) {
                            const int hx = ox + tx * scale + static_cast<int>(std::round(static_cast<float>(hb.x) * scale / kTileSize));
                            const int hy = oy + ty * scale + static_cast<int>(std::round(static_cast<float>(hb.y) * scale / kTileSize));
                            const int hw = std::max(1, static_cast<int>(std::round(static_cast<float>(hb.w) * scale / kTileSize)));
                            const int hh = std::max(1, static_cast<int>(std::round(static_cast<float>(hb.h) * scale / kTileSize)));
                            dc.DrawRectangle(hx, hy, hw, hh);
                        }
                    } else {
                        const int hx = ox + tx * scale + static_cast<int>(std::round(static_cast<float>(tile->hitboxX) * scale / kTileSize));
                        const int hy = oy + ty * scale + static_cast<int>(std::round(static_cast<float>(tile->hitboxY) * scale / kTileSize));
                        const int hw = std::max(1, static_cast<int>(std::round(static_cast<float>(tile->hitboxW) * scale / kTileSize)));
                        const int hh = std::max(1, static_cast<int>(std::round(static_cast<float>(tile->hitboxH) * scale / kTileSize)));
                        dc.DrawRectangle(hx, hy, hw, hh);
                    }

                    break;
                }
            }
        }
    }

private:
    ScreenLoadData* screen_ = nullptr;
    const TileCollection* collection_ = nullptr;
    const std::vector<TileCollection>* allCollections_ = nullptr;
    const std::vector<ItemDefinition>* itemDefinitions_ = nullptr;
    const std::vector<ItemPlacement>* itemPlacements_ = nullptr;
    const std::vector<EnemyDefinition>* enemyDefinitions_ = nullptr;
    const std::vector<EnemyPlacement>* enemyPlacements_ = nullptr;
    const std::vector<WarpDefinition>* warpDefinitions_ = nullptr;
    const std::vector<WarpPlacement>* warpPlacements_ = nullptr;
    const std::unordered_map<int, std::string>* tileOwnerHints_ = nullptr;
    std::unordered_map<std::string, wxBitmap> collectionAtlasCache_;
    int selectedTile_ = 0;
    std::string selectedItemId_;
    std::string selectedEnemyId_;
    std::string selectedWarpId_;
    int selectedWarpEndpointIndex_ = 0;
    int paintLayer_ = 0;
    std::array<bool, kTileLayers> layerVisible_{true, true, true};
    bool showHitboxOverlay_ = false;
    bool dragging_ = false;
    bool itemDragging_ = false;
    bool itemDragMoved_ = false;
    int draggedItemIndex_ = -1;
    std::string draggedItemId_;
    float dragStartPixelX_ = 0.0f;
    float dragStartPixelY_ = 0.0f;
    float dragAnchorOffsetX_ = 0.0f;
    float dragAnchorOffsetY_ = 0.0f;
    CanvasMode mode_ = CanvasMode::PaintTile;
    std::function<void()> onDirty_;
    std::function<void(float, float)> onWarpPlace_;
    std::function<void(const std::string&)> onEdgeClick_;
    std::function<void(float, float)> onItemPlace_;
    std::function<void(int, float, float)> onItemMove_;
    std::function<void(const std::string&)> onItemPick_;
    std::function<void(int)> onItemPlacementPick_;
    std::function<void(float, float)> onEnemyPlace_;
    std::function<void(const std::string&)> onEnemyPick_;
    std::function<void(int)> onTilePick_;
    std::function<void(int)> onLayerChanged_;
};

class WorldGridCanvas final : public wxPanel {
public:
    WorldGridCanvas(wxWindow* parent)
        : wxPanel(parent) {
        SetMinSize(wxSize(240, 240));
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        Bind(wxEVT_PAINT, &WorldGridCanvas::OnPaint, this);
        Bind(wxEVT_LEFT_DOWN, &WorldGridCanvas::OnLeftDown, this);
    }

    void SetMap(MapLoadData* map) {
        map_ = map;
        Refresh();
    }

    void SetSelectedScreen(int x, int y) {
        selectedX_ = x;
        selectedY_ = y;
        Refresh();
    }

    void SetGlobalStart(const std::string& mapId, int x, int y) {
        globalStartMapId_ = mapId;
        globalStartX_ = x;
        globalStartY_ = y;
        Refresh();
    }

    void SetSelectCallback(std::function<void(int, int)> cb) {
        onSelect_ = std::move(cb);
    }

private:
    wxRect CellRect(int x, int y) const {
        if (!map_) {
            return wxRect();
        }

        const wxSize size = GetClientSize();
        const int margin = 12;
        const int cellSize = std::max(24, std::min((size.GetWidth() - margin * 2) / std::max(1, map_->widthScreens), (size.GetHeight() - margin * 2) / std::max(1, map_->heightScreens)));
        const int drawW = cellSize * map_->widthScreens;
        const int drawH = cellSize * map_->heightScreens;
        const int ox = (size.GetWidth() - drawW) / 2;
        const int oy = (size.GetHeight() - drawH) / 2;
        return wxRect(ox + x * cellSize, oy + y * cellSize, cellSize, cellSize);
    }

    void OnLeftDown(wxMouseEvent& event) {
        if (!map_) {
            return;
        }

        for (int y = 0; y < map_->heightScreens; ++y) {
            for (int x = 0; x < map_->widthScreens; ++x) {
                if (CellRect(x, y).Contains(event.GetPosition())) {
                    if (onSelect_) {
                        onSelect_(x, y);
                    }
                    return;
                }
            }
        }
    }

    void OnPaint(wxPaintEvent&) {
        wxAutoBufferedPaintDC dc(this);
        dc.SetBackground(wxBrush(wxColour(17, 21, 27)));
        dc.Clear();

        if (!map_) {
            dc.SetTextForeground(wxColour(180, 186, 198));
            dc.DrawText("No map selected", 16, 16);
            return;
        }

        dc.SetFont(wxFont(8, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Segoe UI"));
        for (int y = 0; y < map_->heightScreens; ++y) {
            for (int x = 0; x < map_->widthScreens; ++x) {
                const wxRect rect = CellRect(x, y);
                const ScreenLoadData* screen = FindScreen(*map_, x, y);

                dc.SetPen(wxPen(wxColour(64, 74, 88), 1));
                dc.SetBrush(wxBrush(screen ? wxColour(51, 83, 56) : wxColour(30, 34, 40)));
                dc.DrawRectangle(rect);

                if (screen && !screen->dungeonId.empty()) {
                    dc.SetBrush(wxBrush(wxColour(74, 54, 35)));
                    dc.SetPen(*wxTRANSPARENT_PEN);
                    dc.DrawRectangle(rect.x + 4, rect.y + 4, 10, 10);
                }

                if (screen && !screen->transitions.empty()) {
                    dc.SetBrush(wxBrush(wxColour(102, 160, 225)));
                    dc.SetPen(*wxTRANSPARENT_PEN);
                    dc.DrawCircle(rect.x + rect.width - 12, rect.y + 10, 4);
                }

                if (screen && !screen->warpPlacements.empty()) {
                    dc.SetBrush(wxBrush(wxColour(228, 180, 64)));
                    dc.SetPen(*wxTRANSPARENT_PEN);
                    dc.DrawCircle(rect.x + rect.width - 12, rect.y + rect.height - 12, 4);
                }

                if (map_->id == globalStartMapId_ && globalStartX_ == x && globalStartY_ == y) {
                    dc.SetPen(wxPen(wxColour(86, 220, 206), 2));
                    dc.SetBrush(*wxTRANSPARENT_BRUSH);
                    dc.DrawRectangle(rect.x + 2, rect.y + 2, rect.width - 4, rect.height - 4);
                }

                if (selectedX_ == x && selectedY_ == y) {
                    dc.SetPen(wxPen(wxColour(255, 214, 90), 3));
                    dc.SetBrush(*wxTRANSPARENT_BRUSH);
                    dc.DrawRectangle(rect.x + 1, rect.y + 1, rect.width - 2, rect.height - 2);
                }

                dc.SetTextForeground(wxColour(226, 231, 237));
                dc.DrawText(wxString::Format("%d,%d", x, y), rect.x + 6, rect.y + 6);
            }
        }
    }

private:
    MapLoadData* map_ = nullptr;
    std::string globalStartMapId_;
    int globalStartX_ = 0;
    int globalStartY_ = 0;
    int selectedX_ = 0;
    int selectedY_ = 0;
    std::function<void(int, int)> onSelect_;
};

class TileHitboxEditor final : public wxDialog {
public:
    TileHitboxEditor(wxWindow* parent, TileDef& tile, const wxBitmap& atlas, const TileCollection& collection, bool allowEmpty = false)
        : wxDialog(parent, wxID_ANY, "Edit Tile Hitbox", wxDefaultPosition, wxSize(520, 560),
                   wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
          tile_(tile),
          atlas_(atlas),
          collection_(collection),
          allowEmpty_(allowEmpty) {
        if (!tile_.hitboxes.empty()) {
            hitboxes_ = tile_.hitboxes;
        } else {
            hitboxes_.push_back(TileHitbox{tile_.hitboxX, tile_.hitboxY, tile_.hitboxW, tile_.hitboxH});
        }
        if (hitboxes_.empty() && !allowEmpty_) {
            hitboxes_.push_back(TileHitbox{});
        }

        SetBackgroundColour(wxColour(31, 36, 45));
        SetForegroundColour(wxColour(236, 240, 246));

        const int tileW = std::max(1, collection_.tileWidth);
        const int tileH = std::max(1, collection_.tileHeight);

        auto* rootSizer = new wxBoxSizer(wxVERTICAL);
        auto* scrolled = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
        scrolled->SetScrollRate(0, 10);
        scrolled->SetBackgroundColour(wxColour(31, 36, 45));
        scrolled->SetForegroundColour(wxColour(236, 240, 246));
        wxWindow* contentParent = scrolled;

        wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

        canvasPanel_ = new wxPanel(contentParent, wxID_ANY, wxDefaultPosition, wxSize(320, 240));
        canvasPanel_->SetBackgroundColour(wxColour(19, 24, 31));
        canvasPanel_->Bind(wxEVT_PAINT, [this](wxPaintEvent&) { OnCanvasPaint(); });
        canvasPanel_->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) { OnCanvasLeftDown(e); });
        canvasPanel_->Bind(wxEVT_RIGHT_DOWN, [this](wxMouseEvent& e) {
            const int idx = HitboxAtPoint(e.GetPosition());
            if (idx >= 0) {
                SelectHitbox(idx);
            }
        });
        canvasPanel_->Bind(wxEVT_MOTION, [this](wxMouseEvent& e) { OnCanvasMotion(e); });
        canvasPanel_->Bind(wxEVT_LEFT_UP, [this](wxMouseEvent&) { OnCanvasLeftUp(); });

        mainSizer->Add(canvasPanel_, 1, wxALL | wxEXPAND, 10);

        auto* lowerSizer = new wxBoxSizer(wxHORIZONTAL);

        auto* listSizer = new wxBoxSizer(wxVERTICAL);
        listSizer->Add(new wxStaticText(contentParent, wxID_ANY, "Hitboxes"), 0, wxBOTTOM, 4);
        hitboxList_ = new wxListBox(contentParent, wxID_ANY, wxDefaultPosition, wxSize(170, 200));
        hitboxList_->SetBackgroundColour(wxColour(24, 29, 36));
        hitboxList_->SetForegroundColour(wxColour(236, 240, 246));
        listSizer->Add(hitboxList_, 1, wxEXPAND | wxBOTTOM, 6);

        auto* listButtons = new wxBoxSizer(wxHORIZONTAL);
        auto* addBtn = new wxButton(contentParent, wxID_ANY, "Add");
        auto* removeBtn = new wxButton(contentParent, wxID_ANY, "Remove");
        listButtons->Add(addBtn, 1, wxRIGHT, 6);
        listButtons->Add(removeBtn, 1);
        listSizer->Add(listButtons, 0, wxEXPAND);
        lowerSizer->Add(listSizer, 0, wxRIGHT, 10);

        auto* inputSizer = new wxBoxSizer(wxVERTICAL);
        inputSizer->Add(new wxStaticText(contentParent, wxID_ANY, "Active Hitbox"), 0, wxBOTTOM, 4);
        wxGridSizer* gridSizer = new wxGridSizer(2, 4, 5, 5);

        gridSizer->Add(new wxStaticText(contentParent, wxID_ANY, "X:"), 0, wxALIGN_CENTER_VERTICAL);
        spinX_ = new wxSpinCtrl(contentParent, wxID_ANY, "0", wxDefaultPosition, wxSize(64, -1), 0, -tileW, tileW * 2);
        gridSizer->Add(spinX_, 0);

        gridSizer->Add(new wxStaticText(contentParent, wxID_ANY, "Y:"), 0, wxALIGN_CENTER_VERTICAL);
        spinY_ = new wxSpinCtrl(contentParent, wxID_ANY, "0", wxDefaultPosition, wxSize(64, -1), 0, -tileH, tileH * 2);
        gridSizer->Add(spinY_, 0);

        gridSizer->Add(new wxStaticText(contentParent, wxID_ANY, "Width:"), 0, wxALIGN_CENTER_VERTICAL);
        spinW_ = new wxSpinCtrl(contentParent, wxID_ANY, std::to_string(tileW), wxDefaultPosition, wxSize(64, -1), 1, 1, tileW);
        gridSizer->Add(spinW_, 0);

        gridSizer->Add(new wxStaticText(contentParent, wxID_ANY, "Height:"), 0, wxALIGN_CENTER_VERTICAL);
        spinH_ = new wxSpinCtrl(contentParent, wxID_ANY, std::to_string(tileH), wxDefaultPosition, wxSize(64, -1), 1, 1, tileH);
        gridSizer->Add(spinH_, 0);

        auto styleSpinField = [](wxSpinCtrl* ctrl) {
            if (!ctrl) {
                return;
            }
            ctrl->SetBackgroundColour(wxColour(0, 0, 0));
            ctrl->SetForegroundColour(wxColour(236, 240, 246));
        };
        styleSpinField(spinX_);
        styleSpinField(spinY_);
        styleSpinField(spinW_);
        styleSpinField(spinH_);

        inputSizer->Add(gridSizer, 0, wxEXPAND | wxBOTTOM, 8);
        inputSizer->Add(new wxStaticText(contentParent, wxID_ANY, "Click or right-click a hitbox on the preview to select it."), 0, wxBOTTOM, 8);
        lowerSizer->Add(inputSizer, 1, wxEXPAND);
        mainSizer->Add(lowerSizer, 0, wxALL | wxEXPAND, 10);

        scrolled->SetSizer(mainSizer);
        scrolled->FitInside();
        rootSizer->Add(scrolled, 1, wxEXPAND);

        wxBoxSizer* buttonSizer = new wxBoxSizer(wxHORIZONTAL);
        wxButton* okBtn = new wxButton(this, wxID_OK, "OK");
        wxButton* cancelBtn = new wxButton(this, wxID_CANCEL, "Cancel");
        buttonSizer->Add(okBtn, 0, wxRIGHT, 5);
        buttonSizer->Add(cancelBtn, 0);
        rootSizer->Add(buttonSizer, 0, wxALL | wxALIGN_RIGHT, 10);

        SetSizer(rootSizer);
        SetMinSize(wxSize(500, 520));

        RebuildHitboxList();
        SelectHitbox(hitboxes_.empty() ? -1 : 0);

        addBtn->Bind(wxEVT_BUTTON, [this, tileW, tileH](wxCommandEvent&) {
            hitboxes_.push_back(TileHitbox{0, 0, tileW, tileH});
            RebuildHitboxList();
            SelectHitbox(static_cast<int>(hitboxes_.size()) - 1);
            RefreshCanvas();
        });

        removeBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            if ((!allowEmpty_ && hitboxes_.size() <= 1) || selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(hitboxes_.size())) {
                return;
            }
            hitboxes_.erase(hitboxes_.begin() + selectedIndex_);
            RebuildHitboxList();
            SelectHitbox(hitboxes_.empty() ? -1 : std::min(selectedIndex_, static_cast<int>(hitboxes_.size()) - 1));
            RefreshCanvas();
        });

        hitboxList_->Bind(wxEVT_LISTBOX, [this](wxCommandEvent&) {
            SelectHitbox(hitboxList_->GetSelection());
        });

        auto onSpin = [this](wxCommandEvent&) { UpdateFromSpins(); };
        spinX_->Bind(wxEVT_SPINCTRL, onSpin);
        spinY_->Bind(wxEVT_SPINCTRL, onSpin);
        spinW_->Bind(wxEVT_SPINCTRL, onSpin);
        spinH_->Bind(wxEVT_SPINCTRL, onSpin);

        Bind(wxEVT_BUTTON, [this](wxCommandEvent& e) {
            if (e.GetId() == wxID_OK) {
                CommitToTile();
            }
            EndModal(e.GetId());
        });

        // Window close behaves like cancel: discard edits and close.
        Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& e) {
            EndModal(wxID_CANCEL);
            e.Skip(false);
        });
    }

private:
    static wxColour HitboxColor(int index, int activeIndex) {
        if (index == activeIndex) {
            return wxColour(255, 214, 90, 255);
        }
        const std::array<wxColour, 4> palette = {
            wxColour(102, 200, 255, 170),
            wxColour(118, 230, 144, 170),
            wxColour(255, 165, 102, 170),
            wxColour(192, 140, 255, 170)
        };
        return palette[static_cast<size_t>(index % static_cast<int>(palette.size()))];
    }

    TileHitbox* SelectedHitbox() {
        if (selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(hitboxes_.size())) {
            return nullptr;
        }
        return &hitboxes_[static_cast<size_t>(selectedIndex_)];
    }

    const TileHitbox* SelectedHitbox() const {
        if (selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(hitboxes_.size())) {
            return nullptr;
        }
        return &hitboxes_[static_cast<size_t>(selectedIndex_)];
    }

    void CommitToTile() {
        tile_.hitboxes = hitboxes_;
        if (tile_.hitboxes.empty() && !allowEmpty_) {
            tile_.hitboxes.push_back(TileHitbox{});
        }
        if (!tile_.hitboxes.empty()) {
            tile_.hitboxX = tile_.hitboxes.front().x;
            tile_.hitboxY = tile_.hitboxes.front().y;
            tile_.hitboxW = tile_.hitboxes.front().w;
            tile_.hitboxH = tile_.hitboxes.front().h;
        } else {
            tile_.hitboxX = 0;
            tile_.hitboxY = 0;
            tile_.hitboxW = 0;
            tile_.hitboxH = 0;
        }
    }

    void RebuildHitboxList() {
        if (!hitboxList_) {
            return;
        }
        hitboxList_->Clear();
        for (size_t i = 0; i < hitboxes_.size(); ++i) {
            const TileHitbox& hitbox = hitboxes_[i];
            hitboxList_->Append(wxString::Format("#%d  x=%d y=%d w=%d h=%d", static_cast<int>(i + 1), hitbox.x, hitbox.y, hitbox.w, hitbox.h));
        }
    }

    void SelectHitbox(int index) {
        if (hitboxes_.empty()) {
            selectedIndex_ = -1;
            if (hitboxList_) {
                hitboxList_->SetSelection(wxNOT_FOUND);
            }
            SyncControlsFromHitbox();
            RefreshCanvas();
            return;
        }
        selectedIndex_ = std::clamp(index, 0, static_cast<int>(hitboxes_.size()) - 1);
        if (hitboxList_) {
            hitboxList_->SetSelection(selectedIndex_);
        }
        SyncControlsFromHitbox();
        RefreshCanvas();
    }

    void SyncControlsFromHitbox() {
        const TileHitbox* hitbox = SelectedHitbox();
        if (!hitbox) {
            if (spinX_) {
                spinX_->Enable(false);
                spinY_->Enable(false);
                spinW_->Enable(false);
                spinH_->Enable(false);
            }
            return;
        }
        spinX_->Enable(true);
        spinY_->Enable(true);
        spinW_->Enable(true);
        spinH_->Enable(true);
        spinX_->SetValue(hitbox->x);
        spinY_->SetValue(hitbox->y);
        spinW_->SetValue(hitbox->w);
        spinH_->SetValue(hitbox->h);
    }

    void UpdateFromSpins() {
        TileHitbox* hitbox = SelectedHitbox();
        if (!hitbox) {
            return;
        }
        hitbox->x = spinX_->GetValue();
        hitbox->y = spinY_->GetValue();
        hitbox->w = spinW_->GetValue();
        hitbox->h = spinH_->GetValue();
        if (hitboxList_ && selectedIndex_ >= 0 && selectedIndex_ < static_cast<int>(hitboxes_.size())) {
            hitboxList_->SetString(static_cast<size_t>(selectedIndex_), wxString::Format("#%d  x=%d y=%d w=%d h=%d", selectedIndex_ + 1, hitbox->x, hitbox->y, hitbox->w, hitbox->h));
        }
        RefreshCanvas();
    }

    int HitboxAtPoint(const wxPoint& pt) const {
        const int tileW = std::max(1, collection_.tileWidth);
        const int tileH = std::max(1, collection_.tileHeight);
        const wxSize client = canvasPanel_ ? canvasPanel_->GetClientSize() : wxSize(320, 240);
        const float scaleX = static_cast<float>(std::max(80, client.GetWidth() - 80)) / static_cast<float>(tileW);
        const float scaleY = static_cast<float>(std::max(80, client.GetHeight() - 80)) / static_cast<float>(tileH);
        const float tileScale = std::max(0.25f, std::min(scaleX, scaleY));
        const int tileDisplayW = std::max(1, static_cast<int>(std::round(static_cast<float>(tileW) * tileScale)));
        const int tileDisplayH = std::max(1, static_cast<int>(std::round(static_cast<float>(tileH) * tileScale)));
        const int baseX = std::max(8, (client.GetWidth() - tileDisplayW) / 2);
        const int baseY = std::max(8, (client.GetHeight() - tileDisplayH) / 2);
        if (pt.x < baseX || pt.y < baseY || pt.x >= baseX + tileDisplayW || pt.y >= baseY + tileDisplayH) {
            return -1;
        }
        const int x = std::clamp(static_cast<int>(std::floor(static_cast<float>(pt.x - baseX) / tileScale)), 0, tileW - 1);
        const int y = std::clamp(static_cast<int>(std::floor(static_cast<float>(pt.y - baseY) / tileScale)), 0, tileH - 1);
        for (int i = static_cast<int>(hitboxes_.size()) - 1; i >= 0; --i) {
            const TileHitbox& hitbox = hitboxes_[static_cast<size_t>(i)];
            if (x >= hitbox.x && x < hitbox.x + hitbox.w && y >= hitbox.y && y < hitbox.y + hitbox.h) {
                return i;
            }
        }
        return -1;
    }

    void RefreshCanvas() {
        if (canvasPanel_) {
            canvasPanel_->Refresh();
        }
    }

    void OnCanvasPaint() {
        if (!canvasPanel_) {
            return;
        }
        wxAutoBufferedPaintDC dc(canvasPanel_);
        dc.SetBackground(wxBrush(wxColour(19, 24, 31)));
        dc.Clear();

        const int tileW = std::max(1, collection_.tileWidth);
        const int tileH = std::max(1, collection_.tileHeight);
        const wxSize client = canvasPanel_->GetClientSize();
        const float scaleX = static_cast<float>(std::max(80, client.GetWidth() - 80)) / static_cast<float>(tileW);
        const float scaleY = static_cast<float>(std::max(80, client.GetHeight() - 80)) / static_cast<float>(tileH);
        const float tileScale = std::max(0.25f, std::min(scaleX, scaleY));
        const int tileDisplayW = std::max(1, static_cast<int>(std::round(static_cast<float>(tileW) * tileScale)));
        const int tileDisplayH = std::max(1, static_cast<int>(std::round(static_cast<float>(tileH) * tileScale)));
        const int baseX = std::max(8, (client.GetWidth() - tileDisplayW) / 2);
        const int baseY = std::max(8, (client.GetHeight() - tileDisplayH) / 2);

        if (atlas_.IsOk() && collection_.tileWidth > 0 && collection_.tileHeight > 0) {
            wxMemoryDC atlasDc;
            wxBitmap atlasCopy(atlas_);  // Make a non-const copy for SelectObject
            atlasDc.SelectObject(atlasCopy);
            dc.StretchBlit(
                baseX,
                baseY,
                tileDisplayW,
                tileDisplayH,
                &atlasDc,
                tile_.sourceX,
                tile_.sourceY,
                collection_.tileWidth,
                collection_.tileHeight
            );
            atlasDc.SelectObject(wxNullBitmap);
        } else {
            dc.SetBrush(wxBrush(TileColorFromId(tile_.id)));
            dc.DrawRectangle(baseX, baseY, tileDisplayW, tileDisplayH);
        }

        for (size_t i = 0; i < hitboxes_.size(); ++i) {
            const TileHitbox& hitbox = hitboxes_[i];
            const int hx = baseX + static_cast<int>(std::round(static_cast<float>(hitbox.x) * tileScale));
            const int hy = baseY + static_cast<int>(std::round(static_cast<float>(hitbox.y) * tileScale));
            const int hw = std::max(1, static_cast<int>(std::round(static_cast<float>(hitbox.w) * tileScale)));
            const int hh = std::max(1, static_cast<int>(std::round(static_cast<float>(hitbox.h) * tileScale)));
            const wxColour color = HitboxColor(static_cast<int>(i), selectedIndex_);

            // Draw only the border with transparent fill
            dc.SetPen(wxPen(color, i == static_cast<size_t>(selectedIndex_) ? 3 : 2));
            dc.SetBrush(*wxTRANSPARENT_BRUSH);
            dc.DrawRectangle(hx, hy, hw, hh);

            // Draw corner handles
            dc.SetBrush(wxBrush(color));
            dc.SetPen(*wxTRANSPARENT_PEN);
            const int handleSize = 4;
            dc.DrawRectangle(hx - handleSize / 2, hy - handleSize / 2, handleSize, handleSize);
            dc.DrawRectangle(hx + hw - handleSize / 2, hy - handleSize / 2, handleSize, handleSize);
            dc.DrawRectangle(hx - handleSize / 2, hy + hh - handleSize / 2, handleSize, handleSize);
            dc.DrawRectangle(hx + hw - handleSize / 2, hy + hh - handleSize / 2, handleSize, handleSize);
        }
    }

    void OnCanvasLeftDown(wxMouseEvent& event) {
        const int hitboxIndex = HitboxAtPoint(event.GetPosition());
        if (hitboxIndex >= 0) {
            SelectHitbox(hitboxIndex);
            dragging_ = true;
            dragStartX_ = event.GetX();
            dragStartY_ = event.GetY();
        }
    }

    void OnCanvasMotion(wxMouseEvent& event) {
        if (!dragging_) {
            return;
        }

        const int tileW = std::max(1, collection_.tileWidth);
        const int tileH = std::max(1, collection_.tileHeight);

        const wxSize client = canvasPanel_ ? canvasPanel_->GetClientSize() : wxSize(320, 240);
        const float scaleX = static_cast<float>(std::max(16, client.GetWidth() - 16)) / static_cast<float>(tileW);
        const float scaleY = static_cast<float>(std::max(16, client.GetHeight() - 16)) / static_cast<float>(tileH);
        const float tileScale = std::max(0.25f, std::min(scaleX, scaleY));
        const int tileDisplayW = std::max(1, static_cast<int>(std::round(static_cast<float>(tileW) * tileScale)));
        const int tileDisplayH = std::max(1, static_cast<int>(std::round(static_cast<float>(tileH) * tileScale)));
        const int baseX = std::max(8, (client.GetWidth() - tileDisplayW) / 2);
        const int baseY = std::max(8, (client.GetHeight() - tileDisplayH) / 2);

        int newX = static_cast<int>(std::floor(static_cast<float>(event.GetX() - baseX) / tileScale));
        int newY = static_cast<int>(std::floor(static_cast<float>(event.GetY() - baseY) / tileScale));
        int oldX = static_cast<int>(std::floor(static_cast<float>(dragStartX_ - baseX) / tileScale));
        int oldY = static_cast<int>(std::floor(static_cast<float>(dragStartY_ - baseY) / tileScale));

        int dx = newX - oldX;
        int dy = newY - oldY;

        TileHitbox* hitbox = SelectedHitbox();
        if (!hitbox) {
            return;
        }

        hitbox->x = std::clamp(hitbox->x + dx, 0, std::max(0, tileW - hitbox->w));
        hitbox->y = std::clamp(hitbox->y + dy, 0, std::max(0, tileH - hitbox->h));

        dragStartX_ = event.GetX();
        dragStartY_ = event.GetY();

        SyncControlsFromHitbox();
        if (hitboxList_ && selectedIndex_ >= 0 && selectedIndex_ < static_cast<int>(hitboxes_.size())) {
            hitboxList_->SetString(static_cast<size_t>(selectedIndex_), wxString::Format("#%d  x=%d y=%d w=%d h=%d", selectedIndex_ + 1, hitbox->x, hitbox->y, hitbox->w, hitbox->h));
        }
        RefreshCanvas();
    }

    void OnCanvasLeftUp() {
        dragging_ = false;
    }

private:
    TileDef& tile_;
    const wxBitmap& atlas_;
    const TileCollection& collection_;
    bool allowEmpty_ = false;
    std::vector<TileHitbox> hitboxes_;
    wxPanel* canvasPanel_ = nullptr;
    wxListBox* hitboxList_ = nullptr;
    wxSpinCtrl* spinX_ = nullptr;
    wxSpinCtrl* spinY_ = nullptr;
    wxSpinCtrl* spinW_ = nullptr;
    wxSpinCtrl* spinH_ = nullptr;
    bool dragging_ = false;
    int dragStartX_ = 0;
    int dragStartY_ = 0;
    int selectedIndex_ = 0;
};

class TilePalettePanel final : public wxScrolledWindow {
public:
    TilePalettePanel(wxWindow* parent)
        : wxScrolledWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_SIMPLE | wxVSCROLL | wxHSCROLL) {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetScrollRate(12, 12);
        SetMinSize(wxSize(620, 220));
        Bind(wxEVT_PAINT, &TilePalettePanel::OnPaint, this);
        Bind(wxEVT_LEFT_DOWN, &TilePalettePanel::OnLeftDown, this);
        Bind(wxEVT_LEFT_DCLICK, &TilePalettePanel::OnLeftDClick, this);
        Bind(wxEVT_RIGHT_DOWN, &TilePalettePanel::OnRightDown, this);
        Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
            RefreshVirtualSize();
            Refresh();
            event.Skip();
        });
    }

    void SetSelectionChangedCallback(std::function<void(int)> cb) {
        onSelect_ = std::move(cb);
    }

    void SetEditTileCallback(std::function<void(TileDef&)> cb) {
        onEditTile_ = std::move(cb);
    }

    void SetMoveTileCallback(std::function<void(int, const std::string&)> cb) {
        onMoveTile_ = std::move(cb);
    }

    void SetMoveTargets(const std::vector<std::pair<std::string, std::string>>& targets) {
        moveTargets_ = targets;
    }

    void SetCollection(const TileCollection* collection) {
        collection_ = collection;
        if (collection_ && !collection_->tiles.empty()) {
            bool found = false;
            for (const TileDef& tile : collection_->tiles) {
                if (tile.id == selectedTileId_) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                selectedTileId_ = collection_->tiles.front().id;
            }
        } else {
            selectedTileId_ = 0;
        }
        atlas_ = wxBitmap();

        if (collection_ && !collection_->imagePath.empty()) {
            wxFileName directPath(wxString::FromUTF8(collection_->imagePath));
            if (directPath.FileExists()) {
                atlas_.LoadFile(directPath.GetFullPath(), wxBITMAP_TYPE_PNG);
            } else {
                wxFileName relPath(wxString::FromUTF8(collection_->imagePath));
                relPath.MakeAbsolute(wxGetCwd());
                if (relPath.FileExists()) {
                    atlas_.LoadFile(relPath.GetFullPath(), wxBITMAP_TYPE_PNG);
                }
            }
        }

        RefreshVirtualSize();
        Refresh();
        if (onSelect_) {
            onSelect_(selectedTileId_);
        }
    }

    void SetSelectedTileId(int tileId, bool ensureVisible = false) {
        selectedTileId_ = tileId;
        if (ensureVisible) {
            ScrollTileIntoViewById(tileId);
        }
        Refresh();
    }

    int SelectedTileId() const {
        return selectedTileId_;
    }

    void SetPreferredColumns(int columns) {
        preferredColumns_ = std::max(0, columns);
        RefreshVirtualSize();
        Refresh();
    }

    int PreferredColumns() const {
        return preferredColumns_;
    }

    int ComputedAutoColumns() const {
        return ComputeAutoPaletteColumns();
    }

private:
    int ComputeAutoPaletteColumns() const {
        // Calculate how many columns fit in the available width.
        const int cell = 40;  // Each cell is 40px wide
        const int leftPadding = 4;  // Left margin
        const int scrollbarWidth = 17;  // Approximate width of vertical scrollbar on Windows
        
        const wxSize clientSize = GetClientSize();
        if (clientSize.GetWidth() <= leftPadding) {
            return 1;
        }
        
        int availableWidth = clientSize.GetWidth() - leftPadding;
        
        // Check if scrollbar will be visible
        const int count = collection_ ? static_cast<int>(collection_->tiles.size()) : 0;
        if (count <= 0) {
            return 1;
        }
        
        // Try to see if scrollbar would be visible with a given column count
        auto wouldHaveScrollbar = [this, cell, count](int cols) -> bool {
            if (cols <= 0) return false;
            const int rows = (count + cols - 1) / cols;
            const int virtualHeight = rows * cell + 8;
            return virtualHeight > GetClientSize().GetHeight();
        };
        
        // Start with max columns that fit
        int columns = availableWidth / cell;
        columns = std::max(1, columns);
        
        // If scrollbar would appear, reduce to account for scrollbar width
        if (wouldHaveScrollbar(columns)) {
            availableWidth -= scrollbarWidth;
            columns = availableWidth / cell;
            columns = std::max(1, columns);
        }
        
        return columns;
    }

    int GetPaletteColumns() const {
        if (preferredColumns_ > 0) {
            return preferredColumns_;
        }
        return ComputeAutoPaletteColumns();
    }

    void RefreshVirtualSize() {
        const int columns = GetPaletteColumns();
        const int cell = 40;
        const int count = collection_ ? static_cast<int>(collection_->tiles.size()) : 0;
        const int rows = std::max(1, (count + columns - 1) / columns);
        SetVirtualSize(columns * cell + 8, rows * cell + 8);
    }

    wxRect TileRect(int index) const {
        const int columns = GetPaletteColumns();
        const int cell = 40;
        const int col = index % columns;
        const int row = index / columns;
        return wxRect(4 + col * cell, 4 + row * cell, 36, 36);
    }

    void ScrollTileIntoViewById(int tileId) {
        if (!collection_) {
            return;
        }

        int index = -1;
        for (size_t i = 0; i < collection_->tiles.size(); ++i) {
            if (collection_->tiles[i].id == tileId) {
                index = static_cast<int>(i);
                break;
            }
        }
        if (index < 0) {
            return;
        }

        const wxRect rect = TileRect(index);

        int xUnit = 0;
        int yUnit = 0;
        GetScrollPixelsPerUnit(&xUnit, &yUnit);
        xUnit = std::max(1, xUnit);
        yUnit = std::max(1, yUnit);

        int viewXUnits = 0;
        int viewYUnits = 0;
        GetViewStart(&viewXUnits, &viewYUnits);

        const int viewLeft = viewXUnits * xUnit;
        const int viewRight = viewLeft + GetClientSize().GetWidth();
        const int viewTop = viewYUnits * yUnit;
        const int viewBottom = viewTop + GetClientSize().GetHeight();

        int targetLeft = viewLeft;
        if (rect.GetLeft() < viewLeft) {
            targetLeft = rect.GetLeft();
        } else if (rect.GetRight() > viewRight) {
            targetLeft = rect.GetRight() - GetClientSize().GetWidth();
        }

        int targetTop = viewTop;
        if (rect.GetTop() < viewTop) {
            targetTop = rect.GetTop();
        } else if (rect.GetBottom() > viewBottom) {
            targetTop = rect.GetBottom() - GetClientSize().GetHeight();
        }

        const int maxLeft = std::max(0, GetVirtualSize().GetWidth() - GetClientSize().GetWidth());
        targetLeft = std::clamp(targetLeft, 0, maxLeft);
        const int maxTop = std::max(0, GetVirtualSize().GetHeight() - GetClientSize().GetHeight());
        targetTop = std::clamp(targetTop, 0, maxTop);

        if (targetLeft != viewLeft || targetTop != viewTop) {
            Scroll(targetLeft / xUnit, targetTop / yUnit);
        }
    }

    void DrawTilePreview(wxDC& dc, const TileDef& tile, const wxRect& rect) {
        if (atlas_.IsOk() && collection_ && collection_->tileWidth > 0 && collection_->tileHeight > 0) {
            wxMemoryDC atlasDc;
            atlasDc.SelectObject(atlas_);
            dc.StretchBlit(
                rect.x + 2,
                rect.y + 2,
                32,
                32,
                &atlasDc,
                tile.sourceX,
                tile.sourceY,
                collection_->tileWidth,
                collection_->tileHeight
            );
            atlasDc.SelectObject(wxNullBitmap);
        } else {
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.SetBrush(wxBrush(TileColorFromId(tile.id)));
            dc.DrawRectangle(rect.x + 2, rect.y + 2, 32, 32);
        }
    }

    void OnPaint(wxPaintEvent&) {
        wxAutoBufferedPaintDC dc(this);
        PrepareDC(dc);
        dc.SetBackground(wxBrush(wxColour(19, 24, 31)));
        dc.Clear();

        if (!collection_) {
            dc.SetTextForeground(wxColour(190, 196, 208));
            dc.DrawText("No tile collection", 8, 8);
            return;
        }

        for (size_t i = 0; i < collection_->tiles.size(); ++i) {
            const TileDef& tile = collection_->tiles[i];
            const wxRect rect = TileRect(static_cast<int>(i));
            dc.SetPen(wxPen(wxColour(72, 84, 102), 1));
            dc.SetBrush(wxBrush(wxColour(31, 36, 45)));
            dc.DrawRectangle(rect);
            DrawTilePreview(dc, tile, rect);

            if (tile.solid) {
                dc.SetPen(*wxTRANSPARENT_PEN);
                dc.SetBrush(wxBrush(wxColour(220, 78, 78)));
                dc.DrawCircle(rect.x + rect.width - 6, rect.y + 6, 3);
            }

            if (tile.id == selectedTileId_) {
                dc.SetPen(wxPen(wxColour(255, 215, 88), 2));
                dc.SetBrush(*wxTRANSPARENT_BRUSH);
                dc.DrawRectangle(rect.x, rect.y, rect.width, rect.height);
            }
        }
    }

    void OnLeftDown(wxMouseEvent& event) {
        if (!collection_) {
            return;
        }

        const wxPoint p = CalcUnscrolledPosition(event.GetPosition());
        for (size_t i = 0; i < collection_->tiles.size(); ++i) {
            const wxRect rect = TileRect(static_cast<int>(i));
            if (rect.Contains(p)) {
                selectedTileId_ = collection_->tiles[i].id;
                Refresh();
                if (onSelect_) {
                    onSelect_(selectedTileId_);
                }
                return;
            }
        }
    }

    void OnLeftDClick(wxMouseEvent& event) {
        if (!collection_) {
            return;
        }

        const wxPoint p = CalcUnscrolledPosition(event.GetPosition());
        for (size_t i = 0; i < collection_->tiles.size(); ++i) {
            const wxRect rect = TileRect(static_cast<int>(i));
            if (rect.Contains(p)) {
                TileDef& tile = const_cast<TileDef&>(collection_->tiles[i]);
                if (tile.solid && onEditTile_) {
                    onEditTile_(tile);
                    Refresh();
                }
                return;
            }
        }
    }

    void OnRightDown(wxMouseEvent& event) {
        if (!collection_ || !onMoveTile_) {
            return;
        }

        const wxPoint p = CalcUnscrolledPosition(event.GetPosition());
        int clickedTileId = -1;
        for (size_t i = 0; i < collection_->tiles.size(); ++i) {
            const wxRect rect = TileRect(static_cast<int>(i));
            if (!rect.Contains(p)) {
                continue;
            }
            clickedTileId = collection_->tiles[i].id;
            selectedTileId_ = clickedTileId;
            Refresh();
            if (onSelect_) {
                onSelect_(selectedTileId_);
            }
            break;
        }

        if (clickedTileId < 0) {
            return;
        }

        wxMenu menu;
        auto* moveMenu = new wxMenu();
        bool hasTarget = false;
        for (const auto& target : moveTargets_) {
            if (target.first.empty()) {
                continue;
            }
            if (collection_ && target.first == collection_->id) {
                continue;
            }
            const int moveId = wxWindow::NewControlId();
            moveMenu->Append(moveId, wxString::FromUTF8(target.second));
            moveMenu->Bind(wxEVT_MENU, [this, clickedTileId, targetId = target.first](wxCommandEvent&) {
                if (onMoveTile_) {
                    onMoveTile_(clickedTileId, targetId);
                }
            }, moveId);
            hasTarget = true;
        }

        if (!hasTarget) {
            const int disabledId = wxWindow::NewControlId();
            moveMenu->Append(disabledId, "(No other collections)");
            moveMenu->Enable(disabledId, false);
        }

        menu.AppendSubMenu(moveMenu, "Move");
        PopupMenu(&menu, event.GetPosition());
    }

private:
    const TileCollection* collection_ = nullptr;
    wxBitmap atlas_;
    int selectedTileId_ = 0;
    int preferredColumns_ = 0;
    std::function<void(int)> onSelect_;
    std::function<void(TileDef&)> onEditTile_;
    std::function<void(int, const std::string&)> onMoveTile_;
    std::vector<std::pair<std::string, std::string>> moveTargets_;
};

class TilePickerDialog final : public wxDialog {
public:
    TilePickerDialog(wxWindow* parent, const std::vector<TileCollection>& collections, int initialTileId)
        : wxDialog(parent, wxID_ANY, "Pick Tile", wxDefaultPosition, wxSize(760, 620), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
          collections_(collections) {
        auto* root = new wxBoxSizer(wxVERTICAL);

        auto* topRow = new wxBoxSizer(wxHORIZONTAL);
        topRow->Add(new wxStaticText(this, wxID_ANY, "Collection"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
        collectionChoice_ = new wxChoice(this, wxID_ANY);
        topRow->Add(collectionChoice_, 1, wxEXPAND);
        root->Add(topRow, 0, wxEXPAND | wxALL, 8);

        palette_ = new TilePalettePanel(this);
        palette_->SetMinSize(wxSize(680, 460));
        root->Add(palette_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

        tileInfo_ = new wxStaticText(this, wxID_ANY, "No tile selected");
        root->Add(tileInfo_, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);

        auto* buttons = new wxStdDialogButtonSizer();
        okButton_ = new wxButton(this, wxID_OK, "OK");
        buttons->AddButton(okButton_);
        buttons->AddButton(new wxButton(this, wxID_CANCEL, "Cancel"));
        buttons->Realize();
        root->Add(buttons, 0, wxALIGN_RIGHT | wxALL, 8);

        SetSizer(root);

        for (const TileCollection& collection : collections_) {
            collectionChoice_->Append(wxString::FromUTF8(collection.name.empty() ? collection.id : collection.name));
        }

        if (!collections_.empty()) {
            int initialCollectionIndex = 0;
            for (size_t i = 0; i < collections_.size(); ++i) {
                if (FindTileDef(collections_[i], initialTileId)) {
                    initialCollectionIndex = static_cast<int>(i);
                    break;
                }
            }

            collectionChoice_->SetSelection(initialCollectionIndex);
            selectedCollectionIndex_ = initialCollectionIndex;
            selectedTileId_ = initialTileId;
            palette_->SetCollection(&collections_[static_cast<size_t>(initialCollectionIndex)]);

            if (!FindTileDef(collections_[static_cast<size_t>(initialCollectionIndex)], selectedTileId_)
                && !collections_[static_cast<size_t>(initialCollectionIndex)].tiles.empty()) {
                selectedTileId_ = collections_[static_cast<size_t>(initialCollectionIndex)].tiles.front().id;
            }
            palette_->SetSelectedTileId(selectedTileId_, true);
        }

        palette_->SetSelectionChangedCallback([this](int tileId) {
            selectedTileId_ = tileId;
            UpdateTileInfo();
            UpdateOkEnabled();
        });

        collectionChoice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
            selectedCollectionIndex_ = collectionChoice_->GetSelection();
            if (selectedCollectionIndex_ < 0 || selectedCollectionIndex_ >= static_cast<int>(collections_.size())) {
                palette_->SetCollection(nullptr);
                selectedTileId_ = -1;
                UpdateTileInfo();
                UpdateOkEnabled();
                return;
            }

            const TileCollection& collection = collections_[static_cast<size_t>(selectedCollectionIndex_)];
            palette_->SetCollection(&collection);
            if (!FindTileDef(collection, selectedTileId_) && !collection.tiles.empty()) {
                selectedTileId_ = collection.tiles.front().id;
            }
            palette_->SetSelectedTileId(selectedTileId_, true);
            UpdateTileInfo();
            UpdateOkEnabled();
        });

        UpdateTileInfo();
        UpdateOkEnabled();
    }

    int GetSelectedTileId() const {
        return selectedTileId_;
    }

private:
    void UpdateTileInfo() {
        if (!tileInfo_) {
            return;
        }
        if (selectedCollectionIndex_ < 0 || selectedCollectionIndex_ >= static_cast<int>(collections_.size())) {
            tileInfo_->SetLabel("No tile selected");
            return;
        }

        const TileCollection& collection = collections_[static_cast<size_t>(selectedCollectionIndex_)];
        const TileDef* tile = FindTileDef(collection, selectedTileId_);
        if (!tile) {
            tileInfo_->SetLabel("No tile selected");
            return;
        }

        tileInfo_->SetLabel(wxString::Format(
            "%s  id=%d",
            wxString::FromUTF8(tile->name.empty() ? std::string("tile") : tile->name),
            tile->id
        ));
    }

    void UpdateOkEnabled() {
        if (!okButton_) {
            return;
        }
        const bool validCollection = selectedCollectionIndex_ >= 0 && selectedCollectionIndex_ < static_cast<int>(collections_.size());
        const bool validTile = validCollection && FindTileDef(collections_[static_cast<size_t>(selectedCollectionIndex_)], selectedTileId_) != nullptr;
        okButton_->Enable(validTile);
    }

private:
    const std::vector<TileCollection>& collections_;
    wxChoice* collectionChoice_ = nullptr;
    TilePalettePanel* palette_ = nullptr;
    wxStaticText* tileInfo_ = nullptr;
    wxButton* okButton_ = nullptr;
    int selectedCollectionIndex_ = -1;
    int selectedTileId_ = -1;
};

class ItemPalettePanel final : public wxScrolledWindow {
public:
    ItemPalettePanel(wxWindow* parent)
        : wxScrolledWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_SIMPLE | wxVSCROLL) {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetScrollRate(0, 12);
        SetMinSize(wxSize(260, 320));
        Bind(wxEVT_PAINT, &ItemPalettePanel::OnPaint, this);
        Bind(wxEVT_LEFT_DOWN, &ItemPalettePanel::OnLeftDown, this);
        Bind(wxEVT_LEFT_DCLICK, &ItemPalettePanel::OnLeftDClick, this);
    }

    void SetItems(const std::vector<ItemDefinition>* items) {
        items_ = items;
        if (items_ == nullptr || items_->empty()) {
            selectedItemId_.clear();
        } else if (selectedItemId_.empty() || FindItemDefinitionIndex(selectedItemId_) < 0) {
            selectedItemId_ = items_->front().id;
        }
        RefreshVirtualSize();
        Refresh();
    }

    void SetSelectedItemId(const std::string& itemId, bool ensureVisible = false) {
        selectedItemId_ = itemId;
        if (ensureVisible) {
            const int index = FindItemDefinitionIndex(selectedItemId_);
            if (index >= 0) {
                const int scrollUnitY = 12;
                Scroll(0, std::max(0, index * kRowHeight / scrollUnitY));
            }
        }
        Refresh();
    }

    const std::string& SelectedItemId() const {
        return selectedItemId_;
    }

    void SetSelectionChangedCallback(std::function<void(const std::string&)> callback) {
        onSelectionChanged_ = std::move(callback);
    }

    void SetEditItemCallback(std::function<void(const std::string&)> callback) {
        onEditItem_ = std::move(callback);
    }

private:
    static constexpr int kRowHeight = 86;

    int FindItemDefinitionIndex(const std::string& itemId) const {
        if (!items_) {
            return -1;
        }
        for (size_t i = 0; i < items_->size(); ++i) {
            if ((*items_)[i].id == itemId) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    wxRect ItemRect(int index, int width) const {
        return wxRect(6, 6 + index * kRowHeight, std::max(120, width - 12), kRowHeight - 8);
    }

    void RefreshVirtualSize() {
        const int count = items_ ? static_cast<int>(items_->size()) : 0;
        SetVirtualSize(wxSize(std::max(240, GetClientSize().GetWidth()), 12 + count * kRowHeight));
    }

    void OnPaint(wxPaintEvent&) {
        wxAutoBufferedPaintDC dc(this);
        PrepareDC(dc);
        dc.SetBackground(wxBrush(wxColour(20, 24, 30)));
        dc.Clear();

        if (!items_ || items_->empty()) {
            dc.SetTextForeground(wxColour(180, 186, 198));
            dc.DrawText("No item definitions", 12, 12);
            return;
        }

        const int width = std::max(GetVirtualSize().GetWidth(), GetClientSize().GetWidth());
        dc.SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Segoe UI"));

        for (size_t i = 0; i < items_->size(); ++i) {
            const ItemDefinition& item = (*items_)[i];
            const wxRect rect = ItemRect(static_cast<int>(i), width);
            const bool selected = item.id == selectedItemId_;

            dc.SetPen(selected ? wxPen(wxColour(255, 210, 96), 2) : wxPen(wxColour(46, 56, 71), 1));
            dc.SetBrush(wxBrush(selected ? wxColour(53, 62, 78) : wxColour(29, 35, 44)));
            dc.DrawRoundedRectangle(rect, 6);

            const ItemAnimationFrame* previewFrame = item.frames.empty() ? nullptr : &item.frames.front();
            wxBitmap preview = BuildItemFramePreviewBitmap(previewFrame, 4, wxColour(18, 22, 28));
            dc.DrawBitmap(preview, rect.x + 10, rect.y + 10, true);

            dc.SetTextForeground(wxColour(236, 240, 246));
            dc.SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Segoe UI"));
            dc.DrawText(wxString::FromUTF8(item.name.empty() ? item.id : item.name), rect.x + 88, rect.y + 10);

            dc.SetFont(wxFont(8, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Segoe UI"));
            dc.SetTextForeground(wxColour(180, 186, 198));
            dc.DrawText(ItemSummaryLabel(item), rect.x + 88, rect.y + 32);
        }
    }

    void OnLeftDown(wxMouseEvent& event) {
        if (!items_) {
            return;
        }

        const wxPoint point = CalcUnscrolledPosition(event.GetPosition());
        const int width = std::max(GetVirtualSize().GetWidth(), GetClientSize().GetWidth());
        for (size_t i = 0; i < items_->size(); ++i) {
            const wxRect rect = ItemRect(static_cast<int>(i), width);
            if (!rect.Contains(point)) {
                continue;
            }
            selectedItemId_ = (*items_)[i].id;
            Refresh();
            if (onSelectionChanged_) {
                onSelectionChanged_(selectedItemId_);
            }
            return;
        }
    }

    void OnLeftDClick(wxMouseEvent& event) {
        OnLeftDown(event);
        if (!selectedItemId_.empty() && onEditItem_) {
            onEditItem_(selectedItemId_);
        }
    }

    const std::vector<ItemDefinition>* items_ = nullptr;
    std::string selectedItemId_;
    std::function<void(const std::string&)> onSelectionChanged_;
    std::function<void(const std::string&)> onEditItem_;
};

class DropTablePalettePanel final : public wxScrolledWindow {
public:
    DropTablePalettePanel(wxWindow* parent, const std::vector<ItemDefinition>* itemDefs)
        : wxScrolledWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_SIMPLE | wxVSCROLL),
          itemDefs_(itemDefs) {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetScrollRate(0, 12);
        SetMinSize(wxSize(260, 160));
        Bind(wxEVT_PAINT, &DropTablePalettePanel::OnPaint, this);
        Bind(wxEVT_LEFT_DOWN, &DropTablePalettePanel::OnLeftDown, this);
        Bind(wxEVT_LEFT_DCLICK, &DropTablePalettePanel::OnLeftDClick, this);
    }

    void SetDropTables(const std::vector<EnemyDropTable>* tables) {
        tables_ = tables;
        if (!tables_ || tables_->empty()) {
            selectedTableId_.clear();
        } else if (selectedTableId_.empty() || FindTableIndex(selectedTableId_) < 0) {
            selectedTableId_ = tables_->front().id;
        }
        RefreshVirtualSize();
        Refresh();
    }

    void SetSelectedTableId(const std::string& tableId) {
        selectedTableId_ = tableId;
        Refresh();
    }

    const std::string& SelectedTableId() const {
        return selectedTableId_;
    }

    void SetSelectionChangedCallback(std::function<void(const std::string&)> callback) {
        onSelectionChanged_ = std::move(callback);
    }

    void SetEditTableCallback(std::function<void(const std::string&)> callback) {
        onEditTable_ = std::move(callback);
    }

private:
    static constexpr int kRowHeight = 80;  // Increased for item image display

    int FindTableIndex(const std::string& tableId) const {
        if (!tables_) {
            return -1;
        }
        for (size_t i = 0; i < tables_->size(); ++i) {
            if ((*tables_)[i].id == tableId) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    wxRect ItemRect(int index, int width) const {
        return wxRect(6, 6 + index * kRowHeight, std::max(120, width - 12), kRowHeight - 8);
    }

    void RefreshVirtualSize() {
        const int count = tables_ ? static_cast<int>(tables_->size()) : 0;
        SetVirtualSize(wxSize(std::max(240, GetClientSize().GetWidth()), 12 + count * kRowHeight));
    }

    void OnPaint(wxPaintEvent&) {
        wxAutoBufferedPaintDC dc(this);
        PrepareDC(dc);
        dc.SetBackground(wxBrush(wxColour(20, 24, 30)));
        dc.Clear();

        if (!tables_ || tables_->empty() || !itemDefs_) {
            dc.SetTextForeground(wxColour(180, 186, 198));
            dc.DrawText("No drop tables", 12, 12);
            return;
        }

        const int width = std::max(GetVirtualSize().GetWidth(), GetClientSize().GetWidth());
        dc.SetFont(wxFont(8, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Segoe UI"));

        for (size_t i = 0; i < tables_->size(); ++i) {
            const EnemyDropTable& table = (*tables_)[i];
            const wxRect rect = ItemRect(static_cast<int>(i), width);
            const bool selected = table.id == selectedTableId_;

            dc.SetPen(selected ? wxPen(wxColour(255, 210, 96), 2) : wxPen(wxColour(46, 56, 71), 1));
            dc.SetBrush(wxBrush(selected ? wxColour(53, 62, 78) : wxColour(29, 35, 44)));
            dc.DrawRoundedRectangle(rect, 4);

            // Table name
            dc.SetTextForeground(wxColour(236, 240, 246));
            dc.SetFont(wxFont(8, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Segoe UI"));
            const wxString tableName = wxString::FromUTF8(table.name.empty() ? table.id : table.name);
            dc.DrawText(tableName, rect.x + 8, rect.y + 4);

            // Items with images and percentages
            dc.SetFont(wxFont(7, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Segoe UI"));
            dc.SetTextForeground(wxColour(180, 186, 198));
            int entryX = rect.x + 8;
            for (const EnemyDropEntry& entry : table.entries) {
                // Find item definition and render its preview
                const ItemDefinition* itemDef = nullptr;
                for (const ItemDefinition& def : *itemDefs_) {
                    if (def.id == entry.itemId) {
                        itemDef = &def;
                        break;
                    }
                }
                
                if (itemDef) {
                    // Draw item preview image
                    const ItemAnimationFrame* previewFrame = itemDef->frames.empty() ? nullptr : &itemDef->frames.front();
                    wxBitmap preview = BuildItemFramePreviewBitmap(previewFrame, 2, wxColour(18, 22, 28));
                    dc.DrawBitmap(preview, entryX, rect.y + 18, true);
                    
                    // Draw percentage below image
                    const wxString percentText = wxString::Format("%d%%", entry.weight);
                    dc.DrawText(percentText, entryX, rect.y + 52);
                }
                
                entryX += 42;  // Space for item image (32px) + padding
                if (entryX > rect.GetRight() - 40) {
                    break;  // Don't overflow rectangle
                }
            }
        }

        SetVirtualSize(wxSize(GetClientSize().GetWidth(), 6 + static_cast<int>(tables_->size()) * kRowHeight + 6));
    }

    void OnLeftDown(wxMouseEvent& event) {
        if (!tables_) {
            return;
        }

        const wxPoint point = CalcUnscrolledPosition(event.GetPosition());
        const int width = std::max(GetVirtualSize().GetWidth(), GetClientSize().GetWidth());
        for (size_t i = 0; i < tables_->size(); ++i) {
            const wxRect rect = ItemRect(static_cast<int>(i), width);
            if (!rect.Contains(point)) {
                continue;
            }
            selectedTableId_ = (*tables_)[i].id;
            Refresh();
            if (onSelectionChanged_) {
                onSelectionChanged_(selectedTableId_);
            }
            return;
        }
    }

    void OnLeftDClick(wxMouseEvent& event) {
        OnLeftDown(event);
        if (!selectedTableId_.empty() && onEditTable_) {
            onEditTable_(selectedTableId_);
        }
    }

    const std::vector<EnemyDropTable>* tables_ = nullptr;
    const std::vector<ItemDefinition>* itemDefs_ = nullptr;
    std::string selectedTableId_;
    std::function<void(const std::string&)> onSelectionChanged_;
    std::function<void(const std::string&)> onEditTable_;
};

class EnemyPalettePanel final : public wxScrolledWindow {
public:
    EnemyPalettePanel(wxWindow* parent)
        : wxScrolledWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_SIMPLE | wxVSCROLL) {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetScrollRate(0, 12);
        SetMinSize(wxSize(260, 320));
        Bind(wxEVT_PAINT, &EnemyPalettePanel::OnPaint, this);
        Bind(wxEVT_LEFT_DOWN, &EnemyPalettePanel::OnLeftDown, this);
        Bind(wxEVT_LEFT_DCLICK, &EnemyPalettePanel::OnLeftDClick, this);
    }

    void SetItems(const std::vector<EnemyDefinition>* enemies) {
        enemies_ = enemies;
        if (!enemies_ || enemies_->empty()) {
            selectedEnemyId_.clear();
        } else if (selectedEnemyId_.empty() || FindEnemyDefinitionIndex(selectedEnemyId_) < 0) {
            selectedEnemyId_ = enemies_->front().id;
        }
        RefreshVirtualSize();
        Refresh();
    }

    void SetSelectedEnemyId(const std::string& enemyId, bool ensureVisible = false) {
        selectedEnemyId_ = enemyId;
        if (ensureVisible) {
            const int index = FindEnemyDefinitionIndex(selectedEnemyId_);
            if (index >= 0) {
                Scroll(0, std::max(0, index * kRowHeight / 12));
            }
        }
        Refresh();
    }

    const std::string& SelectedEnemyId() const {
        return selectedEnemyId_;
    }

    void SetSelectionChangedCallback(std::function<void(const std::string&)> callback) {
        onSelectionChanged_ = std::move(callback);
    }

    void SetEditEnemyCallback(std::function<void(const std::string&)> callback) {
        onEditEnemy_ = std::move(callback);
    }

private:
    static constexpr int kRowHeight = 86;

    int FindEnemyDefinitionIndex(const std::string& enemyId) const {
        if (!enemies_) {
            return -1;
        }
        for (size_t i = 0; i < enemies_->size(); ++i) {
            if ((*enemies_)[i].id == enemyId) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    wxRect ItemRect(int index, int width) const {
        return wxRect(6, 6 + index * kRowHeight, std::max(120, width - 12), kRowHeight - 8);
    }

    int CountVisibleEnemies() const {
        if (!enemies_) return 0;
        int count = 0;
        for (const auto& enemy : *enemies_) {
            if (!enemy.isNpc) count++;
        }
        return count;
    }

    void RefreshVirtualSize() {
        const int count = CountVisibleEnemies();
        SetVirtualSize(wxSize(std::max(240, GetClientSize().GetWidth()), 12 + count * kRowHeight));
    }

    wxBitmap EnemyPreviewBitmap(const EnemyDefinition& enemy) const {
        if (enemy.moves.empty()) {
            wxBitmap fallback(64, 64);
            wxMemoryDC dc;
            dc.SelectObject(fallback);
            dc.SetBackground(wxBrush(wxColour(25, 30, 38)));
            dc.Clear();
            dc.SetBrush(wxBrush(wxColour(140, 48, 48)));
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.DrawRectangle(12, 12, 40, 40);
            dc.SelectObject(wxNullBitmap);
            return fallback;
        }
        const EnemyMoveDefinition::AnimationFrame* frame = FirstEnemyFrame(enemy.moves.front());
        return BuildEnemyFramePreviewBitmap(frame, 4, wxColour(18, 22, 28));
    }

    wxString EnemySummaryLabel(const EnemyDefinition& enemy) const {
        return wxString::Format("hp=%d  dmg=%d  moves=%d", enemy.hitpoints, enemy.baseDamage, static_cast<int>(enemy.moves.size()));
    }

    wxBitmap ScaleEnemyPreviewToFit(const wxBitmap& bitmap, int maxWidth, int maxHeight) const {
        if (!bitmap.IsOk()) {
            return bitmap;
        }
        if (bitmap.GetWidth() <= maxWidth && bitmap.GetHeight() <= maxHeight) {
            return bitmap;
        }

        const double scaleX = static_cast<double>(maxWidth) / static_cast<double>(std::max(1, bitmap.GetWidth()));
        const double scaleY = static_cast<double>(maxHeight) / static_cast<double>(std::max(1, bitmap.GetHeight()));
        const double scale = std::min(scaleX, scaleY);
        const int width = std::max(1, static_cast<int>(std::floor(bitmap.GetWidth() * scale)));
        const int height = std::max(1, static_cast<int>(std::floor(bitmap.GetHeight() * scale)));
        return wxBitmap(bitmap.ConvertToImage().Scale(width, height, wxIMAGE_QUALITY_NEAREST));
    }

    void OnPaint(wxPaintEvent&) {
        wxAutoBufferedPaintDC dc(this);
        PrepareDC(dc);
        dc.SetBackground(wxBrush(wxColour(20, 24, 30)));
        dc.Clear();

        if (!enemies_ || enemies_->empty()) {
            dc.SetTextForeground(wxColour(180, 186, 198));
            dc.DrawText("No enemy definitions", 12, 12);
            return;
        }

        const int width = std::max(GetVirtualSize().GetWidth(), GetClientSize().GetWidth());
        dc.SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Segoe UI"));

        int visibleIndex = 0;
        for (size_t i = 0; i < enemies_->size(); ++i) {
            const EnemyDefinition& enemy = (*enemies_)[i];
            if (enemy.isNpc) continue;  // Skip NPCs in enemy palette
            const wxRect rect = ItemRect(visibleIndex, width);
            const bool selected = enemy.id == selectedEnemyId_;

            dc.SetPen(selected ? wxPen(wxColour(255, 210, 96), 2) : wxPen(wxColour(46, 56, 71), 1));
            dc.SetBrush(wxBrush(selected ? wxColour(53, 62, 78) : wxColour(29, 35, 44)));
            dc.DrawRoundedRectangle(rect, 6);

            const wxBitmap preview = ScaleEnemyPreviewToFit(EnemyPreviewBitmap(enemy), 64, 64);
            dc.DrawBitmap(preview, rect.x + 10, rect.y + std::max(10, (rect.height - preview.GetHeight()) / 2), true);

            dc.SetTextForeground(wxColour(236, 240, 246));
            dc.SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Segoe UI"));
            dc.DrawText(wxString::FromUTF8(enemy.name.empty() ? enemy.id : enemy.name), rect.x + 88, rect.y + 10);

            dc.SetFont(wxFont(8, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Segoe UI"));
            dc.SetTextForeground(wxColour(180, 186, 198));
            dc.DrawText(EnemySummaryLabel(enemy), rect.x + 88, rect.y + 32);
            visibleIndex++;
        }
    }

    void OnLeftDown(wxMouseEvent& event) {
        if (!enemies_) {
            return;
        }

        const wxPoint point = CalcUnscrolledPosition(event.GetPosition());
        const int width = std::max(GetVirtualSize().GetWidth(), GetClientSize().GetWidth());
        int visibleIndex = 0;
        for (size_t i = 0; i < enemies_->size(); ++i) {
            const EnemyDefinition& enemy = (*enemies_)[i];
            if (enemy.isNpc) continue;  // Skip NPCs
            const wxRect rect = ItemRect(visibleIndex, width);
            if (!rect.Contains(point)) {
                visibleIndex++;
                continue;
            }
            selectedEnemyId_ = enemy.id;
            Refresh();
            if (onSelectionChanged_) {
                onSelectionChanged_(selectedEnemyId_);
            }
            return;
        }
    }

    void OnLeftDClick(wxMouseEvent& event) {
        OnLeftDown(event);
        if (!selectedEnemyId_.empty() && onEditEnemy_) {
            onEditEnemy_(selectedEnemyId_);
        }
    }

    const std::vector<EnemyDefinition>* enemies_ = nullptr;
    std::string selectedEnemyId_;
    std::function<void(const std::string&)> onSelectionChanged_;
    std::function<void(const std::string&)> onEditEnemy_;
};

class NpcPalettePanel final : public wxScrolledWindow {
public:
    NpcPalettePanel(wxWindow* parent)
        : wxScrolledWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_SIMPLE | wxVSCROLL) {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetScrollRate(0, 12);
        SetMinSize(wxSize(260, 320));
        Bind(wxEVT_PAINT, &NpcPalettePanel::OnPaint, this);
        Bind(wxEVT_LEFT_DOWN, &NpcPalettePanel::OnLeftDown, this);
        Bind(wxEVT_LEFT_DCLICK, &NpcPalettePanel::OnLeftDClick, this);
    }

    void SetItems(const std::vector<EnemyDefinition>* enemies) {
        enemies_ = enemies;
        if (!enemies_ || FindNpcDefinitionIndex(selectedNpcId_) < 0) {
            selectedNpcId_.clear();
            if (enemies_) {
                for (const EnemyDefinition& enemy : *enemies_) {
                    if (enemy.isNpc) {
                        selectedNpcId_ = enemy.id;
                        break;
                    }
                }
            }
        }
        RefreshVirtualSize();
        Refresh();
    }

    void SetSelectedNpcId(const std::string& npcId, bool ensureVisible = false) {
        selectedNpcId_ = npcId;
        if (ensureVisible) {
            const int index = FindNpcDefinitionIndex(selectedNpcId_);
            if (index >= 0) {
                Scroll(0, std::max(0, index * kRowHeight / 12));
            }
        }
        Refresh();
    }

    void SetSelectionChangedCallback(std::function<void(const std::string&)> callback) {
        onSelectionChanged_ = std::move(callback);
    }

    void SetEditNpcCallback(std::function<void(const std::string&)> callback) {
        onEditNpc_ = std::move(callback);
    }

private:
    static constexpr int kRowHeight = 96;

    std::vector<const EnemyDefinition*> VisibleNpcs() const {
        std::vector<const EnemyDefinition*> npcs;
        if (!enemies_) {
            return npcs;
        }
        for (const EnemyDefinition& enemy : *enemies_) {
            if (enemy.isNpc) {
                npcs.push_back(&enemy);
            }
        }
        return npcs;
    }

    int FindNpcDefinitionIndex(const std::string& npcId) const {
        if (!enemies_) {
            return -1;
        }
        int visibleIndex = 0;
        for (const EnemyDefinition& enemy : *enemies_) {
            if (!enemy.isNpc) {
                continue;
            }
            if (enemy.id == npcId) {
                return visibleIndex;
            }
            ++visibleIndex;
        }
        return -1;
    }

    wxRect ItemRect(int index, int width) const {
        return wxRect(6, 6 + index * kRowHeight, std::max(120, width - 12), kRowHeight - 8);
    }

    void RefreshVirtualSize() {
        const std::vector<const EnemyDefinition*> npcs = VisibleNpcs();
        SetVirtualSize(wxSize(std::max(240, GetClientSize().GetWidth()), 12 + static_cast<int>(npcs.size()) * kRowHeight));
    }

    wxBitmap NpcPreviewBitmap(const EnemyDefinition& npc) const {
        if (npc.moves.empty()) {
            wxBitmap fallback(64, 64);
            wxMemoryDC dc;
            dc.SelectObject(fallback);
            dc.SetBackground(wxBrush(wxColour(25, 30, 38)));
            dc.Clear();
            dc.SetBrush(wxBrush(wxColour(60, 100, 150)));
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.DrawRectangle(12, 12, 40, 40);
            dc.SelectObject(wxNullBitmap);
            return fallback;
        }
        const EnemyMoveDefinition::AnimationFrame* frame = FirstEnemyFrame(npc.moves.front());
        return BuildEnemyFramePreviewBitmap(frame, 4, wxColour(18, 22, 28));
    }

    wxBitmap ScalePreviewToFit(const wxBitmap& bitmap, int maxWidth, int maxHeight) const {
        if (!bitmap.IsOk()) {
            return bitmap;
        }
        if (bitmap.GetWidth() <= maxWidth && bitmap.GetHeight() <= maxHeight) {
            return bitmap;
        }
        const double scaleX = static_cast<double>(maxWidth) / static_cast<double>(std::max(1, bitmap.GetWidth()));
        const double scaleY = static_cast<double>(maxHeight) / static_cast<double>(std::max(1, bitmap.GetHeight()));
        const double scale = std::min(scaleX, scaleY);
        const int width = std::max(1, static_cast<int>(std::floor(bitmap.GetWidth() * scale)));
        const int height = std::max(1, static_cast<int>(std::floor(bitmap.GetHeight() * scale)));
        return wxBitmap(bitmap.ConvertToImage().Scale(width, height, wxIMAGE_QUALITY_NEAREST));
    }

    wxString NpcTextSnippet(const EnemyDefinition& npc) const {
        std::string snippet = npc.npcText;
        std::replace(snippet.begin(), snippet.end(), '\n', ' ');
        if (snippet.size() > 56) {
            snippet = snippet.substr(0, 56) + "...";
        }
        return wxString::FromUTF8(snippet.empty() ? std::string("(no text)") : snippet);
    }

    void OnPaint(wxPaintEvent&) {
        wxAutoBufferedPaintDC dc(this);
        PrepareDC(dc);
        dc.SetBackground(wxBrush(wxColour(20, 24, 30)));
        dc.Clear();

        const std::vector<const EnemyDefinition*> npcs = VisibleNpcs();
        if (npcs.empty()) {
            dc.SetTextForeground(wxColour(180, 186, 198));
            dc.DrawText("No NPC definitions", 12, 12);
            return;
        }

        const int width = std::max(GetVirtualSize().GetWidth(), GetClientSize().GetWidth());
        for (size_t i = 0; i < npcs.size(); ++i) {
            const EnemyDefinition& npc = *npcs[i];
            const wxRect rect = ItemRect(static_cast<int>(i), width);
            const bool selected = npc.id == selectedNpcId_;

            dc.SetPen(selected ? wxPen(wxColour(120, 210, 255), 2) : wxPen(wxColour(46, 56, 71), 1));
            dc.SetBrush(wxBrush(selected ? wxColour(45, 60, 74) : wxColour(29, 35, 44)));
            dc.DrawRoundedRectangle(rect, 6);

            const wxBitmap preview = ScalePreviewToFit(NpcPreviewBitmap(npc), 64, 64);
            dc.DrawBitmap(preview, rect.x + 10, rect.y + std::max(8, (rect.height - preview.GetHeight()) / 2), true);

            dc.SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Segoe UI"));
            dc.SetTextForeground(wxColour(236, 240, 246));
            dc.DrawText(wxString::FromUTF8(npc.name.empty() ? npc.id : npc.name), rect.x + 88, rect.y + 10);

            dc.SetFont(wxFont(8, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Segoe UI"));
            dc.SetTextForeground(wxColour(180, 186, 198));
            dc.DrawText(NpcTextSnippet(npc), rect.x + 88, rect.y + 32);
        }
    }

    void OnLeftDown(wxMouseEvent& event) {
        const std::vector<const EnemyDefinition*> npcs = VisibleNpcs();
        if (npcs.empty()) {
            return;
        }

        const wxPoint point = CalcUnscrolledPosition(event.GetPosition());
        const int width = std::max(GetVirtualSize().GetWidth(), GetClientSize().GetWidth());
        for (size_t i = 0; i < npcs.size(); ++i) {
            const wxRect rect = ItemRect(static_cast<int>(i), width);
            if (!rect.Contains(point)) {
                continue;
            }
            selectedNpcId_ = npcs[i]->id;
            Refresh();
            if (onSelectionChanged_) {
                onSelectionChanged_(selectedNpcId_);
            }
            return;
        }
    }

    void OnLeftDClick(wxMouseEvent& event) {
        OnLeftDown(event);
        if (!selectedNpcId_.empty() && onEditNpc_) {
            onEditNpc_(selectedNpcId_);
        }
    }

    const std::vector<EnemyDefinition>* enemies_ = nullptr;
    std::string selectedNpcId_;
    std::function<void(const std::string&)> onSelectionChanged_;
    std::function<void(const std::string&)> onEditNpc_;
};

class ProjectilePalettePanel final : public wxScrolledWindow {
public:
    ProjectilePalettePanel(wxWindow* parent)
        : wxScrolledWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_SIMPLE | wxVSCROLL) {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetScrollRate(0, 12);
        SetMinSize(wxSize(260, 320));
        Bind(wxEVT_PAINT, &ProjectilePalettePanel::OnPaint, this);
        Bind(wxEVT_LEFT_DOWN, &ProjectilePalettePanel::OnLeftDown, this);
        Bind(wxEVT_LEFT_DCLICK, &ProjectilePalettePanel::OnLeftDClick, this);
    }

    void SetItems(const std::vector<ProjectileDefinition>* projectiles) {
        projectiles_ = projectiles;
        if (!projectiles_ || projectiles_->empty()) {
            selectedProjectileId_.clear();
        } else if (selectedProjectileId_.empty() || FindProjectileDefinitionIndex(selectedProjectileId_) < 0) {
            selectedProjectileId_ = projectiles_->front().id;
        }
        RefreshVirtualSize();
        Refresh();
    }

    void SetSelectedProjectileId(const std::string& projectileId, bool ensureVisible = false) {
        selectedProjectileId_ = projectileId;
        if (ensureVisible) {
            const int index = FindProjectileDefinitionIndex(selectedProjectileId_);
            if (index >= 0) {
                Scroll(0, std::max(0, index * kRowHeight / 12));
            }
        }
        Refresh();
    }

    void SetSelectionChangedCallback(std::function<void(const std::string&)> callback) {
        onSelectionChanged_ = std::move(callback);
    }

    void SetEditProjectileCallback(std::function<void(const std::string&)> callback) {
        onEditProjectile_ = std::move(callback);
    }

private:
    static constexpr int kRowHeight = 86;

    int FindProjectileDefinitionIndex(const std::string& projectileId) const {
        if (!projectiles_) {
            return -1;
        }
        for (size_t i = 0; i < projectiles_->size(); ++i) {
            if ((*projectiles_)[i].id == projectileId) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    wxRect ItemRect(int index, int width) const {
        return wxRect(6, 6 + index * kRowHeight, std::max(120, width - 12), kRowHeight - 8);
    }

    void RefreshVirtualSize() {
        const int count = projectiles_ ? static_cast<int>(projectiles_->size()) : 0;
        SetVirtualSize(wxSize(std::max(240, GetClientSize().GetWidth()), 12 + count * kRowHeight));
    }

    const ItemAnimationFrame* ProjectilePreviewFrame(const ProjectileDefinition& projectile) const {
        if (!projectile.flightFrames.empty()) {
            return &projectile.flightFrames.front();
        }
        if (!projectile.startFrames.empty()) {
            return &projectile.startFrames.front();
        }
        if (!projectile.impactFrames.empty()) {
            return &projectile.impactFrames.front();
        }
        return nullptr;
    }

    wxString ProjectileSummaryLabel(const ProjectileDefinition& projectile) const {
        wxString movement = "track";
        if (projectile.movementType == ProjectileMovementType::FixedFunction) {
            movement = "fixed";
        } else if (projectile.movementType == ProjectileMovementType::StraightLimitedDistance) {
            movement = "straight";
        } else if (projectile.movementType == ProjectileMovementType::Homing) {
            movement = "homing";
        }
        return wxString::Format("%s  dmg=%d  speed=%.1f", movement, projectile.baseDamage, projectile.speedTilesPerSecond);
    }

    void OnPaint(wxPaintEvent&) {
        wxAutoBufferedPaintDC dc(this);
        PrepareDC(dc);
        dc.SetBackground(wxBrush(wxColour(20, 24, 30)));
        dc.Clear();

        if (!projectiles_ || projectiles_->empty()) {
            dc.SetTextForeground(wxColour(180, 186, 198));
            dc.DrawText("No projectile definitions", 12, 12);
            return;
        }

        const int width = std::max(GetVirtualSize().GetWidth(), GetClientSize().GetWidth());
        dc.SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Segoe UI"));

        for (size_t i = 0; i < projectiles_->size(); ++i) {
            const ProjectileDefinition& projectile = (*projectiles_)[i];
            const wxRect rect = ItemRect(static_cast<int>(i), width);
            const bool selected = projectile.id == selectedProjectileId_;

            dc.SetPen(selected ? wxPen(wxColour(255, 210, 96), 2) : wxPen(wxColour(46, 56, 71), 1));
            dc.SetBrush(wxBrush(selected ? wxColour(53, 62, 78) : wxColour(29, 35, 44)));
            dc.DrawRoundedRectangle(rect, 6);

            const wxBitmap preview = BuildItemFramePreviewBitmap(ProjectilePreviewFrame(projectile), 4, wxColour(18, 22, 28));
            dc.DrawBitmap(preview, rect.x + 10, rect.y + std::max(10, (rect.height - preview.GetHeight()) / 2), true);

            dc.SetTextForeground(wxColour(236, 240, 246));
            dc.SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Segoe UI"));
            dc.DrawText(wxString::FromUTF8(projectile.name.empty() ? projectile.id : projectile.name), rect.x + 88, rect.y + 10);

            dc.SetFont(wxFont(8, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Segoe UI"));
            dc.SetTextForeground(wxColour(180, 186, 198));
            dc.DrawText(ProjectileSummaryLabel(projectile), rect.x + 88, rect.y + 32);
        }
    }

    void OnLeftDown(wxMouseEvent& event) {
        if (!projectiles_) {
            return;
        }

        const wxPoint point = CalcUnscrolledPosition(event.GetPosition());
        const int width = std::max(GetVirtualSize().GetWidth(), GetClientSize().GetWidth());
        for (size_t i = 0; i < projectiles_->size(); ++i) {
            const wxRect rect = ItemRect(static_cast<int>(i), width);
            if (!rect.Contains(point)) {
                continue;
            }
            selectedProjectileId_ = (*projectiles_)[i].id;
            Refresh();
            if (onSelectionChanged_) {
                onSelectionChanged_(selectedProjectileId_);
            }
            return;
        }
    }

    void OnLeftDClick(wxMouseEvent& event) {
        OnLeftDown(event);
        if (!selectedProjectileId_.empty() && onEditProjectile_) {
            onEditProjectile_(selectedProjectileId_);
        }
    }

    const std::vector<ProjectileDefinition>* projectiles_ = nullptr;
    std::string selectedProjectileId_;
    std::function<void(const std::string&)> onSelectionChanged_;
    std::function<void(const std::string&)> onEditProjectile_;
};

class WeaponPalettePanel final : public wxScrolledWindow {
public:
    WeaponPalettePanel(wxWindow* parent)
        : wxScrolledWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_SIMPLE | wxVSCROLL) {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetScrollRate(0, 12);
        SetMinSize(wxSize(260, 320));
        Bind(wxEVT_PAINT, &WeaponPalettePanel::OnPaint, this);
        Bind(wxEVT_LEFT_DOWN, &WeaponPalettePanel::OnLeftDown, this);
        Bind(wxEVT_LEFT_DCLICK, &WeaponPalettePanel::OnLeftDClick, this);
    }

    void SetItems(const std::vector<WeaponDefinition>* weapons) {
        weapons_ = weapons;
        if (!weapons_ || weapons_->empty()) {
            selectedWeaponId_.clear();
        } else if (selectedWeaponId_.empty() || FindWeaponDefinitionIndex(selectedWeaponId_) < 0) {
            selectedWeaponId_ = weapons_->front().id;
        }
        RefreshVirtualSize();
        Refresh();
    }

    void SetSelectedWeaponId(const std::string& weaponId, bool ensureVisible = false) {
        selectedWeaponId_ = weaponId;
        if (ensureVisible) {
            const int index = FindWeaponDefinitionIndex(selectedWeaponId_);
            if (index >= 0) {
                Scroll(0, std::max(0, index * kRowHeight / 12));
            }
        }
        Refresh();
    }

    void SetSelectionChangedCallback(std::function<void(const std::string&)> callback) {
        onSelectionChanged_ = std::move(callback);
    }

    void SetEditWeaponCallback(std::function<void(const std::string&)> callback) {
        onEditWeapon_ = std::move(callback);
    }

private:
    static constexpr int kRowHeight = 86;

    int FindWeaponDefinitionIndex(const std::string& weaponId) const {
        if (!weapons_) {
            return -1;
        }
        for (size_t i = 0; i < weapons_->size(); ++i) {
            if ((*weapons_)[i].id == weaponId) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    wxRect ItemRect(int index, int width) const {
        return wxRect(6, 6 + index * kRowHeight, std::max(120, width - 12), kRowHeight - 8);
    }

    void RefreshVirtualSize() {
        const int count = weapons_ ? static_cast<int>(weapons_->size()) : 0;
        SetVirtualSize(wxSize(std::max(240, GetClientSize().GetWidth()), 12 + count * kRowHeight));
    }

    const ItemAnimationFrame* WeaponPreviewFrame(const WeaponDefinition& weapon) const {
        if (weapon.hudSprite.sourceImagePath.empty() || weapon.hudSprite.sourceW <= 0 || weapon.hudSprite.sourceH <= 0) {
            return nullptr;
        }
        return &weapon.hudSprite;
    }

    wxString WeaponSummaryLabel(const WeaponDefinition& weapon) const {
        return wxString::Format("%s  dmg=%d", weapon.isProjectile ? "projectile" : "melee", std::max(0, weapon.damage));
    }

    void OnPaint(wxPaintEvent&) {
        wxAutoBufferedPaintDC dc(this);
        PrepareDC(dc);
        dc.SetBackground(wxBrush(wxColour(20, 24, 30)));
        dc.Clear();

        if (!weapons_ || weapons_->empty()) {
            dc.SetTextForeground(wxColour(180, 186, 198));
            dc.DrawText("No weapon definitions", 12, 12);
            return;
        }

        const int width = std::max(GetVirtualSize().GetWidth(), GetClientSize().GetWidth());
        dc.SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Segoe UI"));

        for (size_t i = 0; i < weapons_->size(); ++i) {
            const WeaponDefinition& weapon = (*weapons_)[i];
            const wxRect rect = ItemRect(static_cast<int>(i), width);
            const bool selected = weapon.id == selectedWeaponId_;

            dc.SetPen(selected ? wxPen(wxColour(255, 210, 96), 2) : wxPen(wxColour(46, 56, 71), 1));
            dc.SetBrush(wxBrush(selected ? wxColour(53, 62, 78) : wxColour(29, 35, 44)));
            dc.DrawRoundedRectangle(rect, 6);

            const wxBitmap preview = BuildItemFramePreviewBitmap(WeaponPreviewFrame(weapon), 4, wxColour(18, 22, 28));
            dc.DrawBitmap(preview, rect.x + 10, rect.y + std::max(10, (rect.height - preview.GetHeight()) / 2), true);

            dc.SetTextForeground(wxColour(236, 240, 246));
            dc.SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Segoe UI"));
            dc.DrawText(wxString::FromUTF8(weapon.name.empty() ? weapon.id : weapon.name), rect.x + 88, rect.y + 10);

            dc.SetFont(wxFont(8, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Segoe UI"));
            dc.SetTextForeground(wxColour(180, 186, 198));
            dc.DrawText(WeaponSummaryLabel(weapon), rect.x + 88, rect.y + 32);
        }
    }

    void OnLeftDown(wxMouseEvent& event) {
        if (!weapons_) {
            return;
        }

        const wxPoint point = CalcUnscrolledPosition(event.GetPosition());
        const int width = std::max(GetVirtualSize().GetWidth(), GetClientSize().GetWidth());
        for (size_t i = 0; i < weapons_->size(); ++i) {
            const wxRect rect = ItemRect(static_cast<int>(i), width);
            if (!rect.Contains(point)) {
                continue;
            }
            selectedWeaponId_ = (*weapons_)[i].id;
            Refresh();
            if (onSelectionChanged_) {
                onSelectionChanged_(selectedWeaponId_);
            }
            return;
        }
    }

    void OnLeftDClick(wxMouseEvent& event) {
        OnLeftDown(event);
        if (!selectedWeaponId_.empty() && onEditWeapon_) {
            onEditWeapon_(selectedWeaponId_);
        }
    }

    const std::vector<WeaponDefinition>* weapons_ = nullptr;
    std::string selectedWeaponId_;
    std::function<void(const std::string&)> onSelectionChanged_;
    std::function<void(const std::string&)> onEditWeapon_;
};

class CharacterPalettePanel final : public wxScrolledWindow {
public:
    CharacterPalettePanel(wxWindow* parent)
        : wxScrolledWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_SIMPLE | wxVSCROLL) {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetScrollRate(0, 12);
        SetMinSize(wxSize(280, 320));
        Bind(wxEVT_PAINT, &CharacterPalettePanel::OnPaint, this);
        Bind(wxEVT_LEFT_DOWN, &CharacterPalettePanel::OnLeftDown, this);
        Bind(wxEVT_LEFT_DCLICK, &CharacterPalettePanel::OnLeftDClick, this);
    }

    void SetItems(const std::vector<CharacterSpriteset>* characters) {
        characters_ = characters;
        if (!characters_ || characters_->empty()) {
            selectedCharacterId_.clear();
        } else if (selectedCharacterId_.empty() || FindCharacterIndex(selectedCharacterId_) < 0) {
            selectedCharacterId_ = characters_->front().id;
        }
        RefreshVirtualSize();
        Refresh();
    }

    void SetSelectedCharacterId(const std::string& characterId, bool ensureVisible = false) {
        selectedCharacterId_ = characterId;
        if (ensureVisible) {
            const int index = FindCharacterIndex(selectedCharacterId_);
            if (index >= 0) {
                Scroll(0, std::max(0, index * kRowHeight / 12));
            }
        }
        Refresh();
    }

    const std::string& SelectedCharacterId() const {
        return selectedCharacterId_;
    }

    void SetSelectionChangedCallback(std::function<void(const std::string&)> callback) {
        onSelectionChanged_ = std::move(callback);
    }

    void SetEditCharacterCallback(std::function<void(const std::string&)> callback) {
        onEditCharacter_ = std::move(callback);
    }

private:
    static constexpr int kRowHeight = 86;

    int FindCharacterIndex(const std::string& characterId) const {
        if (!characters_) {
            return -1;
        }
        for (size_t i = 0; i < characters_->size(); ++i) {
            if ((*characters_)[i].id == characterId) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    wxRect ItemRect(int index, int width) const {
        return wxRect(6, 6 + index * kRowHeight, std::max(120, width - 12), kRowHeight - 8);
    }

    void RefreshVirtualSize() {
        const int count = characters_ ? static_cast<int>(characters_->size()) : 0;
        SetVirtualSize(wxSize(std::max(260, GetClientSize().GetWidth()), 12 + count * kRowHeight));
    }

    const CharacterFrame* PreviewFrame(const CharacterSpriteset& spriteset) const {
        const CharacterAction* standingAction = nullptr;
        for (const CharacterAction& action : spriteset.actions) {
            if (action.id == "standing") {
                standingAction = &action;
                break;
            }
        }
        const CharacterAction* action = standingAction ? standingAction : (spriteset.actions.empty() ? nullptr : &spriteset.actions.front());
        if (!action) {
            return nullptr;
        }

        const auto& downFrames = action->directionalFrames[0];
        if (!downFrames.empty()) {
            return &downFrames.front();
        }
        for (const auto& directionalFrames : action->directionalFrames) {
            if (!directionalFrames.empty()) {
                return &directionalFrames.front();
            }
        }
        return nullptr;
    }

    wxBitmap BuildCharacterPreviewBitmap(const CharacterSpriteset& spriteset, int scale, wxColour backColor) const {
        const CharacterFrame* frame = PreviewFrame(spriteset);
        if (!frame) {
            wxBitmap fallback(64, 64);
            wxMemoryDC dc;
            dc.SelectObject(fallback);
            dc.SetBackground(wxBrush(backColor));
            dc.Clear();
            dc.SetBrush(wxBrush(wxColour(88, 108, 140)));
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.DrawRectangle(12, 12, 40, 40);
            dc.SelectObject(wxNullBitmap);
            return fallback;
        }

        const wxBitmap spritesheet = LoadBitmapMaybeRelative(spriteset.imagePath);
        if (!spritesheet.IsOk()) {
            wxBitmap fallback(64, 64);
            wxMemoryDC dc;
            dc.SelectObject(fallback);
            dc.SetBackground(wxBrush(backColor));
            dc.Clear();
            dc.SelectObject(wxNullBitmap);
            return fallback;
        }

        const int tileW = std::max(1, spriteset.tileWidth);
        const int tileH = std::max(1, spriteset.tileHeight);
        const int frameW = std::max(1, frame->frameWidth);
        const int frameH = std::max(1, frame->frameHeight);
        const int srcX = frame->tileX * tileW;
        const int srcY = frame->tileY * tileH;
        const int srcWidth = frameW * tileW;
        const int srcHeight = frameH * tileH;

        if (srcX < 0 || srcY < 0 || srcX + srcWidth > spritesheet.GetWidth() || srcY + srcHeight > spritesheet.GetHeight()) {
            wxBitmap fallback(64, 64);
            wxMemoryDC dc;
            dc.SelectObject(fallback);
            dc.SetBackground(wxBrush(backColor));
            dc.Clear();
            dc.SelectObject(wxNullBitmap);
            return fallback;
        }

        wxImage cropped = spritesheet.ConvertToImage().GetSubImage(wxRect(srcX, srcY, srcWidth, srcHeight));
        const int scaledW = srcWidth * scale;
        const int scaledH = srcHeight * scale;
        cropped = cropped.Scale(scaledW, scaledH, wxIMAGE_QUALITY_NEAREST);
        return wxBitmap(cropped);
    }

    wxBitmap CharacterPreviewBitmap(const CharacterSpriteset& spriteset) const {
        return BuildCharacterPreviewBitmap(spriteset, 2, wxColour(18, 22, 28));
    }

    wxBitmap ScalePreviewToFit(const wxBitmap& bitmap, int maxWidth, int maxHeight) const {
        if (!bitmap.IsOk()) {
            return bitmap;
        }
        if (bitmap.GetWidth() <= maxWidth && bitmap.GetHeight() <= maxHeight) {
            return bitmap;
        }
        const double scaleX = static_cast<double>(maxWidth) / static_cast<double>(std::max(1, bitmap.GetWidth()));
        const double scaleY = static_cast<double>(maxHeight) / static_cast<double>(std::max(1, bitmap.GetHeight()));
        const double scale = std::min(scaleX, scaleY);
        const int width = std::max(1, static_cast<int>(std::floor(bitmap.GetWidth() * scale)));
        const int height = std::max(1, static_cast<int>(std::floor(bitmap.GetHeight() * scale)));
        return wxBitmap(bitmap.ConvertToImage().Scale(width, height, wxIMAGE_QUALITY_NEAREST));
    }

    wxString CharacterSummaryLabel(const CharacterSpriteset& spriteset) const {
        return wxString::Format("%s  actions=%d", wxString::FromUTF8(spriteset.id), static_cast<int>(spriteset.actions.size()));
    }

    void OnPaint(wxPaintEvent&) {
        wxAutoBufferedPaintDC dc(this);
        PrepareDC(dc);
        dc.SetBackground(wxBrush(wxColour(20, 24, 30)));
        dc.Clear();

        if (!characters_ || characters_->empty()) {
            dc.SetTextForeground(wxColour(180, 186, 198));
            dc.DrawText("No character spritesets", 12, 12);
            return;
        }

        const int width = std::max(GetVirtualSize().GetWidth(), GetClientSize().GetWidth());
        dc.SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Segoe UI"));

        for (size_t i = 0; i < characters_->size(); ++i) {
            const CharacterSpriteset& spriteset = (*characters_)[i];
            const wxRect rect = ItemRect(static_cast<int>(i), width);
            const bool selected = spriteset.id == selectedCharacterId_;

            dc.SetPen(selected ? wxPen(wxColour(255, 210, 96), 2) : wxPen(wxColour(46, 56, 71), 1));
            dc.SetBrush(wxBrush(selected ? wxColour(53, 62, 78) : wxColour(29, 35, 44)));
            dc.DrawRoundedRectangle(rect, 6);

            const wxBitmap preview = ScalePreviewToFit(CharacterPreviewBitmap(spriteset), 64, 64);
            dc.DrawBitmap(preview, rect.x + 10, rect.y + std::max(10, (rect.height - preview.GetHeight()) / 2), true);

            dc.SetTextForeground(wxColour(236, 240, 246));
            dc.SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Segoe UI"));
            dc.DrawText(wxString::FromUTF8(spriteset.name.empty() ? spriteset.id : spriteset.name), rect.x + 88, rect.y + 10);

            dc.SetFont(wxFont(8, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Segoe UI"));
            dc.SetTextForeground(wxColour(180, 186, 198));
            dc.DrawText(CharacterSummaryLabel(spriteset), rect.x + 88, rect.y + 32);
        }
    }

    void OnLeftDown(wxMouseEvent& event) {
        if (!characters_) {
            return;
        }

        const wxPoint point = CalcUnscrolledPosition(event.GetPosition());
        const int width = std::max(GetVirtualSize().GetWidth(), GetClientSize().GetWidth());
        for (size_t i = 0; i < characters_->size(); ++i) {
            const wxRect rect = ItemRect(static_cast<int>(i), width);
            if (!rect.Contains(point)) {
                continue;
            }
            selectedCharacterId_ = (*characters_)[i].id;
            Refresh();
            if (onSelectionChanged_) {
                onSelectionChanged_(selectedCharacterId_);
            }
            return;
        }
    }

    void OnLeftDClick(wxMouseEvent& event) {
        OnLeftDown(event);
        if (!selectedCharacterId_.empty() && onEditCharacter_) {
            onEditCharacter_(selectedCharacterId_);
        }
    }

    const std::vector<CharacterSpriteset>* characters_ = nullptr;
    std::string selectedCharacterId_;
    std::function<void(const std::string&)> onSelectionChanged_;
    std::function<void(const std::string&)> onEditCharacter_;
};

class WarpPalettePanel final : public wxScrolledWindow {
public:
    WarpPalettePanel(wxWindow* parent)
        : wxScrolledWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_SIMPLE | wxVSCROLL) {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetScrollRate(0, 12);
        SetMinSize(wxSize(220, 260));
        Bind(wxEVT_PAINT, &WarpPalettePanel::OnPaint, this);
        Bind(wxEVT_LEFT_DOWN, &WarpPalettePanel::OnLeftDown, this);
        Bind(wxEVT_LEFT_DCLICK, &WarpPalettePanel::OnLeftDClick, this);
    }

    void SetItems(const std::vector<WarpDefinition>* warps, const std::vector<TileCollection>* collections) {
        warps_ = warps;
        collections_ = collections;
        if (!warps_ || warps_->empty()) {
            selectedWarpId_.clear();
            selectedEndpointIndex_ = 0;
        }
        RefreshVirtualSize();
        Refresh();
    }

    void SetSelected(const std::string& warpId, int endpointIndex) {
        selectedWarpId_ = warpId;
        selectedEndpointIndex_ = std::clamp(endpointIndex, 0, 1);
        Refresh();
    }

    const std::string& SelectedWarpId() const { return selectedWarpId_; }
    int SelectedEndpointIndex() const { return selectedEndpointIndex_; }

    void SetSelectionChangedCallback(std::function<void(const std::string&, int)> callback) {
        onSelectionChanged_ = std::move(callback);
    }

    void SetEditWarpCallback(std::function<void(const std::string&)> callback) {
        onEditWarp_ = std::move(callback);
    }

private:
    static constexpr int kRowHeight = 90;
    static constexpr int kThumbSize = 40;
    static constexpr int kThumbGap = 8;

    wxRect RowRect(int index, int width) const {
        return wxRect(6, 6 + index * kRowHeight, std::max(140, width - 12), kRowHeight - 6);
    }

    wxRect ThumbRect(const wxRect& row, int ep) const {
        const int thumbX = row.x + 8 + ep * (kThumbSize + kThumbGap);
        const int thumbY = row.y + 38;
        return wxRect(thumbX, thumbY, kThumbSize, kThumbSize);
    }

    wxRect EndpointHitArea(const wxRect& row, int ep) const {
        const wxRect thumb = ThumbRect(row, ep);
        return wxRect(thumb.x - 2, row.y + 22, kThumbSize + 4, thumb.y - (row.y + 22) + kThumbSize + 4);
    }

    void RefreshVirtualSize() {
        const int count = warps_ ? static_cast<int>(warps_->size()) : 0;
        SetVirtualSize(wxSize(std::max(220, GetClientSize().GetWidth()), 12 + count * kRowHeight));
    }

    wxBitmap BuildThumb(int tileId) const {
        wxBitmap bmp(kThumbSize, kThumbSize);
        wxMemoryDC dc;
        dc.SelectObject(bmp);
        dc.SetBackground(wxBrush(wxColour(18, 22, 28)));
        dc.Clear();

        const TileCollection* collection = nullptr;
        const TileDef* tile = nullptr;
        if (collections_) {
            for (const TileCollection& c : *collections_) {
                for (const TileDef& t : c.tiles) {
                    if (t.id == tileId) {
                        collection = &c;
                        tile = &t;
                        break;
                    }
                }
                if (collection) break;
            }
        }

        if (collection && tile) {
            const int tileW = std::max(1, collection->tileWidth);
            const int tileH = std::max(1, collection->tileHeight);
            const int scale = std::max(1, std::min(kThumbSize / tileW, kThumbSize / tileH));
            const int dw = tileW * scale;
            const int dh = tileH * scale;
            const int dx = (kThumbSize - dw) / 2;
            const int dy = (kThumbSize - dh) / 2;
            wxBitmap atlas = LoadBitmapMaybeRelative(collection->imagePath);
            if (atlas.IsOk()) {
                wxMemoryDC atlasDc;
                atlasDc.SelectObject(atlas);
                dc.StretchBlit(dx, dy, dw, dh, &atlasDc, tile->sourceX, tile->sourceY, tileW, tileH);
                atlasDc.SelectObject(wxNullBitmap);
            } else {
                dc.SetBrush(wxBrush(TileColorFromId(tileId)));
                dc.SetPen(*wxTRANSPARENT_PEN);
                dc.DrawRectangle(dx, dy, dw, dh);
            }
        } else {
            dc.SetBrush(wxBrush(wxColour(55, 60, 78)));
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.DrawRectangle(2, 2, kThumbSize - 4, kThumbSize - 4);
            dc.SetTextForeground(wxColour(160, 165, 185));
            dc.SetFont(wxFont(8, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
            dc.DrawText("?", kThumbSize / 2 - 4, kThumbSize / 2 - 6);
        }

        dc.SelectObject(wxNullBitmap);
        return bmp;
    }

    static wxString SpawnOffsetGlyph(WarpSpawnOffset offset) {
        switch (offset) {
            case WarpSpawnOffset::Above:  return wxString::FromUTF8("\xe2\x86\x91");  // ↑
            case WarpSpawnOffset::Below:  return wxString::FromUTF8("\xe2\x86\x93");  // ↓
            case WarpSpawnOffset::Left:   return wxString::FromUTF8("\xe2\x86\x90");  // ←
            case WarpSpawnOffset::Right:  return wxString::FromUTF8("\xe2\x86\x92");  // →
            case WarpSpawnOffset::OnTop:
            default:                      return wxString::FromUTF8("\xe2\x80\xa2");  // •
        }
    }

    void OnPaint(wxPaintEvent&) {
        wxAutoBufferedPaintDC dc(this);
        PrepareDC(dc);
        dc.SetBackground(wxBrush(wxColour(20, 24, 30)));
        dc.Clear();

        if (!warps_ || warps_->empty()) {
            dc.SetTextForeground(wxColour(180, 186, 198));
            dc.DrawText("No warp definitions", 12, 12);
            return;
        }

        const int width = std::max(GetVirtualSize().GetWidth(), GetClientSize().GetWidth());

        for (size_t i = 0; i < warps_->size(); ++i) {
            const WarpDefinition& warp = (*warps_)[i];
            const wxRect row = RowRect(static_cast<int>(i), width);
            const bool rowSel = warp.id == selectedWarpId_;

            dc.SetPen(rowSel ? wxPen(wxColour(255, 210, 96), 2) : wxPen(wxColour(46, 56, 71), 1));
            dc.SetBrush(wxBrush(rowSel ? wxColour(53, 62, 78) : wxColour(29, 35, 44)));
            dc.DrawRoundedRectangle(row, 6);

            dc.SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Segoe UI"));
            dc.SetTextForeground(wxColour(236, 240, 246));
            dc.DrawText(wxString::FromUTF8(warp.name.empty() ? warp.id : warp.name), row.x + 8, row.y + 5);

            for (int ep = 0; ep < 2; ++ep) {
                const wxRect thumb = ThumbRect(row, ep);
                const bool epSel = rowSel && ep == selectedEndpointIndex_;
                const WarpSpawnOffset spawnOffset = warp.endpoints[static_cast<size_t>(ep)].spawnOffset;

                dc.SetFont(wxFont(8, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Segoe UI"));
                dc.SetTextForeground(epSel ? wxColour(255, 210, 96) : wxColour(155, 165, 185));
                dc.DrawText(ep == 0 ? "A" : "B", thumb.x, row.y + 24);

                dc.SetPen(epSel ? wxPen(wxColour(255, 210, 96), 2) : wxPen(wxColour(75, 85, 105), 1));
                dc.SetBrush(*wxTRANSPARENT_BRUSH);
                dc.DrawRectangle(thumb.x - 2, thumb.y - 2, kThumbSize + 4, kThumbSize + 4);

                dc.DrawBitmap(BuildThumb(warp.endpoints[static_cast<size_t>(ep)].tileId), thumb.x, thumb.y, false);

                // Spawn offset badge: small pill in bottom-right corner of thumb
                const wxString glyph = SpawnOffsetGlyph(spawnOffset);
                dc.SetFont(wxFont(9, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
                const wxSize glyphSz = dc.GetTextExtent(glyph);
                const int badgeW = glyphSz.GetWidth() + 6;
                const int badgeH = glyphSz.GetHeight() + 2;
                const int badgeX = thumb.x + kThumbSize - badgeW + 2;
                const int badgeY = thumb.y + kThumbSize - badgeH + 2;
                dc.SetBrush(wxBrush(wxColour(30, 36, 48)));
                dc.SetPen(wxPen(epSel ? wxColour(255, 210, 96) : wxColour(100, 120, 160), 1));
                dc.DrawRoundedRectangle(badgeX, badgeY, badgeW, badgeH, 3);
                dc.SetTextForeground(epSel ? wxColour(255, 210, 96) : wxColour(160, 200, 255));
                dc.DrawText(glyph, badgeX + 3, badgeY + 1);
            }
        }
    }

    void OnLeftDown(wxMouseEvent& event) {
        if (!warps_) return;
        const wxPoint pt = CalcUnscrolledPosition(event.GetPosition());
        const int width = std::max(GetVirtualSize().GetWidth(), GetClientSize().GetWidth());
        for (size_t i = 0; i < warps_->size(); ++i) {
            const wxRect row = RowRect(static_cast<int>(i), width);
            if (!row.Contains(pt)) continue;
            const WarpDefinition& warp = (*warps_)[i];
            int clickedEp = -1;
            for (int ep = 0; ep < 2; ++ep) {
                if (EndpointHitArea(row, ep).Contains(pt)) {
                    clickedEp = ep;
                    break;
                }
            }
            if (clickedEp < 0) {
                clickedEp = (warp.id == selectedWarpId_) ? selectedEndpointIndex_ : 0;
            }
            selectedWarpId_ = warp.id;
            selectedEndpointIndex_ = clickedEp;
            Refresh();
            if (onSelectionChanged_) onSelectionChanged_(selectedWarpId_, selectedEndpointIndex_);
            return;
        }
    }

    void OnLeftDClick(wxMouseEvent& event) {
        OnLeftDown(event);
        if (!selectedWarpId_.empty() && onEditWarp_) {
            onEditWarp_(selectedWarpId_);
        }
    }

    const std::vector<WarpDefinition>* warps_ = nullptr;
    const std::vector<TileCollection>* collections_ = nullptr;
    std::string selectedWarpId_;
    int selectedEndpointIndex_ = 0;
    std::function<void(const std::string&, int)> onSelectionChanged_;
    std::function<void(const std::string&)> onEditWarp_;
};

class SpriteGridPanel final : public wxScrolledWindow {
public:
    SpriteGridPanel(wxWindow* parent)
        : wxScrolledWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_SIMPLE | wxVSCROLL) {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetScrollRate(0, 12);
        Bind(wxEVT_PAINT, &SpriteGridPanel::OnPaint, this);
        Bind(wxEVT_LEFT_DOWN, &SpriteGridPanel::OnLeftDown, this);
        Bind(wxEVT_LEFT_DCLICK, &SpriteGridPanel::OnLeftDClick, this);
    }

    void SetCollection(const SheetSpriteCollectionDef* collection) {
        collection_ = collection;
        selectedIndex_ = (collection_ && !collection_->sprites.empty()) ? 0 : -1;
        RefreshVirtualSize();
        Refresh();
        if (onSelectionChanged_) {
            onSelectionChanged_(selectedIndex_);
        }
    }

    void SetSelectionChangedCallback(std::function<void(int)> cb) {
        onSelectionChanged_ = std::move(cb);
    }

    void SetItemActivatedCallback(std::function<void(int)> cb) {
        onItemActivated_ = std::move(cb);
    }

    void SetSelectedIndex(int index) {
        if (!collection_ || collection_->sprites.empty()) {
            selectedIndex_ = -1;
            Refresh();
            return;
        }
        selectedIndex_ = std::clamp(index, 0, static_cast<int>(collection_->sprites.size()) - 1);
        ScrollSpriteIntoView(selectedIndex_);
        Refresh();
        if (onSelectionChanged_) {
            onSelectionChanged_(selectedIndex_);
        }
    }

    int SelectedIndex() const {
        return selectedIndex_;
    }

private:
    int GridColumns() const {
        const int cell = 44;
        const int leftPadding = 4;
        const wxSize client = GetClientSize();
        if (client.GetWidth() <= leftPadding) {
            return 1;
        }
        return std::max(1, (client.GetWidth() - leftPadding) / cell);
    }

    void RefreshVirtualSize() {
        const int columns = GridColumns();
        const int cell = 44;
        const int count = collection_ ? static_cast<int>(collection_->sprites.size()) : 0;
        const int rows = std::max(1, (count + columns - 1) / columns);
        SetVirtualSize(columns * cell + 8, rows * cell + 8);
    }

    wxRect SpriteRect(int index) const {
        const int columns = GridColumns();
        const int cell = 44;
        const int col = index % columns;
        const int row = index / columns;
        return wxRect(4 + col * cell, 4 + row * cell, 40, 40);
    }

    void ScrollSpriteIntoView(int index) {
        if (!collection_ || index < 0 || index >= static_cast<int>(collection_->sprites.size())) {
            return;
        }

        const wxRect rect = SpriteRect(index);
        int xUnit = 0;
        int yUnit = 0;
        GetScrollPixelsPerUnit(&xUnit, &yUnit);
        yUnit = std::max(1, yUnit);

        int viewXUnits = 0;
        int viewYUnits = 0;
        GetViewStart(&viewXUnits, &viewYUnits);

        const int viewTop = viewYUnits * yUnit;
        const int viewBottom = viewTop + GetClientSize().GetHeight();

        int targetTop = viewTop;
        if (rect.GetTop() < viewTop) {
            targetTop = rect.GetTop();
        } else if (rect.GetBottom() > viewBottom) {
            targetTop = rect.GetBottom() - GetClientSize().GetHeight();
        }

        const int maxTop = std::max(0, GetVirtualSize().GetHeight() - GetClientSize().GetHeight());
        targetTop = std::clamp(targetTop, 0, maxTop);

        if (targetTop != viewTop) {
            Scroll(viewXUnits, targetTop / yUnit);
        }
    }

    void OnLeftDown(wxMouseEvent& event) {
        if (!collection_) {
            return;
        }

        SelectSpriteAtPosition(event.GetPosition());
    }

    void OnLeftDClick(wxMouseEvent& event) {
        if (!collection_) {
            return;
        }

        if (SelectSpriteAtPosition(event.GetPosition()) && onItemActivated_) {
            onItemActivated_(selectedIndex_);
        }
    }

    bool SelectSpriteAtPosition(const wxPoint& windowPoint) {
        if (!collection_) {
            return false;
        }

        const wxPoint point = CalcUnscrolledPosition(windowPoint);
        for (size_t i = 0; i < collection_->sprites.size(); ++i) {
            if (!SpriteRect(static_cast<int>(i)).Contains(point)) {
                continue;
            }
            selectedIndex_ = static_cast<int>(i);
            ScrollSpriteIntoView(selectedIndex_);
            Refresh();
            if (onSelectionChanged_) {
                onSelectionChanged_(selectedIndex_);
            }
            return true;
        }
        return false;
    }

    void OnPaint(wxPaintEvent&) {
        wxAutoBufferedPaintDC dc(this);
        PrepareDC(dc);
        dc.SetBackground(wxBrush(wxColour(19, 24, 31)));
        dc.Clear();

        if (!collection_) {
            dc.SetTextForeground(wxColour(190, 196, 208));
            dc.DrawText("No sprites in this library", 8, 8);
            return;
        }

        for (size_t i = 0; i < collection_->sprites.size(); ++i) {
            const wxRect rect = SpriteRect(static_cast<int>(i));
            dc.SetPen(wxPen(wxColour(72, 84, 102), 1));
            dc.SetBrush(wxBrush(wxColour(31, 36, 45)));
            dc.DrawRectangle(rect);

            if (collection_->atlas.IsOk()) {
                wxMemoryDC atlasDc;
                wxBitmap bitmapCopy;
                bitmapCopy = collection_->atlas;
                atlasDc.SelectObject(bitmapCopy);
                const SheetSpriteDef& sprite = collection_->sprites[i];
                dc.StretchBlit(
                    rect.x + 4,
                    rect.y + 4,
                    32,
                    32,
                    &atlasDc,
                    sprite.sourceX,
                    sprite.sourceY,
                    collection_->tileWidth,
                    collection_->tileHeight
                );
                atlasDc.SelectObject(wxNullBitmap);
            }

            if (static_cast<int>(i) == selectedIndex_) {
                dc.SetPen(wxPen(wxColour(255, 215, 88), 2));
                dc.SetBrush(*wxTRANSPARENT_BRUSH);
                dc.DrawRectangle(rect);
            }
        }
    }

private:
    const SheetSpriteCollectionDef* collection_ = nullptr;
    int selectedIndex_ = -1;
    std::function<void(int)> onSelectionChanged_;
    std::function<void(int)> onItemActivated_;
};

class SpriteLibraryPickerDialog final : public wxDialog {
public:
    SpriteLibraryPickerDialog(
        wxWindow* parent,
        const std::vector<SheetSpriteCollectionDef>& collections,
        const wxString& title = "Pick Sprite"
    )
        : wxDialog(parent, wxID_ANY, title, wxDefaultPosition, wxSize(860, 640), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
          collections_(collections) {
        auto* root = new wxBoxSizer(wxVERTICAL);

        auto* topRow = new wxBoxSizer(wxHORIZONTAL);
        topRow->Add(new wxStaticText(this, wxID_ANY, "Library"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
        collectionChoice_ = new wxChoice(this, wxID_ANY);
        topRow->Add(collectionChoice_, 1, wxEXPAND);
        root->Add(topRow, 0, wxEXPAND | wxALL, 8);

        auto* middle = new wxBoxSizer(wxHORIZONTAL);
        spriteGrid_ = new SpriteGridPanel(this);
        spriteGrid_->SetMinSize(wxSize(560, 420));
        middle->Add(spriteGrid_, 1, wxEXPAND | wxRIGHT, 8);

        auto* previewBox = new wxStaticBoxSizer(wxVERTICAL, this, "Preview");
        previewPanel_ = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(200, 200));
        previewPanel_->SetMinSize(wxSize(200, 200));
        previewPanel_->SetBackgroundStyle(wxBG_STYLE_PAINT);
        previewBox->Add(previewPanel_, 0, wxEXPAND | wxALL, 6);
        infoText_ = new wxStaticText(this, wxID_ANY, "No sprite selected");
        previewBox->Add(infoText_, 0, wxLEFT | wxRIGHT | wxBOTTOM, 6);
        middle->Add(previewBox, 0, wxEXPAND);

        root->Add(middle, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

        auto* buttons = new wxStdDialogButtonSizer();
        okButton_ = new wxButton(this, wxID_OK, "OK");
        buttons->AddButton(okButton_);
        buttons->AddButton(new wxButton(this, wxID_CANCEL, "Cancel"));
        buttons->Realize();
        root->Add(buttons, 0, wxALIGN_RIGHT | wxALL, 8);

        SetSizer(root);

        for (const SheetSpriteCollectionDef& collection : collections_) {
            const wxString label = collection.name.empty() ? wxString::FromUTF8(collection.id) : wxString::FromUTF8(collection.name);
            collectionChoice_->Append(label);
        }

        if (!collections_.empty()) {
            int initialCollectionIndex = 0;
            if (!g_lastSelectedSpriteLibraryCollectionId.empty()) {
                for (size_t i = 0; i < collections_.size(); ++i) {
                    if (collections_[i].id == g_lastSelectedSpriteLibraryCollectionId) {
                        initialCollectionIndex = static_cast<int>(i);
                        break;
                    }
                }
            }

            collectionChoice_->SetSelection(initialCollectionIndex);
            selectedCollectionIndex_ = initialCollectionIndex;
            spriteGrid_->SetCollection(&collections_[static_cast<size_t>(initialCollectionIndex)]);
            spriteGrid_->SetSelectedIndex(0);
            selectedSpriteIndex_ = spriteGrid_->SelectedIndex();
        } else {
            spriteGrid_->SetCollection(nullptr);
            selectedCollectionIndex_ = -1;
            selectedSpriteIndex_ = -1;
        }

        collectionChoice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
            selectedCollectionIndex_ = collectionChoice_->GetSelection();
            if (selectedCollectionIndex_ >= 0 && selectedCollectionIndex_ < static_cast<int>(collections_.size())) {
                g_lastSelectedSpriteLibraryCollectionId = collections_[static_cast<size_t>(selectedCollectionIndex_)].id;
                spriteGrid_->SetCollection(&collections_[static_cast<size_t>(selectedCollectionIndex_)]);
                spriteGrid_->SetSelectedIndex(0);
                selectedSpriteIndex_ = spriteGrid_->SelectedIndex();
            } else {
                spriteGrid_->SetCollection(nullptr);
                selectedSpriteIndex_ = -1;
            }
            UpdatePreviewInfo();
            UpdateOkEnabled();
        });

        spriteGrid_->SetSelectionChangedCallback([this](int index) {
            selectedSpriteIndex_ = index;
            UpdatePreviewInfo();
            UpdateOkEnabled();
        });

        spriteGrid_->SetItemActivatedCallback([this](int) {
            if (okButton_ && okButton_->IsEnabled()) {
                wxCommandEvent okEvent(wxEVT_BUTTON, wxID_OK);
                okEvent.SetEventObject(okButton_);
                ProcessWindowEvent(okEvent);
            }
        });

        previewPanel_->Bind(wxEVT_PAINT, &SpriteLibraryPickerDialog::OnPreviewPaint, this);

        UpdatePreviewInfo();
        UpdateOkEnabled();
    }

    bool GetSelectedPick(SheetSpritePick& out) const {
        if (selectedCollectionIndex_ < 0 || selectedCollectionIndex_ >= static_cast<int>(collections_.size())) {
            return false;
        }

        const SheetSpriteCollectionDef& collection = collections_[static_cast<size_t>(selectedCollectionIndex_)];
        if (selectedSpriteIndex_ < 0 || selectedSpriteIndex_ >= static_cast<int>(collection.sprites.size())) {
            return false;
        }

        const SheetSpriteDef& sprite = collection.sprites[static_cast<size_t>(selectedSpriteIndex_)];
        out.collectionId = collection.id;
        out.collectionName = collection.name;
        out.sourceImagePath = collection.sourceImagePath;
        out.sourceX = sprite.sourceX;
        out.sourceY = sprite.sourceY;
        out.width = collection.tileWidth;
        out.height = collection.tileHeight;
        return true;
    }

private:
    void UpdateOkEnabled() {
        if (!okButton_) {
            return;
        }
        okButton_->Enable(selectedCollectionIndex_ >= 0 && selectedSpriteIndex_ >= 0);
    }

    void UpdatePreviewInfo() {
        if (!infoText_) {
            return;
        }
        if (selectedCollectionIndex_ < 0 || selectedCollectionIndex_ >= static_cast<int>(collections_.size())) {
            infoText_->SetLabel("No sprite selected");
            if (previewPanel_) {
                previewPanel_->Refresh();
            }
            return;
        }

        const SheetSpriteCollectionDef& collection = collections_[static_cast<size_t>(selectedCollectionIndex_)];
        if (selectedSpriteIndex_ < 0 || selectedSpriteIndex_ >= static_cast<int>(collection.sprites.size())) {
            infoText_->SetLabel("No sprite selected");
            if (previewPanel_) {
                previewPanel_->Refresh();
            }
            return;
        }

        const SheetSpriteDef& sprite = collection.sprites[static_cast<size_t>(selectedSpriteIndex_)];
        infoText_->SetLabel(wxString::Format(
            "%s  (%d, %d)",
            wxString::FromUTF8(collection.name),
            sprite.sourceX / std::max(1, collection.tileWidth),
            sprite.sourceY / std::max(1, collection.tileHeight)
        ));
        if (previewPanel_) {
            previewPanel_->Refresh();
        }
    }

    void OnPreviewPaint(wxPaintEvent&) {
        wxAutoBufferedPaintDC dc(previewPanel_);
        dc.SetBackground(wxBrush(wxColour(19, 24, 31)));
        dc.Clear();

        if (selectedCollectionIndex_ < 0 || selectedCollectionIndex_ >= static_cast<int>(collections_.size())) {
            return;
        }
        const SheetSpriteCollectionDef& collection = collections_[static_cast<size_t>(selectedCollectionIndex_)];
        if (selectedSpriteIndex_ < 0 || selectedSpriteIndex_ >= static_cast<int>(collection.sprites.size())) {
            return;
        }
        if (!collection.atlas.IsOk()) {
            return;
        }

        const SheetSpriteDef& sprite = collection.sprites[static_cast<size_t>(selectedSpriteIndex_)];
        const int scale = 8;
        const int dstW = collection.tileWidth * scale;
        const int dstH = collection.tileHeight * scale;
        const int dstX = std::max(8, (previewPanel_->GetClientSize().GetWidth() - dstW) / 2);
        const int dstY = std::max(8, (previewPanel_->GetClientSize().GetHeight() - dstH) / 2);

        wxMemoryDC atlasDc;
        wxBitmap bitmapCopy;
        bitmapCopy = collection.atlas;
        atlasDc.SelectObject(bitmapCopy);
        dc.StretchBlit(
            dstX,
            dstY,
            dstW,
            dstH,
            &atlasDc,
            sprite.sourceX,
            sprite.sourceY,
            collection.tileWidth,
            collection.tileHeight
        );
        atlasDc.SelectObject(wxNullBitmap);
    }

private:
    const std::vector<SheetSpriteCollectionDef>& collections_;
    wxChoice* collectionChoice_ = nullptr;
    SpriteGridPanel* spriteGrid_ = nullptr;
    wxPanel* previewPanel_ = nullptr;
    wxStaticText* infoText_ = nullptr;
    wxButton* okButton_ = nullptr;
    int selectedCollectionIndex_ = -1;
    int selectedSpriteIndex_ = -1;
};

class CharacterSpritesetEditorDialog final : public wxDialog {
public:
    CharacterSpritesetEditorDialog(wxWindow* parent, CharacterSpriteset& spriteset, const std::vector<WeaponDefinition>& weaponDefinitions)
        : wxDialog(parent, wxID_ANY, "Edit Character Spriteset", wxDefaultPosition, wxSize(980, 720), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
          spriteset_(spriteset),
          weaponDefinitions_(weaponDefinitions) {
        atlas_ = LoadBitmapMaybeRelative(spriteset_.imagePath);
        EnsureCoreActions();

        wxBoxSizer* root = new wxBoxSizer(wxVERTICAL);

        wxFlexGridSizer* meta = new wxFlexGridSizer(2, 4, 6, 6);
        meta->AddGrowableCol(1, 1);
        meta->Add(new wxStaticText(this, wxID_ANY, "Name"), 0, wxALIGN_CENTER_VERTICAL);
        nameCtrl_ = new wxTextCtrl(this, wxID_ANY, wxString::FromUTF8(spriteset_.name));
        meta->Add(nameCtrl_, 1, wxEXPAND);
        meta->Add(new wxStaticText(this, wxID_ANY, "Description"), 0, wxALIGN_CENTER_VERTICAL);
        descCtrl_ = new wxTextCtrl(this, wxID_ANY, wxString::FromUTF8(spriteset_.description));
        meta->Add(descCtrl_, 1, wxEXPAND);
        root->Add(meta, 0, wxEXPAND | wxALL, 8);

        wxBoxSizer* actionRow = new wxBoxSizer(wxHORIZONTAL);
        actionRow->Add(new wxStaticText(this, wxID_ANY, "Action"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
        actionChoice_ = new wxChoice(this, wxID_ANY);
        actionRow->Add(actionChoice_, 1, wxRIGHT, 16);
        actionRow->Add(new wxStaticText(this, wxID_ANY, "Direction"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
        directionChoice_ = new wxChoice(this, wxID_ANY);
        directionChoice_->Append("S");
        directionChoice_->Append("W");
        directionChoice_->Append("E");
        directionChoice_->Append("N");
        directionChoice_->SetSelection(0);
        actionRow->Add(directionChoice_, 0, wxRIGHT, 16);
        actionRow->Add(new wxStaticText(this, wxID_ANY, "Animation Speed (fps)"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
        speedCtrl_ = new wxSpinCtrlDouble(this, wxID_ANY);
        speedCtrl_->SetDigits(2);
        speedCtrl_->SetRange(0.1, 30.0);
        speedCtrl_->SetIncrement(0.1);
        actionRow->Add(speedCtrl_, 0);
        actionHitboxBtn_ = new wxButton(this, wxID_ANY, "Edit Attack Hitboxes...");
        actionRow->Add(actionHitboxBtn_, 0, wxLEFT, 10);
        actionPlayerHitboxBtn_ = new wxButton(this, wxID_ANY, "Edit Player Hitboxes...");
        actionRow->Add(actionPlayerHitboxBtn_, 0, wxLEFT, 8);
        root->Add(actionRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

        wxBoxSizer* center = new wxBoxSizer(wxHORIZONTAL);

        wxBoxSizer* leftCol = new wxBoxSizer(wxVERTICAL);
        leftCol->Add(new wxStaticText(this, wxID_ANY, "Spritesheet (click to set frame tile origin)"), 0, wxBOTTOM, 4);
        sheetPanel_ = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(420, 420));
        sheetPanel_->SetBackgroundStyle(wxBG_STYLE_PAINT);
        leftCol->Add(sheetPanel_, 1, wxEXPAND);
        center->Add(leftCol, 1, wxEXPAND | wxRIGHT, 8);

        wxBoxSizer* rightCol = new wxBoxSizer(wxVERTICAL);
        rightCol->Add(new wxStaticText(this, wxID_ANY, "Frames (drag to reorder)"), 0, wxBOTTOM, 4);
        frameList_ = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxSize(340, 240), wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_NO_HEADER);
        frameList_->InsertColumn(0, "Frame", wxLIST_FORMAT_LEFT, 320);
        rightCol->Add(frameList_, 1, wxEXPAND | wxBOTTOM, 8);

        wxBoxSizer* frameButtons = new wxBoxSizer(wxHORIZONTAL);
        wxButton* addFrameBtn = new wxButton(this, wxID_ANY, "Add Frame");
        wxButton* removeFrameBtn = new wxButton(this, wxID_ANY, "Remove Frame");
        frameButtons->Add(addFrameBtn, 1, wxRIGHT, 6);
        frameButtons->Add(removeFrameBtn, 1);
        rightCol->Add(frameButtons, 0, wxEXPAND | wxBOTTOM, 8);

        wxFlexGridSizer* frameGrid = new wxFlexGridSizer(2, 4, 6, 6);
        frameGrid->Add(new wxStaticText(this, wxID_ANY, "Tile X"), 0, wxALIGN_CENTER_VERTICAL);
        frameTileX_ = new wxSpinCtrl(this, wxID_ANY);
        frameTileX_->SetRange(0, 2048);
        frameGrid->Add(frameTileX_, 1, wxEXPAND);
        frameGrid->Add(new wxStaticText(this, wxID_ANY, "Tile Y"), 0, wxALIGN_CENTER_VERTICAL);
        frameTileY_ = new wxSpinCtrl(this, wxID_ANY);
        frameTileY_->SetRange(0, 2048);
        frameGrid->Add(frameTileY_, 1, wxEXPAND);
        frameGrid->Add(new wxStaticText(this, wxID_ANY, "Frame W (tiles)"), 0, wxALIGN_CENTER_VERTICAL);
        frameW_ = new wxSpinCtrl(this, wxID_ANY);
        frameW_->SetRange(1, 8);
        frameGrid->Add(frameW_, 1, wxEXPAND);
        frameGrid->Add(new wxStaticText(this, wxID_ANY, "Frame H (tiles)"), 0, wxALIGN_CENTER_VERTICAL);
        frameH_ = new wxSpinCtrl(this, wxID_ANY);
        frameH_->SetRange(1, 8);
        frameGrid->Add(frameH_, 1, wxEXPAND);
        rightCol->Add(frameGrid, 0, wxEXPAND | wxBOTTOM, 8);

        rightCol->Add(new wxStaticText(this, wxID_ANY, "Animation Preview"), 0, wxBOTTOM, 4);
        previewPanel_ = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(340, 160));
        previewPanel_->SetBackgroundStyle(wxBG_STYLE_PAINT);
        rightCol->Add(previewPanel_, 0, wxEXPAND);

        center->Add(rightCol, 0, wxEXPAND);
        root->Add(center, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

        wxBoxSizer* buttons = new wxBoxSizer(wxHORIZONTAL);
        buttons->Add(new wxButton(this, wxID_OK, "OK"), 0, wxRIGHT, 6);
        buttons->Add(new wxButton(this, wxID_CANCEL, "Cancel"), 0);
        root->Add(buttons, 0, wxALIGN_RIGHT | wxALL, 8);

        SetSizer(root);

        PopulateActionChoice();
        RefreshFrameList();
        SelectFrame(0);

        actionChoice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
            previewElapsed_ = 0.0f;
            previewFrameIndex_ = 0;
            RefreshActionControls();
            RefreshFrameList();
            SelectFrame(0);
            if (previewPanel_) {
                previewPanel_->Refresh();
            }
        });
        directionChoice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
            previewElapsed_ = 0.0f;
            previewFrameIndex_ = 0;
            RefreshFrameList();
            SelectFrame(0);
            if (sheetPanel_) {
                sheetPanel_->Refresh();
            }
            if (previewPanel_) {
                previewPanel_->Refresh();
            }
        });
        speedCtrl_->Bind(wxEVT_SPINCTRLDOUBLE, [this](wxSpinDoubleEvent&) {
            CharacterAction* action = CurrentAction();
            if (action) {
                action->animationSpeed = static_cast<float>(speedCtrl_->GetValue());
            }
        });
        actionHitboxBtn_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            EditCurrentActionHitboxes();
        });
        actionPlayerHitboxBtn_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            EditCurrentActionPlayerHitboxes();
        });

        addFrameBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            CharacterAction* action = CurrentAction();
            if (!action) {
                return;
            }
            std::vector<CharacterFrame>* frames = FramesForCurrentDirection(*action);
            if (!frames) {
                return;
            }
            CharacterFrame frame;
            if (!frames->empty()) {
                frame = frames->back();
            }
            frames->push_back(frame);
            RefreshFrameList();
            SelectFrame(static_cast<int>(frames->size()) - 1);
            if (previewPanel_) {
                previewPanel_->Refresh();
            }
        });

        removeFrameBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            CharacterAction* action = CurrentAction();
            const int index = SelectedFrameIndex();
            std::vector<CharacterFrame>* frames = action ? FramesForCurrentDirection(*action) : nullptr;
            if (!frames || index < 0 || index >= static_cast<int>(frames->size())) {
                return;
            }
            if (frames->size() <= 1) {
                return;
            }
            frames->erase(frames->begin() + index);
            RefreshFrameList();
            SelectFrame(std::max(0, index - 1));
            if (previewPanel_) {
                previewPanel_->Refresh();
            }
        });

        frameList_->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent&) {
            SyncFrameControlsFromSelection();
            if (sheetPanel_) {
                sheetPanel_->Refresh();
            }
        });

        frameList_->Bind(wxEVT_LIST_BEGIN_DRAG, [this](wxListEvent& event) {
            dragSourceIndex_ = static_cast<int>(event.GetIndex());
            frameList_->CaptureMouse();
        });

        frameList_->Bind(wxEVT_LEFT_UP, [this](wxMouseEvent& event) {
            if (dragSourceIndex_ >= 0) {
                int flags = 0;
                const long target = frameList_->HitTest(event.GetPosition(), flags);
                if (target >= 0) {
                    ReorderFrame(dragSourceIndex_, static_cast<int>(target));
                }
                dragSourceIndex_ = -1;
                if (frameList_->HasCapture()) {
                    frameList_->ReleaseMouse();
                }
            }
            event.Skip();
        });

        auto onFrameCtrl = [this](wxCommandEvent&) { UpdateSelectedFrameFromControls(); };
        frameTileX_->Bind(wxEVT_SPINCTRL, onFrameCtrl);
        frameTileY_->Bind(wxEVT_SPINCTRL, onFrameCtrl);
        frameW_->Bind(wxEVT_SPINCTRL, onFrameCtrl);
        frameH_->Bind(wxEVT_SPINCTRL, onFrameCtrl);

        sheetPanel_->Bind(wxEVT_PAINT, &CharacterSpritesetEditorDialog::OnSheetPaint, this);
        sheetPanel_->Bind(wxEVT_LEFT_DOWN, &CharacterSpritesetEditorDialog::OnSheetClick, this);
        previewPanel_->Bind(wxEVT_PAINT, &CharacterSpritesetEditorDialog::OnPreviewPaint, this);

        previewTimer_.Bind(wxEVT_TIMER, [this](wxTimerEvent&) {
            CharacterAction* action = CurrentAction();
            std::vector<CharacterFrame>* frames = action ? FramesForCurrentDirection(*action) : nullptr;
            if (!frames || frames->empty()) {
                return;
            }
            const float speed = std::max(0.1f, action->animationSpeed);
            previewElapsed_ += 0.016f;
            const float frameDuration = 1.0f / speed;
            if (previewElapsed_ >= frameDuration) {
                previewElapsed_ = 0.0f;
                previewFrameIndex_ = (previewFrameIndex_ + 1) % static_cast<int>(frames->size());
                if (previewPanel_) {
                    previewPanel_->Refresh();
                }
            }
        });
        previewTimer_.Start(16);

        Bind(wxEVT_BUTTON, [this](wxCommandEvent& e) {
            if (e.GetId() == wxID_OK) {
                spriteset_.name = nameCtrl_->GetValue().ToStdString();
                spriteset_.description = descCtrl_->GetValue().ToStdString();
            }
            EndModal(e.GetId());
        });
    }

private:
    void EnsureCoreActions() {
        spriteset_.actions.erase(
            std::remove_if(spriteset_.actions.begin(), spriteset_.actions.end(), [](const CharacterAction& action) {
                return action.id == "sword_slash" || action.id == "projectile_fire";
            }),
            spriteset_.actions.end()
        );

        auto ensureAction = [this](const std::string& id, const std::string& name, float fps) {
            for (CharacterAction& action : spriteset_.actions) {
                if (action.id == id) {
                    for (int dir = 0; dir < 4; ++dir) {
                        auto& frames = action.directionalFrames[static_cast<size_t>(dir)];
                        if (frames.empty()) {
                            frames.push_back(CharacterFrame{});
                        }
                    }
                    return;
                }
            }
            CharacterAction action;
            action.id = id;
            action.name = name;
            action.animationSpeed = fps;
            for (int dir = 0; dir < 4; ++dir) {
                action.directionalFrames[static_cast<size_t>(dir)].push_back(CharacterFrame{});
            }
            spriteset_.actions.push_back(action);
        };

        ensureAction("standing", "Standing", 1.0f);
        ensureAction("walking", "Walking", 8.0f);
        ensureAction("knockback", "Knockback", 10.0f);
        ensureAction("item pickup", "Item Pickup", 6.0f);
        for (const WeaponDefinition& weapon : weaponDefinitions_) {
            const std::string actionName = weapon.name.empty() ? weapon.id : weapon.name;
            ensureAction(weapon.id, actionName, 10.0f);
        }
    }

    CharacterAction* CurrentAction() {
        const int selection = actionChoice_ ? actionChoice_->GetSelection() : wxNOT_FOUND;
        if (selection == wxNOT_FOUND || selection < 0 || selection >= static_cast<int>(spriteset_.actions.size())) {
            return nullptr;
        }
        return &spriteset_.actions[static_cast<size_t>(selection)];
    }

    const CharacterAction* CurrentAction() const {
        const int selection = actionChoice_ ? actionChoice_->GetSelection() : wxNOT_FOUND;
        if (selection == wxNOT_FOUND || selection < 0 || selection >= static_cast<int>(spriteset_.actions.size())) {
            return nullptr;
        }
        return &spriteset_.actions[static_cast<size_t>(selection)];
    }

    int SelectedFrameIndex() const {
        if (!frameList_) {
            return -1;
        }
        const long index = frameList_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
        return index >= 0 ? static_cast<int>(index) : -1;
    }

    CharacterFrame* SelectedFrame() {
        CharacterAction* action = CurrentAction();
        const int index = SelectedFrameIndex();
        std::vector<CharacterFrame>* frames = action ? FramesForCurrentDirection(*action) : nullptr;
        if (!frames || index < 0 || index >= static_cast<int>(frames->size())) {
            return nullptr;
        }
        return &(*frames)[static_cast<size_t>(index)];
    }

    std::vector<CharacterFrame>* FramesForCurrentDirection(CharacterAction& action) {
        const int dir = directionChoice_ ? directionChoice_->GetSelection() : 0;
        const int clamped = std::clamp(dir, 0, 3);
        return &action.directionalFrames[static_cast<size_t>(clamped)];
    }

    const std::vector<CharacterFrame>* FramesForCurrentDirection(const CharacterAction& action) const {
        const int dir = directionChoice_ ? directionChoice_->GetSelection() : 0;
        const int clamped = std::clamp(dir, 0, 3);
        return &action.directionalFrames[static_cast<size_t>(clamped)];
    }

    void PopulateActionChoice() {
        if (!actionChoice_) {
            return;
        }
        actionChoice_->Clear();
        for (const CharacterAction& action : spriteset_.actions) {
            actionChoice_->Append(wxString::FromUTF8(action.name));
        }
        if (!spriteset_.actions.empty()) {
            actionChoice_->SetSelection(0);
        }
        RefreshActionControls();
    }

    bool IsWeaponAction(const std::string& actionId) const {
        for (const WeaponDefinition& weapon : weaponDefinitions_) {
            if (weapon.id == actionId) {
                return true;
            }
        }
        return false;
    }

    void RefreshActionControls() {
        CharacterAction* action = CurrentAction();
        if (!action || !speedCtrl_) {
            return;
        }
        speedCtrl_->SetValue(action->animationSpeed);
        if (actionHitboxBtn_) {
            const int directionIndex = std::clamp(directionChoice_ ? directionChoice_->GetSelection() : 0, 0, 3);
            const bool isWeapon = IsWeaponAction(action->id);
            const int hitboxCount = static_cast<int>(action->directionalHitboxes[static_cast<size_t>(directionIndex)].size());
            actionHitboxBtn_->SetLabel(wxString::Format("Edit Attack Hitboxes... (%d)", hitboxCount));
            actionHitboxBtn_->Enable(isWeapon);
        }
        if (actionPlayerHitboxBtn_) {
            actionPlayerHitboxBtn_->SetLabel(wxString::Format("Edit Player Hitboxes... (%d)", static_cast<int>(action->hitboxes.size())));
        }
    }

    void EditCurrentActionHitboxes() {
        CharacterAction* action = CurrentAction();
        if (!action || !IsWeaponAction(action->id)) {
            return;
        }

        const int directionIndex = std::clamp(directionChoice_ ? directionChoice_->GetSelection() : 0, 0, 3);

        CharacterFrame previewFrame{};
        bool havePreviewFrame = false;
        const std::vector<CharacterFrame>& selectedDirectionFrames = action->directionalFrames[static_cast<size_t>(directionIndex)];
        if (!selectedDirectionFrames.empty()) {
            previewFrame = selectedDirectionFrames.front();
            havePreviewFrame = true;
        }

        for (int dir = 0; dir < 4 && !havePreviewFrame; ++dir) {
            const std::vector<CharacterFrame>& frames = action->directionalFrames[static_cast<size_t>(dir)];
            if (!frames.empty()) {
                previewFrame = frames.front();
                havePreviewFrame = true;
            }
        }
        if (!havePreviewFrame) {
            previewFrame = CharacterFrame{};
        }

        const int frameW = std::max(1, previewFrame.frameWidth * std::max(1, spriteset_.tileWidth));
        const int frameH = std::max(1, previewFrame.frameHeight * std::max(1, spriteset_.tileHeight));

        TileCollection proxyCollection;
        proxyCollection.tileWidth = frameW;
        proxyCollection.tileHeight = frameH;

        TileDef proxyTile;
        proxyTile.id = 0;
        proxyTile.name = action->name + " Hitbox";
        proxyTile.solid = true;
        proxyTile.sourceX = previewFrame.tileX * std::max(1, spriteset_.tileWidth);
        proxyTile.sourceY = previewFrame.tileY * std::max(1, spriteset_.tileHeight);
        proxyTile.hitboxes = action->directionalHitboxes[static_cast<size_t>(directionIndex)];
        if (proxyTile.hitboxes.empty()) {
            proxyTile.hitboxes.push_back(TileHitbox{0, 0, frameW, frameH});
        }

        TileHitboxEditor editor(this, proxyTile, atlas_, proxyCollection);
        if (editor.ShowModal() != wxID_OK) {
            return;
        }

        action->directionalHitboxes[static_cast<size_t>(directionIndex)] = proxyTile.hitboxes;
        RefreshActionControls();
    }

    void EditCurrentActionPlayerHitboxes() {
        CharacterAction* action = CurrentAction();
        if (!action) {
            return;
        }

        const int directionIndex = std::clamp(directionChoice_ ? directionChoice_->GetSelection() : 0, 0, 3);

        CharacterFrame previewFrame{};
        bool havePreviewFrame = false;
        const std::vector<CharacterFrame>& selectedDirectionFrames = action->directionalFrames[static_cast<size_t>(directionIndex)];
        if (!selectedDirectionFrames.empty()) {
            previewFrame = selectedDirectionFrames.front();
            havePreviewFrame = true;
        }

        for (int dir = 0; dir < 4 && !havePreviewFrame; ++dir) {
            const std::vector<CharacterFrame>& frames = action->directionalFrames[static_cast<size_t>(dir)];
            if (!frames.empty()) {
                previewFrame = frames.front();
                havePreviewFrame = true;
            }
        }
        if (!havePreviewFrame) {
            previewFrame = CharacterFrame{};
        }

        const int frameW = std::max(1, previewFrame.frameWidth * std::max(1, spriteset_.tileWidth));
        const int frameH = std::max(1, previewFrame.frameHeight * std::max(1, spriteset_.tileHeight));

        TileCollection proxyCollection;
        proxyCollection.tileWidth = frameW;
        proxyCollection.tileHeight = frameH;

        TileDef proxyTile;
        proxyTile.id = 0;
        proxyTile.name = action->name + " Player Hitbox";
        proxyTile.solid = true;
        proxyTile.sourceX = previewFrame.tileX * std::max(1, spriteset_.tileWidth);
        proxyTile.sourceY = previewFrame.tileY * std::max(1, spriteset_.tileHeight);
        proxyTile.hitboxes = action->hitboxes;
        if (proxyTile.hitboxes.empty()) {
            proxyTile.hitboxes.push_back(TileHitbox{0, 0, frameW, frameH});
        }

        TileHitboxEditor editor(this, proxyTile, atlas_, proxyCollection);
        if (editor.ShowModal() != wxID_OK) {
            return;
        }

        action->hitboxes = proxyTile.hitboxes;
        RefreshActionControls();
    }

    void RefreshFrameList() {
        if (!frameList_) {
            return;
        }
        frameList_->DeleteAllItems();
        const CharacterAction* action = CurrentAction();
        const std::vector<CharacterFrame>* frames = action ? FramesForCurrentDirection(*action) : nullptr;
        if (!frames) {
            return;
        }
        for (size_t i = 0; i < frames->size(); ++i) {
            const CharacterFrame& frame = (*frames)[i];
            frameList_->InsertItem(static_cast<long>(i), wxString::Format("#%d  tile(%d,%d)  size(%dx%d)", static_cast<int>(i), frame.tileX, frame.tileY, frame.frameWidth, frame.frameHeight));
        }
    }

    void SelectFrame(int index) {
        if (!frameList_ || frameList_->GetItemCount() <= 0) {
            return;
        }
        const int clamped = std::clamp(index, 0, static_cast<int>(frameList_->GetItemCount()) - 1);
        frameList_->SetItemState(clamped, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
        frameList_->EnsureVisible(clamped);
        SyncFrameControlsFromSelection();
    }

    void SyncFrameControlsFromSelection() {
        CharacterFrame* frame = SelectedFrame();
        if (!frame) {
            return;
        }
        frameTileX_->SetValue(frame->tileX);
        frameTileY_->SetValue(frame->tileY);
        frameW_->SetValue(frame->frameWidth);
        frameH_->SetValue(frame->frameHeight);
    }

    void UpdateSelectedFrameFromControls() {
        CharacterFrame* frame = SelectedFrame();
        if (!frame) {
            return;
        }
        frame->tileX = frameTileX_->GetValue();
        frame->tileY = frameTileY_->GetValue();
        frame->frameWidth = frameW_->GetValue();
        frame->frameHeight = frameH_->GetValue();
        const int selected = SelectedFrameIndex();
        RefreshFrameList();
        SelectFrame(selected);
        if (sheetPanel_) {
            sheetPanel_->Refresh();
        }
        if (previewPanel_) {
            previewPanel_->Refresh();
        }
    }

    void ReorderFrame(int from, int to) {
        CharacterAction* action = CurrentAction();
        std::vector<CharacterFrame>* frames = action ? FramesForCurrentDirection(*action) : nullptr;
        if (!frames || from < 0 || to < 0 || from >= static_cast<int>(frames->size()) || to >= static_cast<int>(frames->size()) || from == to) {
            return;
        }
        CharacterFrame frame = (*frames)[static_cast<size_t>(from)];
        frames->erase(frames->begin() + from);
        frames->insert(frames->begin() + to, frame);
        RefreshFrameList();
        SelectFrame(to);
        if (previewPanel_) {
            previewPanel_->Refresh();
        }
    }

    void DrawFrameFromAtlas(wxDC& dc, const CharacterFrame& frame, int dstX, int dstY, int scale) {
        const int srcW = frame.frameWidth * spriteset_.tileWidth;
        const int srcH = frame.frameHeight * spriteset_.tileHeight;
        const int dstW = srcW * scale;
        const int dstH = srcH * scale;

        if (atlas_.IsOk()) {
            wxMemoryDC atlasDc;
            atlasDc.SelectObject(atlas_);
            dc.StretchBlit(
                dstX,
                dstY,
                dstW,
                dstH,
                &atlasDc,
                frame.tileX * spriteset_.tileWidth,
                frame.tileY * spriteset_.tileHeight,
                srcW,
                srcH
            );
            atlasDc.SelectObject(wxNullBitmap);
        } else {
            dc.SetBrush(wxBrush(wxColour(70, 80, 100)));
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.DrawRectangle(dstX, dstY, dstW, dstH);
        }
    }

    void OnSheetPaint(wxPaintEvent&) {
        wxAutoBufferedPaintDC dc(sheetPanel_);
        dc.SetBackground(wxBrush(wxColour(19, 24, 31)));
        dc.Clear();

        const int scale = 2;
        if (atlas_.IsOk()) {
            wxMemoryDC atlasDc;
            atlasDc.SelectObject(atlas_);
            dc.StretchBlit(8, 8, atlas_.GetWidth() * scale, atlas_.GetHeight() * scale, &atlasDc, 0, 0, atlas_.GetWidth(), atlas_.GetHeight());
            atlasDc.SelectObject(wxNullBitmap);
        } else {
            dc.SetTextForeground(wxColour(190, 196, 208));
            dc.DrawText("Unable to load spritesheet", 8, 8);
        }

        CharacterFrame* frame = SelectedFrame();
        if (frame) {
            dc.SetPen(wxPen(wxColour(255, 215, 88), 2));
            dc.SetBrush(*wxTRANSPARENT_BRUSH);
            dc.DrawRectangle(
                8 + frame->tileX * spriteset_.tileWidth * scale,
                8 + frame->tileY * spriteset_.tileHeight * scale,
                frame->frameWidth * spriteset_.tileWidth * scale,
                frame->frameHeight * spriteset_.tileHeight * scale
            );
        }
    }

    void OnSheetClick(wxMouseEvent& event) {
        CharacterFrame* frame = SelectedFrame();
        if (!frame) {
            return;
        }
        const int scale = 2;
        const int localX = event.GetX() - 8;
        const int localY = event.GetY() - 8;
        if (localX < 0 || localY < 0) {
            return;
        }
        frame->tileX = localX / (std::max(1, spriteset_.tileWidth * scale));
        frame->tileY = localY / (std::max(1, spriteset_.tileHeight * scale));
        SyncFrameControlsFromSelection();
        const int selected = SelectedFrameIndex();
        RefreshFrameList();
        SelectFrame(selected);
        sheetPanel_->Refresh();
        if (previewPanel_) {
            previewPanel_->Refresh();
        }
    }

    void OnPreviewPaint(wxPaintEvent&) {
        wxAutoBufferedPaintDC dc(previewPanel_);
        dc.SetBackground(wxBrush(wxColour(16, 18, 22)));
        dc.Clear();

        const CharacterAction* action = CurrentAction();
        const std::vector<CharacterFrame>* frames = action ? FramesForCurrentDirection(*action) : nullptr;
        if (!frames || frames->empty()) {
            return;
        }

        const int frameIdx = std::clamp(previewFrameIndex_, 0, static_cast<int>(frames->size()) - 1);
        const CharacterFrame& frame = (*frames)[static_cast<size_t>(frameIdx)];
        const int scale = 4;
        const int dstW = frame.frameWidth * spriteset_.tileWidth * scale;
        const int dstH = frame.frameHeight * spriteset_.tileHeight * scale;
        const int x = std::max(8, (previewPanel_->GetClientSize().x - dstW) / 2);
        const int y = std::max(8, (previewPanel_->GetClientSize().y - dstH) / 2);
        DrawFrameFromAtlas(dc, frame, x, y, scale);
    }

private:
    CharacterSpriteset& spriteset_;
    const std::vector<WeaponDefinition>& weaponDefinitions_;
    wxBitmap atlas_;
    wxTextCtrl* nameCtrl_ = nullptr;
    wxTextCtrl* descCtrl_ = nullptr;
    wxChoice* actionChoice_ = nullptr;
    wxChoice* directionChoice_ = nullptr;
    wxSpinCtrlDouble* speedCtrl_ = nullptr;
    wxButton* actionHitboxBtn_ = nullptr;
    wxButton* actionPlayerHitboxBtn_ = nullptr;
    wxPanel* sheetPanel_ = nullptr;
    wxListCtrl* frameList_ = nullptr;
    wxSpinCtrl* frameTileX_ = nullptr;
    wxSpinCtrl* frameTileY_ = nullptr;
    wxSpinCtrl* frameW_ = nullptr;
    wxSpinCtrl* frameH_ = nullptr;
    wxPanel* previewPanel_ = nullptr;
    wxTimer previewTimer_;
    int previewFrameIndex_ = 0;
    float previewElapsed_ = 0.0f;
    int dragSourceIndex_ = -1;
};

class ItemEditorDialog final : public wxDialog {
public:
    ItemEditorDialog(wxWindow* parent, ItemDefinition& item, const std::vector<SheetSpriteCollectionDef>& spriteCollections)
        : wxDialog(parent, wxID_ANY, "Edit Item", wxDefaultPosition, wxSize(640, 560),
                   wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
          item_(item),
          spriteCollections_(spriteCollections),
          working_(item) {
        if (working_.name.empty()) {
            working_.name = "item";
        }

        SetBackgroundColour(wxColour(31, 36, 45));
        SetForegroundColour(wxColour(236, 240, 246));

        auto* rootSizer = new wxBoxSizer(wxVERTICAL);
        auto* contentSizer = new wxBoxSizer(wxVERTICAL);

        auto* formGrid = new wxFlexGridSizer(2, 4, 8, 8);
        formGrid->AddGrowableCol(1, 1);
        formGrid->AddGrowableCol(3, 1);

        formGrid->Add(new wxStaticText(this, wxID_ANY, "Name"), 0, wxALIGN_CENTER_VERTICAL);
        nameCtrl_ = new wxTextCtrl(this, wxID_ANY, wxString::FromUTF8(working_.name));
        formGrid->Add(nameCtrl_, 1, wxEXPAND);

        formGrid->Add(new wxStaticText(this, wxID_ANY, "Animation Speed"), 0, wxALIGN_CENTER_VERTICAL);
        speedCtrl_ = new wxSpinCtrlDouble(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0, 0.0, 24.0, working_.animationSpeed, 0.1);
        formGrid->Add(speedCtrl_, 1, wxEXPAND);

        formGrid->Add(new wxStaticText(this, wxID_ANY, "Function"), 0, wxALIGN_CENTER_VERTICAL);
        wxArrayString functionChoices;
        functionChoices.Add("none");
        functionChoices.Add("increase_coins");
        functionChoices.Add("increase_health");
        functionChoices.Add("increase_max_health");
        functionChoices.Add("apply_speed_boost");
        functionChoices.Add("heart_piece");
        functionChoice_ = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, functionChoices);
        formGrid->Add(functionChoice_, 1, wxEXPAND);

        formGrid->Add(new wxStaticText(this, wxID_ANY, "Amount"), 0, wxALIGN_CENTER_VERTICAL);
        amountCtrl_ = new wxSpinCtrl(this, wxID_ANY, wxString::Format("%d", ItemTriggerAmount(working_)), wxDefaultPosition, wxDefaultSize, 0, 0, 999);
        amountCtrl_->SetBackgroundColour(wxColour(255, 255, 255));
        amountCtrl_->SetForegroundColour(wxColour(0, 0, 0));
        formGrid->Add(amountCtrl_, 1, wxEXPAND);

        formGrid->Add(new wxStaticText(this, wxID_ANY, "Duration (s)"), 0, wxALIGN_CENTER_VERTICAL);
        durationCtrl_ = new wxSpinCtrlDouble(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0, 0.0, 999.0, ItemTriggerDurationSeconds(working_, 0.0f), 0.1);
        durationCtrl_->SetDigits(2);
        formGrid->Add(durationCtrl_, 1, wxEXPAND);

        formGrid->Add(new wxStaticText(this, wxID_ANY, "Container"), 0, wxALIGN_CENTER_VERTICAL);
        containerCheck_ = new wxCheckBox(this, wxID_ANY, "Can contain one item/weapon");
        containerCheck_->SetValue(working_.isContainer);
        formGrid->Add(containerCheck_, 1, wxEXPAND);

        formGrid->Add(new wxStaticText(this, wxID_ANY, "Important Item"), 0, wxALIGN_CENTER_VERTICAL);
        importantItemCheck_ = new wxCheckBox(this, wxID_ANY, "Use container-style pickup presentation");
        importantItemCheck_->SetValue(working_.importantItem);
        formGrid->Add(importantItemCheck_, 1, wxEXPAND);

        formGrid->Add(new wxStaticText(this, wxID_ANY, "Empty Animation Speed"), 0, wxALIGN_CENTER_VERTICAL);
        emptySpeedCtrl_ = new wxSpinCtrlDouble(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0, 0.0, 24.0, working_.emptyAnimationSpeed, 0.1);
        formGrid->Add(emptySpeedCtrl_, 1, wxEXPAND);

        formGrid->AddSpacer(0);
        formGrid->AddSpacer(0);

        contentSizer->Add(formGrid, 0, wxEXPAND | wxALL, 10);

        auto* middleSizer = new wxBoxSizer(wxHORIZONTAL);

        auto* frameColumn = new wxBoxSizer(wxVERTICAL);
        frameColumn->Add(new wxStaticText(this, wxID_ANY, "Animation Frames"), 0, wxBOTTOM, 4);
        frameList_ = new wxListBox(this, wxID_ANY, wxDefaultPosition, wxSize(260, 220));
        frameColumn->Add(frameList_, 1, wxEXPAND | wxBOTTOM, 8);

        auto* frameButtons = new wxBoxSizer(wxHORIZONTAL);
        auto* addFrameBtn = new wxButton(this, wxID_ANY, "Add Animation Frame");
        auto* removeFrameBtn = new wxButton(this, wxID_ANY, "Remove Frame");
        frameButtons->Add(addFrameBtn, 1, wxRIGHT, 6);
        frameButtons->Add(removeFrameBtn, 1);
        frameColumn->Add(frameButtons, 0, wxEXPAND | wxBOTTOM, 8);

        auto* hitboxBtn = new wxButton(this, wxID_ANY, "Edit Hitboxes");
        frameColumn->Add(hitboxBtn, 0, wxEXPAND);

        frameColumn->AddSpacer(8);
        frameColumn->Add(new wxStaticText(this, wxID_ANY, "Empty (Opened) Frames"), 0, wxBOTTOM, 4);
        emptyFrameList_ = new wxListBox(this, wxID_ANY, wxDefaultPosition, wxSize(260, 120));
        frameColumn->Add(emptyFrameList_, 0, wxEXPAND | wxBOTTOM, 8);

        auto* emptyFrameButtons = new wxBoxSizer(wxHORIZONTAL);
        auto* addEmptyFrameBtn = new wxButton(this, wxID_ANY, "Add Empty Frame");
        auto* removeEmptyFrameBtn = new wxButton(this, wxID_ANY, "Remove Empty Frame");
        emptyFrameButtons->Add(addEmptyFrameBtn, 1, wxRIGHT, 6);
        emptyFrameButtons->Add(removeEmptyFrameBtn, 1);
        frameColumn->Add(emptyFrameButtons, 0, wxEXPAND);
        middleSizer->Add(frameColumn, 1, wxEXPAND | wxALL, 10);

        auto* previewColumn = new wxBoxSizer(wxVERTICAL);
        previewColumn->Add(new wxStaticText(this, wxID_ANY, "Preview"), 0, wxBOTTOM, 4);
        previewBitmap_ = new wxStaticBitmap(this, wxID_ANY, wxBitmap(96, 96));
        previewBitmap_->SetMinSize(wxSize(120, 120));
        previewColumn->Add(previewBitmap_, 0, wxBOTTOM, 8);
        previewLabel_ = new wxStaticText(this, wxID_ANY, wxEmptyString);
        previewColumn->Add(previewLabel_, 0, wxEXPAND);
        middleSizer->Add(previewColumn, 0, wxTOP | wxRIGHT | wxBOTTOM, 10);

        contentSizer->Add(middleSizer, 1, wxEXPAND);

        rootSizer->Add(contentSizer, 1, wxEXPAND);

        auto* buttonSizer = new wxBoxSizer(wxHORIZONTAL);
        buttonSizer->Add(new wxButton(this, wxID_OK, "OK"), 0, wxRIGHT, 6);
        buttonSizer->Add(new wxButton(this, wxID_CANCEL, "Cancel"), 0);
        rootSizer->Add(buttonSizer, 0, wxALIGN_RIGHT | wxALL, 10);

        SetSizer(rootSizer);

        switch (working_.triggerFunction) {
            case ItemTriggerFunction::IncreaseCoins:
                functionChoice_->SetSelection(1);
                break;
            case ItemTriggerFunction::IncreaseHealth:
                functionChoice_->SetSelection(2);
                break;
            case ItemTriggerFunction::IncreaseMaxHealth:
                functionChoice_->SetSelection(3);
                break;
            case ItemTriggerFunction::ApplySpeedBoost:
                functionChoice_->SetSelection(4);
                break;
            case ItemTriggerFunction::HeartPiece:
                functionChoice_->SetSelection(5);
                break;
            case ItemTriggerFunction::None:
            default:
                functionChoice_->SetSelection(0);
                break;
        }

        addFrameBtn->Bind(wxEVT_BUTTON, &ItemEditorDialog::OnAddFrame, this);
        removeFrameBtn->Bind(wxEVT_BUTTON, &ItemEditorDialog::OnRemoveFrame, this);
        addEmptyFrameBtn->Bind(wxEVT_BUTTON, &ItemEditorDialog::OnAddEmptyFrame, this);
        removeEmptyFrameBtn->Bind(wxEVT_BUTTON, &ItemEditorDialog::OnRemoveEmptyFrame, this);
        hitboxBtn->Bind(wxEVT_BUTTON, &ItemEditorDialog::OnEditHitboxes, this);
        frameList_->Bind(wxEVT_LISTBOX, [this](wxCommandEvent&) { UpdatePreview(); });
        functionChoice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { UpdateFunctionControls(); });
        containerCheck_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { UpdateContainerControls(); });
        speedCtrl_->Bind(wxEVT_SPINCTRLDOUBLE, [this](wxSpinDoubleEvent&) {
            working_.animationSpeed = static_cast<float>(speedCtrl_->GetValue());
            previewElapsed_ = 0.0f;
            previewFrameIndex_ = 0;
            UpdatePreview();
        });
        Bind(wxEVT_BUTTON, &ItemEditorDialog::OnOk, this, wxID_OK);
        previewTimer_.SetOwner(this);
        Bind(wxEVT_TIMER, &ItemEditorDialog::OnPreviewTimer, this);
        previewTimer_.Start(100);

        RebuildFrameList();
        RebuildEmptyFrameList();
        UpdateFunctionControls();
        UpdateContainerControls();
        UpdatePreview();
    }

private:
    void RebuildFrameList() {
        frameList_->Clear();
        for (size_t i = 0; i < working_.frames.size(); ++i) {
            const ItemAnimationFrame& frame = working_.frames[i];
            const wxString label = frame.sourceLabel.empty()
                ? wxString::Format("frame %d (%d,%d)", static_cast<int>(i + 1), frame.sourceX, frame.sourceY)
                : wxString::FromUTF8(frame.sourceLabel);
            frameList_->Append(label);
        }
        if (!working_.frames.empty()) {
            frameList_->SetSelection(std::clamp(frameList_->GetSelection(), 0, static_cast<int>(working_.frames.size()) - 1));
        }
    }

    void UpdateFunctionControls() {
        const int selection = functionChoice_->GetSelection();
        amountCtrl_->Enable(selection > 0 && selection != 5);
        if (durationCtrl_) {
            durationCtrl_->Enable(selection == 4);
        }
    }

    void UpdateContainerControls() {
        const bool isContainer = containerCheck_ && containerCheck_->GetValue();
        if (emptySpeedCtrl_) {
            emptySpeedCtrl_->Enable(isContainer);
        }
        if (emptyFrameList_) {
            emptyFrameList_->Enable(isContainer);
        }
    }

    void RebuildEmptyFrameList() {
        if (!emptyFrameList_) {
            return;
        }
        emptyFrameList_->Clear();
        for (size_t i = 0; i < working_.emptyFrames.size(); ++i) {
            const ItemAnimationFrame& frame = working_.emptyFrames[i];
            const wxString label = frame.sourceLabel.empty()
                ? wxString::Format("empty %d (%d,%d)", static_cast<int>(i + 1), frame.sourceX, frame.sourceY)
                : wxString::FromUTF8(frame.sourceLabel);
            emptyFrameList_->Append(label);
        }
        if (!working_.emptyFrames.empty()) {
            emptyFrameList_->SetSelection(std::clamp(emptyFrameList_->GetSelection(), 0, static_cast<int>(working_.emptyFrames.size()) - 1));
        }
    }

    wxBitmap BuildPreviewBitmap() const {
        const int frameIndex = ActivePreviewFrameIndex();
        const ItemAnimationFrame* frame = working_.frames.empty()
            ? nullptr
            : &working_.frames[static_cast<size_t>(std::clamp(frameIndex, 0, static_cast<int>(working_.frames.size()) - 1))];
        return BuildItemFramePreviewBitmap(frame, 6);
    }

    int ActivePreviewFrameIndex() const {
        if (working_.frames.empty()) {
            return -1;
        }

        if (working_.frames.size() == 1 || working_.animationSpeed <= 0.0f) {
            return std::clamp(frameList_->GetSelection(), 0, static_cast<int>(working_.frames.size()) - 1);
        }

        return std::clamp(previewFrameIndex_, 0, static_cast<int>(working_.frames.size()) - 1);
    }

    void UpdatePreview() {
        previewBitmap_->SetBitmap(BuildPreviewBitmap());
        if (working_.frames.empty()) {
            previewLabel_->SetLabel("No animation frames selected.");
            return;
        }

        const int frameIndex = std::max(0, frameList_->GetSelection());
        const ItemAnimationFrame& frame = working_.frames[static_cast<size_t>(std::min(frameIndex, static_cast<int>(working_.frames.size()) - 1))];
        const wxString frameLabel = frame.sourceLabel.empty() ? wxString("frame") : wxString::FromUTF8(frame.sourceLabel);
        previewLabel_->SetLabel(wxString::Format(
            "%s\n%d x %d at (%d,%d)",
            frameLabel,
            frame.sourceW,
            frame.sourceH,
            frame.sourceX,
            frame.sourceY
        ));
        Layout();
    }

    void OnPreviewTimer(wxTimerEvent&) {
        if (working_.frames.size() <= 1) {
            return;
        }

        working_.animationSpeed = static_cast<float>(speedCtrl_->GetValue());
        if (working_.animationSpeed <= 0.0f) {
            return;
        }

        const float frameDuration = 1.0f / std::max(0.01f, working_.animationSpeed);
        previewElapsed_ += 0.1f;
        bool changed = false;
        while (previewElapsed_ >= frameDuration) {
            previewElapsed_ -= frameDuration;
            previewFrameIndex_ = (previewFrameIndex_ + 1) % static_cast<int>(working_.frames.size());
            changed = true;
        }

        if (changed) {
            UpdatePreview();
        }
    }

    void OnAddFrame(wxCommandEvent&) {
        SpriteLibraryPickerDialog dlg(this, spriteCollections_);
        if (dlg.ShowModal() != wxID_OK) {
            return;
        }

        SheetSpritePick pick;
        if (!dlg.GetSelectedPick(pick)) {
            return;
        }

        ItemAnimationFrame frame;
        frame.sourceImagePath = pick.sourceImagePath;
        frame.sourceLabel = pick.collectionName + " (" + std::to_string(pick.sourceX) + "," + std::to_string(pick.sourceY) + ")";
        frame.sourceX = pick.sourceX;
        frame.sourceY = pick.sourceY;
        frame.sourceW = pick.width;
        frame.sourceH = pick.height;
        working_.frames.push_back(frame);
        if (working_.frames.size() == 1) {
            previewFrameIndex_ = 0;
            previewElapsed_ = 0.0f;
        }
        RebuildFrameList();
        frameList_->SetSelection(static_cast<int>(working_.frames.size()) - 1);
        UpdatePreview();
    }

    void OnRemoveFrame(wxCommandEvent&) {
        const int selection = frameList_->GetSelection();
        if (selection == wxNOT_FOUND) {
            return;
        }
        working_.frames.erase(working_.frames.begin() + selection);
        previewFrameIndex_ = std::clamp(previewFrameIndex_, 0, std::max(0, static_cast<int>(working_.frames.size()) - 1));
        RebuildFrameList();
        UpdatePreview();
    }

    void OnAddEmptyFrame(wxCommandEvent&) {
        SpriteLibraryPickerDialog dlg(this, spriteCollections_);
        if (dlg.ShowModal() != wxID_OK) {
            return;
        }

        SheetSpritePick pick;
        if (!dlg.GetSelectedPick(pick)) {
            return;
        }

        ItemAnimationFrame frame;
        frame.sourceImagePath = pick.sourceImagePath;
        frame.sourceLabel = pick.collectionName + " (" + std::to_string(pick.sourceX) + "," + std::to_string(pick.sourceY) + ")";
        frame.sourceX = pick.sourceX;
        frame.sourceY = pick.sourceY;
        frame.sourceW = pick.width;
        frame.sourceH = pick.height;
        working_.emptyFrames.push_back(frame);
        RebuildEmptyFrameList();
        emptyFrameList_->SetSelection(static_cast<int>(working_.emptyFrames.size()) - 1);
    }

    void OnRemoveEmptyFrame(wxCommandEvent&) {
        if (!emptyFrameList_) {
            return;
        }
        const int selection = emptyFrameList_->GetSelection();
        if (selection == wxNOT_FOUND) {
            return;
        }
        working_.emptyFrames.erase(working_.emptyFrames.begin() + selection);
        RebuildEmptyFrameList();
    }

    void OnEditHitboxes(wxCommandEvent&) {
        if (working_.frames.empty()) {
            wxMessageBox("Add at least one frame before editing hitboxes.", "Item Hitboxes", wxOK | wxICON_INFORMATION, this);
            return;
        }

        const ItemAnimationFrame& frame = working_.frames.front();
        TileDef proxyTile;
        proxyTile.id = 0;
        proxyTile.sourceX = frame.sourceX;
        proxyTile.sourceY = frame.sourceY;
        proxyTile.hitboxes = working_.hitboxes;
        proxyTile.hitboxX = 0;
        proxyTile.hitboxY = 0;
        proxyTile.hitboxW = frame.sourceW;
        proxyTile.hitboxH = frame.sourceH;

        TileCollection proxyCollection;
        proxyCollection.imagePath = frame.sourceImagePath;
        proxyCollection.tileWidth = frame.sourceW;
        proxyCollection.tileHeight = frame.sourceH;

        wxBitmap atlas = LoadBitmapMaybeRelative(frame.sourceImagePath);
        TileHitboxEditor dlg(this, proxyTile, atlas, proxyCollection, true);
        if (dlg.ShowModal() != wxID_OK) {
            return;
        }

        working_.hitboxes = proxyTile.hitboxes;
    }

    void OnOk(wxCommandEvent&) {
        if (working_.frames.empty()) {
            wxMessageBox("Items need at least one animation frame.", "Item Validation", wxOK | wxICON_WARNING, this);
            return;
        }

        working_.name = nameCtrl_->GetValue().ToStdString();
        if (working_.name.empty()) {
            working_.name = "item";
        }
        working_.animationSpeed = static_cast<float>(speedCtrl_->GetValue());
        working_.isContainer = containerCheck_ && containerCheck_->GetValue();
        working_.importantItem = importantItemCheck_ && importantItemCheck_->GetValue();
        working_.emptyAnimationSpeed = static_cast<float>(emptySpeedCtrl_->GetValue());
        working_.legacyPickup = false;
        working_.powerupId.clear();

        if (!working_.isContainer) {
            working_.emptyFrames.clear();
            working_.emptyAnimationSpeed = 0.0f;
        }

        switch (functionChoice_->GetSelection()) {
            case 1:
                working_.triggerFunction = ItemTriggerFunction::IncreaseCoins;
                working_.triggerParams.clear();
                SetItemTriggerAmount(working_, amountCtrl_->GetValue());
                break;
            case 2:
                working_.triggerFunction = ItemTriggerFunction::IncreaseHealth;
                working_.triggerParams.clear();
                SetItemTriggerAmount(working_, amountCtrl_->GetValue());
                break;
            case 3:
                working_.triggerFunction = ItemTriggerFunction::IncreaseMaxHealth;
                working_.triggerParams.clear();
                SetItemTriggerAmount(working_, amountCtrl_->GetValue());
                break;
            case 4:
                working_.triggerFunction = ItemTriggerFunction::ApplySpeedBoost;
                working_.triggerParams.clear();
                SetItemTriggerAmount(working_, amountCtrl_->GetValue());
                SetItemTriggerDurationSeconds(working_, static_cast<float>(durationCtrl_->GetValue()));
                break;
            case 5:
                working_.triggerFunction = ItemTriggerFunction::HeartPiece;
                working_.triggerParams.clear();
                break;
            case 0:
            default:
                working_.triggerFunction = ItemTriggerFunction::None;
                working_.triggerParams.clear();
                break;
        }

        item_ = working_;
        EndModal(wxID_OK);
    }

    ItemDefinition& item_;
    const std::vector<SheetSpriteCollectionDef>& spriteCollections_;
    ItemDefinition working_;
    wxTextCtrl* nameCtrl_ = nullptr;
    wxSpinCtrlDouble* speedCtrl_ = nullptr;
    wxChoice* functionChoice_ = nullptr;
    wxSpinCtrl* amountCtrl_ = nullptr;
    wxSpinCtrlDouble* durationCtrl_ = nullptr;
    wxCheckBox* containerCheck_ = nullptr;
    wxCheckBox* importantItemCheck_ = nullptr;
    wxSpinCtrlDouble* emptySpeedCtrl_ = nullptr;
    wxListBox* frameList_ = nullptr;
    wxListBox* emptyFrameList_ = nullptr;
    wxStaticBitmap* previewBitmap_ = nullptr;
    wxStaticText* previewLabel_ = nullptr;
    wxTimer previewTimer_;
    int previewFrameIndex_ = 0;
    float previewElapsed_ = 0.0f;
};

class ProjectileDefinitionEditorDialog final : public wxDialog {
public:
    ProjectileDefinitionEditorDialog(wxWindow* parent, ProjectileDefinition& projectile, const std::vector<SheetSpriteCollectionDef>& spriteCollections)
        : wxDialog(parent, wxID_ANY, "Edit Projectile", wxDefaultPosition, wxSize(920, 700), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
          projectile_(projectile),
          spriteCollections_(spriteCollections),
          working_(projectile) {
        if (working_.name.empty()) {
            working_.name = working_.id.empty() ? "projectile" : working_.id;
        }
        if (working_.hitboxes.empty()) {
            working_.hitboxes.push_back(TileHitbox{0, 0, 8, 8});
        }

        auto* root = new wxBoxSizer(wxVERTICAL);

        auto* meta = new wxFlexGridSizer(2, 6, 8, 8);
        meta->AddGrowableCol(1, 1);
        meta->AddGrowableCol(3, 1);
        meta->AddGrowableCol(5, 1);

        meta->Add(new wxStaticText(this, wxID_ANY, "Name"), 0, wxALIGN_CENTER_VERTICAL);
        nameCtrl_ = new wxTextCtrl(this, wxID_ANY, wxString::FromUTF8(working_.name));
        meta->Add(nameCtrl_, 1, wxEXPAND);

        meta->Add(new wxStaticText(this, wxID_ANY, "Movement"), 0, wxALIGN_CENTER_VERTICAL);
        movementChoice_ = new wxChoice(this, wxID_ANY);
        movementChoice_->Append("track player");
        movementChoice_->Append("homing");
        movementChoice_->Append("fixed function");
        movementChoice_->Append("straight limited distance");
        if (working_.movementType == ProjectileMovementType::Homing) {
            movementChoice_->SetSelection(1);
        } else if (working_.movementType == ProjectileMovementType::FixedFunction) {
            movementChoice_->SetSelection(2);
        } else if (working_.movementType == ProjectileMovementType::StraightLimitedDistance) {
            movementChoice_->SetSelection(3);
        } else {
            movementChoice_->SetSelection(0);
        }
        meta->Add(movementChoice_, 1, wxEXPAND);

        meta->Add(new wxStaticText(this, wxID_ANY, "Speed (tiles/s)"), 0, wxALIGN_CENTER_VERTICAL);
        speedCtrl_ = new wxSpinCtrlDouble(this, wxID_ANY);
        speedCtrl_->SetDigits(2);
        speedCtrl_->SetRange(0.0, 50.0);
        speedCtrl_->SetIncrement(0.1);
        speedCtrl_->SetValue(working_.speedTilesPerSecond);
        meta->Add(speedCtrl_, 1, wxEXPAND);

        meta->Add(new wxStaticText(this, wxID_ANY, "Fixed a (y=a*x)"), 0, wxALIGN_CENTER_VERTICAL);
        fixedACtrl_ = new wxSpinCtrlDouble(this, wxID_ANY);
        fixedACtrl_->SetDigits(3);
        fixedACtrl_->SetRange(-10.0, 10.0);
        fixedACtrl_->SetIncrement(0.05);
        fixedACtrl_->SetValue(working_.fixedFunctionA);
        meta->Add(fixedACtrl_, 1, wxEXPAND);

        meta->Add(new wxStaticText(this, wxID_ANY, "Base Damage"), 0, wxALIGN_CENTER_VERTICAL);
        damageCtrl_ = new wxSpinCtrl(this, wxID_ANY);
        damageCtrl_->SetRange(0, 999);
        damageCtrl_->SetValue(std::max(0, working_.baseDamage));
        meta->Add(damageCtrl_, 1, wxEXPAND);

        throughSolidCheck_ = new wxCheckBox(this, wxID_ANY, "Move through solid tiles");
        throughSolidCheck_->SetValue(working_.moveThroughSolid);
        meta->Add(throughSolidCheck_, 0, wxALIGN_CENTER_VERTICAL);
        meta->AddStretchSpacer();
        root->Add(meta, 0, wxEXPAND | wxALL, 10);

        auto* speedRow = new wxFlexGridSizer(2, 6, 8, 8);
        speedRow->AddGrowableCol(1, 1);
        speedRow->AddGrowableCol(3, 1);
        speedRow->AddGrowableCol(5, 1);
        speedRow->Add(new wxStaticText(this, wxID_ANY, "Start FPS"), 0, wxALIGN_CENTER_VERTICAL);
        startSpeedCtrl_ = new wxSpinCtrlDouble(this, wxID_ANY);
        startSpeedCtrl_->SetDigits(1);
        startSpeedCtrl_->SetRange(0.0, 60.0);
        startSpeedCtrl_->SetIncrement(0.1);
        startSpeedCtrl_->SetValue(working_.startAnimationSpeed);
        speedRow->Add(startSpeedCtrl_, 1, wxEXPAND);

        speedRow->Add(new wxStaticText(this, wxID_ANY, "Flight FPS"), 0, wxALIGN_CENTER_VERTICAL);
        flightSpeedCtrl_ = new wxSpinCtrlDouble(this, wxID_ANY);
        flightSpeedCtrl_->SetDigits(1);
        flightSpeedCtrl_->SetRange(0.0, 60.0);
        flightSpeedCtrl_->SetIncrement(0.1);
        flightSpeedCtrl_->SetValue(working_.flightAnimationSpeed);
        speedRow->Add(flightSpeedCtrl_, 1, wxEXPAND);

        speedRow->Add(new wxStaticText(this, wxID_ANY, "Impact FPS"), 0, wxALIGN_CENTER_VERTICAL);
        impactSpeedCtrl_ = new wxSpinCtrlDouble(this, wxID_ANY);
        impactSpeedCtrl_->SetDigits(1);
        impactSpeedCtrl_->SetRange(0.0, 60.0);
        impactSpeedCtrl_->SetIncrement(0.1);
        impactSpeedCtrl_->SetValue(working_.impactAnimationSpeed);
        speedRow->Add(impactSpeedCtrl_, 1, wxEXPAND);
        root->Add(speedRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);

        auto* limitedRow = new wxFlexGridSizer(2, 4, 8, 8);
        limitedRow->AddGrowableCol(1, 1);
        limitedRow->AddGrowableCol(3, 1);
        limitedRow->Add(new wxStaticText(this, wxID_ANY, "Distance (tiles)"), 0, wxALIGN_CENTER_VERTICAL);
        limitedDistanceCtrl_ = new wxSpinCtrlDouble(this, wxID_ANY);
        limitedDistanceCtrl_->SetDigits(2);
        limitedDistanceCtrl_->SetRange(0.0, 999.0);
        limitedDistanceCtrl_->SetIncrement(0.25);
        limitedDistanceCtrl_->SetValue(working_.limitedDistanceTiles);
        limitedRow->Add(limitedDistanceCtrl_, 1, wxEXPAND);
        limitedRow->Add(new wxStaticText(this, wxID_ANY, "Duration (seconds)"), 0, wxALIGN_CENTER_VERTICAL);
        limitedDurationCtrl_ = new wxSpinCtrlDouble(this, wxID_ANY);
        limitedDurationCtrl_->SetDigits(2);
        limitedDurationCtrl_->SetRange(0.0, 60.0);
        limitedDurationCtrl_->SetIncrement(0.05);
        limitedDurationCtrl_->SetValue(working_.limitedDurationSeconds);
        limitedRow->Add(limitedDurationCtrl_, 1, wxEXPAND);
        root->Add(limitedRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);

        auto* listsRow = new wxBoxSizer(wxHORIZONTAL);

        auto* startCol = BuildPhaseColumn("Start Frames", startFrameList_, IdAddStartFrame_, IdRemoveStartFrame_);
        auto* flightCol = BuildPhaseColumn("Flight Frames", flightFrameList_, IdAddFlightFrame_, IdRemoveFlightFrame_);
        auto* impactCol = BuildPhaseColumn("Impact Frames", impactFrameList_, IdAddImpactFrame_, IdRemoveImpactFrame_);
        listsRow->Add(startCol, 1, wxEXPAND | wxRIGHT, 8);
        listsRow->Add(flightCol, 1, wxEXPAND | wxRIGHT, 8);
        listsRow->Add(impactCol, 1, wxEXPAND);
        root->Add(listsRow, 1, wxEXPAND | wxLEFT | wxRIGHT, 10);

        auto* bottomRow = new wxBoxSizer(wxHORIZONTAL);
        auto* editHitboxesBtn = new wxButton(this, wxID_ANY, "Edit Hitboxes");
        bottomRow->Add(editHitboxesBtn, 0, wxRIGHT, 8);
        bottomRow->AddStretchSpacer(1);
        auto* okBtn = new wxButton(this, wxID_OK, "OK");
        auto* cancelBtn = new wxButton(this, wxID_CANCEL, "Cancel");
        bottomRow->Add(okBtn, 0, wxRIGHT, 6);
        bottomRow->Add(cancelBtn, 0);
        root->Add(bottomRow, 0, wxEXPAND | wxALL, 10);

        SetSizer(root);

        Bind(wxEVT_BUTTON, &ProjectileDefinitionEditorDialog::OnAddStartFrame, this, IdAddStartFrame_);
        Bind(wxEVT_BUTTON, &ProjectileDefinitionEditorDialog::OnRemoveStartFrame, this, IdRemoveStartFrame_);
        Bind(wxEVT_BUTTON, &ProjectileDefinitionEditorDialog::OnAddFlightFrame, this, IdAddFlightFrame_);
        Bind(wxEVT_BUTTON, &ProjectileDefinitionEditorDialog::OnRemoveFlightFrame, this, IdRemoveFlightFrame_);
        Bind(wxEVT_BUTTON, &ProjectileDefinitionEditorDialog::OnAddImpactFrame, this, IdAddImpactFrame_);
        Bind(wxEVT_BUTTON, &ProjectileDefinitionEditorDialog::OnRemoveImpactFrame, this, IdRemoveImpactFrame_);
        editHitboxesBtn->Bind(wxEVT_BUTTON, &ProjectileDefinitionEditorDialog::OnEditHitboxes, this);
        Bind(wxEVT_BUTTON, &ProjectileDefinitionEditorDialog::OnOk, this, wxID_OK);
        movementChoice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { UpdateMovementFieldEnablement(); });

        RebuildFrameLists();
        UpdateMovementFieldEnablement();
    }

private:
    wxSizer* BuildPhaseColumn(const wxString& title, wxListBox*& outList, int addId, int removeId) {
        auto* column = new wxBoxSizer(wxVERTICAL);
        column->Add(new wxStaticText(this, wxID_ANY, title), 0, wxBOTTOM, 4);
        outList = new wxListBox(this, wxID_ANY, wxDefaultPosition, wxSize(260, 260));
        column->Add(outList, 1, wxEXPAND | wxBOTTOM, 6);
        auto* buttons = new wxBoxSizer(wxHORIZONTAL);
        buttons->Add(new wxButton(this, addId, "Add"), 1, wxRIGHT, 4);
        buttons->Add(new wxButton(this, removeId, "Remove"), 1);
        column->Add(buttons, 0, wxEXPAND);
        return column;
    }

    void RebuildFrameList(wxListBox* list, const std::vector<ItemAnimationFrame>& frames, const std::string& phaseName) {
        if (!list) {
            return;
        }
        const int previous = list->GetSelection();
        list->Clear();
        for (size_t i = 0; i < frames.size(); ++i) {
            const ItemAnimationFrame& frame = frames[i];
            const wxString label = wxString::Format(
                "%s %d: src(%d,%d) %dx%d",
                phaseName,
                static_cast<int>(i + 1),
                frame.sourceX,
                frame.sourceY,
                frame.sourceW,
                frame.sourceH
            );
            list->Append(label);
        }
        if (list->GetCount() > 0) {
            list->SetSelection(std::clamp(previous, 0, static_cast<int>(list->GetCount()) - 1));
        }
    }

    void RebuildFrameLists() {
        RebuildFrameList(startFrameList_, working_.startFrames, "start");
        RebuildFrameList(flightFrameList_, working_.flightFrames, "flight");
        RebuildFrameList(impactFrameList_, working_.impactFrames, "impact");
    }

    void AddFrameTo(std::vector<ItemAnimationFrame>& frames) {
        SpriteLibraryPickerDialog dlg(this, spriteCollections_);
        if (dlg.ShowModal() != wxID_OK) {
            return;
        }

        SheetSpritePick pick;
        if (!dlg.GetSelectedPick(pick)) {
            return;
        }

        ItemAnimationFrame frame;
        frame.sourceImagePath = pick.sourceImagePath;
        frame.sourceLabel = pick.collectionName + " (" + std::to_string(pick.sourceX) + "," + std::to_string(pick.sourceY) + ")";
        frame.sourceX = pick.sourceX;
        frame.sourceY = pick.sourceY;
        frame.sourceW = pick.width;
        frame.sourceH = pick.height;
        frames.push_back(frame);
        RebuildFrameLists();
    }

    void RemoveSelectedFrame(wxListBox* list, std::vector<ItemAnimationFrame>& frames) {
        if (!list) {
            return;
        }
        const int selection = list->GetSelection();
        if (selection == wxNOT_FOUND || selection < 0 || selection >= static_cast<int>(frames.size())) {
            return;
        }
        frames.erase(frames.begin() + selection);
        RebuildFrameLists();
    }

    const ItemAnimationFrame* HitboxReferenceFrame() const {
        if (!working_.flightFrames.empty()) {
            return &working_.flightFrames.front();
        }
        if (!working_.startFrames.empty()) {
            return &working_.startFrames.front();
        }
        if (!working_.impactFrames.empty()) {
            return &working_.impactFrames.front();
        }
        return nullptr;
    }

    void UpdateMovementFieldEnablement() {
        const int selection = movementChoice_ ? movementChoice_->GetSelection() : 0;
        const bool fixed = selection == 2;
        const bool limited = selection == 3;
        const bool homing = selection == 1;
        if (fixedACtrl_) {
            fixedACtrl_->Enable(fixed);
        }
        if (limitedDistanceCtrl_) {
            limitedDistanceCtrl_->Enable(limited);
        }
        if (limitedDurationCtrl_) {
            limitedDurationCtrl_->Enable(limited || homing);
        }
    }

    void OnAddStartFrame(wxCommandEvent&) { AddFrameTo(working_.startFrames); }
    void OnRemoveStartFrame(wxCommandEvent&) { RemoveSelectedFrame(startFrameList_, working_.startFrames); }
    void OnAddFlightFrame(wxCommandEvent&) { AddFrameTo(working_.flightFrames); }
    void OnRemoveFlightFrame(wxCommandEvent&) { RemoveSelectedFrame(flightFrameList_, working_.flightFrames); }
    void OnAddImpactFrame(wxCommandEvent&) { AddFrameTo(working_.impactFrames); }
    void OnRemoveImpactFrame(wxCommandEvent&) { RemoveSelectedFrame(impactFrameList_, working_.impactFrames); }

    void OnEditHitboxes(wxCommandEvent&) {
        const ItemAnimationFrame* frame = HitboxReferenceFrame();
        if (!frame) {
            wxMessageBox("Add at least one frame before editing hitboxes.", "Projectile Hitboxes", wxOK | wxICON_INFORMATION, this);
            return;
        }

        TileDef proxyTile;
        proxyTile.id = 0;
        proxyTile.sourceX = frame->sourceX;
        proxyTile.sourceY = frame->sourceY;
        proxyTile.hitboxes = working_.hitboxes;
        proxyTile.hitboxX = 0;
        proxyTile.hitboxY = 0;
        proxyTile.hitboxW = std::max(1, frame->sourceW);
        proxyTile.hitboxH = std::max(1, frame->sourceH);

        TileCollection proxyCollection;
        proxyCollection.imagePath = frame->sourceImagePath;
        proxyCollection.tileWidth = std::max(1, frame->sourceW);
        proxyCollection.tileHeight = std::max(1, frame->sourceH);

        wxBitmap atlas = LoadBitmapMaybeRelative(frame->sourceImagePath);
        TileHitboxEditor dlg(this, proxyTile, atlas, proxyCollection, true);
        if (dlg.ShowModal() != wxID_OK) {
            return;
        }

        working_.hitboxes = proxyTile.hitboxes;
        if (working_.hitboxes.empty()) {
            working_.hitboxes.push_back(TileHitbox{0, 0, std::max(1, frame->sourceW), std::max(1, frame->sourceH)});
        }
    }

    void OnOk(wxCommandEvent&) {
        working_.name = nameCtrl_->GetValue().ToStdString();
        if (working_.name.empty()) {
            working_.name = working_.id.empty() ? "projectile" : working_.id;
        }

        if (working_.flightFrames.empty()) {
            wxMessageBox("Projectile needs at least one flight frame.", "Projectile Validation", wxOK | wxICON_WARNING, this);
            return;
        }

        const int movementSelection = movementChoice_ ? movementChoice_->GetSelection() : 0;
        if (movementSelection == 1) {
            working_.movementType = ProjectileMovementType::Homing;
        } else if (movementSelection == 2) {
            working_.movementType = ProjectileMovementType::FixedFunction;
        } else if (movementSelection == 3) {
            working_.movementType = ProjectileMovementType::StraightLimitedDistance;
        } else {
            working_.movementType = ProjectileMovementType::TrackPlayer;
        }
        working_.speedTilesPerSecond = static_cast<float>(speedCtrl_->GetValue());
        working_.fixedFunctionA = static_cast<float>(fixedACtrl_->GetValue());
        working_.limitedDistanceTiles = static_cast<float>(limitedDistanceCtrl_->GetValue());
        working_.limitedDurationSeconds = static_cast<float>(limitedDurationCtrl_->GetValue());
        working_.moveThroughSolid = throughSolidCheck_->GetValue();
        working_.baseDamage = std::max(0, damageCtrl_->GetValue());
        working_.startAnimationSpeed = static_cast<float>(startSpeedCtrl_->GetValue());
        working_.flightAnimationSpeed = static_cast<float>(flightSpeedCtrl_->GetValue());
        working_.impactAnimationSpeed = static_cast<float>(impactSpeedCtrl_->GetValue());

        if (working_.movementType == ProjectileMovementType::StraightLimitedDistance || working_.movementType == ProjectileMovementType::Homing) {
            working_.limitedDistanceTiles = std::max(0.0f, working_.limitedDistanceTiles);
            working_.limitedDurationSeconds = std::max(0.0f, working_.limitedDurationSeconds);
        }

        if (working_.hitboxes.empty()) {
            const ItemAnimationFrame& ref = working_.flightFrames.front();
            working_.hitboxes.push_back(TileHitbox{0, 0, std::max(1, ref.sourceW), std::max(1, ref.sourceH)});
        }

        projectile_ = working_;
        EndModal(wxID_OK);
    }

    ProjectileDefinition& projectile_;
    const std::vector<SheetSpriteCollectionDef>& spriteCollections_;
    ProjectileDefinition working_;

    wxTextCtrl* nameCtrl_ = nullptr;
    wxChoice* movementChoice_ = nullptr;
    wxSpinCtrlDouble* speedCtrl_ = nullptr;
    wxSpinCtrlDouble* fixedACtrl_ = nullptr;
    wxSpinCtrlDouble* limitedDistanceCtrl_ = nullptr;
    wxSpinCtrlDouble* limitedDurationCtrl_ = nullptr;
    wxCheckBox* throughSolidCheck_ = nullptr;
    wxSpinCtrl* damageCtrl_ = nullptr;
    wxSpinCtrlDouble* startSpeedCtrl_ = nullptr;
    wxSpinCtrlDouble* flightSpeedCtrl_ = nullptr;
    wxSpinCtrlDouble* impactSpeedCtrl_ = nullptr;
    wxListBox* startFrameList_ = nullptr;
    wxListBox* flightFrameList_ = nullptr;
    wxListBox* impactFrameList_ = nullptr;

    static constexpr int IdAddStartFrame_ = wxID_HIGHEST + 410;
    static constexpr int IdRemoveStartFrame_ = wxID_HIGHEST + 411;
    static constexpr int IdAddFlightFrame_ = wxID_HIGHEST + 412;
    static constexpr int IdRemoveFlightFrame_ = wxID_HIGHEST + 413;
    static constexpr int IdAddImpactFrame_ = wxID_HIGHEST + 414;
    static constexpr int IdRemoveImpactFrame_ = wxID_HIGHEST + 415;
};

class WeaponDefinitionEditorDialog final : public wxDialog {
public:
    WeaponDefinitionEditorDialog(wxWindow* parent, WeaponDefinition& weapon, const std::vector<SheetSpriteCollectionDef>& spriteCollections, const std::vector<ProjectileDefinition>& projectileDefinitions)
        : wxDialog(parent, wxID_ANY, "Edit Weapon", wxDefaultPosition, wxSize(620, 360), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
          weapon_(weapon),
          spriteCollections_(spriteCollections),
          projectileDefinitions_(projectileDefinitions),
          working_(weapon) {
        if (working_.name.empty()) {
            working_.name = working_.id.empty() ? "weapon" : working_.id;
        }

        auto* root = new wxBoxSizer(wxVERTICAL);
        auto* grid = new wxFlexGridSizer(2, 4, 8, 8);
        grid->AddGrowableCol(1, 1);
        grid->AddGrowableCol(3, 1);

        grid->Add(new wxStaticText(this, wxID_ANY, "Name"), 0, wxALIGN_CENTER_VERTICAL);
        nameCtrl_ = new wxTextCtrl(this, wxID_ANY, wxString::FromUTF8(working_.name));
        grid->Add(nameCtrl_, 1, wxEXPAND);

        grid->Add(new wxStaticText(this, wxID_ANY, "Damage"), 0, wxALIGN_CENTER_VERTICAL);
        damageCtrl_ = new wxSpinCtrl(this, wxID_ANY);
        damageCtrl_->SetRange(0, 999);
        damageCtrl_->SetValue(std::max(0, working_.damage));
        grid->Add(damageCtrl_, 1, wxEXPAND);

        projectileCheck_ = new wxCheckBox(this, wxID_ANY, "Projectile weapon");
        projectileCheck_->SetValue(working_.isProjectile);
        grid->Add(projectileCheck_, 0, wxALIGN_CENTER_VERTICAL);

        projectileChoice_ = new wxChoice(this, wxID_ANY);
        projectileChoice_->Append("(none)");
        int projectileSelection = 0;
        for (size_t i = 0; i < projectileDefinitions_.size(); ++i) {
            const ProjectileDefinition& projectile = projectileDefinitions_[i];
            projectileChoice_->Append(wxString::FromUTF8(projectile.name.empty() ? projectile.id : projectile.name));
            if (projectile.id == working_.projectileDefinitionId) {
                projectileSelection = static_cast<int>(i) + 1;
            }
        }
        projectileChoice_->SetSelection(projectileSelection);
        grid->Add(projectileChoice_, 1, wxEXPAND);

        root->Add(grid, 0, wxEXPAND | wxALL, 12);

        auto* spriteRow = new wxBoxSizer(wxHORIZONTAL);
        spriteButton_ = new wxButton(this, wxID_ANY, "Pick HUD Sprite...");
        spriteRow->Add(spriteButton_, 0, wxRIGHT, 8);
        spriteLabel_ = new wxStaticText(this, wxID_ANY, "No sprite selected");
        spriteRow->Add(spriteLabel_, 1, wxALIGN_CENTER_VERTICAL);
        root->Add(spriteRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);

        auto* buttons = new wxBoxSizer(wxHORIZONTAL);
        buttons->AddStretchSpacer(1);
        buttons->Add(new wxButton(this, wxID_OK, "OK"), 0, wxRIGHT, 6);
        buttons->Add(new wxButton(this, wxID_CANCEL, "Cancel"), 0);
        root->Add(buttons, 0, wxEXPAND | wxALL, 12);

        SetSizer(root);

        spriteButton_->Bind(wxEVT_BUTTON, &WeaponDefinitionEditorDialog::OnPickSprite, this);
        projectileCheck_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { UpdateProjectileEnablement(); });
        Bind(wxEVT_BUTTON, &WeaponDefinitionEditorDialog::OnOk, this, wxID_OK);

        UpdateSpriteLabel();
        UpdateProjectileEnablement();
    }

private:
    void UpdateProjectileEnablement() {
        const bool projectile = projectileCheck_ && projectileCheck_->GetValue();
        if (projectileChoice_) {
            projectileChoice_->Enable(projectile);
        }
    }

    void UpdateSpriteLabel() {
        if (!spriteLabel_) {
            return;
        }
        if (working_.hudSprite.sourceImagePath.empty()) {
            spriteLabel_->SetLabel("No sprite selected");
            return;
        }
        const wxString label = working_.hudSprite.sourceLabel.empty()
            ? wxString::Format("src(%d,%d) %dx%d", working_.hudSprite.sourceX, working_.hudSprite.sourceY, working_.hudSprite.sourceW, working_.hudSprite.sourceH)
            : wxString::FromUTF8(working_.hudSprite.sourceLabel);
        spriteLabel_->SetLabel(label);
    }

    void OnPickSprite(wxCommandEvent&) {
        SpriteLibraryPickerDialog dlg(this, spriteCollections_);
        if (dlg.ShowModal() != wxID_OK) {
            return;
        }
        SheetSpritePick pick;
        if (!dlg.GetSelectedPick(pick)) {
            return;
        }
        working_.hudSprite.sourceImagePath = pick.sourceImagePath;
        working_.hudSprite.sourceLabel = pick.collectionName + " (" + std::to_string(pick.sourceX) + "," + std::to_string(pick.sourceY) + ")";
        working_.hudSprite.sourceX = pick.sourceX;
        working_.hudSprite.sourceY = pick.sourceY;
        working_.hudSprite.sourceW = pick.width;
        working_.hudSprite.sourceH = pick.height;
        UpdateSpriteLabel();
    }

    void OnOk(wxCommandEvent&) {
        working_.name = nameCtrl_ ? nameCtrl_->GetValue().ToStdString() : working_.name;
        if (working_.name.empty()) {
            working_.name = working_.id.empty() ? "weapon" : working_.id;
        }
        working_.damage = std::max(0, damageCtrl_ ? damageCtrl_->GetValue() : working_.damage);
        working_.isProjectile = projectileCheck_ && projectileCheck_->GetValue();
        if (working_.isProjectile) {
            const int selection = projectileChoice_ ? projectileChoice_->GetSelection() : wxNOT_FOUND;
            if (selection > 0 && selection - 1 < static_cast<int>(projectileDefinitions_.size())) {
                working_.projectileDefinitionId = projectileDefinitions_[static_cast<size_t>(selection - 1)].id;
            } else {
                working_.projectileDefinitionId.clear();
            }
        } else {
            working_.projectileDefinitionId.clear();
        }

        weapon_ = working_;
        EndModal(wxID_OK);
    }

    WeaponDefinition& weapon_;
    const std::vector<SheetSpriteCollectionDef>& spriteCollections_;
    const std::vector<ProjectileDefinition>& projectileDefinitions_;
    WeaponDefinition working_;

    wxTextCtrl* nameCtrl_ = nullptr;
    wxSpinCtrl* damageCtrl_ = nullptr;
    wxCheckBox* projectileCheck_ = nullptr;
    wxChoice* projectileChoice_ = nullptr;
    wxButton* spriteButton_ = nullptr;
    wxStaticText* spriteLabel_ = nullptr;
};

class EnemyDefinitionEditorDialog final : public wxDialog {
public:
    EnemyDefinitionEditorDialog(wxWindow* parent, EnemyDefinition& enemy, const std::vector<SheetSpriteCollectionDef>& spriteCollections, const std::vector<ProjectileDefinition>& projectileDefinitions, const std::vector<EnemyDropTable>& dropTables, const std::vector<WeaponDefinition>& weaponDefinitions, bool npcMode = false)
                : wxDialog(parent, wxID_ANY, npcMode ? "Edit NPC" : "Edit Enemy", wxDefaultPosition, wxSize(1140, 920), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
          enemy_(enemy),
          spriteCollections_(spriteCollections),
          projectileDefinitions_(projectileDefinitions),
          dropTables_(dropTables),
          weaponDefinitions_(weaponDefinitions),
          npcMode_(npcMode),
          working_(enemy) {
        if (working_.name.empty()) {
            working_.name = working_.id;
        }
        if (working_.moves.empty()) {
            working_.moves.push_back(DefaultMove());
        }

        auto* root = new wxBoxSizer(wxVERTICAL);

        auto* meta = new wxFlexGridSizer(2, 4, 8, 8);
        meta->AddGrowableCol(1, 1);
        meta->AddGrowableCol(3, 1);
        meta->Add(new wxStaticText(this, wxID_ANY, "Name"), 0, wxALIGN_CENTER_VERTICAL);
        nameCtrl_ = new wxTextCtrl(this, wxID_ANY, wxString::FromUTF8(working_.name));
        meta->Add(nameCtrl_, 1, wxEXPAND);
        meta->Add(new wxStaticText(this, wxID_ANY, "Hitpoints"), 0, wxALIGN_CENTER_VERTICAL);
        hpCtrl_ = new wxSpinCtrl(this, wxID_ANY);
        hpCtrl_->SetRange(1, 999);
        hpCtrl_->SetValue(std::max(1, working_.hitpoints));
        meta->Add(hpCtrl_, 1, wxEXPAND);
        meta->Add(new wxStaticText(this, wxID_ANY, "Base Damage"), 0, wxALIGN_CENTER_VERTICAL);
        damageCtrl_ = new wxSpinCtrl(this, wxID_ANY);
        damageCtrl_->SetRange(0, 999);
        damageCtrl_->SetValue(std::max(0, working_.baseDamage));
        meta->Add(damageCtrl_, 1, wxEXPAND);
        
        immuneToKnockbackCheck_ = new wxCheckBox(this, wxID_ANY, "Immune to Knockback");
        immuneToKnockbackCheck_->SetValue(working_.immuneToKnockback);
        meta->Add(immuneToKnockbackCheck_, 0, wxALIGN_CENTER_VERTICAL);
        meta->AddStretchSpacer();
        dropTableChoice_ = new wxChoice(this, wxID_ANY);
        dropTableChoice_->Append("<none>");
        for (const EnemyDropTable& table : dropTables_) {
            const std::string label = table.name.empty() ? table.id : table.name;
            dropTableChoice_->Append(wxString::FromUTF8(label));
        }
        int dropTableSelection = 0;
        for (size_t i = 0; i < dropTables_.size(); ++i) {
            if (dropTables_[i].id == working_.dropTableId) {
                dropTableSelection = static_cast<int>(i + 1);
                break;
            }
        }
        dropTableChoice_->SetSelection(dropTableSelection);
        
        root->Add(meta, 0, wxEXPAND | wxALL, 10);

        // Drop table selector on its own full-width row
        auto* dropTableRow = new wxBoxSizer(wxHORIZONTAL);
        dropTableRow->Add(new wxStaticText(this, wxID_ANY, "Drop Table"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
        dropTableRow->Add(dropTableChoice_, 1, wxEXPAND);
        root->Add(dropTableRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);

        if (npcMode_) {
            auto* npcTextRow = new wxBoxSizer(wxVERTICAL);
            npcTextRow->Add(new wxStaticText(this, wxID_ANY, "NPC Interaction Text"), 0, wxBOTTOM, 4);
            npcTextCtrl_ = new wxTextCtrl(this, wxID_ANY, wxString::FromUTF8(working_.npcText), wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE);
            npcTextCtrl_->SetMinSize(wxSize(-1, 80));
            npcTextRow->Add(npcTextCtrl_, 1, wxEXPAND);
            root->Add(npcTextRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);
        }

        auto* invulnRow = new wxBoxSizer(wxHORIZONTAL);

        auto* weaponInvulnBox = new wxStaticBoxSizer(wxVERTICAL, this, "Invulnerable To Weapons");
        weaponInvulnerableList_ = new wxListBox(this, wxID_ANY, wxDefaultPosition, wxSize(320, 110));
        weaponInvulnBox->Add(weaponInvulnerableList_, 1, wxEXPAND | wxBOTTOM, 6);
        auto* weaponInvulnButtons = new wxBoxSizer(wxHORIZONTAL);
        auto* addWeaponInvulnBtn = new wxButton(this, wxID_ANY, "Add");
        auto* removeWeaponInvulnBtn = new wxButton(this, wxID_ANY, "Remove");
        weaponInvulnButtons->Add(addWeaponInvulnBtn, 1, wxRIGHT, 6);
        weaponInvulnButtons->Add(removeWeaponInvulnBtn, 1);
        weaponInvulnBox->Add(weaponInvulnButtons, 0, wxEXPAND);

        auto* projectileInvulnBox = new wxStaticBoxSizer(wxVERTICAL, this, "Invulnerable To Projectiles");
        projectileInvulnerableList_ = new wxListBox(this, wxID_ANY, wxDefaultPosition, wxSize(320, 110));
        projectileInvulnBox->Add(projectileInvulnerableList_, 1, wxEXPAND | wxBOTTOM, 6);
        auto* projectileInvulnButtons = new wxBoxSizer(wxHORIZONTAL);
        auto* addProjectileInvulnBtn = new wxButton(this, wxID_ANY, "Add");
        auto* removeProjectileInvulnBtn = new wxButton(this, wxID_ANY, "Remove");
        projectileInvulnButtons->Add(addProjectileInvulnBtn, 1, wxRIGHT, 6);
        projectileInvulnButtons->Add(removeProjectileInvulnBtn, 1);
        projectileInvulnBox->Add(projectileInvulnButtons, 0, wxEXPAND);

        invulnRow->Add(weaponInvulnBox, 1, wxRIGHT, 8);
        invulnRow->Add(projectileInvulnBox, 1);
        root->Add(invulnRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);

        auto* body = new wxBoxSizer(wxHORIZONTAL);

        auto* left = new wxBoxSizer(wxVERTICAL);
        left->Add(new wxStaticText(this, wxID_ANY, "Tilesheet"), 0, wxBOTTOM, 4);
        tilesheetChoice_ = new wxChoice(this, wxID_ANY);
        left->Add(tilesheetChoice_, 0, wxEXPAND | wxBOTTOM, 8);
        left->Add(new wxStaticText(this, wxID_ANY, "Tilesheet (click to set selected frame tile source)"), 0, wxBOTTOM, 4);
        sheetPanel_ = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxSize(420, 420), wxHSCROLL | wxVSCROLL | wxBORDER_SIMPLE);
        sheetPanel_->SetScrollRate(12, 12);
        sheetPanel_->SetVirtualSize(wxSize(440, 440));
        sheetPanel_->SetBackgroundStyle(wxBG_STYLE_PAINT);
        left->Add(sheetPanel_, 1, wxEXPAND);
        body->Add(left, 1, wxEXPAND | wxLEFT | wxTOP | wxBOTTOM, 10);

        auto* rightScroll = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
        rightScroll->SetScrollRate(0, 12);
        auto* right = new wxBoxSizer(wxVERTICAL);
        right->Add(new wxStaticText(rightScroll, wxID_ANY, "Move Sequence"), 0, wxBOTTOM, 4);
        moveList_ = new wxListBox(rightScroll, wxID_ANY, wxDefaultPosition, wxSize(360, 140));
        right->Add(moveList_, 0, wxEXPAND | wxBOTTOM, 8);

        right->Add(new wxStaticText(rightScroll, wxID_ANY, "Movement Type"), 0, wxBOTTOM, 4);
        moveTypeChoice_ = new wxChoice(rightScroll, wxID_ANY);
        moveTypeChoice_->Append("move random direction");
        moveTypeChoice_->Append("stand still");
        moveTypeChoice_->Append("disappear");
        moveTypeChoice_->Append("fire projectile");
        right->Add(moveTypeChoice_, 0, wxEXPAND | wxBOTTOM, 8);

        auto* moveButtons = new wxBoxSizer(wxHORIZONTAL);
        auto* addMoveBtn = new wxButton(rightScroll, wxID_ANY, "Add Move");
        auto* removeMoveBtn = new wxButton(rightScroll, wxID_ANY, "Remove Move");
        moveButtons->Add(addMoveBtn, 1, wxRIGHT, 6);
        moveButtons->Add(removeMoveBtn, 1);
        right->Add(moveButtons, 0, wxEXPAND | wxBOTTOM, 8);

        auto* form = new wxFlexGridSizer(2, 6, 8, 8);
        form->AddGrowableCol(1, 1);
        form->Add(new wxStaticText(rightScroll, wxID_ANY, "Min Seconds"), 0, wxALIGN_CENTER_VERTICAL);
        minSecondsCtrl_ = new wxSpinCtrlDouble(rightScroll, wxID_ANY);
        minSecondsCtrl_->SetDigits(1);
        minSecondsCtrl_->SetRange(0.1, 999.0);
        minSecondsCtrl_->SetIncrement(0.1);
        form->Add(minSecondsCtrl_, 1, wxEXPAND);

        form->Add(new wxStaticText(rightScroll, wxID_ANY, "Max Seconds"), 0, wxALIGN_CENTER_VERTICAL);
        maxSecondsCtrl_ = new wxSpinCtrlDouble(rightScroll, wxID_ANY);
        maxSecondsCtrl_->SetDigits(1);
        maxSecondsCtrl_->SetRange(0.1, 999.0);
        maxSecondsCtrl_->SetIncrement(0.1);
        form->Add(maxSecondsCtrl_, 1, wxEXPAND);

        form->Add(new wxStaticText(rightScroll, wxID_ANY, "Speed (tiles/s)"), 0, wxALIGN_CENTER_VERTICAL);
        speedCtrl_ = new wxSpinCtrlDouble(rightScroll, wxID_ANY);
        speedCtrl_->SetDigits(1);
        speedCtrl_->SetRange(0.0, 20.0);
        speedCtrl_->SetIncrement(0.1);
        form->Add(speedCtrl_, 1, wxEXPAND);

        form->Add(new wxStaticText(rightScroll, wxID_ANY, "Reappear"), 0, wxALIGN_CENTER_VERTICAL);
        reappearChoice_ = new wxChoice(rightScroll, wxID_ANY);
        reappearChoice_->Append("same place");
        reappearChoice_->Append("random position");
        form->Add(reappearChoice_, 1, wxEXPAND);

        form->Add(new wxStaticText(rightScroll, wxID_ANY, "Animation FPS"), 0, wxALIGN_CENTER_VERTICAL);
        animationSpeedCtrl_ = new wxSpinCtrlDouble(rightScroll, wxID_ANY);
        animationSpeedCtrl_->SetDigits(1);
        animationSpeedCtrl_->SetRange(0.0, 30.0);
        animationSpeedCtrl_->SetIncrement(0.1);
        form->Add(animationSpeedCtrl_, 1, wxEXPAND);

        form->Add(new wxStaticText(rightScroll, wxID_ANY, "Projectile"), 0, wxALIGN_CENTER_VERTICAL);
        projectileChoice_ = new wxChoice(rightScroll, wxID_ANY);
        projectileChoice_->Append("<first projectile>");
        for (const ProjectileDefinition& projectile : projectileDefinitions_) {
            const std::string label = projectile.name.empty() ? projectile.id : projectile.name;
            projectileChoice_->Append(wxString::FromUTF8(label));
        }
        form->Add(projectileChoice_, 1, wxEXPAND);
        right->Add(form, 0, wxEXPAND | wxBOTTOM, 8);

        auto* animationTargetRow = new wxBoxSizer(wxHORIZONTAL);
        animationTargetRow->Add(new wxStaticText(rightScroll, wxID_ANY, "Animation Target"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
        animationTargetChoice_ = new wxChoice(rightScroll, wxID_ANY);
        animationTargetChoice_->Append("Selected move");
        animationTargetChoice_->Append("Knockback");
        animationTargetChoice_->Append("Death");
        animationTargetChoice_->SetSelection(0);
        animationTargetRow->Add(animationTargetChoice_, 1, wxEXPAND);
        right->Add(animationTargetRow, 0, wxEXPAND | wxBOTTOM, 8);

        auto* directionRow = new wxBoxSizer(wxHORIZONTAL);
        directionRow->Add(new wxStaticText(rightScroll, wxID_ANY, "Direction"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
        directionChoice_ = new wxChoice(rightScroll, wxID_ANY);
        directionChoice_->Append("N");
        directionChoice_->Append("E");
        directionChoice_->Append("S");
        directionChoice_->Append("W");
        directionChoice_->SetSelection(2);
        directionRow->Add(directionChoice_, 0, wxRIGHT, 16);
        right->Add(directionRow, 0, wxEXPAND | wxBOTTOM, 8);

        right->Add(new wxStaticText(rightScroll, wxID_ANY, "Animation Frames"), 0, wxBOTTOM, 4);
        frameList_ = new wxListBox(rightScroll, wxID_ANY, wxDefaultPosition, wxSize(420, 140));
        right->Add(frameList_, 0, wxEXPAND | wxBOTTOM, 8);

        auto* frameGrid = new wxFlexGridSizer(2, 4, 8, 8);
        frameGrid->Add(new wxStaticText(rightScroll, wxID_ANY, "Frame W (tiles)"), 0, wxALIGN_CENTER_VERTICAL);
        frameWCtrl_ = new wxSpinCtrl(rightScroll, wxID_ANY);
        frameWCtrl_->SetRange(1, 8);
        frameGrid->Add(frameWCtrl_, 1, wxEXPAND);
        frameGrid->Add(new wxStaticText(rightScroll, wxID_ANY, "Frame H (tiles)"), 0, wxALIGN_CENTER_VERTICAL);
        frameHCtrl_ = new wxSpinCtrl(rightScroll, wxID_ANY);
        frameHCtrl_->SetRange(1, 8);
        frameGrid->Add(frameHCtrl_, 1, wxEXPAND);
        right->Add(frameGrid, 0, wxEXPAND | wxBOTTOM, 8);

        auto* frameButtons = new wxBoxSizer(wxHORIZONTAL);
        auto* addFrameBtn = new wxButton(rightScroll, wxID_ANY, "Add Animation Frame");
        auto* removeFrameBtn = new wxButton(rightScroll, wxID_ANY, "Remove Frame");
        auto* hitboxBtn = new wxButton(rightScroll, wxID_ANY, "Edit Hitboxes");
        frameButtons->Add(addFrameBtn, 1, wxRIGHT, 6);
        frameButtons->Add(removeFrameBtn, 1, wxRIGHT, 6);
        frameButtons->Add(hitboxBtn, 1);
        right->Add(frameButtons, 0, wxEXPAND | wxBOTTOM, 8);

        frameTileList_ = new wxListBox(rightScroll, wxID_ANY, wxDefaultPosition, wxSize(420, 96));
        frameTileList_->Hide();

        right->Add(new wxStaticText(rightScroll, wxID_ANY, "Animation Preview"), 0, wxBOTTOM, 4);
        previewPanel_ = new wxPanel(rightScroll, wxID_ANY, wxDefaultPosition, wxSize(420, 150));
        previewPanel_->SetBackgroundStyle(wxBG_STYLE_PAINT);
        right->Add(previewPanel_, 0, wxEXPAND);

        rightScroll->SetSizer(right);
        right->FitInside(rightScroll);
        body->Add(rightScroll, 1, wxEXPAND | wxALL, 10);
        root->Add(body, 1, wxEXPAND);

        auto* buttons = new wxStdDialogButtonSizer();
        buttons->AddButton(new wxButton(this, wxID_OK, "OK"));
        buttons->AddButton(new wxButton(this, wxID_CANCEL, "Cancel"));
        buttons->Realize();
        root->Add(buttons, 0, wxALIGN_RIGHT | wxALL, 10);
        SetSizer(root);

        BuildTilesheetChoice();

        moveTypeChoice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { ApplyMoveUiToSelected(); });
        directionChoice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
            previewElapsed_ = 0.0f;
            previewFrameIndex_ = 0;
            RebuildFrameList();
            SyncFrameSizeControlsFromSelection();
            RebuildFrameTileList();
            SyncTilesheetSelectionFromFrameTile();
            if (sheetPanel_) {
                sheetPanel_->Refresh();
            }
            if (previewPanel_) {
                previewPanel_->Refresh();
            }
        });
        reappearChoice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { ApplyMoveUiToSelected(); });
        projectileChoice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { ApplyMoveUiToSelected(); });
        animationTargetChoice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
            previewElapsed_ = 0.0f;
            previewFrameIndex_ = 0;
            SyncMoveUiFromSelected();
            RebuildFrameList();
            SyncFrameSizeControlsFromSelection();
            RebuildFrameTileList();
            SyncTilesheetSelectionFromFrameTile();
            if (sheetPanel_) {
                sheetPanel_->Refresh();
            }
            if (previewPanel_) {
                previewPanel_->Refresh();
            }
        });
        minSecondsCtrl_->Bind(wxEVT_SPINCTRLDOUBLE, [this](wxSpinDoubleEvent&) { ApplyMoveUiToSelected(); });
        maxSecondsCtrl_->Bind(wxEVT_SPINCTRLDOUBLE, [this](wxSpinDoubleEvent&) { ApplyMoveUiToSelected(); });
        speedCtrl_->Bind(wxEVT_SPINCTRLDOUBLE, [this](wxSpinDoubleEvent&) { ApplyMoveUiToSelected(); });
        animationSpeedCtrl_->Bind(wxEVT_SPINCTRLDOUBLE, [this](wxSpinDoubleEvent&) { ApplyMoveUiToSelected(); });

        moveList_->Bind(wxEVT_LISTBOX, [this](wxCommandEvent&) {
            previewElapsed_ = 0.0f;
            previewFrameIndex_ = 0;
            SyncMoveUiFromSelected();
            RebuildFrameList();
            RebuildFrameTileList();
            SyncTilesheetSelectionFromFrameTile();
            if (sheetPanel_) {
                sheetPanel_->Refresh();
            }
            if (previewPanel_) {
                previewPanel_->Refresh();
            }
        });

        frameList_->Bind(wxEVT_LISTBOX, [this](wxCommandEvent&) {
            previewElapsed_ = 0.0f;
            previewFrameIndex_ = std::max(0, frameList_->GetSelection());
            SyncFrameSizeControlsFromSelection();
            RebuildFrameTileList();
            SyncTilesheetSelectionFromFrameTile();
            if (sheetPanel_) {
                sheetPanel_->Refresh();
            }
            if (previewPanel_) {
                previewPanel_->Refresh();
            }
        });

        auto onFrameSizeChanged = [this](wxCommandEvent&) {
            ApplyFrameSizeControlsToSelected();
        };
        frameWCtrl_->Bind(wxEVT_SPINCTRL, onFrameSizeChanged);
        frameHCtrl_->Bind(wxEVT_SPINCTRL, onFrameSizeChanged);

        frameTileList_->Bind(wxEVT_LISTBOX, [this](wxCommandEvent&) {
            SyncTilesheetSelectionFromFrameTile();
            if (sheetPanel_) {
                sheetPanel_->Refresh();
            }
            if (previewPanel_) {
                previewPanel_->Refresh();
            }
        });

        tilesheetChoice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
            ApplySelectedTilesheetToCurrentFrameTile();
            if (sheetPanel_) {
                sheetPanel_->Refresh();
            }
        });

        addWeaponInvulnBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { AddWeaponInvulnerability(); });
        removeWeaponInvulnBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { RemoveWeaponInvulnerability(); });
        addProjectileInvulnBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { AddProjectileInvulnerability(); });
        removeProjectileInvulnBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { RemoveProjectileInvulnerability(); });

        sheetPanel_->Bind(wxEVT_PAINT, &EnemyDefinitionEditorDialog::OnSheetPaint, this);
        sheetPanel_->Bind(wxEVT_LEFT_DOWN, &EnemyDefinitionEditorDialog::OnSheetClick, this);
        previewPanel_->Bind(wxEVT_PAINT, &EnemyDefinitionEditorDialog::OnPreviewPaint, this);

        addMoveBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            working_.moves.push_back(DefaultMove());
            RebuildMoveList();
            moveList_->SetSelection(static_cast<int>(working_.moves.size()) - 1);
            SyncMoveUiFromSelected();
            RebuildFrameList();
            RebuildFrameTileList();
            if (previewPanel_) {
                previewPanel_->Refresh();
            }
        });

        removeMoveBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            const int index = moveList_->GetSelection();
            if (index == wxNOT_FOUND || index < 0 || index >= static_cast<int>(working_.moves.size()) || working_.moves.size() <= 1) {
                return;
            }
            working_.moves.erase(working_.moves.begin() + index);
            RebuildMoveList();
            moveList_->SetSelection(std::max(0, index - 1));
            SyncMoveUiFromSelected();
            RebuildFrameList();
            RebuildFrameTileList();
            if (previewPanel_) {
                previewPanel_->Refresh();
            }
        });

        addFrameBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            EnemyMoveDefinition* move = SelectedMove();
            if (!move) {
                return;
            }

            const int frameW = std::max(1, frameWCtrl_ ? frameWCtrl_->GetValue() : 1);
            const int frameH = std::max(1, frameHCtrl_ ? frameHCtrl_->GetValue() : 1);

            EnemyMoveDefinition::AnimationFrame frame;
            frame.frameWidth = frameW;
            frame.frameHeight = frameH;

            const SheetSpriteCollectionDef* sheet = SelectedTilesheetCollection();
            const int tileW = sheet ? std::max(1, sheet->tileWidth) : 16;
            const int tileH = sheet ? std::max(1, sheet->tileHeight) : 16;
            const std::string sourcePath = sheet ? sheet->sourceImagePath : std::string();
            const std::string sourceName = sheet ? (!sheet->name.empty() ? sheet->name : sheet->id) : std::string("sheet");

            for (int ty = 0; ty < frameH; ++ty) {
                for (int tx = 0; tx < frameW; ++tx) {
                    EnemyMoveDefinition::AnimationTile tile;
                    tile.sourceImagePath = sourcePath;
                    tile.sourceLabel = sourceName + " (0,0)";
                    tile.sourceX = 0;
                    tile.sourceY = 0;
                    tile.sourceW = tileW;
                    tile.sourceH = tileH;
                    tile.tileX = tx;
                    tile.tileY = ty;
                    frame.tiles.push_back(tile);
                }
            }

            std::vector<EnemyMoveDefinition::AnimationFrame>* frames = ActiveFramesForSelectedDirection();
            if (!frames) {
                return;
            }
            frames->push_back(frame);
            RebuildFrameList();
            frameList_->SetSelection(static_cast<int>(frames->size()) - 1);
            SyncFrameSizeControlsFromSelection();
            RebuildFrameTileList();
            frameTileList_->SetSelection(0);
            SyncTilesheetSelectionFromFrameTile();
            if (sheetPanel_) {
                sheetPanel_->Refresh();
            }
            if (previewPanel_) {
                previewPanel_->Refresh();
            }
        });

        removeFrameBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            const int frameIndex = frameList_->GetSelection();
            std::vector<EnemyMoveDefinition::AnimationFrame>* frames = ActiveFramesForSelectedDirection();
            if (!frames || frameIndex == wxNOT_FOUND || frameIndex < 0 || frameIndex >= static_cast<int>(frames->size())) {
                return;
            }
            frames->erase(frames->begin() + frameIndex);
            RebuildFrameList();
            if (frameList_->GetCount() > 0) {
                frameList_->SetSelection(std::max(0, frameIndex - 1));
            }
            RebuildFrameTileList();
            SyncTilesheetSelectionFromFrameTile();
            if (sheetPanel_) {
                sheetPanel_->Refresh();
            }
            if (previewPanel_) {
                previewPanel_->Refresh();
            }
        });

        hitboxBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            EnemyMoveDefinition* move = SelectedMove();
            if (!move) {
                return;
            }

            TileCollection proxyCollection;
            proxyCollection.tileWidth = 16;
            proxyCollection.tileHeight = 16;
            wxBitmap atlas;
            TileDef proxyTile;
            proxyTile.id = 0;
            proxyTile.name = "Enemy Move Hitbox";
            const EnemyMoveDefinition::AnimationFrame* frame = SelectedFrame();
            if (!frame) {
                frame = FirstEnemyFrame(*move);
            }
            if (frame) {
                const wxSize frameSize = EnemyFramePixelSize(*frame);
                proxyCollection.tileWidth = std::max(1, frameSize.GetWidth());
                proxyCollection.tileHeight = std::max(1, frameSize.GetHeight());
                proxyTile.sourceX = 0;
                proxyTile.sourceY = 0;
                atlas = BuildEnemyFramePreviewBitmap(frame, 1, wxColour(24, 29, 36));
            }
            proxyTile.hitboxes = move->hitboxes;
            if (proxyTile.hitboxes.empty()) {
                proxyTile.hitboxes.push_back(TileHitbox{0, 0, proxyCollection.tileWidth, proxyCollection.tileHeight});
            }

            TileHitboxEditor editor(this, proxyTile, atlas, proxyCollection, true);
            if (editor.ShowModal() != wxID_OK) {
                return;
            }
            move->hitboxes = proxyTile.hitboxes;
            if (move->hitboxes.empty()) {
                move->hitboxes.push_back(TileHitbox{0, 0, proxyCollection.tileWidth, proxyCollection.tileHeight});
            }
            RebuildMoveList();
        });

        previewTimer_.Bind(wxEVT_TIMER, [this](wxTimerEvent&) {
            const std::vector<EnemyMoveDefinition::AnimationFrame>* frames = ActiveFramesForSelectedDirection();
            const float animationSpeed = EditingKnockbackAnimation()
                ? working_.knockbackAnimation.animationSpeed
                : EditingDeathAnimation()
                    ? working_.deathAnimation.animationSpeed
                    : (SelectedMove() ? SelectedMove()->animationSpeed : 0.0f);
            if (!frames || frames->size() <= 1 || animationSpeed <= 0.0f) {
                return;
            }

            const float speed = std::max(0.1f, animationSpeed);
            previewElapsed_ += 0.016f;
            const float frameDuration = 1.0f / speed;
            if (previewElapsed_ >= frameDuration) {
                previewElapsed_ = 0.0f;
                previewFrameIndex_ = (previewFrameIndex_ + 1) % static_cast<int>(frames->size());
                if (previewPanel_) {
                    previewPanel_->Refresh();
                }
            }
        });
        previewTimer_.Start(16);

        selectedInvulnerableWeaponIds_ = working_.invulnerableToWeaponIds;
        selectedInvulnerableProjectileIds_ = working_.invulnerableToProjectileIds;
        RebuildInvulnerabilityLists();
        if (npcMode_) {
            if (dropTableChoice_) {
                dropTableChoice_->Enable(false);
            }
            if (weaponInvulnerableList_) {
                weaponInvulnerableList_->Enable(false);
            }
            if (projectileInvulnerableList_) {
                projectileInvulnerableList_->Enable(false);
            }
            addWeaponInvulnBtn->Enable(false);
            removeWeaponInvulnBtn->Enable(false);
            addProjectileInvulnBtn->Enable(false);
            removeProjectileInvulnBtn->Enable(false);
        }

        Bind(wxEVT_BUTTON, &EnemyDefinitionEditorDialog::OnOk, this, wxID_OK);

        RebuildMoveList();
        moveList_->SetSelection(0);
        SyncMoveUiFromSelected();
        RebuildFrameList();
        SyncFrameSizeControlsFromSelection();
        RebuildFrameTileList();
        SyncTilesheetSelectionFromFrameTile();
    }

private:
    static EnemyMoveDefinition DefaultMove() {
        EnemyMoveDefinition move;
        move.type = EnemyMoveType::StandStill;
        move.minSeconds = 1.0f;
        move.maxSeconds = 1.0f;
        move.speedTilesPerSecond = 1.0f;
        move.reappearMode = EnemyReappearMode::SamePlace;
        move.animationSpeed = 0.0f;
        move.hitboxes.push_back(TileHitbox{0, 0, 12, 12});
        return move;
    }

    EnemyMoveDefinition* SelectedMove() {
        const int index = moveList_ ? moveList_->GetSelection() : wxNOT_FOUND;
        if (index == wxNOT_FOUND || index < 0 || index >= static_cast<int>(working_.moves.size())) {
            return nullptr;
        }
        return &working_.moves[static_cast<size_t>(index)];
    }

    const EnemyMoveDefinition* SelectedMove() const {
        const int index = moveList_ ? moveList_->GetSelection() : wxNOT_FOUND;
        if (index == wxNOT_FOUND || index < 0 || index >= static_cast<int>(working_.moves.size())) {
            return nullptr;
        }
        return &working_.moves[static_cast<size_t>(index)];
    }

    int SelectedDirectionIndex() const {
        return EnemyEditorDirectionChoiceToIndex(directionChoice_ ? directionChoice_->GetSelection() : 2);
    }

    bool EditingKnockbackAnimation() const {
        return animationTargetChoice_ && animationTargetChoice_->GetSelection() == 1;
    }

    bool EditingDeathAnimation() const {
        return animationTargetChoice_ && animationTargetChoice_->GetSelection() == 2;
    }

    std::vector<EnemyMoveDefinition::AnimationFrame>* FramesForSelectedDirection(EnemyMoveDefinition& move) {
        return EnemyFramesForDirection(move, SelectedDirectionIndex());
    }

    const std::vector<EnemyMoveDefinition::AnimationFrame>* FramesForSelectedDirection(const EnemyMoveDefinition& move) const {
        return EnemyFramesForDirection(move, SelectedDirectionIndex());
    }

    std::vector<EnemyMoveDefinition::AnimationFrame>* ActiveFramesForSelectedDirection() {
        if (EditingKnockbackAnimation()) {
            return &working_.knockbackAnimation.directionalFrames[static_cast<size_t>(SelectedDirectionIndex())];
        }
        if (EditingDeathAnimation()) {
            return &working_.deathAnimation.directionalFrames[static_cast<size_t>(SelectedDirectionIndex())];
        }
        EnemyMoveDefinition* move = SelectedMove();
        return move ? FramesForSelectedDirection(*move) : nullptr;
    }

    const std::vector<EnemyMoveDefinition::AnimationFrame>* ActiveFramesForSelectedDirection() const {
        if (EditingKnockbackAnimation()) {
            return &working_.knockbackAnimation.directionalFrames[static_cast<size_t>(SelectedDirectionIndex())];
        }
        if (EditingDeathAnimation()) {
            return &working_.deathAnimation.directionalFrames[static_cast<size_t>(SelectedDirectionIndex())];
        }
        const EnemyMoveDefinition* move = SelectedMove();
        return move ? FramesForSelectedDirection(*move) : nullptr;
    }

    EnemyMoveDefinition::AnimationFrame* SelectedFrame() {
        const int frameIndex = frameList_ ? frameList_->GetSelection() : wxNOT_FOUND;
        std::vector<EnemyMoveDefinition::AnimationFrame>* frames = ActiveFramesForSelectedDirection();
        if (!frames || frameIndex == wxNOT_FOUND || frameIndex < 0 || frameIndex >= static_cast<int>(frames->size())) {
            return nullptr;
        }
        return &(*frames)[static_cast<size_t>(frameIndex)];
    }

    const EnemyMoveDefinition::AnimationFrame* SelectedFrame() const {
        const int frameIndex = frameList_ ? frameList_->GetSelection() : wxNOT_FOUND;
        const std::vector<EnemyMoveDefinition::AnimationFrame>* frames = ActiveFramesForSelectedDirection();
        if (!frames || frameIndex == wxNOT_FOUND || frameIndex < 0 || frameIndex >= static_cast<int>(frames->size())) {
            return nullptr;
        }
        return &(*frames)[static_cast<size_t>(frameIndex)];
    }

    EnemyMoveDefinition::AnimationTile* SelectedFrameTile() {
        EnemyMoveDefinition::AnimationFrame* frame = SelectedFrame();
        const int tileIndex = frameTileList_ ? frameTileList_->GetSelection() : wxNOT_FOUND;
        if (!frame || tileIndex == wxNOT_FOUND || tileIndex < 0 || tileIndex >= static_cast<int>(frame->tiles.size())) {
            return nullptr;
        }
        return &frame->tiles[static_cast<size_t>(tileIndex)];
    }

    const SheetSpriteCollectionDef* SelectedTilesheetCollection() const {
        const int index = tilesheetChoice_ ? tilesheetChoice_->GetSelection() : wxNOT_FOUND;
        if (index == wxNOT_FOUND || index < 0 || index >= static_cast<int>(spriteCollections_.size())) {
            return nullptr;
        }
        return &spriteCollections_[static_cast<size_t>(index)];
    }

    void BuildTilesheetChoice() {
        if (!tilesheetChoice_) {
            return;
        }
        tilesheetChoice_->Clear();
        for (const SheetSpriteCollectionDef& collection : spriteCollections_) {
            const wxString label = collection.name.empty() ? wxString::FromUTF8(collection.id) : wxString::FromUTF8(collection.name);
            tilesheetChoice_->Append(label);
        }
        if (!spriteCollections_.empty()) {
            tilesheetChoice_->SetSelection(0);
        }
        UpdateSheetVirtualSize();
    }

    void UpdateSheetVirtualSize() {
        if (!sheetPanel_) {
            return;
        }
        const SheetSpriteCollectionDef* collection = SelectedTilesheetCollection();
        const int scale = 2;
        const int w = collection ? std::max(64, collection->atlas.GetWidth() * scale + 16) : 440;
        const int h = collection ? std::max(64, collection->atlas.GetHeight() * scale + 16) : 440;
        sheetPanel_->SetVirtualSize(wxSize(w, h));
    }

    void SelectTilesheetBySourcePath(const std::string& sourceImagePath) {
        if (!tilesheetChoice_ || sourceImagePath.empty()) {
            return;
        }
        for (size_t i = 0; i < spriteCollections_.size(); ++i) {
            if (spriteCollections_[i].sourceImagePath == sourceImagePath) {
                tilesheetChoice_->SetSelection(static_cast<int>(i));
                return;
            }
        }
    }

    void ApplySelectedTilesheetToCurrentFrameTile() {
        EnemyMoveDefinition::AnimationTile* tile = SelectedFrameTile();
        const SheetSpriteCollectionDef* sheet = SelectedTilesheetCollection();
        if (!tile || !sheet) {
            return;
        }

        tile->sourceImagePath = sheet->sourceImagePath;
        tile->sourceW = std::max(1, sheet->tileWidth);
        tile->sourceH = std::max(1, sheet->tileHeight);
        tile->sourceLabel = (sheet->name.empty() ? sheet->id : sheet->name) + " (" + std::to_string(tile->sourceX) + "," + std::to_string(tile->sourceY) + ")";
        RebuildFrameTileList();
        if (frameTileList_) {
            frameTileList_->SetSelection(std::max(0, frameTileList_->GetSelection()));
        }
        if (previewPanel_) {
            previewPanel_->Refresh();
        }
    }

    void SyncTilesheetSelectionFromFrameTile() {
        const EnemyMoveDefinition::AnimationTile* tile = SelectedFrameTile();
        if (!tile) {
            return;
        }
        SelectTilesheetBySourcePath(tile->sourceImagePath);
    }

    void RebuildMoveList() {
        moveList_->Clear();
        for (size_t i = 0; i < working_.moves.size(); ++i) {
            const EnemyMoveDefinition& move = working_.moves[i];
            wxString label = wxString::Format("%d. %s  %.1fs-%.1fs", static_cast<int>(i + 1), EnemyMoveTypeLabel(move.type), move.minSeconds, move.maxSeconds);
            if (move.type == EnemyMoveType::FireProjectile && !move.projectileDefinitionId.empty()) {
                label += "  [" + wxString::FromUTF8(move.projectileDefinitionId) + "]";
            }
            moveList_->Append(label);
        }
    }

    void RebuildFrameList() {
        frameList_->Clear();
        const std::vector<EnemyMoveDefinition::AnimationFrame>* frames = ActiveFramesForSelectedDirection();
        if (!frames) {
            return;
        }
        for (size_t i = 0; i < frames->size(); ++i) {
            const EnemyMoveDefinition::AnimationFrame& frame = (*frames)[i];
            const wxSize px = EnemyFramePixelSize(frame);
            const wxString label = wxString::Format(
                "frame %d  %dx%d tiles  (%dx%d px)  sprites=%d",
                static_cast<int>(i + 1),
                std::max(1, frame.frameWidth),
                std::max(1, frame.frameHeight),
                px.GetWidth(),
                px.GetHeight(),
                static_cast<int>(frame.tiles.size())
            );
            frameList_->Append(label);
        }
        if (frameList_->GetCount() > 0) {
            const int selected = frameList_->GetSelection();
            frameList_->SetSelection(std::clamp(selected, 0, static_cast<int>(frameList_->GetCount()) - 1));
        }
    }

    void RebuildFrameTileList() {
        frameTileList_->Clear();
        const EnemyMoveDefinition::AnimationFrame* frame = SelectedFrame();
        if (!frame) {
            return;
        }

        for (size_t i = 0; i < frame->tiles.size(); ++i) {
            const EnemyMoveDefinition::AnimationTile& tile = frame->tiles[i];
            const wxString label = wxString::Format(
                "tile %d: dst(%d,%d) src(%d,%d) %dx%d",
                static_cast<int>(i + 1),
                tile.tileX,
                tile.tileY,
                tile.sourceX,
                tile.sourceY,
                tile.sourceW,
                tile.sourceH
            );
            frameTileList_->Append(label);
        }

        if (frameTileList_->GetCount() > 0) {
            const int selected = frameTileList_->GetSelection();
            frameTileList_->SetSelection(std::clamp(selected, 0, static_cast<int>(frameTileList_->GetCount()) - 1));
        }
    }

    void SyncMoveUiFromSelected() {
        const EnemyMoveDefinition* move = SelectedMove();
        if (!move) {
            return;
        }
        moveTypeChoice_->SetSelection(EnemyMoveTypeChoiceIndex(move->type));
        minSecondsCtrl_->SetValue(move->minSeconds);
        maxSecondsCtrl_->SetValue(move->maxSeconds);
        speedCtrl_->SetValue(move->speedTilesPerSecond);
        reappearChoice_->SetSelection(EnemyReappearModeChoiceIndex(move->reappearMode));
        if (move->projectileDefinitionId.empty()) {
            projectileChoice_->SetSelection(0);
        } else {
            int projectileSelection = 0;
            for (size_t i = 0; i < projectileDefinitions_.size(); ++i) {
                if (projectileDefinitions_[i].id == move->projectileDefinitionId) {
                    projectileSelection = static_cast<int>(i + 1);
                    break;
                }
            }
            projectileChoice_->SetSelection(projectileSelection);
        }
        animationSpeedCtrl_->SetValue(EditingKnockbackAnimation() ? working_.knockbackAnimation.animationSpeed
            : EditingDeathAnimation() ? working_.deathAnimation.animationSpeed
            : move->animationSpeed);
        UpdateMoveFieldEnablement(move->type);
    }

    void ApplyMoveUiToSelected() {
        EnemyMoveDefinition* move = SelectedMove();
        if (!move) {
            return;
        }
        if (EditingKnockbackAnimation()) {
            working_.knockbackAnimation.animationSpeed = static_cast<float>(animationSpeedCtrl_->GetValue());
            UpdateMoveFieldEnablement(move->type);
            if (previewPanel_) {
                previewPanel_->Refresh();
            }
            return;
        }
        if (EditingDeathAnimation()) {
            working_.deathAnimation.animationSpeed = static_cast<float>(animationSpeedCtrl_->GetValue());
            UpdateMoveFieldEnablement(move->type);
            if (previewPanel_) {
                previewPanel_->Refresh();
            }
            return;
        }
        const int selectedMoveIndex = moveList_ ? moveList_->GetSelection() : wxNOT_FOUND;
        move->type = EnemyMoveTypeFromChoiceIndex(moveTypeChoice_->GetSelection());
        move->minSeconds = static_cast<float>(minSecondsCtrl_->GetValue());
        move->maxSeconds = std::max(move->minSeconds, static_cast<float>(maxSecondsCtrl_->GetValue()));
        move->speedTilesPerSecond = static_cast<float>(speedCtrl_->GetValue());
        move->reappearMode = EnemyReappearModeFromChoiceIndex(reappearChoice_->GetSelection());
        move->projectileDefinitionId = projectileChoice_ && projectileChoice_->GetSelection() > 0
            ? projectileDefinitions_[static_cast<size_t>(projectileChoice_->GetSelection() - 1)].id
            : std::string();
        move->animationSpeed = static_cast<float>(animationSpeedCtrl_->GetValue());
        RebuildMoveList();
        if (selectedMoveIndex != wxNOT_FOUND && moveList_ && moveList_->GetCount() > 0) {
            moveList_->SetSelection(std::clamp(selectedMoveIndex, 0, static_cast<int>(moveList_->GetCount()) - 1));
        }
        UpdateMoveFieldEnablement(move->type);
        if (previewPanel_) {
            previewPanel_->Refresh();
        }
    }

    void UpdateMoveFieldEnablement(EnemyMoveType type) {
        const bool editingKnockback = EditingKnockbackAnimation();
        const bool editingDeath = EditingDeathAnimation();
        const bool editingReaction = editingKnockback || editingDeath;
        const bool enableMoveSpeed = !editingReaction && type == EnemyMoveType::MoveRandomDirection;
        const bool enableReappear = !editingReaction && type == EnemyMoveType::Disappear;
        const bool enableProjectile = !editingReaction && type == EnemyMoveType::FireProjectile;

        if (moveTypeChoice_) {
            moveTypeChoice_->Enable(!editingReaction);
        }
        if (minSecondsCtrl_) {
            minSecondsCtrl_->Enable(!editingReaction);
        }
        if (maxSecondsCtrl_) {
            maxSecondsCtrl_->Enable(!editingReaction);
        }
        if (speedCtrl_) {
            speedCtrl_->Enable(enableMoveSpeed);
        }
        if (reappearChoice_) {
            reappearChoice_->Enable(enableReappear);
        }
        if (projectileChoice_) {
            projectileChoice_->Enable(enableProjectile);
        }
    }

    void RebuildInvulnerabilityLists() {
        if (weaponInvulnerableList_) {
            weaponInvulnerableList_->Clear();
            for (const std::string& id : selectedInvulnerableWeaponIds_) {
                weaponInvulnerableList_->Append(wxString::FromUTF8(id));
            }
        }
        if (projectileInvulnerableList_) {
            projectileInvulnerableList_->Clear();
            for (const std::string& id : selectedInvulnerableProjectileIds_) {
                projectileInvulnerableList_->Append(wxString::FromUTF8(id));
            }
        }
    }

    void AddWeaponInvulnerability() {
        wxArrayString choices;
        std::vector<std::string> ids;
        for (const WeaponDefinition& weapon : weaponDefinitions_) {
            ids.push_back(weapon.id);
            const std::string label = weapon.name.empty() ? weapon.id : (weapon.name + " (" + weapon.id + ")");
            choices.Add(wxString::FromUTF8(label));
        }
        if (choices.empty()) {
            return;
        }
        wxSingleChoiceDialog dlg(this, "Choose a weapon", "Add Weapon Invulnerability", choices);
        if (dlg.ShowModal() != wxID_OK) {
            return;
        }
        const int sel = dlg.GetSelection();
        if (sel == wxNOT_FOUND || sel < 0 || sel >= static_cast<int>(ids.size())) {
            return;
        }
        const std::string& id = ids[static_cast<size_t>(sel)];
        if (std::find(selectedInvulnerableWeaponIds_.begin(), selectedInvulnerableWeaponIds_.end(), id) == selectedInvulnerableWeaponIds_.end()) {
            selectedInvulnerableWeaponIds_.push_back(id);
            RebuildInvulnerabilityLists();
        }
    }

    void RemoveWeaponInvulnerability() {
        if (!weaponInvulnerableList_) {
            return;
        }
        const int sel = weaponInvulnerableList_->GetSelection();
        if (sel == wxNOT_FOUND || sel < 0 || sel >= static_cast<int>(selectedInvulnerableWeaponIds_.size())) {
            return;
        }
        selectedInvulnerableWeaponIds_.erase(selectedInvulnerableWeaponIds_.begin() + sel);
        RebuildInvulnerabilityLists();
    }

    void AddProjectileInvulnerability() {
        wxArrayString choices;
        std::vector<std::string> ids;
        for (const ProjectileDefinition& projectile : projectileDefinitions_) {
            ids.push_back(projectile.id);
            const std::string label = projectile.name.empty() ? projectile.id : (projectile.name + " (" + projectile.id + ")");
            choices.Add(wxString::FromUTF8(label));
        }
        if (choices.empty()) {
            return;
        }
        wxSingleChoiceDialog dlg(this, "Choose a projectile", "Add Projectile Invulnerability", choices);
        if (dlg.ShowModal() != wxID_OK) {
            return;
        }
        const int sel = dlg.GetSelection();
        if (sel == wxNOT_FOUND || sel < 0 || sel >= static_cast<int>(ids.size())) {
            return;
        }
        const std::string& id = ids[static_cast<size_t>(sel)];
        if (std::find(selectedInvulnerableProjectileIds_.begin(), selectedInvulnerableProjectileIds_.end(), id) == selectedInvulnerableProjectileIds_.end()) {
            selectedInvulnerableProjectileIds_.push_back(id);
            RebuildInvulnerabilityLists();
        }
    }

    void RemoveProjectileInvulnerability() {
        if (!projectileInvulnerableList_) {
            return;
        }
        const int sel = projectileInvulnerableList_->GetSelection();
        if (sel == wxNOT_FOUND || sel < 0 || sel >= static_cast<int>(selectedInvulnerableProjectileIds_.size())) {
            return;
        }
        selectedInvulnerableProjectileIds_.erase(selectedInvulnerableProjectileIds_.begin() + sel);
        RebuildInvulnerabilityLists();
    }

    void EnsureFrameTilesMatchSize(EnemyMoveDefinition::AnimationFrame& frame) {
        const int targetW = std::max(1, frame.frameWidth);
        const int targetH = std::max(1, frame.frameHeight);

        std::vector<EnemyMoveDefinition::AnimationTile> updated;
        updated.reserve(static_cast<size_t>(targetW * targetH));

        for (int ty = 0; ty < targetH; ++ty) {
            for (int tx = 0; tx < targetW; ++tx) {
                EnemyMoveDefinition::AnimationTile* existing = nullptr;
                for (EnemyMoveDefinition::AnimationTile& tile : frame.tiles) {
                    if (tile.tileX == tx && tile.tileY == ty) {
                        existing = &tile;
                        break;
                    }
                }

                if (existing) {
                    updated.push_back(*existing);
                    continue;
                }

                EnemyMoveDefinition::AnimationTile tile;
                const SheetSpriteCollectionDef* sheet = SelectedTilesheetCollection();
                const int tileW = sheet ? std::max(1, sheet->tileWidth) : 16;
                const int tileH = sheet ? std::max(1, sheet->tileHeight) : 16;
                tile.sourceImagePath = sheet ? sheet->sourceImagePath : std::string();
                tile.sourceLabel = sheet ? (sheet->name.empty() ? sheet->id : sheet->name) + std::string(" (0,0)") : std::string("sheet (0,0)");
                tile.sourceX = 0;
                tile.sourceY = 0;
                tile.sourceW = tileW;
                tile.sourceH = tileH;
                tile.tileX = tx;
                tile.tileY = ty;
                updated.push_back(tile);
            }
        }

        frame.tiles = std::move(updated);
    }

    void SyncFrameSizeControlsFromSelection() {
        const EnemyMoveDefinition::AnimationFrame* frame = SelectedFrame();
        if (!frame || !frameWCtrl_ || !frameHCtrl_) {
            return;
        }
        frameWCtrl_->SetValue(std::max(1, frame->frameWidth));
        frameHCtrl_->SetValue(std::max(1, frame->frameHeight));
    }

    void ApplyFrameSizeControlsToSelected() {
        EnemyMoveDefinition::AnimationFrame* frame = SelectedFrame();
        if (!frame || !frameWCtrl_ || !frameHCtrl_) {
            return;
        }

        const int newW = std::max(1, frameWCtrl_->GetValue());
        const int newH = std::max(1, frameHCtrl_->GetValue());
        if (frame->frameWidth == newW && frame->frameHeight == newH) {
            return;
        }

        frame->frameWidth = newW;
        frame->frameHeight = newH;
        EnsureFrameTilesMatchSize(*frame);

        const int selectedFrame = frameList_ ? frameList_->GetSelection() : wxNOT_FOUND;
        RebuildFrameList();
        if (frameList_ && frameList_->GetCount() > 0 && selectedFrame != wxNOT_FOUND) {
            frameList_->SetSelection(std::clamp(selectedFrame, 0, static_cast<int>(frameList_->GetCount()) - 1));
        }
        RebuildFrameTileList();
        if (sheetPanel_) {
            sheetPanel_->Refresh();
        }
        if (previewPanel_) {
            previewPanel_->Refresh();
        }
    }

    int ActivePreviewFrameIndex() const {
        const std::vector<EnemyMoveDefinition::AnimationFrame>* frames = ActiveFramesForSelectedDirection();
        const float animationSpeed = EditingKnockbackAnimation()
            ? working_.knockbackAnimation.animationSpeed
            : EditingDeathAnimation()
                ? working_.deathAnimation.animationSpeed
                : (SelectedMove() ? SelectedMove()->animationSpeed : 0.0f);
        if (!frames || frames->empty()) {
            return -1;
        }

        if (frames->size() == 1 || animationSpeed <= 0.0f) {
            return std::clamp(frameList_->GetSelection(), 0, static_cast<int>(frames->size()) - 1);
        }

        return std::clamp(previewFrameIndex_, 0, static_cast<int>(frames->size()) - 1);
    }

    void OnSheetPaint(wxPaintEvent&) {
        wxAutoBufferedPaintDC dc(sheetPanel_);
        sheetPanel_->PrepareDC(dc);
        dc.SetBackground(wxBrush(wxColour(19, 24, 31)));
        dc.Clear();

        const SheetSpriteCollectionDef* collection = SelectedTilesheetCollection();
        if (!collection) {
            dc.SetTextForeground(wxColour(190, 196, 208));
            dc.DrawText("No tilesheet selected", 8, 8);
            return;
        }

        const int scale = 2;
        wxBitmap atlas = collection->atlas.IsOk() ? collection->atlas : LoadBitmapMaybeRelative(collection->sourceImagePath);
        if (atlas.IsOk()) {
            sheetPanel_->SetVirtualSize(wxSize(std::max(64, atlas.GetWidth() * scale + 16), std::max(64, atlas.GetHeight() * scale + 16)));
        }
        if (atlas.IsOk()) {
            wxMemoryDC atlasDc;
            atlasDc.SelectObject(atlas);
            dc.StretchBlit(8, 8, atlas.GetWidth() * scale, atlas.GetHeight() * scale, &atlasDc, 0, 0, atlas.GetWidth(), atlas.GetHeight());
            atlasDc.SelectObject(wxNullBitmap);
        } else {
            dc.SetTextForeground(wxColour(190, 196, 208));
            dc.DrawText("Unable to load tilesheet", 8, 8);
        }

        const EnemyMoveDefinition::AnimationFrame* frame = SelectedFrame();
        const EnemyMoveDefinition::AnimationTile* tile00 = nullptr;
        if (frame) {
            for (const EnemyMoveDefinition::AnimationTile& tile : frame->tiles) {
                if (tile.tileX == 0 && tile.tileY == 0) {
                    tile00 = &tile;
                    break;
                }
            }
            if (!tile00 && !frame->tiles.empty()) {
                tile00 = &frame->tiles.front();
            }
        }

        if (frame && tile00 && tile00->sourceImagePath == collection->sourceImagePath) {
            const int tileW = std::max(1, collection->tileWidth);
            const int tileH = std::max(1, collection->tileHeight);
            dc.SetPen(wxPen(wxColour(255, 215, 88), 2));
            dc.SetBrush(*wxTRANSPARENT_BRUSH);
            dc.DrawRectangle(
                8 + tile00->sourceX * scale,
                8 + tile00->sourceY * scale,
                std::max(1, frame->frameWidth) * tileW * scale,
                std::max(1, frame->frameHeight) * tileH * scale
            );
        }
    }

    void OnSheetClick(wxMouseEvent& event) {
        EnemyMoveDefinition::AnimationFrame* frame = SelectedFrame();
        const SheetSpriteCollectionDef* sheet = SelectedTilesheetCollection();
        if (!frame || !sheet) {
            return;
        }

        const int scale = 2;
        const wxPoint unscrolled = sheetPanel_->CalcUnscrolledPosition(event.GetPosition());
        const int localX = unscrolled.x - 8;
        const int localY = unscrolled.y - 8;
        if (localX < 0 || localY < 0) {
            return;
        }

        const int tileW = std::max(1, sheet->tileWidth);
        const int tileH = std::max(1, sheet->tileHeight);
        const int sx = std::max(0, localX / (tileW * scale));
        const int sy = std::max(0, localY / (tileH * scale));

        EnsureFrameTilesMatchSize(*frame);
        for (EnemyMoveDefinition::AnimationTile& tile : frame->tiles) {
            tile.sourceImagePath = sheet->sourceImagePath;
            tile.sourceX = (sx + tile.tileX) * tileW;
            tile.sourceY = (sy + tile.tileY) * tileH;
            tile.sourceW = tileW;
            tile.sourceH = tileH;
            tile.sourceLabel = (sheet->name.empty() ? sheet->id : sheet->name) + " (" + std::to_string(tile.sourceX) + "," + std::to_string(tile.sourceY) + ")";
        }

        const int selectedTile = frameTileList_->GetSelection();
        RebuildFrameTileList();
        if (frameTileList_->GetCount() > 0) {
            frameTileList_->SetSelection(std::clamp(selectedTile, 0, static_cast<int>(frameTileList_->GetCount()) - 1));
        }
        if (sheetPanel_) {
            sheetPanel_->Refresh();
        }
        if (previewPanel_) {
            previewPanel_->Refresh();
        }
    }

    void OnPreviewPaint(wxPaintEvent&) {
        wxAutoBufferedPaintDC dc(previewPanel_);
        dc.SetBackground(wxBrush(wxColour(16, 18, 22)));
        dc.Clear();

        const std::vector<EnemyMoveDefinition::AnimationFrame>* frames = ActiveFramesForSelectedDirection();
        if (!frames || frames->empty()) {
            return;
        }

        const int frameIndex = ActivePreviewFrameIndex();
        if (frameIndex < 0 || frameIndex >= static_cast<int>(frames->size())) {
            return;
        }

        const EnemyMoveDefinition::AnimationFrame& frame = (*frames)[static_cast<size_t>(frameIndex)];
        const wxSize frameSize = EnemyFramePixelSize(frame);
        const wxSize panelSize = previewPanel_->GetClientSize();
        const int availableW = std::max(32, panelSize.GetWidth() - 16);
        const int availableH = std::max(32, panelSize.GetHeight() - 16);
        int scale = std::max(1, std::min(availableW / std::max(1, frameSize.GetWidth()), availableH / std::max(1, frameSize.GetHeight())));
        wxBitmap composed = BuildEnemyFramePreviewBitmap(&frame, std::max(1, scale), wxColour(16, 18, 22));
        if (composed.GetWidth() > availableW || composed.GetHeight() > availableH) {
            wxImage image = composed.ConvertToImage();
            const float sx = static_cast<float>(availableW) / static_cast<float>(std::max(1, composed.GetWidth()));
            const float sy = static_cast<float>(availableH) / static_cast<float>(std::max(1, composed.GetHeight()));
            const float s = std::max(0.1f, std::min(sx, sy));
            image.Rescale(
                std::max(1, static_cast<int>(std::round(static_cast<float>(composed.GetWidth()) * s))),
                std::max(1, static_cast<int>(std::round(static_cast<float>(composed.GetHeight()) * s))),
                wxIMAGE_QUALITY_NEAREST
            );
            composed = wxBitmap(image);
        }
        const int x = std::max(8, (panelSize.x - composed.GetWidth()) / 2);
        const int y = std::max(8, (panelSize.y - composed.GetHeight()) / 2);
        dc.DrawBitmap(composed, x, y, true);
    }

    void OnOk(wxCommandEvent&) {
        ApplyMoveUiToSelected();
        working_.name = nameCtrl_->GetValue().ToStdString();
        if (working_.name.empty()) {
            working_.name = working_.id.empty() ? "enemy" : working_.id;
        }
        working_.hitpoints = std::max(1, hpCtrl_->GetValue());
        working_.baseDamage = std::max(0, damageCtrl_ ? damageCtrl_->GetValue() : 1);
        working_.immuneToKnockback = immuneToKnockbackCheck_ ? immuneToKnockbackCheck_->GetValue() : false;
        if (working_.moves.empty()) {
            wxMessageBox("Enemy needs at least one move.", "Enemy Validation", wxOK | wxICON_WARNING, this);
            return;
        }

        if (npcMode_) {
            working_.isNpc = true;
            working_.npcText = npcTextCtrl_ ? npcTextCtrl_->GetValue().ToStdString() : std::string();
            working_.hitpoints = 1;
            working_.baseDamage = 0;
            working_.immuneToKnockback = true;
            for (EnemyMoveDefinition& move : working_.moves) {
                if (move.type != EnemyMoveType::MoveRandomDirection && move.type != EnemyMoveType::StandStill) {
                    move.type = EnemyMoveType::StandStill;
                    move.reappearMode = EnemyReappearMode::SamePlace;
                    move.projectileDefinitionId.clear();
                    move.speedTilesPerSecond = 0.0f;
                }
            }
        } else {
            working_.isNpc = false;
            working_.npcText.clear();
        }

        if (dropTableChoice_ && dropTableChoice_->GetSelection() > 0
            && static_cast<size_t>(dropTableChoice_->GetSelection() - 1) < dropTables_.size()) {
            working_.dropTableId = dropTables_[static_cast<size_t>(dropTableChoice_->GetSelection() - 1)].id;
        } else {
            working_.dropTableId.clear();
        }
        working_.invulnerableToWeaponIds = selectedInvulnerableWeaponIds_;
        working_.invulnerableToProjectileIds = selectedInvulnerableProjectileIds_;

        if (npcMode_) {
            working_.dropTableId.clear();
            working_.invulnerableToWeaponIds.clear();
            working_.invulnerableToProjectileIds.clear();
        }

        for (EnemyMoveDefinition& move : working_.moves) {
            if (move.hitboxes.empty()) {
                move.hitboxes.push_back(TileHitbox{0, 0, 12, 12});
            }
        }
        enemy_ = working_;
        EndModal(wxID_OK);
    }

    EnemyDefinition& enemy_;
    const std::vector<SheetSpriteCollectionDef>& spriteCollections_;
    const std::vector<ProjectileDefinition>& projectileDefinitions_;
    const std::vector<EnemyDropTable>& dropTables_;
    const std::vector<WeaponDefinition>& weaponDefinitions_;
    bool npcMode_ = false;
    EnemyDefinition working_;
    std::vector<std::string> selectedInvulnerableWeaponIds_;
    std::vector<std::string> selectedInvulnerableProjectileIds_;

    wxTextCtrl* nameCtrl_ = nullptr;
    wxSpinCtrl* hpCtrl_ = nullptr;
    wxSpinCtrl* damageCtrl_ = nullptr;
    wxTextCtrl* npcTextCtrl_ = nullptr;
    wxCheckBox* immuneToKnockbackCheck_ = nullptr;
    wxChoice* dropTableChoice_ = nullptr;
    wxListBox* weaponInvulnerableList_ = nullptr;
    wxListBox* projectileInvulnerableList_ = nullptr;
    wxChoice* tilesheetChoice_ = nullptr;
    wxScrolledWindow* sheetPanel_ = nullptr;
    wxListBox* moveList_ = nullptr;
    wxChoice* moveTypeChoice_ = nullptr;
    wxChoice* projectileChoice_ = nullptr;
    wxChoice* animationTargetChoice_ = nullptr;
    wxChoice* directionChoice_ = nullptr;
    wxSpinCtrlDouble* minSecondsCtrl_ = nullptr;
    wxSpinCtrlDouble* maxSecondsCtrl_ = nullptr;
    wxSpinCtrlDouble* speedCtrl_ = nullptr;
    wxChoice* reappearChoice_ = nullptr;
    wxSpinCtrlDouble* animationSpeedCtrl_ = nullptr;
    wxListBox* frameList_ = nullptr;
    wxSpinCtrl* frameWCtrl_ = nullptr;
    wxSpinCtrl* frameHCtrl_ = nullptr;
    wxListBox* frameTileList_ = nullptr;
    wxPanel* previewPanel_ = nullptr;
    wxTimer previewTimer_;
    int previewFrameIndex_ = 0;
    float previewElapsed_ = 0.0f;
};

class DropTableEditorDialog final : public wxDialog {
public:
    DropTableEditorDialog(wxWindow* parent, EnemyDropTable& table, const std::vector<ItemDefinition>& itemDefinitions)
        : wxDialog(parent, wxID_ANY, "Edit Drop Table", wxDefaultPosition, wxSize(560, 500), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
          table_(table),
          itemDefinitions_(itemDefinitions),
          working_(table) {
        auto* root = new wxBoxSizer(wxVERTICAL);

        auto* nameRow = new wxBoxSizer(wxHORIZONTAL);
        nameRow->Add(new wxStaticText(this, wxID_ANY, "Name"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
        nameCtrl_ = new wxTextCtrl(this, wxID_ANY, wxString::FromUTF8(working_.name));
        nameRow->Add(nameCtrl_, 1, wxEXPAND);
        root->Add(nameRow, 0, wxEXPAND | wxALL, 10);

        root->Add(new wxStaticText(this, wxID_ANY, "Entries"), 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);
        entryList_ = new wxListBox(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 260));
        root->Add(entryList_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);

        auto* buttonRow = new wxBoxSizer(wxHORIZONTAL);
        auto* addBtn = new wxButton(this, wxID_ANY, "Add Entry");
        auto* editBtn = new wxButton(this, wxID_ANY, "Edit Entry");
        auto* removeBtn = new wxButton(this, wxID_ANY, "Remove Entry");
        buttonRow->Add(addBtn, 1, wxRIGHT, 6);
        buttonRow->Add(editBtn, 1, wxRIGHT, 6);
        buttonRow->Add(removeBtn, 1);
        root->Add(buttonRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);

        auto* helpText = new wxStaticText(this, wxID_ANY, "Weights are relative probabilities. Total can be <= 100.");
        root->Add(helpText, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);

        auto* buttons = new wxStdDialogButtonSizer();
        buttons->AddButton(new wxButton(this, wxID_OK, "OK"));
        buttons->AddButton(new wxButton(this, wxID_CANCEL, "Cancel"));
        buttons->Realize();
        root->Add(buttons, 0, wxALIGN_RIGHT | wxALL, 10);
        SetSizer(root);

        addBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { AddEntry(); });
        editBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EditEntry(); });
        removeBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { RemoveEntry(); });
        Bind(wxEVT_BUTTON, &DropTableEditorDialog::OnOk, this, wxID_OK);

        RebuildEntryList();
    }

private:
    std::vector<size_t> SelectableItemDefinitionIndexes() const {
        std::vector<size_t> indexes;
        for (size_t i = 0; i < itemDefinitions_.size(); ++i) {
            if (!itemDefinitions_[i].isContainer) {
                indexes.push_back(i);
            }
        }
        return indexes;
    }

    int CurrentWeightSumExcluding(int excludeIndex) const {
        int total = 0;
        for (size_t i = 0; i < working_.entries.size(); ++i) {
            if (static_cast<int>(i) == excludeIndex) {
                continue;
            }
            total += std::max(0, working_.entries[i].weight);
        }
        return total;
    }

    void RebuildEntryList() {
        if (!entryList_) {
            return;
        }
        entryList_->Clear();
        int total = 0;
        for (const EnemyDropEntry& entry : working_.entries) {
            total += std::max(0, entry.weight);
            entryList_->Append(wxString::Format("%s | weight=%d", entry.itemId, entry.weight));
        }
        if (GetSizer()) {
            SetTitle(wxString::Format("Edit Drop Table (%d total weight)", total));
        }
    }

    bool PromptForEntry(EnemyDropEntry& outEntry, int editingIndex) {
        const std::vector<size_t> itemIndexes = SelectableItemDefinitionIndexes();
        if (itemIndexes.empty()) {
            wxMessageBox("No non-container item definitions found.", "Drop Table", wxOK | wxICON_WARNING, this);
            return false;
        }

        wxArrayString choices;
        int selectedChoice = 0;
        for (size_t i = 0; i < itemIndexes.size(); ++i) {
            const ItemDefinition& def = itemDefinitions_[itemIndexes[i]];
            const std::string label = def.name.empty() ? def.id : (def.name + " (" + def.id + ")");
            choices.Add(wxString::FromUTF8(label));
            if (def.id == outEntry.itemId) {
                selectedChoice = static_cast<int>(i);
            }
        }

        wxSingleChoiceDialog itemDlg(this, "Choose item", "Drop Entry", choices);
        itemDlg.SetSelection(selectedChoice);
        if (itemDlg.ShowModal() != wxID_OK) {
            return false;
        }
        const int itemSel = itemDlg.GetSelection();
        if (itemSel == wxNOT_FOUND || itemSel < 0 || itemSel >= static_cast<int>(itemIndexes.size())) {
            return false;
        }

        const ItemDefinition& selectedDef = itemDefinitions_[itemIndexes[static_cast<size_t>(itemSel)]];
        int currentWeight = std::max(0, outEntry.weight);
        const int maxWeight = std::max(0, 100 - CurrentWeightSumExcluding(editingIndex));
        const int weight = static_cast<int>(wxGetNumberFromUser(
            "Weight (0-100)",
            "weight",
            "Drop Entry",
            std::clamp(currentWeight, 0, maxWeight),
            0,
            maxWeight,
            this));
        if (weight < 0) {
            return false;
        }

        outEntry.itemId = selectedDef.id;
        outEntry.weight = weight;
        return true;
    }

    void AddEntry() {
        EnemyDropEntry entry;
        if (!PromptForEntry(entry, -1)) {
            return;
        }
        working_.entries.push_back(entry);
        RebuildEntryList();
    }

    void EditEntry() {
        const int sel = entryList_ ? entryList_->GetSelection() : wxNOT_FOUND;
        if (sel == wxNOT_FOUND || sel < 0 || sel >= static_cast<int>(working_.entries.size())) {
            return;
        }
        EnemyDropEntry entry = working_.entries[static_cast<size_t>(sel)];
        if (!PromptForEntry(entry, sel)) {
            return;
        }
        working_.entries[static_cast<size_t>(sel)] = entry;
        RebuildEntryList();
        if (entryList_->GetCount() > 0) {
            entryList_->SetSelection(std::clamp(sel, 0, static_cast<int>(entryList_->GetCount()) - 1));
        }
    }

    void RemoveEntry() {
        const int sel = entryList_ ? entryList_->GetSelection() : wxNOT_FOUND;
        if (sel == wxNOT_FOUND || sel < 0 || sel >= static_cast<int>(working_.entries.size())) {
            return;
        }
        working_.entries.erase(working_.entries.begin() + sel);
        RebuildEntryList();
    }

    void OnOk(wxCommandEvent&) {
        working_.name = nameCtrl_ ? nameCtrl_->GetValue().ToStdString() : std::string();
        if (working_.name.empty()) {
            working_.name = working_.id;
        }
        table_ = working_;
        EndModal(wxID_OK);
    }

    EnemyDropTable& table_;
    const std::vector<ItemDefinition>& itemDefinitions_;
    EnemyDropTable working_;
    wxTextCtrl* nameCtrl_ = nullptr;
    wxListBox* entryList_ = nullptr;
};

class WarpDefinitionEditorDialog final : public wxDialog {
public:
    WarpDefinitionEditorDialog(wxWindow* parent, WarpDefinition& definition, const WorldLoadData& world, int fallbackTileId)
        : wxDialog(parent, wxID_ANY, "Edit Warp Definition", wxDefaultPosition, wxSize(840, 620), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
          definition_(definition),
          world_(world),
          working_(definition),
          fallbackTileId_(fallbackTileId) {
        if (working_.name.empty()) {
            working_.name = working_.id;
        }

        auto* root = new wxBoxSizer(wxVERTICAL);

        auto* form = new wxFlexGridSizer(2, 4, 8, 8);
        form->AddGrowableCol(1, 1);
        form->AddGrowableCol(3, 1);

        form->Add(new wxStaticText(this, wxID_ANY, "Name"), 0, wxALIGN_CENTER_VERTICAL);
        nameCtrl_ = new wxTextCtrl(this, wxID_ANY, wxString::FromUTF8(working_.name));
        form->Add(nameCtrl_, 1, wxEXPAND);

        form->Add(new wxStaticText(this, wxID_ANY, "Kind"), 0, wxALIGN_CENTER_VERTICAL);
        wxArrayString kindChoices;
        kindChoices.Add("instant");
        kindChoices.Add("fade");
        kindChoice_ = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, kindChoices);
        kindChoice_->SetSelection(working_.kind == TransitionKind::Fade ? 1 : 0);
        form->Add(kindChoice_, 1, wxEXPAND);

        root->Add(form, 0, wxEXPAND | wxALL, 10);

        auto* endpointsRow = new wxBoxSizer(wxHORIZONTAL);
        endpointsRow->Add(BuildEndpointEditorPanel(0, "Endpoint A"), 1, wxEXPAND | wxRIGHT, 8);
        endpointsRow->Add(BuildEndpointEditorPanel(1, "Endpoint B"), 1, wxEXPAND);
        root->Add(endpointsRow, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);

        auto* buttons = new wxStdDialogButtonSizer();
        buttons->AddButton(new wxButton(this, wxID_OK, "OK"));
        buttons->AddButton(new wxButton(this, wxID_CANCEL, "Cancel"));
        buttons->Realize();
        root->Add(buttons, 0, wxALIGN_RIGHT | wxALL, 10);

        SetSizer(root);

        Bind(wxEVT_BUTTON, &WarpDefinitionEditorDialog::OnOk, this, wxID_OK);

        EnsureEndpointDefaults(0);
        EnsureEndpointDefaults(1);
        RefreshEndpointUi(0);
        RefreshEndpointUi(1);
    }

private:
    struct EndpointWidgets {
        wxStaticBitmap* preview = nullptr;
        wxStaticText* info = nullptr;
        wxChoice* spawnOffsetChoice = nullptr;
        wxButton* pickTileButton = nullptr;
        wxButton* editHitboxesButton = nullptr;
    };

    wxWindow* BuildEndpointEditorPanel(int endpointIndex, const wxString& title) {
        auto* panel = new wxPanel(this);
        auto* box = new wxStaticBoxSizer(wxVERTICAL, panel, title);

        endpointWidgets_[static_cast<size_t>(endpointIndex)].preview = new wxStaticBitmap(panel, wxID_ANY, wxBitmap(128, 128));
        endpointWidgets_[static_cast<size_t>(endpointIndex)].preview->SetMinSize(wxSize(180, 180));
        box->Add(endpointWidgets_[static_cast<size_t>(endpointIndex)].preview, 0, wxEXPAND | wxALL, 6);

        endpointWidgets_[static_cast<size_t>(endpointIndex)].info = new wxStaticText(panel, wxID_ANY, "");
        box->Add(endpointWidgets_[static_cast<size_t>(endpointIndex)].info, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

        auto* offsetRow = new wxBoxSizer(wxHORIZONTAL);
        offsetRow->Add(new wxStaticText(panel, wxID_ANY, "Arrival spawn"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
        endpointWidgets_[static_cast<size_t>(endpointIndex)].spawnOffsetChoice = new wxChoice(panel, wxID_ANY);
        endpointWidgets_[static_cast<size_t>(endpointIndex)].spawnOffsetChoice->Append("on top");
        endpointWidgets_[static_cast<size_t>(endpointIndex)].spawnOffsetChoice->Append("above");
        endpointWidgets_[static_cast<size_t>(endpointIndex)].spawnOffsetChoice->Append("below");
        endpointWidgets_[static_cast<size_t>(endpointIndex)].spawnOffsetChoice->Append("left");
        endpointWidgets_[static_cast<size_t>(endpointIndex)].spawnOffsetChoice->Append("right");
        offsetRow->Add(endpointWidgets_[static_cast<size_t>(endpointIndex)].spawnOffsetChoice, 1, wxEXPAND);
        box->Add(offsetRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

        auto* row = new wxBoxSizer(wxHORIZONTAL);
        endpointWidgets_[static_cast<size_t>(endpointIndex)].pickTileButton = new wxButton(panel, wxID_ANY, "Pick Tile...");
        endpointWidgets_[static_cast<size_t>(endpointIndex)].editHitboxesButton = new wxButton(panel, wxID_ANY, "Edit Hitboxes...");
        row->Add(endpointWidgets_[static_cast<size_t>(endpointIndex)].pickTileButton, 1, wxRIGHT, 6);
        row->Add(endpointWidgets_[static_cast<size_t>(endpointIndex)].editHitboxesButton, 1);
        box->Add(row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

        endpointWidgets_[static_cast<size_t>(endpointIndex)].pickTileButton->Bind(wxEVT_BUTTON, [this, endpointIndex](wxCommandEvent&) {
            PickEndpointTile(endpointIndex);
        });
        endpointWidgets_[static_cast<size_t>(endpointIndex)].editHitboxesButton->Bind(wxEVT_BUTTON, [this, endpointIndex](wxCommandEvent&) {
            EditEndpointHitboxes(endpointIndex);
        });
        endpointWidgets_[static_cast<size_t>(endpointIndex)].spawnOffsetChoice->Bind(wxEVT_CHOICE, [this, endpointIndex](wxCommandEvent&) {
            EndpointWidgets& widgets = endpointWidgets_[static_cast<size_t>(endpointIndex)];
            if (!widgets.spawnOffsetChoice) {
                return;
            }
            working_.endpoints[static_cast<size_t>(endpointIndex)].spawnOffset = WarpSpawnOffsetFromChoiceIndex(widgets.spawnOffsetChoice->GetSelection());
            RefreshEndpointUi(endpointIndex);
        });

        panel->SetSizer(box);
        return panel;
    }

    void EnsureEndpointDefaults(int endpointIndex) {
        WarpEndpointDefinition& endpoint = working_.endpoints[static_cast<size_t>(endpointIndex)];
        if (endpoint.tileId <= 0) {
            endpoint.tileId = fallbackTileId_;
        }
        if (endpoint.hitboxes.empty()) {
            endpoint.hitboxes.push_back(TileHitbox{0, 0, 16, 16});
        }
    }

    wxBitmap BuildEndpointPreviewBitmap(int endpointIndex) const {
        const WarpEndpointDefinition& endpoint = working_.endpoints[static_cast<size_t>(endpointIndex)];
        const wxColour bg(18, 22, 28);
        wxBitmap bitmap(180, 180);
        wxMemoryDC dc;
        dc.SelectObject(bitmap);
        dc.SetBackground(wxBrush(bg));
        dc.Clear();

        const TileCollection* collection = FindTileCollectionByTileId(world_, endpoint.tileId);
        const TileDef* tile = collection ? FindTileDef(*collection, endpoint.tileId) : nullptr;
        if (!collection || !tile) {
            dc.SetTextForeground(wxColour(220, 220, 220));
            dc.DrawText("Tile missing", 12, 12);
            dc.SelectObject(wxNullBitmap);
            return bitmap;
        }

        const int tileW = std::max(1, collection->tileWidth);
        const int tileH = std::max(1, collection->tileHeight);
        const int scale = std::max(4, std::min(10, std::min(150 / tileW, 150 / tileH)));
        const int drawW = tileW * scale;
        const int drawH = tileH * scale;
        const int drawX = (180 - drawW) / 2;
        const int drawY = (180 - drawH) / 2;

        wxBitmap atlas = LoadBitmapMaybeRelative(collection->imagePath);
        if (atlas.IsOk()) {
            wxMemoryDC atlasDc;
            atlasDc.SelectObject(atlas);
            dc.StretchBlit(drawX, drawY, drawW, drawH, &atlasDc, tile->sourceX, tile->sourceY, tileW, tileH);
            atlasDc.SelectObject(wxNullBitmap);
        } else {
            dc.SetBrush(wxBrush(TileColorFromId(tile->id)));
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.DrawRectangle(drawX, drawY, drawW, drawH);
        }

        dc.SetPen(wxPen(wxColour(255, 210, 96), 2));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        for (const TileHitbox& hitbox : endpoint.hitboxes) {
            const int hx = drawX + static_cast<int>(std::round(static_cast<float>(hitbox.x) * scale));
            const int hy = drawY + static_cast<int>(std::round(static_cast<float>(hitbox.y) * scale));
            const int hw = std::max(1, static_cast<int>(std::round(static_cast<float>(hitbox.w) * scale)));
            const int hh = std::max(1, static_cast<int>(std::round(static_cast<float>(hitbox.h) * scale)));
            dc.DrawRectangle(hx, hy, hw, hh);
        }

        dc.SelectObject(wxNullBitmap);
        return bitmap;
    }

    void RefreshEndpointUi(int endpointIndex) {
        EndpointWidgets& widgets = endpointWidgets_[static_cast<size_t>(endpointIndex)];
        if (!widgets.preview || !widgets.info) {
            return;
        }

        const WarpEndpointDefinition& endpoint = working_.endpoints[static_cast<size_t>(endpointIndex)];
        const TileCollection* collection = FindTileCollectionByTileId(world_, endpoint.tileId);
        const TileDef* tile = collection ? FindTileDef(*collection, endpoint.tileId) : nullptr;

        widgets.preview->SetBitmap(BuildEndpointPreviewBitmap(endpointIndex));
        if (widgets.spawnOffsetChoice) {
            widgets.spawnOffsetChoice->SetSelection(WarpSpawnOffsetChoiceIndex(endpoint.spawnOffset));
        }
        if (collection && tile) {
            widgets.info->SetLabel(wxString::Format(
                "%s | id=%d | hitboxes=%d | spawn=%s",
                wxString::FromUTF8(tile->name.empty() ? std::string("tile") : tile->name),
                endpoint.tileId,
                static_cast<int>(endpoint.hitboxes.size()),
                WarpSpawnOffsetLabel(endpoint.spawnOffset)
            ));
        } else {
            widgets.info->SetLabel(wxString::Format(
                "tile id=%d (missing) | spawn=%s",
                endpoint.tileId,
                WarpSpawnOffsetLabel(endpoint.spawnOffset)
            ));
        }
        Layout();
    }

    void PickEndpointTile(int endpointIndex) {
        int initialTileId = working_.endpoints[static_cast<size_t>(endpointIndex)].tileId;
        if (initialTileId <= 0) {
            initialTileId = fallbackTileId_;
        }

        TilePickerDialog picker(this, world_.tileCollections, initialTileId);
        if (picker.ShowModal() != wxID_OK) {
            return;
        }

        working_.endpoints[static_cast<size_t>(endpointIndex)].tileId = picker.GetSelectedTileId();
        EnsureEndpointDefaults(endpointIndex);
        RefreshEndpointUi(endpointIndex);
    }

    void EditEndpointHitboxes(int endpointIndex) {
        WarpEndpointDefinition& endpoint = working_.endpoints[static_cast<size_t>(endpointIndex)];
        const TileCollection* collection = FindTileCollectionByTileId(world_, endpoint.tileId);
        const TileDef* tile = collection ? FindTileDef(*collection, endpoint.tileId) : nullptr;
        if (!collection || !tile) {
            wxMessageBox("Pick a valid tile first.", "Warp Hitboxes", wxOK | wxICON_INFORMATION, this);
            return;
        }

        TileDef proxyTile;
        proxyTile.id = tile->id;
        proxyTile.sourceX = tile->sourceX;
        proxyTile.sourceY = tile->sourceY;
        proxyTile.hitboxes = endpoint.hitboxes;
        proxyTile.hitboxX = 0;
        proxyTile.hitboxY = 0;
        proxyTile.hitboxW = std::max(1, collection->tileWidth);
        proxyTile.hitboxH = std::max(1, collection->tileHeight);

        wxBitmap atlas = LoadBitmapMaybeRelative(collection->imagePath);
        TileHitboxEditor editor(this, proxyTile, atlas, *collection, true);
        if (editor.ShowModal() != wxID_OK) {
            return;
        }

        endpoint.hitboxes = proxyTile.hitboxes;
        if (endpoint.hitboxes.empty()) {
            endpoint.hitboxes.push_back(TileHitbox{0, 0, std::max(1, collection->tileWidth), std::max(1, collection->tileHeight)});
        }
        RefreshEndpointUi(endpointIndex);
    }

    void OnOk(wxCommandEvent&) {
        working_.name = nameCtrl_->GetValue().ToStdString();
        if (working_.name.empty()) {
            working_.name = working_.id.empty() ? "warp" : working_.id;
        }
        working_.kind = kindChoice_->GetSelection() == 1 ? TransitionKind::Fade : TransitionKind::Instant;

        for (int endpointIndex = 0; endpointIndex < 2; ++endpointIndex) {
            const WarpEndpointDefinition& endpoint = working_.endpoints[static_cast<size_t>(endpointIndex)];
            const TileCollection* collection = FindTileCollectionByTileId(world_, endpoint.tileId);
            const TileDef* tile = collection ? FindTileDef(*collection, endpoint.tileId) : nullptr;
            if (!collection || !tile) {
                wxMessageBox(
                    wxString::Format("Endpoint %c has an invalid tile.", endpointIndex == 0 ? 'A' : 'B'),
                    "Warp Validation",
                    wxOK | wxICON_WARNING,
                    this
                );
                return;
            }
        }

        definition_ = working_;
        EndModal(wxID_OK);
    }

private:
    WarpDefinition& definition_;
    const WorldLoadData& world_;
    WarpDefinition working_;
    int fallbackTileId_ = 0;
    wxTextCtrl* nameCtrl_ = nullptr;
    wxChoice* kindChoice_ = nullptr;
    std::array<EndpointWidgets, 2> endpointWidgets_{};
};

class EditorFrame final : public wxFrame {
public:
    EditorFrame()
        : wxFrame(nullptr, wxID_ANY, "Quest for Rome Editor", wxDefaultPosition, wxSize(1520, 920)) {
        BuildUi();
        BuildMenus();
        CreateDefaultWorld();
        TryLoadLastOpenedWorld();
        Bind(wxEVT_CLOSE_WINDOW, &EditorFrame::OnCloseWindow, this);
    }

private:
    enum {
        IdNewWorld = 1001,
        IdOpenWorld,
        IdSaveWorld,
        IdSaveWorldAs,
        IdAddMap,
        IdRemoveMap,
        IdResizeMap,
        IdUseCurrentAsGlobalStart,
        IdCopyScreenToScreen,
        IdAddItem = 2001,
        IdEditItem,
        IdRemoveItem,
        IdAddEnemy = 2101,
        IdEditEnemy,
        IdRemoveEnemy,
        IdAddNpc,
        IdEditNpc,
        IdRemoveNpc,
        IdAddProjectile = 2151,
        IdEditProjectile,
        IdRemoveProjectile,
        IdAddWeapon = 2171,
        IdEditWeapon,
        IdRemoveWeapon,
        IdAddTransition = 2201,
        IdRemoveTransition,
        IdAddWarp = 2251,
        IdEditWarp,
        IdRemoveWarp,
        IdAddPowerupDef = 2301,
        IdEditPowerupDef,
        IdRemovePowerupDef,
        IdAddDropTableDef = 2351,
        IdEditDropTableDef,
        IdRemoveDropTableDef,
        IdAddCharacter = 2401,
        IdEditCharacter,
        IdRemoveCharacter,
        IdAddTileCollectionFromSheet,
        IdAddTileFromSheet,
        IdRemoveTile,
        IdMoveLayer,
    };

    void MarkDirty() {
        dirty_ = true;
        UpdateTitle();
    }

    std::string LastOpenedWorldStatePath() const {
        const std::vector<std::filesystem::path> candidates = {
            "data/editor_last_world.txt",
            "../data/editor_last_world.txt",
            "../../data/editor_last_world.txt"
        };

        for (const std::filesystem::path& p : candidates) {
            if (std::filesystem::exists(p.parent_path())) {
                return p.lexically_normal().string();
            }
        }
        return "data/editor_last_world.txt";
    }

    void PersistLastOpenedWorld() {
        if (currentPath_.empty()) {
            return;
        }
        std::ofstream out(LastOpenedWorldStatePath(), std::ios::trunc);
        if (!out.is_open()) {
            return;
        }
        out << currentPath_;
    }

    bool PromptSaveIfDirty() {
        if (!dirty_) {
            return true;
        }

        const int choice = wxMessageBox(
            "You have unsaved changes. Save before closing?",
            "Unsaved Changes",
            wxYES_NO | wxCANCEL | wxICON_QUESTION,
            this
        );

        if (choice == wxCANCEL) {
            return false;
        }
        if (choice == wxNO) {
            return true;
        }

        if (currentPath_.empty()) {
            wxFileDialog dlg(this, "Save world", "", "world.json", "World JSON (*.json)|*.json", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
            if (dlg.ShowModal() != wxID_OK) {
                return false;
            }
            return SaveToPath(dlg.GetPath().ToStdString());
        }
        return SaveToPath(currentPath_);
    }

    void TryLoadLastOpenedWorld() {
        std::ifstream in(LastOpenedWorldStatePath());
        if (!in.is_open()) {
            return;
        }

        std::string lastPath;
        std::getline(in, lastPath);
        if (lastPath.empty()) {
            return;
        }

        WorldLoadData loaded;
        if (!MapLoader::LoadWorldJson(lastPath, loaded) || loaded.maps.empty()) {
            return;
        }

        world_ = loaded;
        for (MapLoadData& map : world_.maps) {
            EnsureMapScreens(map);
        }
        currentPath_ = lastPath;
        dirty_ = false;
        SelectDefaultStartLocation();
        UpdateTitle();
    }

    void OnCloseWindow(wxCloseEvent& event) {
        CommitActiveCollectionDescriptionFromUi();
        if (!PromptSaveIfDirty()) {
            event.Veto();
            return;
        }
        PersistLastOpenedWorld();
        event.Skip();
    }

    void ApplyDarkTheme(wxWindow* w) {
        w->SetBackgroundColour(wxColour(16, 20, 26));
        w->SetForegroundColour(wxColour(222, 226, 235));
        const wxWindowList& children = w->GetChildren();
        for (wxWindowList::const_iterator it = children.begin(); it != children.end(); ++it) {
            wxWindow* child = *it;
            child->SetBackgroundColour(wxColour(24, 29, 36));
            child->SetForegroundColour(wxColour(222, 226, 235));
            ApplyDarkTheme(child);
        }
    }

    void BuildMenus() {
        auto* bar = new wxMenuBar();
        auto* fileMenu = new wxMenu();
        fileMenu->Append(IdNewWorld, "New World\tCtrl+N");
        fileMenu->Append(IdOpenWorld, "Open...\tCtrl+O");
        fileMenu->Append(IdSaveWorld, "Save\tCtrl+S");
        fileMenu->Append(IdSaveWorldAs, "Save As...");
        fileMenu->AppendSeparator();
        fileMenu->Append(wxID_EXIT, "Quit");

        auto* mapMenu = new wxMenu();
        mapMenu->Append(IdAddMap, "Add Map");
        mapMenu->Append(IdRemoveMap, "Remove Map");
        mapMenu->Append(IdResizeMap, "Resize Current Map");
        mapMenu->Append(IdUseCurrentAsGlobalStart, "Use Current Screen As Global Start");
        mapMenu->Append(IdCopyScreenToScreen, "Copy Screen Tiles...");

        bar->Append(fileMenu, "File");
        bar->Append(mapMenu, "Maps");
        SetMenuBar(bar);

        Bind(wxEVT_MENU, &EditorFrame::OnNewWorld, this, IdNewWorld);
        Bind(wxEVT_MENU, &EditorFrame::OnOpenWorld, this, IdOpenWorld);
        Bind(wxEVT_MENU, &EditorFrame::OnSaveWorld, this, IdSaveWorld);
        Bind(wxEVT_MENU, &EditorFrame::OnSaveWorldAs, this, IdSaveWorldAs);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { Close(true); }, wxID_EXIT);
        Bind(wxEVT_MENU, &EditorFrame::OnAddMap, this, IdAddMap);
        Bind(wxEVT_MENU, &EditorFrame::OnRemoveMap, this, IdRemoveMap);
        Bind(wxEVT_MENU, &EditorFrame::OnResizeCurrentMap, this, IdResizeMap);
        Bind(wxEVT_MENU, &EditorFrame::OnUseCurrentAsGlobalStart, this, IdUseCurrentAsGlobalStart);
        Bind(wxEVT_MENU, &EditorFrame::OnCopyScreenToScreen, this, IdCopyScreenToScreen);
    }

    void BuildUi() {
        auto* root = new wxPanel(this);
        auto* rootSizer = new wxBoxSizer(wxVERTICAL);

        auto* banner = new wxPanel(root);
        banner->SetMinSize(wxSize(-1, 56));
        banner->SetBackgroundColour(wxColour(22, 29, 38));
        auto* bannerSizer = new wxBoxSizer(wxHORIZONTAL);
        auto* title = new wxStaticText(banner, wxID_ANY, "QUEST FOR ROME - WORLD EDITOR");
        title->SetFont(wxFont(13, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Segoe UI"));
        title->SetForegroundColour(wxColour(145, 206, 255));
        bannerSizer->Add(title, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 16);
        banner->SetSizer(bannerSizer);

        splitterOuter_ = new wxSplitterWindow(root);
        splitterOuter_->SetSashGravity(0.25);
        splitterOuter_->SetMinimumPaneSize(260);

        auto* leftPanel = new wxPanel(splitterOuter_);
        centerRightSplit_ = new wxSplitterWindow(splitterOuter_);
        centerRightSplit_->SetSashGravity(0.67);
        centerRightSplit_->SetMinimumPaneSize(300);

        auto* centerPanel = new wxPanel(centerRightSplit_);
        auto* rightPanel = new wxPanel(centerRightSplit_);

        splitterOuter_->SplitVertically(leftPanel, centerRightSplit_, 380);
        centerRightSplit_->SplitVertically(centerPanel, rightPanel, 760);

        rootSizer->Add(banner, 0, wxEXPAND);
        rootSizer->Add(splitterOuter_, 1, wxEXPAND);
        root->SetSizer(rootSizer);

        auto* leftSizer = new wxBoxSizer(wxVERTICAL);
        leftSizer->Add(new wxStaticText(leftPanel, wxID_ANY, "Maps"), 0, wxLEFT | wxTOP, 8);
        mapList_ = new wxListBox(leftPanel, wxID_ANY);
        leftSizer->Add(mapList_, 0, wxEXPAND | wxALL, 8);

        auto* mapButtons = new wxBoxSizer(wxHORIZONTAL);
        mapButtons->Add(new wxButton(leftPanel, IdAddMap, "Add Map"), 1, wxRIGHT, 6);
        mapButtons->Add(new wxButton(leftPanel, IdRemoveMap, "Remove Map"), 1);
        leftSizer->Add(mapButtons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

        auto* mapForm = new wxFlexGridSizer(2, 6, 6);
        mapForm->AddGrowableCol(1, 1);
        mapForm->Add(new wxStaticText(leftPanel, wxID_ANY, "Map ID"), 0, wxALIGN_CENTER_VERTICAL);
        mapIdCtrl_ = new wxTextCtrl(leftPanel, wxID_ANY);
        mapForm->Add(mapIdCtrl_, 1, wxEXPAND);
        mapForm->Add(new wxStaticText(leftPanel, wxID_ANY, "Map Name"), 0, wxALIGN_CENTER_VERTICAL);
        mapNameCtrl_ = new wxTextCtrl(leftPanel, wxID_ANY);
        mapForm->Add(mapNameCtrl_, 1, wxEXPAND);
        mapForm->Add(new wxStaticText(leftPanel, wxID_ANY, "Width"), 0, wxALIGN_CENTER_VERTICAL);
        mapWidthCtrl_ = new wxSpinCtrl(leftPanel, wxID_ANY);
        mapWidthCtrl_->SetRange(1, 20);
        mapForm->Add(mapWidthCtrl_, 0, wxEXPAND);
        mapForm->Add(new wxStaticText(leftPanel, wxID_ANY, "Height"), 0, wxALIGN_CENTER_VERTICAL);
        mapHeightCtrl_ = new wxSpinCtrl(leftPanel, wxID_ANY);
        mapHeightCtrl_->SetRange(1, 20);
        mapForm->Add(mapHeightCtrl_, 0, wxEXPAND);
        mapForm->Add(new wxStaticText(leftPanel, wxID_ANY, "Map Start X"), 0, wxALIGN_CENTER_VERTICAL);
        mapStartXCtrl_ = new wxSpinCtrl(leftPanel, wxID_ANY);
        mapStartXCtrl_->SetRange(0, 19);
        mapForm->Add(mapStartXCtrl_, 0, wxEXPAND);
        mapForm->Add(new wxStaticText(leftPanel, wxID_ANY, "Map Start Y"), 0, wxALIGN_CENTER_VERTICAL);
        mapStartYCtrl_ = new wxSpinCtrl(leftPanel, wxID_ANY);
        mapStartYCtrl_->SetRange(0, 19);
        mapForm->Add(mapStartYCtrl_, 0, wxEXPAND);
        leftSizer->Add(mapForm, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

        auto* resizeBtn = new wxButton(leftPanel, IdResizeMap, "Apply Map Size");
        leftSizer->Add(resizeBtn, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

        auto* globalBox = new wxStaticBoxSizer(wxVERTICAL, leftPanel, "Game Start");
        globalStartMapChoice_ = new wxChoice(leftPanel, wxID_ANY);
        globalStartXCtrl_ = new wxSpinCtrl(leftPanel, wxID_ANY);
        globalStartYCtrl_ = new wxSpinCtrl(leftPanel, wxID_ANY);
        globalStartXCtrl_->SetRange(0, 19);
        globalStartYCtrl_->SetRange(0, 19);
        globalBox->Add(new wxStaticText(leftPanel, wxID_ANY, "Start Map"), 0, wxBOTTOM, 4);
        globalBox->Add(globalStartMapChoice_, 0, wxEXPAND | wxBOTTOM, 6);
        auto* globalGrid = new wxFlexGridSizer(2, 6, 6);
        globalGrid->Add(new wxStaticText(leftPanel, wxID_ANY, "Start Screen X"), 0, wxALIGN_CENTER_VERTICAL);
        globalGrid->Add(globalStartXCtrl_, 0, wxEXPAND);
        globalGrid->Add(new wxStaticText(leftPanel, wxID_ANY, "Start Screen Y"), 0, wxALIGN_CENTER_VERTICAL);
        globalGrid->Add(globalStartYCtrl_, 0, wxEXPAND);
        globalBox->Add(globalGrid, 0, wxEXPAND | wxBOTTOM, 6);
        globalBox->Add(new wxButton(leftPanel, IdUseCurrentAsGlobalStart, "Use Current Screen"), 0, wxEXPAND);
        leftSizer->Add(globalBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

        leftSizer->Add(new wxStaticText(leftPanel, wxID_ANY, "World Layout"), 0, wxLEFT | wxRIGHT | wxTOP, 8);
        worldGrid_ = new WorldGridCanvas(leftPanel);
        leftSizer->Add(worldGrid_, 1, wxEXPAND | wxALL, 8);
        leftPanel->SetSizer(leftSizer);

        auto* centerSizer = new wxBoxSizer(wxVERTICAL);
        auto* tileBar = new wxBoxSizer(wxHORIZONTAL);
        currentScreenLabel_ = new wxStaticText(centerPanel, wxID_ANY, "Screen (0, 0)");
        tileBar->Add(currentScreenLabel_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 16);
        tileBar->AddStretchSpacer(1);
        centerSizer->Add(tileBar, 0, wxEXPAND | wxALL, 8);
        canvas_ = new TileCanvas(centerPanel);
        centerSizer->Add(canvas_, 1, wxEXPAND | wxALL, 8);
        auto* screenTextBox = new wxStaticBoxSizer(wxVERTICAL, centerPanel, "Screen Text");
        displayTextCheck_ = new wxCheckBox(centerPanel, wxID_ANY, "Display text for this screen");
        screenTextBox->Add(displayTextCheck_, 0, wxBOTTOM, 6);
        hideFromMapCheck_ = new wxCheckBox(centerPanel, wxID_ANY, "Hide this screen from the map");
        screenTextBox->Add(hideFromMapCheck_, 0, wxBOTTOM, 6);
        displayTextCtrl_ = new wxTextCtrl(centerPanel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE);
        displayTextCtrl_->SetMinSize(wxSize(-1, 64));
        screenTextBox->Add(displayTextCtrl_, 0, wxEXPAND);
        centerSizer->Add(screenTextBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
        centerPanel->SetSizer(centerSizer);

        auto* rightSizer = new wxBoxSizer(wxVERTICAL);
        notebook_ = new wxNotebook(rightPanel, wxID_ANY);
        wxPanel* tilesPage = new wxPanel(notebook_);
        wxPanel* itemsPage = new wxPanel(notebook_);
        wxPanel* enemiesPage = new wxPanel(notebook_);
        wxPanel* npcsPage = new wxPanel(notebook_);
        wxPanel* projectilesPage = new wxPanel(notebook_);
        wxPanel* weaponsPage = new wxPanel(notebook_);
        wxPanel* transitionsPage = new wxPanel(notebook_);
        wxPanel* warpsPage = new wxPanel(notebook_);
        wxPanel* dropTablesPage = new wxPanel(notebook_);
        wxPanel* globalSettingsPage = new wxPanel(notebook_);
        wxPanel* textPage = new wxPanel(notebook_);
        wxPanel* charactersPage = new wxPanel(notebook_);
        notebook_->AddPage(tilesPage, "Tiles");
        notebook_->AddPage(itemsPage, "Items");
        notebook_->AddPage(enemiesPage, "Enemies");
        notebook_->AddPage(npcsPage, "NPCs");
        notebook_->AddPage(projectilesPage, "Projectiles");
        notebook_->AddPage(weaponsPage, "Weapons");
        notebook_->AddPage(transitionsPage, "Edge Links");
        notebook_->AddPage(warpsPage, "Warps");
        notebook_->AddPage(dropTablesPage, "Drop Tables");
        notebook_->AddPage(globalSettingsPage, "Global Settings");
        notebook_->AddPage(textPage, "Text");
        notebook_->AddPage(charactersPage, "Characters");
        rightSizer->Add(notebook_, 1, wxEXPAND | wxALL, 8);
        rightPanel->SetSizer(rightSizer);

        auto* itemSizer = new wxBoxSizer(wxVERTICAL);
        itemSizer->Add(new wxStaticText(itemsPage, wxID_ANY, "Item Definitions"), 0, wxLEFT | wxRIGHT | wxTOP, 8);
        itemPalette_ = new ItemPalettePanel(itemsPage);
        itemSizer->Add(itemPalette_, 1, wxEXPAND | wxALL, 8);
        auto* itemButtons = new wxBoxSizer(wxHORIZONTAL);
        itemButtons->Add(new wxButton(itemsPage, IdAddItem, "Add Item"), 1, wxRIGHT, 4);
        itemButtons->Add(new wxButton(itemsPage, IdEditItem, "Edit Item"), 1, wxRIGHT, 4);
        itemButtons->Add(new wxButton(itemsPage, IdRemoveItem, "Remove"), 1);
        itemSizer->Add(itemButtons, 0, wxEXPAND | wxALL, 8);
        itemSizer->Add(new wxStaticText(itemsPage, wxID_ANY, "Select an item here, switch canvas mode to Place Items, then click on the map to place or remove it on the current screen."), 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
        itemsPage->SetSizer(itemSizer);

        auto* enemySizer = new wxBoxSizer(wxVERTICAL);
        enemySizer->Add(new wxStaticText(enemiesPage, wxID_ANY, "Enemy Definitions"), 0, wxLEFT | wxRIGHT | wxTOP, 8);
        enemyPalette_ = new EnemyPalettePanel(enemiesPage);
        enemySizer->Add(enemyPalette_, 1, wxEXPAND | wxALL, 8);
        auto* enemyButtons = new wxBoxSizer(wxHORIZONTAL);
        enemyButtons->Add(new wxButton(enemiesPage, IdAddEnemy, "Add Enemy"), 1, wxRIGHT, 4);
        enemyButtons->Add(new wxButton(enemiesPage, IdEditEnemy, "Edit Enemy"), 1, wxRIGHT, 4);
        enemyButtons->Add(new wxButton(enemiesPage, IdRemoveEnemy, "Remove"), 1);
        enemySizer->Add(enemyButtons, 0, wxEXPAND | wxALL, 8);
        enemySizer->Add(new wxStaticText(enemiesPage, wxID_ANY, "Right-click enemy on map to make it active. Click with active enemy to place, click existing active enemy to remove it."), 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
        enemiesPage->SetSizer(enemySizer);

        auto* npcSizer = new wxBoxSizer(wxVERTICAL);
        npcSizer->Add(new wxStaticText(npcsPage, wxID_ANY, "NPC Definitions"), 0, wxLEFT | wxRIGHT | wxTOP, 8);
        npcPalette_ = new NpcPalettePanel(npcsPage);
        npcSizer->Add(npcPalette_, 1, wxEXPAND | wxALL, 8);
        auto* npcButtons = new wxBoxSizer(wxHORIZONTAL);
        npcButtons->Add(new wxButton(npcsPage, IdAddNpc, "Add NPC"), 1, wxRIGHT, 4);
        npcButtons->Add(new wxButton(npcsPage, IdEditNpc, "Edit NPC"), 1, wxRIGHT, 4);
        npcButtons->Add(new wxButton(npcsPage, IdRemoveNpc, "Remove"), 1);
        npcSizer->Add(npcButtons, 0, wxEXPAND | wxALL, 8);
        npcSizer->Add(new wxStaticText(npcsPage, wxID_ANY, "NPCs are placed on the map canvas like enemies and can show interaction text when the action button is pressed nearby."), 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
        npcsPage->SetSizer(npcSizer);

        auto* projectileSizer = new wxBoxSizer(wxVERTICAL);
        projectileSizer->Add(new wxStaticText(projectilesPage, wxID_ANY, "Projectile Definitions"), 0, wxLEFT | wxRIGHT | wxTOP, 8);
        projectilePalette_ = new ProjectilePalettePanel(projectilesPage);
        projectileSizer->Add(projectilePalette_, 1, wxEXPAND | wxALL, 8);
        auto* projectileButtons = new wxBoxSizer(wxHORIZONTAL);
        projectileButtons->Add(new wxButton(projectilesPage, IdAddProjectile, "Add Projectile"), 1, wxRIGHT, 4);
        projectileButtons->Add(new wxButton(projectilesPage, IdEditProjectile, "Edit Projectile"), 1, wxRIGHT, 4);
        projectileButtons->Add(new wxButton(projectilesPage, IdRemoveProjectile, "Remove"), 1);
        projectileSizer->Add(projectileButtons, 0, wxEXPAND | wxALL, 8);
        projectileSizer->Add(new wxStaticText(projectilesPage, wxID_ANY, "Projectile definitions are used by both player and enemy firing logic."), 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
        projectilesPage->SetSizer(projectileSizer);

        auto* weaponSizer = new wxBoxSizer(wxVERTICAL);
        weaponSizer->Add(new wxStaticText(weaponsPage, wxID_ANY, "Weapon Definitions"), 0, wxLEFT | wxRIGHT | wxTOP, 8);
        weaponPalette_ = new WeaponPalettePanel(weaponsPage);
        weaponSizer->Add(weaponPalette_, 1, wxEXPAND | wxALL, 8);
        auto* weaponButtons = new wxBoxSizer(wxHORIZONTAL);
        weaponButtons->Add(new wxButton(weaponsPage, IdAddWeapon, "Add Weapon"), 1, wxRIGHT, 4);
        weaponButtons->Add(new wxButton(weaponsPage, IdEditWeapon, "Edit Weapon"), 1, wxRIGHT, 4);
        weaponButtons->Add(new wxButton(weaponsPage, IdRemoveWeapon, "Remove"), 1);
        weaponSizer->Add(weaponButtons, 0, wxEXPAND | wxALL, 8);
        weaponsPage->SetSizer(weaponSizer);

        auto* transitionSizer = new wxBoxSizer(wxVERTICAL);
        transitionList_ = new wxListBox(transitionsPage, wxID_ANY);
        transitionSizer->Add(transitionList_, 1, wxEXPAND | wxALL, 8);
        auto* transitionButtons = new wxBoxSizer(wxHORIZONTAL);
        transitionButtons->Add(new wxButton(transitionsPage, IdAddTransition, "Add Link"), 1, wxRIGHT, 6);
        transitionButtons->Add(new wxButton(transitionsPage, IdRemoveTransition, "Remove"), 1);
        transitionSizer->Add(transitionButtons, 0, wxEXPAND | wxALL, 8);
        transitionsPage->SetSizer(transitionSizer);

        auto* warpSizer = new wxBoxSizer(wxVERTICAL);
        warpPalette_ = new WarpPalettePanel(warpsPage);
        warpSizer->Add(warpPalette_, 1, wxEXPAND | wxALL, 8);
        auto* warpButtons = new wxBoxSizer(wxHORIZONTAL);
        warpButtons->Add(new wxButton(warpsPage, IdAddWarp, "Add Warp Def"), 1, wxRIGHT, 6);
        warpButtons->Add(new wxButton(warpsPage, IdEditWarp, "Edit"), 1, wxRIGHT, 6);
        warpButtons->Add(new wxButton(warpsPage, IdRemoveWarp, "Remove"), 1);
        warpSizer->Add(warpButtons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
        warpSizer->Add(new wxStaticText(warpsPage, wxID_ANY, "Click an A or B endpoint sprite to select it, then click on the map to place it."), 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
        warpsPage->SetSizer(warpSizer);

        auto* dropTableSizer = new wxBoxSizer(wxVERTICAL);
        dropTableSizer->Add(new wxStaticText(dropTablesPage, wxID_ANY, "Enemy Drop Tables"), 0, wxLEFT | wxRIGHT | wxTOP, 8);
        dropTablePalette_ = new DropTablePalettePanel(dropTablesPage, &world_.itemDefinitions);
        dropTableSizer->Add(dropTablePalette_, 1, wxEXPAND | wxALL, 8);
        auto* dropTableButtons = new wxBoxSizer(wxHORIZONTAL);
        dropTableButtons->Add(new wxButton(dropTablesPage, IdAddDropTableDef, "Add Table"), 1, wxRIGHT, 4);
        dropTableButtons->Add(new wxButton(dropTablesPage, IdEditDropTableDef, "Edit Table"), 1, wxRIGHT, 4);
        dropTableButtons->Add(new wxButton(dropTablesPage, IdRemoveDropTableDef, "Remove"), 1);
        dropTableSizer->Add(dropTableButtons, 0, wxEXPAND | wxALL, 8);
        dropTableSizer->Add(new wxStaticText(dropTablesPage, wxID_ANY, "Each entry references an item definition and a weight. Non-container items only."), 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
        dropTablesPage->SetSizer(dropTableSizer);

        auto* globalSettingsSizer = new wxBoxSizer(wxVERTICAL);
        globalSettingsSizer->Add(new wxStaticText(globalSettingsPage, wxID_ANY, "Combat Settings"), 0, wxALL, 8);
        auto* globalSettingsGrid = new wxFlexGridSizer(2, 2, 8, 8);
        globalSettingsGrid->AddGrowableCol(1, 1);
        globalSettingsGrid->Add(new wxStaticText(globalSettingsPage, wxID_ANY, "Knockback Distance (tiles)"), 0, wxALIGN_CENTER_VERTICAL);
        globalKnockbackDistanceCtrl_ = new wxSpinCtrlDouble(globalSettingsPage, wxID_ANY);
        globalKnockbackDistanceCtrl_->SetDigits(2);
        globalKnockbackDistanceCtrl_->SetRange(0.0, 8.0);
        globalKnockbackDistanceCtrl_->SetIncrement(0.05);
        globalSettingsGrid->Add(globalKnockbackDistanceCtrl_, 1, wxEXPAND);
        globalSettingsGrid->Add(new wxStaticText(globalSettingsPage, wxID_ANY, "Invulnerability (seconds)"), 0, wxALIGN_CENTER_VERTICAL);
        globalInvulnerabilityCtrl_ = new wxSpinCtrlDouble(globalSettingsPage, wxID_ANY);
        globalInvulnerabilityCtrl_->SetDigits(2);
        globalInvulnerabilityCtrl_->SetRange(0.0, 10.0);
        globalInvulnerabilityCtrl_->SetIncrement(0.05);
        globalSettingsGrid->Add(globalInvulnerabilityCtrl_, 1, wxEXPAND);
        globalSettingsGrid->Add(new wxStaticText(globalSettingsPage, wxID_ANY, "Text Speed (letters/sec)"), 0, wxALIGN_CENTER_VERTICAL);
        globalTextSpeedCtrl_ = new wxSpinCtrlDouble(globalSettingsPage, wxID_ANY);
        globalTextSpeedCtrl_->SetDigits(1);
        globalTextSpeedCtrl_->SetRange(1.0, 60.0);
        globalTextSpeedCtrl_->SetIncrement(1.0);
        globalSettingsGrid->Add(globalTextSpeedCtrl_, 1, wxEXPAND);
        globalSettingsGrid->Add(new wxStaticText(globalSettingsPage, wxID_ANY, "Drop Item Lifetime (seconds)"), 0, wxALIGN_CENTER_VERTICAL);
        globalDropItemLifetimeCtrl_ = new wxSpinCtrlDouble(globalSettingsPage, wxID_ANY);
        globalDropItemLifetimeCtrl_->SetDigits(2);
        globalDropItemLifetimeCtrl_->SetRange(0.5, 30.0);
        globalDropItemLifetimeCtrl_->SetIncrement(0.1);
        globalSettingsGrid->Add(globalDropItemLifetimeCtrl_, 1, wxEXPAND);
        globalSettingsGrid->Add(new wxStaticText(globalSettingsPage, wxID_ANY, "Item Pickup Duration (seconds)"), 0, wxALIGN_CENTER_VERTICAL);
        globalItemPickupDurationCtrl_ = new wxSpinCtrlDouble(globalSettingsPage, wxID_ANY);
        globalItemPickupDurationCtrl_->SetDigits(2);
        globalItemPickupDurationCtrl_->SetRange(0.1, 30.0);
        globalItemPickupDurationCtrl_->SetIncrement(0.1);
        globalSettingsGrid->Add(globalItemPickupDurationCtrl_, 1, wxEXPAND);
        globalSettingsSizer->Add(globalSettingsGrid, 0, wxEXPAND | wxALL, 8);
        globalSettingsSizer->Add(new wxStaticText(globalSettingsPage, wxID_ANY, "Powerup-style behavior is now authored on item definitions. Legacy powerup data still loads, but new tuning lives here and in Items."), 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
        globalSettingsPage->SetSizer(globalSettingsSizer);

        auto* textSettingsSizer = new wxBoxSizer(wxVERTICAL);
        textSettingsSizer->Add(new wxStaticText(textPage, wxID_ANY, "Glyph Map (6 rows x 30 columns)"), 0, wxALL, 8);
        textGlyphMapCtrl_ = new wxTextCtrl(textPage, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_DONTWRAP);
        textGlyphMapCtrl_->SetMinSize(wxSize(-1, 120));
        wxFont textGlyphFont = textGlyphMapCtrl_->GetFont();
        textGlyphFont.SetFamily(wxFONTFAMILY_TELETYPE);
        textGlyphMapCtrl_->SetFont(textGlyphFont);
        textSettingsSizer->Add(textGlyphMapCtrl_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
        textSettingsSizer->Add(new wxStaticText(textPage, wxID_ANY, "Each cell represents one glyph sprite. Use '_' for an unused sprite slot."), 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
        textSettingsSizer->Add(new wxStaticText(textPage, wxID_ANY, "Rows map to sprite-sheet rows 0..5 and columns 0..29."), 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
        textSettingsSizer->Add(new wxStaticText(textPage, wxID_ANY, "Letters are parsed as 8x16 from the left side (A-M then N-Z); digits use the 3x4 block on the right with '9' in the last row center."), 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
        textSettingsSizer->Add(new wxStaticText(textPage, wxID_ANY, "Extra 8x16 glyph row: use row 4, columns 0..13 for the characters you want; row 5 is the lower sprite half."), 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
        textPage->SetSizer(textSettingsSizer);

        auto* characterSizer = new wxBoxSizer(wxVERTICAL);
        auto* characterTop = new wxBoxSizer(wxHORIZONTAL);
        characterTop->Add(new wxStaticText(charactersPage, wxID_ANY, "Active In-Game"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
        activeCharacterChoice_ = new wxChoice(charactersPage, wxID_ANY);
        characterTop->Add(activeCharacterChoice_, 1, wxEXPAND);
        characterSizer->Add(characterTop, 0, wxEXPAND | wxALL, 8);
        characterPalette_ = new CharacterPalettePanel(charactersPage);
        characterSizer->Add(characterPalette_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
        auto* characterButtons = new wxBoxSizer(wxHORIZONTAL);
        characterButtons->Add(new wxButton(charactersPage, IdAddCharacter, "Add"), 1, wxRIGHT, 6);
        characterButtons->Add(new wxButton(charactersPage, IdEditCharacter, "Edit"), 1, wxRIGHT, 6);
        characterButtons->Add(new wxButton(charactersPage, IdRemoveCharacter, "Remove"), 1);
        characterSizer->Add(characterButtons, 0, wxEXPAND | wxALL, 8);
        charactersPage->SetSizer(characterSizer);

        auto* tilesSizer = new wxBoxSizer(wxVERTICAL);
        auto* collectionRow = new wxBoxSizer(wxHORIZONTAL);
        collectionRow->Add(new wxStaticText(tilesPage, wxID_ANY, "Collection"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
        tileCollectionChoice_ = new wxChoice(tilesPage, wxID_ANY);
        collectionRow->Add(tileCollectionChoice_, 1, wxEXPAND);
        auto* addCollectionBtn = new wxButton(tilesPage, IdAddTileCollectionFromSheet, "Add Collection...");
        addCollectionBtn->SetToolTip("Create a new tile collection from a sheet in data/sheets");
        collectionRow->Add(addCollectionBtn, 0, wxLEFT, 8);
        tilesSizer->Add(collectionRow, 0, wxEXPAND | wxALL, 8);

        auto* paletteColumnsRow = new wxBoxSizer(wxHORIZONTAL);
        paletteColumnsRow->Add(new wxStaticText(tilesPage, wxID_ANY, "Palette Columns"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
        tilePaletteColumnsCtrl_ = new wxSpinCtrl(tilesPage, wxID_ANY);
        tilePaletteColumnsCtrl_->SetRange(1, 200);
        tilePaletteColumnsCtrl_->SetValue(1);
        tilePaletteColumnsCtrl_->SetToolTip("Number of columns to display in the tile palette for this collection");
        paletteColumnsRow->Add(tilePaletteColumnsCtrl_, 0, wxALIGN_CENTER_VERTICAL);
        tilesSizer->Add(paletteColumnsRow, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);

        tilesSizer->Add(new wxStaticText(tilesPage, wxID_ANY, "Description"), 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
        tileCollectionDescriptionCtrl_ = new wxTextCtrl(tilesPage, wxID_ANY);
        tilesSizer->Add(tileCollectionDescriptionCtrl_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

        auto* paintLayerRow = new wxBoxSizer(wxHORIZONTAL);
        paintLayerRow->Add(new wxStaticText(tilesPage, wxID_ANY, "Paint Layer"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
        paintLayerChoice_ = new wxChoice(tilesPage, wxID_ANY);
        paintLayerChoice_->Append("Layer 1 (Base)");
        paintLayerChoice_->Append("Layer 2");
        paintLayerChoice_->Append("Layer 3");
        paintLayerChoice_->SetSelection(0);
        paintLayerRow->Add(paintLayerChoice_, 1, wxEXPAND);
        tilesSizer->Add(paintLayerRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

        auto* moveLayerRow = new wxBoxSizer(wxHORIZONTAL);
        auto* moveLayerBtn = new wxButton(tilesPage, IdMoveLayer, "Move Layer...");
        moveLayerBtn->SetToolTip("Move non-empty tiles from one layer to another on current screen");
        moveLayerRow->Add(moveLayerBtn, 0);
        tilesSizer->Add(moveLayerRow, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);

        auto* canvasModeRow = new wxBoxSizer(wxHORIZONTAL);
        canvasModeRow->Add(new wxStaticText(tilesPage, wxID_ANY, "Canvas Mode"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
        canvasModeChoice_ = new wxChoice(tilesPage, wxID_ANY);
        canvasModeChoice_->Append("Paint Tiles");
        canvasModeChoice_->Append("Draw Warp");
        canvasModeChoice_->Append("Add Edge Link");
        canvasModeChoice_->SetSelection(0);
        canvasModeRow->Add(canvasModeChoice_, 1, wxEXPAND);
        tilesSizer->Add(canvasModeRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

        auto* tileSolidRow = new wxBoxSizer(wxHORIZONTAL);
        tileSolidCheck_ = new wxCheckBox(tilesPage, wxID_ANY, "Selected Tile Solid");
        tileSolidRow->Add(tileSolidCheck_, 1, wxEXPAND | wxALIGN_CENTER_VERTICAL);
        tilesSizer->Add(tileSolidRow, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);

        auto* removeTileRow = new wxBoxSizer(wxHORIZONTAL);
        auto* addTileBtn = new wxButton(tilesPage, IdAddTileFromSheet, "Add Tile...");
        addTileBtn->SetToolTip("Add a new tile from sprite libraries in data/sheets");
        removeTileRow->Add(addTileBtn, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, 8);
        removeTileRow->AddStretchSpacer(1);
        auto* removeTileBtn = new wxButton(tilesPage, IdRemoveTile, "");
        removeTileBtn->SetToolTip("Remove selected tile");
        removeTileBtn->SetBitmap(wxArtProvider::GetBitmap(wxART_DELETE, wxART_BUTTON));
        removeTileRow->Add(removeTileBtn, 0, wxALIGN_CENTER_VERTICAL);
        tilesSizer->Add(removeTileRow, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);

        auto* layerVisibilityRow = new wxBoxSizer(wxHORIZONTAL);
        layerVisibilityRow->Add(new wxStaticText(tilesPage, wxID_ANY, "Visible Layers:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
        layerVisibleCheck0_ = new wxCheckBox(tilesPage, wxID_ANY, "L1");
        layerVisibleCheck1_ = new wxCheckBox(tilesPage, wxID_ANY, "L2");
        layerVisibleCheck2_ = new wxCheckBox(tilesPage, wxID_ANY, "L3");
        layerVisibleCheck0_->SetValue(true);
        layerVisibleCheck1_->SetValue(true);
        layerVisibleCheck2_->SetValue(true);
        layerVisibilityRow->Add(layerVisibleCheck0_, 0, wxRIGHT, 8);
        layerVisibilityRow->Add(layerVisibleCheck1_, 0, wxRIGHT, 8);
        layerVisibilityRow->Add(layerVisibleCheck2_, 0, wxRIGHT, 8);
        tilesSizer->Add(layerVisibilityRow, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);

        hitboxOverlayCheck_ = new wxCheckBox(tilesPage, wxID_ANY, "Show Tile Hitboxes");
        hitboxOverlayCheck_->SetValue(false);
        tilesSizer->Add(hitboxOverlayCheck_, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);

        tilePalette_ = new TilePalettePanel(tilesPage);
        tilePalette_->SetMinSize(wxSize(-1, 360));
        tilePalette_->SetEditTileCallback([this](TileDef& tile) { OnEditTileHitbox(tile); });
        tilePalette_->SetMoveTileCallback([this](int tileId, const std::string& targetCollectionId) {
            MoveTileToCollection(tileId, targetCollectionId);
        });
        tilesSizer->Add(tilePalette_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
        tilesPage->SetSizer(tilesSizer);

        Bind(wxEVT_LISTBOX, &EditorFrame::OnMapSelected, this, mapList_->GetId());
        Bind(wxEVT_CHOICE, &EditorFrame::OnTileCollectionChanged, this, tileCollectionChoice_->GetId());
        Bind(wxEVT_TEXT, &EditorFrame::OnTileCollectionDescriptionChanged, this, tileCollectionDescriptionCtrl_->GetId());
        Bind(wxEVT_SPINCTRL, &EditorFrame::OnTilePaletteColumnsChanged, this, tilePaletteColumnsCtrl_->GetId());
        Bind(wxEVT_CHOICE, &EditorFrame::OnPaintLayerChanged, this, paintLayerChoice_->GetId());
        Bind(wxEVT_CHECKBOX, &EditorFrame::OnSelectedTileSolidChanged, this, tileSolidCheck_->GetId());
        Bind(wxEVT_CHECKBOX, &EditorFrame::OnLayerVisibilityChanged, this, layerVisibleCheck0_->GetId());
        Bind(wxEVT_CHECKBOX, &EditorFrame::OnLayerVisibilityChanged, this, layerVisibleCheck1_->GetId());
        Bind(wxEVT_CHECKBOX, &EditorFrame::OnLayerVisibilityChanged, this, layerVisibleCheck2_->GetId());
        Bind(wxEVT_CHECKBOX, &EditorFrame::OnHitboxOverlayToggled, this, hitboxOverlayCheck_->GetId());
        Bind(wxEVT_CHOICE, &EditorFrame::OnCanvasModeChanged, this, canvasModeChoice_->GetId());
        notebook_->Bind(wxEVT_NOTEBOOK_PAGE_CHANGED, &EditorFrame::OnNotebookPageChanged, this);
        Bind(wxEVT_TEXT, &EditorFrame::OnMapMetadataChanged, this, mapIdCtrl_->GetId());
        Bind(wxEVT_TEXT, &EditorFrame::OnMapMetadataChanged, this, mapNameCtrl_->GetId());
        Bind(wxEVT_SPINCTRL, &EditorFrame::OnMapStartChanged, this, mapStartXCtrl_->GetId());
        Bind(wxEVT_SPINCTRL, &EditorFrame::OnMapStartChanged, this, mapStartYCtrl_->GetId());
        Bind(wxEVT_SPINCTRL, &EditorFrame::OnGlobalStartChanged, this, globalStartXCtrl_->GetId());
        Bind(wxEVT_SPINCTRL, &EditorFrame::OnGlobalStartChanged, this, globalStartYCtrl_->GetId());
        Bind(wxEVT_CHOICE, &EditorFrame::OnGlobalStartMapChanged, this, globalStartMapChoice_->GetId());
        Bind(wxEVT_CHOICE, &EditorFrame::OnActiveCharacterChanged, this, activeCharacterChoice_->GetId());
        Bind(wxEVT_SPINCTRLDOUBLE, &EditorFrame::OnGlobalSettingsChanged, this, globalKnockbackDistanceCtrl_->GetId());
        Bind(wxEVT_SPINCTRLDOUBLE, &EditorFrame::OnGlobalSettingsChanged, this, globalInvulnerabilityCtrl_->GetId());
        Bind(wxEVT_SPINCTRLDOUBLE, &EditorFrame::OnGlobalSettingsChanged, this, globalTextSpeedCtrl_->GetId());
        Bind(wxEVT_SPINCTRLDOUBLE, &EditorFrame::OnGlobalSettingsChanged, this, globalDropItemLifetimeCtrl_->GetId());
        Bind(wxEVT_SPINCTRLDOUBLE, &EditorFrame::OnGlobalSettingsChanged, this, globalItemPickupDurationCtrl_->GetId());
        Bind(wxEVT_TEXT, &EditorFrame::OnTextGlyphMapChanged, this, textGlyphMapCtrl_->GetId());
        Bind(wxEVT_CHECKBOX, &EditorFrame::OnDisplayTextToggleChanged, this, displayTextCheck_->GetId());
        Bind(wxEVT_CHECKBOX, &EditorFrame::OnHideFromMapToggleChanged, this, hideFromMapCheck_->GetId());
        Bind(wxEVT_TEXT, &EditorFrame::OnDisplayTextValueChanged, this, displayTextCtrl_->GetId());

        Bind(wxEVT_BUTTON, &EditorFrame::OnAddMap, this, IdAddMap);
        Bind(wxEVT_BUTTON, &EditorFrame::OnRemoveMap, this, IdRemoveMap);
        Bind(wxEVT_BUTTON, &EditorFrame::OnResizeCurrentMap, this, IdResizeMap);
        Bind(wxEVT_BUTTON, &EditorFrame::OnUseCurrentAsGlobalStart, this, IdUseCurrentAsGlobalStart);

        Bind(wxEVT_BUTTON, &EditorFrame::OnAddItem, this, IdAddItem);
        Bind(wxEVT_BUTTON, &EditorFrame::OnEditItem, this, IdEditItem);
        Bind(wxEVT_BUTTON, &EditorFrame::OnRemoveItem, this, IdRemoveItem);
        Bind(wxEVT_BUTTON, &EditorFrame::OnAddEnemy, this, IdAddEnemy);
        Bind(wxEVT_BUTTON, &EditorFrame::OnEditEnemy, this, IdEditEnemy);
        Bind(wxEVT_BUTTON, &EditorFrame::OnRemoveEnemy, this, IdRemoveEnemy);
        Bind(wxEVT_BUTTON, &EditorFrame::OnAddNpc, this, IdAddNpc);
        Bind(wxEVT_BUTTON, &EditorFrame::OnEditNpc, this, IdEditNpc);
        Bind(wxEVT_BUTTON, &EditorFrame::OnRemoveNpc, this, IdRemoveNpc);
        Bind(wxEVT_BUTTON, &EditorFrame::OnAddProjectile, this, IdAddProjectile);
        Bind(wxEVT_BUTTON, &EditorFrame::OnEditProjectile, this, IdEditProjectile);
        Bind(wxEVT_BUTTON, &EditorFrame::OnRemoveProjectile, this, IdRemoveProjectile);
        Bind(wxEVT_BUTTON, &EditorFrame::OnAddWeapon, this, IdAddWeapon);
        Bind(wxEVT_BUTTON, &EditorFrame::OnEditWeapon, this, IdEditWeapon);
        Bind(wxEVT_BUTTON, &EditorFrame::OnRemoveWeapon, this, IdRemoveWeapon);
        Bind(wxEVT_BUTTON, &EditorFrame::OnAddTransition, this, IdAddTransition);
        Bind(wxEVT_BUTTON, &EditorFrame::OnRemoveTransition, this, IdRemoveTransition);
        Bind(wxEVT_BUTTON, &EditorFrame::OnAddWarp, this, IdAddWarp);
        Bind(wxEVT_BUTTON, &EditorFrame::OnEditWarp, this, IdEditWarp);
        Bind(wxEVT_BUTTON, &EditorFrame::OnRemoveWarp, this, IdRemoveWarp);
        Bind(wxEVT_BUTTON, &EditorFrame::OnAddDropTableDef, this, IdAddDropTableDef);
        Bind(wxEVT_BUTTON, &EditorFrame::OnEditDropTableDef, this, IdEditDropTableDef);
        Bind(wxEVT_BUTTON, &EditorFrame::OnRemoveDropTableDef, this, IdRemoveDropTableDef);
        Bind(wxEVT_BUTTON, &EditorFrame::OnAddTileCollectionFromSheet, this, IdAddTileCollectionFromSheet);
        Bind(wxEVT_BUTTON, &EditorFrame::OnAddTileFromSheet, this, IdAddTileFromSheet);
        Bind(wxEVT_BUTTON, &EditorFrame::OnRemoveTile, this, IdRemoveTile);
        Bind(wxEVT_BUTTON, &EditorFrame::OnMoveLayer, this, IdMoveLayer);
        Bind(wxEVT_BUTTON, &EditorFrame::OnAddCharacter, this, IdAddCharacter);
        Bind(wxEVT_BUTTON, &EditorFrame::OnEditCharacter, this, IdEditCharacter);
        Bind(wxEVT_BUTTON, &EditorFrame::OnRemoveCharacter, this, IdRemoveCharacter);

        worldGrid_->SetSelectCallback([this](int x, int y) { SelectScreen(x, y); });
        canvas_->SetDirtyCallback([this]() { MarkDirty(); });
        canvas_->SetWarpPlaceCallback([this](float x, float y) { OnWarpPlacedOnCanvas(x, y); });
        canvas_->SetEdgeClickCallback([this](const std::string& edge) { OnEdgeLinkClickedOnCanvas(edge); });
        canvas_->SetItemPlaceCallback([this](float x, float y) { PlaceSelectedItemAtCurrentScreen(x, y); });
        canvas_->SetItemMoveCallback([this](int index, float x, float y) { MoveItemPlacementOnCurrentScreen(index, x, y); });
        canvas_->SetItemPickCallback([this](const std::string& itemId) { SelectItemDefinition(itemId); });
        canvas_->SetItemPlacementPickCallback([this](int index) { OnItemPlacementPickedOnCanvas(index); });
        canvas_->SetEnemyPlaceCallback([this](float x, float y) { PlaceSelectedEnemyAtCurrentScreen(x, y); });
        canvas_->SetEnemyPickCallback([this](const std::string& enemyId) { SelectEnemyDefinition(enemyId); });
        canvas_->SetTilePickCallback([this](int tileId) { SelectTileFromCanvas(tileId); });
        canvas_->SetLayerChangedCallback([this](int layer) { SetPaintLayerIndex(layer); });
        tilePalette_->SetSelectionChangedCallback([this](int tileId) {
            selectedTileId_ = tileId;
            canvas_->SetTileSelection(selectedTileId_);
            RefreshSelectedTileControls();
        });
        itemPalette_->SetSelectionChangedCallback([this](const std::string& itemId) { SelectItemDefinition(itemId, false); });
        itemPalette_->SetEditItemCallback([this](const std::string&) {
            wxCommandEvent evt;
            OnEditItem(evt);
        });
        enemyPalette_->SetSelectionChangedCallback([this](const std::string& enemyId) { SelectEnemyDefinition(enemyId, false); });
        enemyPalette_->SetEditEnemyCallback([this](const std::string&) {
            wxCommandEvent evt;
            OnEditEnemy(evt);
        });
        if (npcPalette_) {
            npcPalette_->SetSelectionChangedCallback([this](const std::string& npcId) { SelectNpcDefinition(npcId, false); });
            npcPalette_->SetEditNpcCallback([this](const std::string&) {
                wxCommandEvent evt;
                OnEditNpc(evt);
            });
        }
        if (projectilePalette_) {
            projectilePalette_->SetSelectionChangedCallback([this](const std::string& projectileId) { SelectProjectileDefinition(projectileId, false); });
            projectilePalette_->SetEditProjectileCallback([this](const std::string&) {
                wxCommandEvent evt;
                OnEditProjectile(evt);
            });
        }
        if (characterPalette_) {
            characterPalette_->SetSelectionChangedCallback([this](const std::string& characterId) {
                selectedCharacterSpritesetId_ = characterId;
            });
            characterPalette_->SetEditCharacterCallback([this](const std::string&) {
                wxCommandEvent evt;
                OnEditCharacter(evt);
            });
        }
        if (weaponPalette_) {
            weaponPalette_->SetSelectionChangedCallback([this](const std::string& weaponId) {
                selectedWeaponDefinitionId_ = weaponId;
            });
            weaponPalette_->SetEditWeaponCallback([this](const std::string&) {
                wxCommandEvent evt;
                OnEditWeapon(evt);
            });
        }
        warpPalette_->SetSelectionChangedCallback([this](const std::string& warpId, int endpointIndex) {
            selectedWarpDefinitionId_ = warpId;
            selectedWarpEndpointIndex_ = std::clamp(endpointIndex, 0, 1);
            if (canvas_) {
                canvas_->SetWarpSelection(selectedWarpDefinitionId_, selectedWarpEndpointIndex_);
            }
        });
        warpPalette_->SetEditWarpCallback([this](const std::string&) {
            wxCommandEvent evt;
            OnEditWarp(evt);
        });

        ApplyDarkTheme(root);
        Bind(wxEVT_SIZE, &EditorFrame::OnFrameResized, this);
        CallAfter([this]() { ApplyBalancedSplitLayout(); });
        CreateStatusBar(2);
        SetStatusText("Editor ready", 0);
        SetStatusText("Use the world grid to navigate screens", 1);
    }

    void CreateDefaultWorld() {
        world_ = WorldLoadData{};
        world_.formatVersion = 16;
        world_.globalSettings.textGlyphMap = DefaultTextGlyphMap();
        world_.maps.push_back(MakeBlankMap("overworld", "Overworld", 5, 4));
        world_.tileCollections.clear();
        world_.tileCollections.push_back(BuildForestCollectionFromImage("data/tiles/Overworld.png"));
        world_.activeTileCollectionId = world_.tileCollections.front().id;
        world_.characterSpritesets.clear();
        world_.characterSpritesets.push_back(BuildDefaultPlayerSpriteset());
        world_.activeCharacterSpritesetId = world_.characterSpritesets.front().id;
        world_.itemDefinitions.clear();
        world_.enemyDefinitions.clear();
        world_.dropTables.clear();
        world_.weaponDefinitions.clear();
        world_.projectileDefinitions.clear();
        WeaponDefinition melee;
        melee.id = "weapon_1";
        melee.name = "Sword";
        melee.damage = 1;
        world_.weaponDefinitions.push_back(melee);

        WeaponDefinition ranged;
        ranged.id = "weapon_2";
        ranged.name = "Bow";
        ranged.damage = 1;
        ranged.isProjectile = true;
        world_.weaponDefinitions.push_back(ranged);
        world_.defaultMapId = "overworld";
        world_.defaultStartScreenX = 0;
        world_.defaultStartScreenY = 0;
        selectedTileId_ = 0;
        selectedItemDefinitionId_.clear();
        selectedEnemyDefinitionId_.clear();
        selectedProjectileDefinitionId_.clear();
        selectedWeaponDefinitionId_.clear();
        selectedWarpDefinitionId_.clear();
        selectedWarpEndpointIndex_ = 0;
        paintLayerIndex_ = 0;
        layerVisible_[0] = true;
        layerVisible_[1] = true;
        layerVisible_[2] = true;
        showHitboxOverlay_ = false;
        SetPaintLayerIndex(0);
        layerVisibleCheck0_->SetValue(true);
        layerVisibleCheck1_->SetValue(true);
        layerVisibleCheck2_->SetValue(true);
        if (hitboxOverlayCheck_) {
            hitboxOverlayCheck_->SetValue(false);
        }
        if (canvas_) {
            canvas_->SetShowHitboxOverlay(false);
        }
        currentPath_.clear();
        dirty_ = false;
        SelectDefaultStartLocation();
        UpdateTitle();
    }

    MapLoadData* CurrentMap() {
        if (currentMapIndex_ < 0 || currentMapIndex_ >= static_cast<int>(world_.maps.size())) {
            return nullptr;
        }
        return &world_.maps[static_cast<size_t>(currentMapIndex_)];
    }

    const MapLoadData* CurrentMap() const {
        if (currentMapIndex_ < 0 || currentMapIndex_ >= static_cast<int>(world_.maps.size())) {
            return nullptr;
        }
        return &world_.maps[static_cast<size_t>(currentMapIndex_)];
    }

    ScreenLoadData* CurrentScreen() {
        MapLoadData* map = CurrentMap();
        return map ? FindScreen(*map, currentScreenX_, currentScreenY_) : nullptr;
    }

    TileCollection* ActiveTileCollection() {
        TileCollection* collection = FindTileCollection(world_, world_.activeTileCollectionId);
        if (!collection && !world_.tileCollections.empty()) {
            world_.activeTileCollectionId = world_.tileCollections.front().id;
            collection = &world_.tileCollections.front();
        }
        return collection;
    }

    const TileCollection* ActiveTileCollection() const {
        const TileCollection* collection = FindTileCollection(world_, world_.activeTileCollectionId);
        if (!collection && !world_.tileCollections.empty()) {
            return &world_.tileCollections.front();
        }
        return collection;
    }

    void RemoveTileReferences(int tileId) {
        for (MapLoadData& map : world_.maps) {
            for (ScreenLoadData& screen : map.screens) {
                for (int layer = 0; layer < kTileLayers; ++layer) {
                    for (int& placedTileId : screen.screen.tileLayerIds[static_cast<size_t>(layer)]) {
                        if (placedTileId == tileId) {
                            placedTileId = -1;
                        }
                    }
                }
            }
        }
    }

    std::string NextItemDefinitionId() const {
        int suffix = static_cast<int>(world_.itemDefinitions.size()) + 1;
        while (FindItemDefinition(world_, "item_" + std::to_string(suffix)) != nullptr) {
            ++suffix;
        }
        return "item_" + std::to_string(suffix);
    }

    std::string NextEnemyDefinitionId() const {
        int suffix = static_cast<int>(world_.enemyDefinitions.size()) + 1;
        while (FindEnemyDefinition(world_, "enemy_" + std::to_string(suffix)) != nullptr) {
            ++suffix;
        }
        return "enemy_" + std::to_string(suffix);
    }

    std::string NextProjectileDefinitionId() const {
        int suffix = static_cast<int>(world_.projectileDefinitions.size()) + 1;
        while (FindProjectileDefinition(world_, "projectile_" + std::to_string(suffix)) != nullptr) {
            ++suffix;
        }
        return "projectile_" + std::to_string(suffix);
    }

    std::string NextWeaponDefinitionId() const {
        int suffix = static_cast<int>(world_.weaponDefinitions.size()) + 1;
        while (FindWeaponDefinition(world_, "weapon_" + std::to_string(suffix)) != nullptr) {
            ++suffix;
        }
        return "weapon_" + std::to_string(suffix);
    }

    void RemoveItemDefinitionReferences(const std::string& itemId) {
        for (MapLoadData& map : world_.maps) {
            for (ScreenLoadData& screen : map.screens) {
                screen.itemPlacements.erase(
                    std::remove_if(screen.itemPlacements.begin(), screen.itemPlacements.end(), [&itemId](const ItemPlacement& placement) {
                        return placement.itemId == itemId;
                    }),
                    screen.itemPlacements.end()
                );
            }
        }
    }

    void RemoveEnemyDefinitionReferences(const std::string& enemyId) {
        for (MapLoadData& map : world_.maps) {
            for (ScreenLoadData& screen : map.screens) {
                screen.enemyPlacements.erase(
                    std::remove_if(screen.enemyPlacements.begin(), screen.enemyPlacements.end(), [&enemyId](const EnemyPlacement& placement) {
                        return placement.enemyId == enemyId;
                    }),
                    screen.enemyPlacements.end()
                );
            }
        }
    }

    std::string NextWarpDefinitionId() const {
        int suffix = static_cast<int>(world_.warpDefinitions.size()) + 1;
        while (FindWarpDefinition(world_, "warp_" + std::to_string(suffix)) != nullptr) {
            ++suffix;
        }
        return "warp_" + std::to_string(suffix);
    }

    void RemoveWarpDefinitionReferences(const std::string& warpId) {
        for (MapLoadData& map : world_.maps) {
            for (ScreenLoadData& screen : map.screens) {
                screen.warpPlacements.erase(
                    std::remove_if(screen.warpPlacements.begin(), screen.warpPlacements.end(), [&warpId](const WarpPlacement& placement) {
                        return placement.warpId == warpId;
                    }),
                    screen.warpPlacements.end()
                );
            }
        }
    }

    void PlaceSelectedWarpEndpointAtCurrentScreen(float pixelX, float pixelY) {
        if (selectedWarpDefinitionId_.empty()) {
            return;
        }
        MapLoadData* map = CurrentMap();
        ScreenLoadData* screen = CurrentScreen();
        if (!FindWarpDefinition(world_, selectedWarpDefinitionId_) || !map || !screen) {
            return;
        }

        const int endpointIndex = std::clamp(selectedWarpEndpointIndex_, 0, 1);
        const float placedX = std::clamp(
            std::floor(pixelX / static_cast<float>(kTileSize)) * static_cast<float>(kTileSize),
            0.0f,
            static_cast<float>(kScreenPixelWidth - kTileSize)
        );
        const float placedY = std::clamp(
            std::floor(pixelY / static_cast<float>(kTileSize)) * static_cast<float>(kTileSize),
            0.0f,
            static_cast<float>(kScreenPixelHeight - kTileSize)
        );

        // Keep one placement per warp endpoint globally.
        for (MapLoadData& iterMap : world_.maps) {
            for (ScreenLoadData& iterScreen : iterMap.screens) {
                iterScreen.warpPlacements.erase(
                    std::remove_if(
                        iterScreen.warpPlacements.begin(),
                        iterScreen.warpPlacements.end(),
                        [&](const WarpPlacement& placement) {
                            return placement.warpId == selectedWarpDefinitionId_ && placement.endpointIndex == endpointIndex;
                        }
                    ),
                    iterScreen.warpPlacements.end()
                );
            }
        }

        WarpPlacement placement;
        placement.warpId = selectedWarpDefinitionId_;
        placement.endpointIndex = endpointIndex;
        placement.x = placedX;
        placement.y = placedY;
        placement.mapId = map->id;
        placement.screenX = currentScreenX_;
        placement.screenY = currentScreenY_;
        screen->warpPlacements.push_back(placement);

        MarkDirty();
        RefreshCurrentScreenViews();
    }

    void PlaceSelectedItemAtCurrentScreen(float pixelX, float pixelY) {
        ScreenLoadData* screen = CurrentScreen();
        const ItemDefinition* item = FindItemDefinition(world_, selectedItemDefinitionId_);
        if (!screen || !item) {
            return;
        }

        const wxSize itemSize = ItemFrameSize(*item);
        const float maxX = static_cast<float>(std::max(0, kScreenPixelWidth - itemSize.GetWidth()));
        const float maxY = static_cast<float>(std::max(0, kScreenPixelHeight - itemSize.GetHeight()));
        const float placedX = std::clamp(std::floor(pixelX / static_cast<float>(kTileSize)) * static_cast<float>(kTileSize), 0.0f, maxX);
        const float placedY = std::clamp(std::floor(pixelY / static_cast<float>(kTileSize)) * static_cast<float>(kTileSize), 0.0f, maxY);

        auto existing = std::find_if(screen->itemPlacements.begin(), screen->itemPlacements.end(), [&](const ItemPlacement& placement) {
            if (placement.itemId != selectedItemDefinitionId_) {
                return false;
            }
            return pixelX >= placement.x && pixelX <= placement.x + itemSize.GetWidth()
                && pixelY >= placement.y && pixelY <= placement.y + itemSize.GetHeight();
        });

        if (existing != screen->itemPlacements.end()) {
            screen->itemPlacements.erase(existing);
        } else {
            ItemPlacement placement;
            placement.itemId = selectedItemDefinitionId_;
            placement.x = placedX;
            placement.y = placedY;
            placement.mapId = CurrentMap() ? CurrentMap()->id : "overworld";
            placement.screenX = currentScreenX_;
            placement.screenY = currentScreenY_;
            screen->itemPlacements.push_back(placement);
        }

        MarkDirty();
        RefreshCurrentScreenViews();
    }

    void OnItemPlacementPickedOnCanvas(int placementIndex) {
        ScreenLoadData* screen = CurrentScreen();
        if (!screen || placementIndex < 0 || placementIndex >= static_cast<int>(screen->itemPlacements.size())) {
            return;
        }

        const ItemPlacement& placement = screen->itemPlacements[static_cast<size_t>(placementIndex)];
        SelectItemDefinition(placement.itemId);
        const ItemDefinition* itemDef = FindItemDefinition(world_, placement.itemId);
        if (!itemDef || !itemDef->isContainer) {
            return;
        }

        ItemPlacement& editablePlacement = screen->itemPlacements[static_cast<size_t>(placementIndex)];

        wxArrayString typeChoices;
        typeChoices.Add("none");
        typeChoices.Add("item");
        typeChoices.Add("weapon");
        wxSingleChoiceDialog typeDlg(this, "Container content type", "Container Contents", typeChoices);
        int currentTypeSelection = 0;
        if (editablePlacement.containerContentKind == ContainerContentKind::Item) {
            currentTypeSelection = 1;
        } else if (editablePlacement.containerContentKind == ContainerContentKind::Weapon) {
            currentTypeSelection = 2;
        }
        typeDlg.SetSelection(currentTypeSelection);
        if (typeDlg.ShowModal() != wxID_OK) {
            return;
        }

        const int selectedType = typeDlg.GetSelection();
        if (selectedType == 0) {
            editablePlacement.containerContentKind = ContainerContentKind::None;
            editablePlacement.containerContentId.clear();
            MarkDirty();
            RefreshCurrentScreenViews();
            return;
        }

        if (selectedType == 1) {
            std::vector<std::string> eligibleItemIds;
            wxArrayString labels;
            int preselect = wxNOT_FOUND;
            for (const ItemDefinition& definition : world_.itemDefinitions) {
                if (definition.isContainer) {
                    continue;
                }
                eligibleItemIds.push_back(definition.id);
                labels.Add(wxString::FromUTF8(definition.id + " | " + (definition.name.empty() ? definition.id : definition.name)));
                if (definition.id == editablePlacement.containerContentId) {
                    preselect = static_cast<int>(eligibleItemIds.size()) - 1;
                }
            }

            if (eligibleItemIds.empty()) {
                wxMessageBox("No non-container item definitions exist.", "Container Contents", wxOK | wxICON_INFORMATION, this);
                return;
            }

            wxSingleChoiceDialog choiceDlg(this, "Choose item content", "Container Contents", labels);
            if (preselect != wxNOT_FOUND) {
                choiceDlg.SetSelection(preselect);
            }
            if (choiceDlg.ShowModal() != wxID_OK) {
                return;
            }

            const int choice = choiceDlg.GetSelection();
            if (choice < 0 || choice >= static_cast<int>(eligibleItemIds.size())) {
                return;
            }
            editablePlacement.containerContentKind = ContainerContentKind::Item;
            editablePlacement.containerContentId = eligibleItemIds[static_cast<size_t>(choice)];
            MarkDirty();
            RefreshCurrentScreenViews();
            return;
        }

        std::vector<std::string> eligibleWeaponIds;
        wxArrayString weaponLabels;
        int preselectWeapon = wxNOT_FOUND;
        for (const WeaponDefinition& weapon : world_.weaponDefinitions) {
            eligibleWeaponIds.push_back(weapon.id);
            weaponLabels.Add(wxString::FromUTF8(weapon.id + " | " + (weapon.name.empty() ? weapon.id : weapon.name)));
            if (weapon.id == editablePlacement.containerContentId) {
                preselectWeapon = static_cast<int>(eligibleWeaponIds.size()) - 1;
            }
        }

        if (eligibleWeaponIds.empty()) {
            wxMessageBox("No weapon definitions exist.", "Container Contents", wxOK | wxICON_INFORMATION, this);
            return;
        }

        wxSingleChoiceDialog weaponDlg(this, "Choose weapon content", "Container Contents", weaponLabels);
        if (preselectWeapon != wxNOT_FOUND) {
            weaponDlg.SetSelection(preselectWeapon);
        }
        if (weaponDlg.ShowModal() != wxID_OK) {
            return;
        }

        const int selectedWeapon = weaponDlg.GetSelection();
        if (selectedWeapon < 0 || selectedWeapon >= static_cast<int>(eligibleWeaponIds.size())) {
            return;
        }

        editablePlacement.containerContentKind = ContainerContentKind::Weapon;
        editablePlacement.containerContentId = eligibleWeaponIds[static_cast<size_t>(selectedWeapon)];
        MarkDirty();
        RefreshCurrentScreenViews();
    }

    void PlaceSelectedEnemyAtCurrentScreen(float pixelX, float pixelY) {
        ScreenLoadData* screen = CurrentScreen();
        const EnemyDefinition* enemy = FindEnemyDefinition(world_, selectedEnemyDefinitionId_);
        if (!screen || !enemy) {
            return;
        }

        float width = 12.0f;
        float height = 12.0f;
        if (!enemy->moves.empty()) {
            const EnemyMoveDefinition& move = enemy->moves.front();
            if (const EnemyMoveDefinition::AnimationFrame* frame = FirstEnemyFrame(move)) {
                const wxSize frameSize = EnemyFramePixelSize(*frame);
                width = static_cast<float>(frameSize.GetWidth());
                height = static_cast<float>(frameSize.GetHeight());
            } else if (!move.hitboxes.empty()) {
                width = static_cast<float>(std::max(1, move.hitboxes.front().w));
                height = static_cast<float>(std::max(1, move.hitboxes.front().h));
            }
        }

        const float maxX = static_cast<float>(std::max(0, kScreenPixelWidth - static_cast<int>(width)));
        const float maxY = static_cast<float>(std::max(0, kScreenPixelHeight - static_cast<int>(height)));
        const float placedX = std::clamp(std::floor(pixelX / static_cast<float>(kTileSize)) * static_cast<float>(kTileSize), 0.0f, maxX);
        const float placedY = std::clamp(std::floor(pixelY / static_cast<float>(kTileSize)) * static_cast<float>(kTileSize), 0.0f, maxY);

        auto existing = std::find_if(screen->enemyPlacements.begin(), screen->enemyPlacements.end(), [&](const EnemyPlacement& placement) {
            if (placement.enemyId != selectedEnemyDefinitionId_) {
                return false;
            }
            return pixelX >= placement.x && pixelX <= placement.x + width && pixelY >= placement.y && pixelY <= placement.y + height;
        });

        if (existing != screen->enemyPlacements.end()) {
            screen->enemyPlacements.erase(existing);
        } else {
            EnemyPlacement placement;
            placement.enemyId = selectedEnemyDefinitionId_;
            placement.x = placedX;
            placement.y = placedY;
            placement.mapId = CurrentMap() ? CurrentMap()->id : "overworld";
            placement.screenX = currentScreenX_;
            placement.screenY = currentScreenY_;
            screen->enemyPlacements.push_back(placement);
        }

        MarkDirty();
        RefreshCurrentScreenViews();
    }

    void MoveItemPlacementOnCurrentScreen(int placementIndex, float nextX, float nextY) {
        ScreenLoadData* screen = CurrentScreen();
        if (!screen || placementIndex < 0 || placementIndex >= static_cast<int>(screen->itemPlacements.size())) {
            return;
        }

        ItemPlacement& placement = screen->itemPlacements[static_cast<size_t>(placementIndex)];
        const ItemDefinition* item = FindItemDefinition(world_, placement.itemId);
        const wxSize itemSize = item ? ItemFrameSize(*item) : wxSize(16, 16);
        const float maxX = static_cast<float>(std::max(0, kScreenPixelWidth - itemSize.GetWidth()));
        const float maxY = static_cast<float>(std::max(0, kScreenPixelHeight - itemSize.GetHeight()));
        const float clampedX = std::clamp(std::round(nextX / static_cast<float>(kTileSize)) * static_cast<float>(kTileSize), 0.0f, maxX);
        const float clampedY = std::clamp(std::round(nextY / static_cast<float>(kTileSize)) * static_cast<float>(kTileSize), 0.0f, maxY);
        if (placement.x == clampedX && placement.y == clampedY) {
            return;
        }

        placement.x = clampedX;
        placement.y = clampedY;
        MarkDirty();
        RefreshCurrentScreenViews();
    }

    void SelectItemDefinition(const std::string& itemId, bool ensureVisible = true) {
        selectedItemDefinitionId_ = itemId;
        if (itemPalette_) {
            itemPalette_->SetSelectedItemId(selectedItemDefinitionId_, ensureVisible);
        }
        if (canvas_) {
            canvas_->SetItemSelection(selectedItemDefinitionId_);
        }
    }

    void SelectEnemyDefinition(const std::string& enemyId, bool ensureVisible = true) {
        selectedEnemyDefinitionId_ = enemyId;
        if (enemyPalette_) {
            enemyPalette_->SetSelectedEnemyId(selectedEnemyDefinitionId_, ensureVisible);
        }
        const EnemyDefinition* definition = FindEnemyDefinition(world_, selectedEnemyDefinitionId_);
        if (definition && definition->isNpc) {
            selectedNpcDefinitionId_ = selectedEnemyDefinitionId_;
        }
        if (npcPalette_) {
            npcPalette_->SetSelectedNpcId(selectedNpcDefinitionId_, ensureVisible);
        }
        if (canvas_) {
            canvas_->SetEnemySelection(selectedEnemyDefinitionId_);
        }
    }

    void SelectNpcDefinition(const std::string& npcId, bool ensureVisible = true) {
        selectedNpcDefinitionId_ = npcId;
        if (npcPalette_) {
            npcPalette_->SetSelectedNpcId(selectedNpcDefinitionId_, ensureVisible);
        }
        if (!selectedNpcDefinitionId_.empty()) {
            SelectEnemyDefinition(selectedNpcDefinitionId_, ensureVisible);
        }
    }

    void SelectProjectileDefinition(const std::string& projectileId, bool ensureVisible = true) {
        selectedProjectileDefinitionId_ = projectileId;
        if (!projectilePalette_) {
            return;
        }
        projectilePalette_->SetSelectedProjectileId(selectedProjectileDefinitionId_, ensureVisible);
    }

    void SelectWeaponDefinition(const std::string& weaponId, bool ensureVisible = true) {
        selectedWeaponDefinitionId_ = weaponId;
        if (!weaponPalette_) {
            return;
        }
        weaponPalette_->SetSelectedWeaponId(selectedWeaponDefinitionId_, ensureVisible);
    }

    void SelectWarpDefinition(const std::string& warpId, bool ensureVisible = true) {
        selectedWarpDefinitionId_ = warpId;
        if (warpPalette_) {
            warpPalette_->SetSelected(selectedWarpDefinitionId_, selectedWarpEndpointIndex_);
        }
        if (canvas_) {
            canvas_->SetWarpSelection(selectedWarpDefinitionId_, selectedWarpEndpointIndex_);
        }
    }

    void SetSelectedWarpEndpointIndex(int endpointIndex) {
        selectedWarpEndpointIndex_ = std::clamp(endpointIndex, 0, 1);
        if (warpPalette_) {
            warpPalette_->SetSelected(selectedWarpDefinitionId_, selectedWarpEndpointIndex_);
        }
        if (canvas_) {
            canvas_->SetWarpSelection(selectedWarpDefinitionId_, selectedWarpEndpointIndex_);
        }
    }

    void SyncCanvasInteractionModeFromUi() {
        if (!canvas_) {
            return;
        }

        CanvasMode mode = CanvasMode::PaintTile;
        const int notebookSelection = notebook_ ? notebook_->GetSelection() : 0;
        if (notebookSelection == 1) {
            mode = CanvasMode::PlaceItem;
        } else if (notebookSelection == 2 || notebookSelection == 3) {
            mode = CanvasMode::PlaceEnemy;
        } else if (notebookSelection == 7) {
            mode = CanvasMode::DrawWarp;
        } else {
            const int selection = canvasModeChoice_ ? canvasModeChoice_->GetSelection() : 0;
            if (selection == 1) {
                mode = CanvasMode::DrawWarp;
            } else if (selection == 2) {
                mode = CanvasMode::EdgeLink;
            }
        }

        canvas_->SetMode(mode);
    }

    static bool TileMetadataMatches(const TileDef& a, const TileDef& b) {
        if (a.hitboxes.size() != b.hitboxes.size()) {
            return false;
        }
        for (size_t i = 0; i < a.hitboxes.size(); ++i) {
            const TileHitbox& ah = a.hitboxes[i];
            const TileHitbox& bh = b.hitboxes[i];
            if (ah.x != bh.x || ah.y != bh.y || ah.w != bh.w || ah.h != bh.h) {
                return false;
            }
        }
        return a.name == b.name &&
               a.description == b.description &&
               a.solid == b.solid &&
               a.hitboxX == b.hitboxX &&
               a.hitboxY == b.hitboxY &&
               a.hitboxW == b.hitboxW &&
               a.hitboxH == b.hitboxH;
    }

    void RebuildTileOwnerHints(
        const std::unordered_map<int, std::string>& previousOwnerById,
        const std::unordered_map<int, TileDef>& previousTileById
    ) {
        std::unordered_map<int, std::vector<const TileCollection*>> ownersByTileId;
        for (const TileCollection& collection : world_.tileCollections) {
            for (const TileDef& tile : collection.tiles) {
                ownersByTileId[tile.id].push_back(&collection);
            }
        }

        tileOwnerHints_.clear();
        for (const auto& [tileId, owners] : ownersByTileId) {
            if (owners.empty()) {
                continue;
            }

            const TileCollection* chosen = nullptr;

            auto prevOwner = previousOwnerById.find(tileId);
            if (prevOwner != previousOwnerById.end()) {
                for (const TileCollection* owner : owners) {
                    if (owner && owner->id == prevOwner->second) {
                        chosen = owner;
                        break;
                    }
                }
            }

            if (!chosen && owners.size() > 1) {
                auto prevTile = previousTileById.find(tileId);
                if (prevTile != previousTileById.end()) {
                    for (const TileCollection* owner : owners) {
                        if (!owner) {
                            continue;
                        }
                        for (const TileDef& tile : owner->tiles) {
                            if (tile.id == tileId && TileMetadataMatches(tile, prevTile->second)) {
                                chosen = owner;
                                break;
                            }
                        }
                        if (chosen) {
                            break;
                        }
                    }
                }
            }

            if (!chosen) {
                chosen = owners.front();
            }

            if (chosen) {
                tileOwnerHints_[tileId] = chosen->id;
            }
        }
    }

    static int StableTileIdFromName(const std::string& name, const std::unordered_set<int>& usedIds) {
        uint32_t hash = 2166136261u;
        for (unsigned char c : name) {
            hash ^= static_cast<uint32_t>(std::tolower(c));
            hash *= 16777619u;
        }
        int candidate = static_cast<int>(hash & 0x7fffffff);
        while (usedIds.find(candidate) != usedIds.end()) {
            candidate = (candidate + 1) & 0x7fffffff;
        }
        return candidate;
    }

    static void CopyImageWithAlpha(const wxImage& src, wxImage& dst, int dstX, int dstY) {
        const int srcW = src.GetWidth();
        const int srcH = src.GetHeight();
        const int dstW = dst.GetWidth();
        unsigned char* dstRgb = dst.GetData();
        if (!dst.HasAlpha()) {
            dst.InitAlpha();
        }
        unsigned char* dstAlpha = dst.GetAlpha();
        const unsigned char* srcRgb = src.GetData();
        const unsigned char* srcAlpha = src.HasAlpha() ? src.GetAlpha() : nullptr;

        for (int y = 0; y < srcH; ++y) {
            for (int x = 0; x < srcW; ++x) {
                const int srcPixel = y * srcW + x;
                const int dstPixel = (dstY + y) * dstW + (dstX + x);
                dstRgb[dstPixel * 3 + 0] = srcRgb[srcPixel * 3 + 0];
                dstRgb[dstPixel * 3 + 1] = srcRgb[srcPixel * 3 + 1];
                dstRgb[dstPixel * 3 + 2] = srcRgb[srcPixel * 3 + 2];
                dstAlpha[dstPixel] = srcAlpha ? srcAlpha[srcPixel] : 255;
            }
        }
    }

    static bool DirectoryHasTilePng(const std::filesystem::path& dir) {
        std::error_code ec;
        if (!std::filesystem::exists(dir, ec)) {
            return false;
        }
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            int parsedId = -1;
            if (TryParseTileIdFromFilename(entry.path(), parsedId)) {
                return true;
            }
        }
        return false;
    }

    void ForceCollectionTileIdsIntoRange(const std::string& collectionId, int startId) {
        if (collectionId.empty()) {
            return;
        }
        if (startId < 0) {
            return;
        }

        const std::filesystem::path root = ResolveTilesRootPath();
        const std::filesystem::path targetDir = root / collectionId;
        std::error_code ec;
        if (!std::filesystem::exists(targetDir, ec)) {
            return;
        }

        struct TargetFile {
            int id = -1;
            std::filesystem::path path;
        };

        std::vector<TargetFile> targetFiles;
        for (const auto& fileEntry : std::filesystem::directory_iterator(targetDir, ec)) {
            if (!fileEntry.is_regular_file()) {
                continue;
            }
            int parsedId = -1;
            if (!TryParseTileIdFromFilename(fileEntry.path(), parsedId)) {
                continue;
            }
            targetFiles.push_back(TargetFile{parsedId, fileEntry.path()});
        }

        if (targetFiles.empty()) {
            return;
        }

        std::sort(targetFiles.begin(), targetFiles.end(), [](const TargetFile& a, const TargetFile& b) {
            if (a.id != b.id) {
                return a.id < b.id;
            }
            return a.path.filename().string() < b.path.filename().string();
        });

        struct RenamePlan {
            std::filesystem::path fromPath;
            std::filesystem::path tempPath;
            std::filesystem::path finalPath;
            int oldId = -1;
            int newId = -1;
        };

        std::vector<RenamePlan> plans;
        plans.reserve(targetFiles.size());
        for (size_t i = 0; i < targetFiles.size(); ++i) {
            const TargetFile& file = targetFiles[i];
            const int newId = startId + static_cast<int>(i);
            std::filesystem::path finalPath = targetDir / ("tile_" + std::to_string(newId) + ".png");
            std::filesystem::path tempPath = targetDir / ("_reid_" + std::to_string(i) + "_" + std::to_string(file.id) + ".tmp.png");
            plans.push_back(RenamePlan{file.path, tempPath, finalPath, file.id, newId});
        }

        bool anyChange = false;
        for (const RenamePlan& plan : plans) {
            if (plan.oldId != plan.newId) {
                anyChange = true;
                break;
            }
        }
        if (!anyChange) {
            return;
        }

        for (const RenamePlan& plan : plans) {
            std::filesystem::rename(plan.fromPath, plan.tempPath, ec);
            if (ec) {
                return;
            }
        }

        for (const RenamePlan& plan : plans) {
            std::filesystem::rename(plan.tempPath, plan.finalPath, ec);
            if (ec) {
                return;
            }
        }
    }

    void EnsureTileIdsUniqueAcrossCollections() {
        const std::filesystem::path root = ResolveTilesRootPath();
        std::error_code ec;
        if (!std::filesystem::exists(root, ec)) {
            return;
        }

        struct CollectionFiles {
            std::filesystem::path dir;
            std::vector<std::pair<int, std::filesystem::path>> files;
        };

        std::vector<CollectionFiles> collections;
        int maxSeenId = -1;

        for (const auto& dirEntry : std::filesystem::directory_iterator(root, ec)) {
            if (!dirEntry.is_directory()) {
                continue;
            }

            CollectionFiles c;
            c.dir = dirEntry.path();

            for (const auto& fileEntry : std::filesystem::directory_iterator(c.dir, ec)) {
                if (!fileEntry.is_regular_file()) {
                    continue;
                }
                int parsedId = -1;
                if (!TryParseTileIdFromFilename(fileEntry.path(), parsedId)) {
                    continue;
                }
                c.files.emplace_back(parsedId, fileEntry.path());
                maxSeenId = std::max(maxSeenId, parsedId);
            }

            if (!c.files.empty()) {
                std::sort(c.files.begin(), c.files.end(), [](const auto& a, const auto& b) {
                    if (a.first != b.first) {
                        return a.first < b.first;
                    }
                    return a.second.filename().string() < b.second.filename().string();
                });
                collections.push_back(std::move(c));
            }
        }

        if (collections.empty()) {
            return;
        }

        std::sort(collections.begin(), collections.end(), [](const CollectionFiles& a, const CollectionFiles& b) {
            return a.dir.filename().string() < b.dir.filename().string();
        });

        int nextFreeId = std::max(0, maxSeenId + 1);
        std::unordered_set<int> usedIds;

        struct RenamePlan {
            std::filesystem::path fromPath;
            std::filesystem::path tempPath;
            std::filesystem::path finalPath;
            int oldId = -1;
            int newId = -1;
        };

        for (size_t ci = 0; ci < collections.size(); ++ci) {
            const CollectionFiles& collection = collections[ci];
            std::vector<RenamePlan> plans;

            for (size_t fi = 0; fi < collection.files.size(); ++fi) {
                const int oldId = collection.files[fi].first;
                const std::filesystem::path& oldPath = collection.files[fi].second;

                int newId = oldId;
                if (usedIds.find(newId) != usedIds.end()) {
                    while (usedIds.find(nextFreeId) != usedIds.end()) {
                        ++nextFreeId;
                    }
                    newId = nextFreeId;
                    ++nextFreeId;
                }
                usedIds.insert(newId);

                if (newId != oldId) {
                    const std::filesystem::path tempPath = collection.dir / ("_dedupe_" + std::to_string(ci) + "_" + std::to_string(fi) + ".tmp.png");
                    const std::filesystem::path finalPath = collection.dir / ("tile_" + std::to_string(newId) + ".png");
                    plans.push_back(RenamePlan{oldPath, tempPath, finalPath, oldId, newId});
                }
            }

            if (plans.empty()) {
                continue;
            }

            for (const RenamePlan& plan : plans) {
                std::filesystem::rename(plan.fromPath, plan.tempPath, ec);
                if (ec) {
                    return;
                }
            }

            for (const RenamePlan& plan : plans) {
                std::filesystem::rename(plan.tempPath, plan.finalPath, ec);
                if (ec) {
                    return;
                }
            }
        }
    }

    void ImportAtlasIntoCollectionFolderIfNeeded(const std::string& atlasFilename, const std::string& collectionId) {
        const std::filesystem::path root = ResolveTilesRootPath();
        const std::filesystem::path atlasPath = root / atlasFilename;
        std::error_code ec;
        if (!std::filesystem::exists(atlasPath, ec)) {
            return;
        }

        const std::filesystem::path targetDir = root / collectionId;
        std::filesystem::create_directories(targetDir, ec);
        if (DirectoryHasTilePng(targetDir)) {
            return;
        }

        wxImage atlas;
        if (!atlas.LoadFile(wxString::FromUTF8(atlasPath.string()), wxBITMAP_TYPE_PNG)) {
            return;
        }

        constexpr int tileW = 16;
        constexpr int tileH = 16;
        const int cols = std::max(1, atlas.GetWidth() / tileW);
        const int rows = std::max(1, atlas.GetHeight() / tileH);
        for (int y = 0; y < rows; ++y) {
            for (int x = 0; x < cols; ++x) {
                const int tileId = y * cols + x;
                const wxRect srcRect(x * tileW, y * tileH, tileW, tileH);
                if (IsImageRectFullyTransparent(atlas, srcRect)) {
                    continue;
                }
                wxImage tileImage = atlas.GetSubImage(srcRect);
                const std::filesystem::path outPath = targetDir / ("tile_" + std::to_string(tileId) + ".png");
                tileImage.SaveFile(wxString::FromUTF8(outPath.string()), wxBITMAP_TYPE_PNG);
            }
        }
    }

    void SyncCollectionFromFolder(
        TileCollection& collection,
        const std::unordered_map<int, TileDef>* metadataByTileId,
        std::unordered_set<int>* removedTileIdsOut
    ) {
        const std::filesystem::path tileDir = CollectionTilesDirectory(collection);
        std::error_code ec;
        if (!std::filesystem::exists(tileDir, ec)) {
            return;
        }

        std::unordered_map<int, TileDef> oldTilesById;
        for (const TileDef& tile : collection.tiles) {
            oldTilesById[tile.id] = tile;
        }

        struct TileFile {
            int id = -1;
            std::filesystem::path path;
            std::string stem;
        };

        std::vector<TileFile> files;
        std::unordered_set<int> usedIds;
        for (const auto& entry : std::filesystem::directory_iterator(tileDir, ec)) {
            if (!entry.is_regular_file()) {
                continue;
            }

            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (ext != ".png") {
                continue;
            }
            if (entry.path().filename() == "_atlas.png") {
                continue;
            }

            int tileId = -1;
            if (!TryParseTileIdFromFilename(entry.path(), tileId)) {
                tileId = StableTileIdFromName(entry.path().stem().string(), usedIds);
            }
            usedIds.insert(tileId);
            files.push_back(TileFile{tileId, entry.path(), entry.path().stem().string()});
        }

        std::sort(files.begin(), files.end(), [](const TileFile& a, const TileFile& b) { return a.id < b.id; });

        std::vector<wxImage> prepared;
        std::vector<TileDef> newTiles;
        std::unordered_set<int> keptIds;
        const int tileW = std::max(1, collection.tileWidth);
        const int tileH = std::max(1, collection.tileHeight);
        for (const TileFile& file : files) {
            wxImage image;
            if (!image.LoadFile(wxString::FromUTF8(file.path.string()), wxBITMAP_TYPE_PNG)) {
                continue;
            }
            if (IsImageFullyTransparent(image)) {
                std::filesystem::remove(file.path, ec);
                continue;
            }

            wxImage fitted = image;
            if (fitted.GetWidth() != tileW || fitted.GetHeight() != tileH) {
                fitted = fitted.Scale(tileW, tileH, wxIMAGE_QUALITY_NEAREST);
            }

            if (!fitted.HasAlpha()) {
                fitted.InitAlpha();
                const int count = tileW * tileH;
                unsigned char* a = fitted.GetAlpha();
                for (int i = 0; i < count; ++i) {
                    a[i] = 255;
                }
            }

            TileDef tile;
            auto existing = oldTilesById.find(file.id);
            if (existing != oldTilesById.end()) {
                tile = existing->second;
            } else if (metadataByTileId) {
                auto migrated = metadataByTileId->find(file.id);
                if (migrated != metadataByTileId->end()) {
                    tile = migrated->second;
                } else {
                    tile.id = file.id;
                    tile.name = file.stem;
                    tile.description = file.path.filename().string();
                    tile.solid = false;
                }
            } else {
                tile.id = file.id;
                tile.name = file.stem;
                tile.description = file.path.filename().string();
                tile.solid = false;
            }
            tile.id = file.id;
            prepared.push_back(fitted);
            newTiles.push_back(tile);
            keptIds.insert(tile.id);
        }

        for (const auto& [tileId, oldTile] : oldTilesById) {
            if (keptIds.find(tileId) == keptIds.end()) {
                if (removedTileIdsOut) {
                    removedTileIdsOut->insert(tileId);
                }
            }
        }

        if (newTiles.empty()) {
            collection.tiles.clear();
            return;
        }

        const int tileCount = static_cast<int>(newTiles.size());
        const int cols = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<double>(tileCount)))));
        const int rows = (tileCount + cols - 1) / cols;
        const int atlasW = cols * tileW;
        const int atlasH = rows * tileH;
        wxImage atlas(atlasW, atlasH, true);
        atlas.InitAlpha();
        std::fill(atlas.GetData(), atlas.GetData() + atlasW * atlasH * 3, 0);
        std::fill(atlas.GetAlpha(), atlas.GetAlpha() + atlasW * atlasH, 0);

        for (size_t i = 0; i < newTiles.size(); ++i) {
            const int col = static_cast<int>(i) % cols;
            const int row = static_cast<int>(i) / cols;
            const int sx = col * tileW;
            const int sy = row * tileH;
            newTiles[i].sourceX = sx;
            newTiles[i].sourceY = sy;
            CopyImageWithAlpha(prepared[i], atlas, sx, sy);
        }

        const std::filesystem::path atlasPath = tileDir / "_atlas.png";
        atlas.SaveFile(wxString::FromUTF8(atlasPath.string()), wxBITMAP_TYPE_PNG);

        collection.imagePath = (std::filesystem::path("data/tiles") / collection.id / "_atlas.png").generic_string();
        collection.imageWidth = atlasW;
        collection.imageHeight = atlasH;
        collection.tileWidth = tileW;
        collection.tileHeight = tileH;
        collection.tiles = std::move(newTiles);
    }

    int NextAvailableTileId() const {
        int maxId = -1;
        for (const TileCollection& collection : world_.tileCollections) {
            for (const TileDef& tile : collection.tiles) {
                maxId = std::max(maxId, tile.id);
            }
        }
        return maxId + 1;
    }

    void RefreshSheetSpriteLibraries() {
        sheetSpriteCollections_.clear();

        const std::filesystem::path root = ResolveSheetsRootPath();
        std::error_code ec;
        if (!std::filesystem::exists(root, ec)) {
            return;
        }

        for (const auto& entry : std::filesystem::recursive_directory_iterator(root, ec)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            if (!IsSupportedSheetImageExtension(entry.path())) {
                continue;
            }

            wxImage image;
            if (!image.LoadFile(wxString::FromUTF8(entry.path().string()), wxBITMAP_TYPE_ANY)) {
                continue;
            }

            const int tileW = 16;
            const int tileH = 16;
            const int cols = image.GetWidth() / tileW;
            const int rows = image.GetHeight() / tileH;
            if (cols <= 0 || rows <= 0) {
                continue;
            }

            std::filesystem::path relativePath;
            std::error_code relEc;
            relativePath = std::filesystem::relative(entry.path(), root, relEc);
            if (relEc) {
                relativePath = entry.path().filename();
            }

            SheetSpriteCollectionDef collection;
            collection.id = relativePath.generic_string();
            collection.name = entry.path().stem().string();
            collection.sourceImagePath = entry.path().generic_string();
            collection.tileWidth = tileW;
            collection.tileHeight = tileH;
            collection.imageWidth = image.GetWidth();
            collection.imageHeight = image.GetHeight();
            collection.atlas = wxBitmap(image);

            for (int y = 0; y < rows; ++y) {
                for (int x = 0; x < cols; ++x) {
                    const wxRect srcRect(x * tileW, y * tileH, tileW, tileH);
                    if (IsImageRectFullyTransparent(image, srcRect)) {
                        continue;
                    }
                    collection.sprites.push_back(SheetSpriteDef{srcRect.x, srcRect.y});
                }
            }

            if (!collection.sprites.empty()) {
                sheetSpriteCollections_.push_back(std::move(collection));
            }
        }

        std::sort(sheetSpriteCollections_.begin(), sheetSpriteCollections_.end(), [](const SheetSpriteCollectionDef& a, const SheetSpriteCollectionDef& b) {
            if (a.name != b.name) {
                return a.name < b.name;
            }
            return a.id < b.id;
        });
    }

    bool AddSheetSpriteToActiveCollection(const SheetSpritePick& pick) {
        TileCollection* collection = ActiveTileCollection();
        if (!collection) {
            return false;
        }

        wxImage sourceImage;
        wxString sourcePath = ResolveExistingPath(pick.sourceImagePath);
        if (sourcePath.empty()) {
            sourcePath = wxString::FromUTF8(pick.sourceImagePath);
        }
        if (!sourceImage.LoadFile(sourcePath, wxBITMAP_TYPE_ANY)) {
            wxMessageBox("Could not load selected sheet image.", "Add Tile", wxOK | wxICON_ERROR, this);
            return false;
        }

        const wxRect srcRect(pick.sourceX, pick.sourceY, pick.width, pick.height);
        if (srcRect.x < 0 || srcRect.y < 0 || srcRect.x + srcRect.width > sourceImage.GetWidth() || srcRect.y + srcRect.height > sourceImage.GetHeight()) {
            wxMessageBox("Selected sprite bounds are outside the source image.", "Add Tile", wxOK | wxICON_ERROR, this);
            return false;
        }

        wxImage spriteImage = sourceImage.GetSubImage(srcRect);
        if (IsImageFullyTransparent(spriteImage)) {
            wxMessageBox("Selected sprite is fully transparent.", "Add Tile", wxOK | wxICON_WARNING, this);
            return false;
        }

        EnsureCollectionTileImages(*collection);

        const int tileId = NextAvailableTileId();
        const std::filesystem::path outPath = TileImagePathForId(*collection, tileId);
        std::error_code ec;
        std::filesystem::create_directories(outPath.parent_path(), ec);
        if (!spriteImage.SaveFile(wxString::FromUTF8(outPath.string()), wxBITMAP_TYPE_PNG)) {
            wxMessageBox("Could not save the new tile image.", "Add Tile", wxOK | wxICON_ERROR, this);
            return false;
        }

        TileDef tile;
        tile.id = tileId;
        tile.name = pick.collectionName + " " + std::to_string(pick.sourceX / std::max(1, pick.width)) + "," + std::to_string(pick.sourceY / std::max(1, pick.height));
        tile.description = "Imported from " + pick.collectionId;
        tile.solid = false;
        tile.hitboxX = 0;
        tile.hitboxY = 0;
        tile.hitboxW = 16;
        tile.hitboxH = 16;
        collection->tiles.push_back(tile);

        selectedTileId_ = tileId;
        return true;
    }

    void EnsureCollectionsFromFilesystem() {
        ImportAtlasIntoCollectionFolderIfNeeded("Overworld.png", "forest_1");
        ImportAtlasIntoCollectionFolderIfNeeded("cave.png", "cave_1");
        ImportAtlasIntoCollectionFolderIfNeeded("inner.png", "furniture_1");
        ImportAtlasIntoCollectionFolderIfNeeded("Inner.png", "furniture_1");

        // Temporary migration rule: keep cave_1 IDs in a reserved high range.
        ForceCollectionTileIdsIntoRange("cave_1", 200000);
        // Temporary migration rule: keep furniture_1 IDs in a separate reserved range.
        ForceCollectionTileIdsIntoRange("furniture_1", 300000);
        // Permanent rule: dedupe any remaining overlaps across all collections.
        EnsureTileIdsUniqueAcrossCollections();

        const std::filesystem::path root = ResolveTilesRootPath();
        std::error_code ec;
        std::filesystem::create_directories(root, ec);

        for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
            if (!entry.is_directory()) {
                continue;
            }
            const std::string collectionId = entry.path().filename().string();
            if (!FindTileCollection(world_, collectionId)) {
                TileCollection collection;
                collection.id = collectionId;
                collection.name = collectionId;
                collection.description = "Filesystem collection";
                collection.tileWidth = 16;
                collection.tileHeight = 16;
                world_.tileCollections.push_back(collection);
            }
        }

        std::unordered_map<int, TileDef> metadataByTileId;
        std::unordered_map<int, std::string> previousOwnerById;
        std::unordered_map<int, TileDef> previousTileById;
        std::unordered_set<int> tileIdsBeforeSync;
        for (const TileCollection& existingCollection : world_.tileCollections) {
            for (const TileDef& tile : existingCollection.tiles) {
                tileIdsBeforeSync.insert(tile.id);
                if (metadataByTileId.find(tile.id) == metadataByTileId.end()) {
                    metadataByTileId[tile.id] = tile;
                }
                if (previousOwnerById.find(tile.id) == previousOwnerById.end()) {
                    previousOwnerById[tile.id] = existingCollection.id;
                    previousTileById[tile.id] = tile;
                }
            }
        }

        std::unordered_set<int> removedTileIds;
        for (TileCollection& collection : world_.tileCollections) {
            SyncCollectionFromFolder(collection, &metadataByTileId, &removedTileIds);
        }

        std::unordered_set<int> tileIdsAfterSync;
        for (const TileCollection& syncedCollection : world_.tileCollections) {
            for (const TileDef& tile : syncedCollection.tiles) {
                tileIdsAfterSync.insert(tile.id);
            }
        }

        for (int removedTileId : removedTileIds) {
            if (tileIdsBeforeSync.find(removedTileId) != tileIdsBeforeSync.end() && tileIdsAfterSync.find(removedTileId) == tileIdsAfterSync.end()) {
                RemoveTileReferences(removedTileId);
            }
        }

        RebuildTileOwnerHints(previousOwnerById, previousTileById);
    }

    void EnsureCollectionTileImages(TileCollection& collection) {
        if (collection.id.empty() || collection.imagePath.empty()) {
            return;
        }

        const std::filesystem::path tileDir = CollectionTilesDirectory(collection);
        std::error_code ec;
        std::filesystem::create_directories(tileDir, ec);

        bool hasAnyPng = false;
        if (std::filesystem::exists(tileDir, ec)) {
            for (const auto& entry : std::filesystem::directory_iterator(tileDir, ec)) {
                if (!entry.is_regular_file()) {
                    continue;
                }
                int parsedId = -1;
                if (TryParseTileIdFromFilename(entry.path(), parsedId)) {
                    hasAnyPng = true;
                    break;
                }
            }
        }
        if (hasAnyPng) {
            return;
        }

        const wxString atlasPath = ResolveExistingPath(collection.imagePath);
        if (atlasPath.empty()) {
            return;
        }

        wxImage atlas;
        if (!atlas.LoadFile(atlasPath, wxBITMAP_TYPE_PNG)) {
            return;
        }

        for (const TileDef& tile : collection.tiles) {
            const int tileW = std::max(1, collection.tileWidth);
            const int tileH = std::max(1, collection.tileHeight);
            const wxRect srcRect(tile.sourceX, tile.sourceY, tileW, tileH);
            if (srcRect.x < 0 || srcRect.y < 0 || srcRect.x + srcRect.width > atlas.GetWidth() || srcRect.y + srcRect.height > atlas.GetHeight()) {
                continue;
            }

            wxImage tileImage = atlas.GetSubImage(srcRect);
            const std::filesystem::path outPath = TileImagePathForId(collection, tile.id);
            tileImage.SaveFile(wxString::FromUTF8(outPath.string()), wxBITMAP_TYPE_PNG);
        }
    }

    void RemoveTransparentTilesFromCollection(TileCollection& collection) {
        if (collection.tiles.empty()) {
            return;
        }

        wxImage atlas;
        bool atlasLoaded = false;
        const wxString atlasPath = ResolveExistingPath(collection.imagePath);
        if (!atlasPath.empty()) {
            atlasLoaded = atlas.LoadFile(atlasPath, wxBITMAP_TYPE_PNG);
        }

        std::vector<int> removedIds;
        collection.tiles.erase(
            std::remove_if(collection.tiles.begin(), collection.tiles.end(), [&](const TileDef& tile) {
                bool isTransparentOnly = false;

                const std::filesystem::path tileImagePath = TileImagePathForId(collection, tile.id);
                std::error_code ec;
                if (std::filesystem::exists(tileImagePath, ec)) {
                    wxImage tileImage;
                    if (tileImage.LoadFile(wxString::FromUTF8(tileImagePath.string()), wxBITMAP_TYPE_PNG)) {
                        isTransparentOnly = IsImageFullyTransparent(tileImage);
                    }
                } else if (atlasLoaded) {
                    const int tileW = std::max(1, collection.tileWidth);
                    const int tileH = std::max(1, collection.tileHeight);
                    const wxRect srcRect(tile.sourceX, tile.sourceY, tileW, tileH);
                    isTransparentOnly = IsImageRectFullyTransparent(atlas, srcRect);
                }

                if (isTransparentOnly) {
                    removedIds.push_back(tile.id);
                    std::error_code removeEc;
                    std::filesystem::remove(tileImagePath, removeEc);
                    return true;
                }
                return false;
            }),
            collection.tiles.end()
        );

        for (int removedId : removedIds) {
            RemoveTileReferences(removedId);
        }
    }

    void SyncCollectionTilesFromFolder(TileCollection& collection) {
        const std::filesystem::path tileDir = CollectionTilesDirectory(collection);
        std::error_code ec;
        if (!std::filesystem::exists(tileDir, ec)) {
            return;
        }

        std::unordered_set<int> keepTileIds;
        for (const auto& entry : std::filesystem::directory_iterator(tileDir, ec)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            int parsedId = -1;
            if (TryParseTileIdFromFilename(entry.path(), parsedId)) {
                keepTileIds.insert(parsedId);
            }
        }
        if (keepTileIds.empty()) {
            return;
        }

        std::vector<int> removedIds;
        collection.tiles.erase(
            std::remove_if(collection.tiles.begin(), collection.tiles.end(), [&keepTileIds, &removedIds](const TileDef& tile) {
                if (keepTileIds.find(tile.id) == keepTileIds.end()) {
                    removedIds.push_back(tile.id);
                    return true;
                }
                return false;
            }),
            collection.tiles.end()
        );

        for (int removedId : removedIds) {
            RemoveTileReferences(removedId);
        }
    }

    void RemoveSelectedTile() {
        TileCollection* collection = ActiveTileCollection();
        if (!collection) {
            return;
        }
        if (collection->tiles.size() <= 1) {
            wxMessageBox("A collection must keep at least one tile.", "Remove tile", wxOK | wxICON_INFORMATION, this);
            return;
        }

        auto it = std::find_if(collection->tiles.begin(), collection->tiles.end(), [this](const TileDef& tile) {
            return tile.id == selectedTileId_;
        });
        if (it == collection->tiles.end()) {
            return;
        }

        if (wxMessageBox("Remove selected tile from palette and delete its image file?", "Remove tile", wxYES_NO | wxICON_QUESTION, this) != wxYES) {
            return;
        }

        const int removedTileId = it->id;
        const std::filesystem::path imagePath = TileImagePathForId(*collection, removedTileId);
        std::error_code ec;
        std::filesystem::remove(imagePath, ec);

        collection->tiles.erase(it);
        RemoveTileReferences(removedTileId);

        if (!collection->tiles.empty()) {
            selectedTileId_ = collection->tiles.front().id;
            canvas_->SetTileSelection(selectedTileId_);
        }

        MarkDirty();
        RefreshAll();
    }

    std::vector<std::pair<std::string, std::string>> BuildTileMoveTargets() const {
        std::vector<std::pair<std::string, std::string>> out;
        out.reserve(world_.tileCollections.size());
        for (const TileCollection& collection : world_.tileCollections) {
            out.emplace_back(collection.id, collection.name.empty() ? collection.id : collection.name);
        }
        return out;
    }

    void MoveTileToCollection(int tileId, const std::string& targetCollectionId) {
        TileCollection* source = ActiveTileCollection();
        if (!source || targetCollectionId.empty() || source->id == targetCollectionId) {
            return;
        }

        const std::string sourceCollectionId = source->id;

        int nextSelectedTileId = -1;
        for (size_t i = 0; i < source->tiles.size(); ++i) {
            if (source->tiles[i].id != tileId) {
                continue;
            }
            if (i + 1 < source->tiles.size()) {
                nextSelectedTileId = source->tiles[i + 1].id;
            } else if (i > 0) {
                nextSelectedTileId = source->tiles[i - 1].id;
            }
            break;
        }

        TileDef* sourceTile = FindTileDef(*source, tileId);
        if (!sourceTile) {
            return;
        }

        TileCollection* target = FindTileCollection(world_, targetCollectionId);
        if (!target) {
            return;
        }

        if (FindTileDef(*target, tileId)) {
            wxMessageBox(
                wxString::Format("Target collection already contains tile id %d.", tileId),
                "Move tile",
                wxOK | wxICON_WARNING,
                this
            );
            return;
        }

        EnsureCollectionTileImages(*source);
        EnsureCollectionTileImages(*target);

        const std::filesystem::path srcPath = TileImagePathForId(*source, tileId);
        const std::filesystem::path dstPath = TileImagePathForId(*target, tileId);

        std::error_code ec;
        std::filesystem::create_directories(dstPath.parent_path(), ec);
        if (!std::filesystem::exists(srcPath, ec)) {
            wxMessageBox("Source tile image file was not found.", "Move tile", wxOK | wxICON_ERROR, this);
            return;
        }
        if (std::filesystem::exists(dstPath, ec)) {
            wxMessageBox("A tile image with the same id already exists in the target collection.", "Move tile", wxOK | wxICON_WARNING, this);
            return;
        }

        ec.clear();
        std::filesystem::rename(srcPath, dstPath, ec);
        if (ec) {
            ec.clear();
            std::filesystem::copy_file(srcPath, dstPath, std::filesystem::copy_options::none, ec);
            if (ec) {
                wxMessageBox("Failed to move tile image file to the target collection.", "Move tile", wxOK | wxICON_ERROR, this);
                return;
            }
            ec.clear();
            std::filesystem::remove(srcPath, ec);
        }

        world_.activeTileCollectionId = sourceCollectionId;
        selectedTileId_ = nextSelectedTileId >= 0 ? nextSelectedTileId : 0;
        MarkDirty();
        RefreshAll();
    }

    void EnsureTileCollectionsInitialized() {
        EnsureCollectionsFromFilesystem();

        if (world_.tileCollections.empty()) {
            world_.tileCollections.push_back(BuildForestCollectionFromImage("data/tiles/Overworld.png"));
        }

        if (world_.activeTileCollectionId.empty()) {
            world_.activeTileCollectionId = world_.tileCollections.front().id;
        }
        if (!FindTileCollection(world_, world_.activeTileCollectionId)) {
            world_.activeTileCollectionId = world_.tileCollections.front().id;
        }
    }

    void EnsureCharacterSpritesetsInitialized() {
        if (world_.characterSpritesets.empty()) {
            world_.characterSpritesets.push_back(BuildDefaultPlayerSpriteset());
        }
        if (world_.activeCharacterSpritesetId.empty()) {
            world_.activeCharacterSpritesetId = world_.characterSpritesets.front().id;
        }
        bool foundActive = false;
        for (const CharacterSpriteset& spriteset : world_.characterSpritesets) {
            if (spriteset.id == world_.activeCharacterSpritesetId) {
                foundActive = true;
                break;
            }
        }
        if (!foundActive && !world_.characterSpritesets.empty()) {
            world_.activeCharacterSpritesetId = world_.characterSpritesets.front().id;
        }
    }

    void RefreshCharactersUi() {
        EnsureCharacterSpritesetsInitialized();

        if (selectedCharacterSpritesetId_.empty()) {
            selectedCharacterSpritesetId_ = world_.activeCharacterSpritesetId;
        }
        if (std::none_of(world_.characterSpritesets.begin(), world_.characterSpritesets.end(), [this](const CharacterSpriteset& spriteset) {
                return spriteset.id == selectedCharacterSpritesetId_;
            })) {
            selectedCharacterSpritesetId_ = world_.characterSpritesets.empty() ? std::string() : world_.characterSpritesets.front().id;
        }

        activeCharacterChoice_->Clear();
        int activeChoice = 0;
        for (size_t i = 0; i < world_.characterSpritesets.size(); ++i) {
            const CharacterSpriteset& spriteset = world_.characterSpritesets[i];
            const std::string label = spriteset.name.empty() ? spriteset.id : spriteset.name;
            activeCharacterChoice_->Append(wxString::FromUTF8(label));
            if (spriteset.id == world_.activeCharacterSpritesetId) {
                activeChoice = static_cast<int>(i);
            }
        }

        if (characterPalette_) {
            characterPalette_->SetItems(&world_.characterSpritesets);
            characterPalette_->SetSelectedCharacterId(selectedCharacterSpritesetId_);
        }

        if (!world_.characterSpritesets.empty()) {
            activeCharacterChoice_->SetSelection(activeChoice);
        }
    }

    int SelectedCharacterSpritesetIndex() const {
        if (selectedCharacterSpritesetId_.empty()) {
            return wxNOT_FOUND;
        }
        for (size_t i = 0; i < world_.characterSpritesets.size(); ++i) {
            if (world_.characterSpritesets[i].id == selectedCharacterSpritesetId_) {
                return static_cast<int>(i);
            }
        }
        return wxNOT_FOUND;
    }

    void RefreshSelectedTileControls() {
        TileCollection* collection = ActiveTileCollection();
        if (!collection) {
            tileSolidCheck_->SetValue(false);
            tileSolidCheck_->Disable();
            return;
        }

        TileDef* tile = FindTileDef(*collection, selectedTileId_);
        if (!tile && !collection->tiles.empty()) {
            selectedTileId_ = collection->tiles.front().id;
            tile = &collection->tiles.front();
            canvas_->SetTileSelection(selectedTileId_);
        }

        if (tile) {
            tileSolidCheck_->Enable();
            tileSolidCheck_->SetValue(tile->solid);
            tileSolidCheck_->SetLabel(wxString::Format("Selected Tile Solid (%s)", tile->name));
        } else {
            tileSolidCheck_->SetValue(false);
            tileSolidCheck_->Disable();
            tileSolidCheck_->SetLabel("Selected Tile Solid");
        }
        tileSolidCheck_->InvalidateBestSize();
        if (wxWindow* parent = tileSolidCheck_->GetParent()) {
            parent->Layout();
        }
    }

    void CommitActiveCollectionDescriptionFromUi() {
        if (!tileCollectionDescriptionCtrl_) {
            return;
        }

        TileCollection* collection = ActiveTileCollection();
        if (!collection) {
            return;
        }

        const std::string nextDescription = tileCollectionDescriptionCtrl_->GetValue().ToStdString();
        if (collection->description != nextDescription) {
            collection->description = nextDescription;
            MarkDirty();
        }
    }

    void RefreshTileCollectionsUi() {
        EnsureTileCollectionsInitialized();
        tileCollectionChoice_->Clear();
        int selection = 0;
        for (size_t i = 0; i < world_.tileCollections.size(); ++i) {
            const TileCollection& collection = world_.tileCollections[i];
            tileCollectionChoice_->Append(wxString::FromUTF8(collection.name));
            if (collection.id == world_.activeTileCollectionId) {
                selection = static_cast<int>(i);
            }
        }
        if (!world_.tileCollections.empty()) {
            tileCollectionChoice_->SetSelection(selection);
        }

        tilePalette_->SetMoveTargets(BuildTileMoveTargets());

        const TileCollection* active = ActiveTileCollection();
        tilePalette_->SetSelectedTileId(selectedTileId_);
        tilePalette_->SetPreferredColumns(active ? active->editorPaletteColumns : 0);
        tilePalette_->SetCollection(active);
        canvas_->SetTileCollections(&world_.tileCollections);
        canvas_->SetTileOwnerHints(&tileOwnerHints_);
        canvas_->SetTileCollection(active);
        SetPaintLayerIndex(paintLayerIndex_);
        canvas_->SetShowHitboxOverlay(showHitboxOverlay_);
        for (int layer = 0; layer < kTileLayers; ++layer) {
            canvas_->SetLayerVisibility(layer, layerVisible_[static_cast<size_t>(layer)]);
        }
        if (active && !active->tiles.empty()) {
            bool foundSelected = false;
            for (const TileDef& tile : active->tiles) {
                if (tile.id == selectedTileId_) {
                    foundSelected = true;
                    break;
                }
            }
            if (!foundSelected) {
                selectedTileId_ = active->tiles.front().id;
            }
            canvas_->SetTileSelection(selectedTileId_);
        }
        if (tileCollectionDescriptionCtrl_) {
            tileCollectionDescriptionCtrl_->Enable(active != nullptr);
            tileCollectionDescriptionCtrl_->ChangeValue(active ? wxString::FromUTF8(active->description) : wxString());
        }
        if (tilePaletteColumnsCtrl_) {
            const int uiColumns = active
                ? (active->editorPaletteColumns > 0 ? active->editorPaletteColumns : tilePalette_->ComputedAutoColumns())
                : 1;
            updatingTilePaletteColumnsUi_ = true;
            tilePaletteColumnsCtrl_->Enable(active != nullptr);
            tilePaletteColumnsCtrl_->SetValue(std::max(1, uiColumns));
            updatingTilePaletteColumnsUi_ = false;
        }
        RefreshSelectedTileControls();
    }

    void UpdateTitle() {
        const wxString mark = dirty_ ? "*" : "";
        const wxString filePart = currentPath_.empty() ? wxString("untitled_world.json") : wxString::FromUTF8(currentPath_);
        SetTitle("Quest for Rome Editor - " + filePart + mark);
    }

    void RefreshMapList() {
        mapList_->Clear();
        for (const MapLoadData& map : world_.maps) {
            mapList_->Append(wxString::Format("%s (%dx%d)", map.id, map.widthScreens, map.heightScreens));
        }
        if (currentMapIndex_ >= 0 && currentMapIndex_ < static_cast<int>(world_.maps.size())) {
            mapList_->SetSelection(currentMapIndex_);
        }
    }

    void RefreshGlobalStartControls() {
        globalStartMapChoice_->Clear();
        int selection = 0;
        for (size_t i = 0; i < world_.maps.size(); ++i) {
            globalStartMapChoice_->Append(wxString::FromUTF8(world_.maps[i].id));
            if (world_.maps[i].id == world_.defaultMapId) {
                selection = static_cast<int>(i);
            }
        }
        if (!world_.maps.empty()) {
            globalStartMapChoice_->SetSelection(selection);
        }
        globalStartXCtrl_->SetValue(world_.defaultStartScreenX);
        globalStartYCtrl_->SetValue(world_.defaultStartScreenY);
        worldGrid_->SetGlobalStart(world_.defaultMapId, world_.defaultStartScreenX, world_.defaultStartScreenY);
    }

    void RefreshMapProperties() {
        MapLoadData* map = CurrentMap();
        if (!map) {
            mapIdCtrl_->ChangeValue("");
            mapNameCtrl_->ChangeValue("");
            return;
        }

        EnsureMapScreens(*map);
        mapIdCtrl_->ChangeValue(wxString::FromUTF8(map->id));
        mapNameCtrl_->ChangeValue(wxString::FromUTF8(map->name));
        mapWidthCtrl_->SetValue(map->widthScreens);
        mapHeightCtrl_->SetValue(map->heightScreens);
        mapStartXCtrl_->SetRange(0, std::max(0, map->widthScreens - 1));
        mapStartYCtrl_->SetRange(0, std::max(0, map->heightScreens - 1));
        mapStartXCtrl_->SetValue(map->defaultStartScreenX);
        mapStartYCtrl_->SetValue(map->defaultStartScreenY);
        worldGrid_->SetMap(map);
        worldGrid_->SetGlobalStart(world_.defaultMapId, world_.defaultStartScreenX, world_.defaultStartScreenY);
        worldGrid_->SetSelectedScreen(currentScreenX_, currentScreenY_);
    }

    void RefreshItemsList() {
        if (!itemPalette_) {
            return;
        }

        if (selectedItemDefinitionId_.empty() && !world_.itemDefinitions.empty()) {
            selectedItemDefinitionId_ = world_.itemDefinitions.front().id;
        }
        if (!selectedItemDefinitionId_.empty() && !FindItemDefinition(world_, selectedItemDefinitionId_)) {
            selectedItemDefinitionId_ = world_.itemDefinitions.empty() ? std::string() : world_.itemDefinitions.front().id;
        }

        itemPalette_->SetItems(&world_.itemDefinitions);
        SelectItemDefinition(selectedItemDefinitionId_, false);
    }

    void RefreshEnemiesList() {
        if (!enemyPalette_) {
            return;
        }

        if (selectedEnemyDefinitionId_.empty() && !world_.enemyDefinitions.empty()) {
            selectedEnemyDefinitionId_ = world_.enemyDefinitions.front().id;
        }
        if (!selectedEnemyDefinitionId_.empty() && !FindEnemyDefinition(world_, selectedEnemyDefinitionId_)) {
            selectedEnemyDefinitionId_ = world_.enemyDefinitions.empty() ? std::string() : world_.enemyDefinitions.front().id;
        }

        enemyPalette_->SetItems(&world_.enemyDefinitions);
        SelectEnemyDefinition(selectedEnemyDefinitionId_, false);
    }

    void RefreshNpcsList() {
        if (!npcPalette_) {
            return;
        }

        if (!selectedNpcDefinitionId_.empty()) {
            const EnemyDefinition* selected = FindEnemyDefinition(world_, selectedNpcDefinitionId_);
            if (!selected || !selected->isNpc) {
                selectedNpcDefinitionId_.clear();
            }
        }

        if (selectedNpcDefinitionId_.empty()) {
            for (const EnemyDefinition& enemy : world_.enemyDefinitions) {
                if (enemy.isNpc) {
                    selectedNpcDefinitionId_ = enemy.id;
                    break;
                }
            }
        }

        npcPalette_->SetItems(&world_.enemyDefinitions);
        SelectNpcDefinition(selectedNpcDefinitionId_, false);
    }

    void RefreshProjectilesList() {
        if (!projectilePalette_) {
            return;
        }

        if (selectedProjectileDefinitionId_.empty() && !world_.projectileDefinitions.empty()) {
            selectedProjectileDefinitionId_ = world_.projectileDefinitions.front().id;
        }
        if (!selectedProjectileDefinitionId_.empty() && !FindProjectileDefinition(world_, selectedProjectileDefinitionId_)) {
            selectedProjectileDefinitionId_ = world_.projectileDefinitions.empty() ? std::string() : world_.projectileDefinitions.front().id;
        }

        projectilePalette_->SetItems(&world_.projectileDefinitions);
        SelectProjectileDefinition(selectedProjectileDefinitionId_, false);
    }

    void RefreshWeaponsList() {
        if (!weaponPalette_) {
            return;
        }

        if (selectedWeaponDefinitionId_.empty() && !world_.weaponDefinitions.empty()) {
            selectedWeaponDefinitionId_ = world_.weaponDefinitions.front().id;
        }
        if (!selectedWeaponDefinitionId_.empty() && !FindWeaponDefinition(world_, selectedWeaponDefinitionId_)) {
            selectedWeaponDefinitionId_ = world_.weaponDefinitions.empty() ? std::string() : world_.weaponDefinitions.front().id;
        }

        weaponPalette_->SetItems(&world_.weaponDefinitions);
        SelectWeaponDefinition(selectedWeaponDefinitionId_, false);
    }

    void RefreshTransitionsList() {
        transitionList_->Clear();
        ScreenLoadData* screen = CurrentScreen();
        if (!screen) {
            return;
        }

        for (const ScreenTransition& tr : screen->transitions) {
            transitionList_->Append(wxString::Format("%s -> %s (%d,%d) spawn(%d,%d) %s", tr.edge, tr.toMapId, tr.toScreenX, tr.toScreenY, tr.spawnX, tr.spawnY, TransitionKindLabel(tr.kind)));
        }
    }

    void RefreshWarpList() {
        if (!warpPalette_) {
            return;
        }

        if (selectedWarpDefinitionId_.empty() && !world_.warpDefinitions.empty()) {
            selectedWarpDefinitionId_ = world_.warpDefinitions.front().id;
        }
        if (!selectedWarpDefinitionId_.empty() && !FindWarpDefinition(world_, selectedWarpDefinitionId_)) {
            selectedWarpDefinitionId_ = world_.warpDefinitions.empty() ? std::string() : world_.warpDefinitions.front().id;
        }

        warpPalette_->SetItems(&world_.warpDefinitions, &world_.tileCollections);
        warpPalette_->SetSelected(selectedWarpDefinitionId_, selectedWarpEndpointIndex_);

        if (canvas_) {
            canvas_->SetWarpSelection(selectedWarpDefinitionId_, selectedWarpEndpointIndex_);
        }
    }

    void RefreshPowerupList() {
        if (!powerupList_) {
            return;
        }
        powerupList_->Clear();
        for (const PowerupDef& powerup : world_.powerups) {
            powerupList_->Append(wxString::Format("%s | %s | +%d %.1fs", powerup.id, powerup.effect, powerup.magnitude, powerup.durationSeconds));
        }
    }

    void RefreshDropTableList() {
        if (!dropTablePalette_) {
            return;
        }
        dropTablePalette_->SetDropTables(&world_.dropTables);
    }

    void RefreshGlobalSettingsControls() {
        if (globalKnockbackDistanceCtrl_) {
            globalKnockbackDistanceCtrl_->SetValue(world_.globalSettings.knockbackDistanceTiles);
        }
        if (globalInvulnerabilityCtrl_) {
            globalInvulnerabilityCtrl_->SetValue(world_.globalSettings.invulnerabilitySeconds);
        }
        if (globalTextSpeedCtrl_) {
            globalTextSpeedCtrl_->SetValue(world_.globalSettings.textLettersPerSecond);
        }
        if (globalDropItemLifetimeCtrl_) {
            globalDropItemLifetimeCtrl_->SetValue(world_.globalSettings.dropItemLifetimeSec);
        }
        if (globalItemPickupDurationCtrl_) {
            globalItemPickupDurationCtrl_->SetValue(world_.globalSettings.itemPickupDurationSec);
        }
    }

    void RefreshTextSettingsControls() {
        if (!textGlyphMapCtrl_) {
            return;
        }

        updatingTextGlyphMapUi_ = true;
        textGlyphMapCtrl_->SetValue(wxString::FromUTF8(FormatTextGlyphMapForEditor(world_.globalSettings.textGlyphMap)));
        updatingTextGlyphMapUi_ = false;
    }

    void RefreshCurrentScreenViews() {
        ScreenLoadData* screen = CurrentScreen();
        canvas_->SetScreen(screen);
        canvas_->SetItemDefinitions(&world_.itemDefinitions);
        canvas_->SetItemPlacements(screen ? &screen->itemPlacements : nullptr);
        canvas_->SetEnemyDefinitions(&world_.enemyDefinitions);
        canvas_->SetEnemyPlacements(screen ? &screen->enemyPlacements : nullptr);
        canvas_->SetWarpDefinitions(&world_.warpDefinitions);
        canvas_->SetWarpPlacements(screen ? &screen->warpPlacements : nullptr);
        canvas_->SetItemSelection(selectedItemDefinitionId_);
        canvas_->SetEnemySelection(selectedEnemyDefinitionId_);
        canvas_->SetWarpSelection(selectedWarpDefinitionId_, selectedWarpEndpointIndex_);
        SyncCanvasInteractionModeFromUi();
        currentScreenLabel_->SetLabel(wxString::Format("Map %s - Screen (%d, %d)", CurrentMap() ? CurrentMap()->id : std::string("-"), currentScreenX_, currentScreenY_));
        RefreshScreenTextControls();
        RefreshItemsList();
        RefreshEnemiesList();
        RefreshNpcsList();
        RefreshProjectilesList();
        RefreshWeaponsList();
        RefreshTransitionsList();
        RefreshWarpList();
        if (CurrentMap()) {
            worldGrid_->SetSelectedScreen(currentScreenX_, currentScreenY_);
            worldGrid_->Refresh();
        }
    }

    void SelectDefaultStartLocation() {
        if (world_.maps.empty()) {
            currentMapIndex_ = 0;
            currentScreenX_ = 0;
            currentScreenY_ = 0;
            RefreshAll();
            return;
        }

        int mapIndex = 0;
        for (size_t i = 0; i < world_.maps.size(); ++i) {
            if (world_.maps[i].id == world_.defaultMapId) {
                mapIndex = static_cast<int>(i);
                break;
            }
        }

        currentMapIndex_ = mapIndex;
        SelectMap(mapIndex);
    }

    void RefreshAll() {
        RefreshSheetSpriteLibraries();
        EnsureTileCollectionsInitialized();
        EnsureCharacterSpritesetsInitialized();
        for (MapLoadData& map : world_.maps) {
            EnsureMapScreens(map);
        }
        RefreshMapList();
        RefreshGlobalStartControls();
        RefreshGlobalSettingsControls();
        RefreshTextSettingsControls();
        RefreshTileCollectionsUi();
        RefreshMapProperties();
        RefreshCurrentScreenViews();
        RefreshPowerupList();
        RefreshDropTableList();
        RefreshCharactersUi();
    }

    void SelectMap(int index) {
        if (index < 0 || index >= static_cast<int>(world_.maps.size())) {
            return;
        }
        currentMapIndex_ = index;
        // Set screen to the world's start screen (not the map's start screen)
        currentScreenX_ = std::clamp(world_.defaultStartScreenX, 0, std::max(0, world_.maps[static_cast<size_t>(index)].widthScreens - 1));
        currentScreenY_ = std::clamp(world_.defaultStartScreenY, 0, std::max(0, world_.maps[static_cast<size_t>(index)].heightScreens - 1));
        RefreshAll();
        // Explicitly update the world grid to ensure the selection border is drawn at the correct screen
        if (worldGrid_) {
            worldGrid_->SetSelectedScreen(currentScreenX_, currentScreenY_);
            worldGrid_->Refresh();
        }
    }

    void SelectScreen(int x, int y) {
        MapLoadData* map = CurrentMap();
        if (!map) {
            return;
        }
        currentScreenX_ = std::clamp(x, 0, std::max(0, map->widthScreens - 1));
        currentScreenY_ = std::clamp(y, 0, std::max(0, map->heightScreens - 1));
        RefreshCurrentScreenViews();
    }

    bool ValidateBeforeSave() {
        std::vector<std::string> ids;
        for (const MapLoadData& map : world_.maps) {
            if (map.id.empty()) {
                wxMessageBox("Each map needs a non-empty ID.", "Save failed", wxOK | wxICON_ERROR, this);
                return false;
            }
            if (std::find(ids.begin(), ids.end(), map.id) != ids.end()) {
                wxMessageBox("Map IDs must be unique.", "Save failed", wxOK | wxICON_ERROR, this);
                return false;
            }
            ids.push_back(map.id);
        }
        return true;
    }

    void OnMapSelected(wxCommandEvent&) {
        SelectMap(mapList_->GetSelection());
    }

    void OnFrameResized(wxSizeEvent& event) {
        ApplyBalancedSplitLayout();
        event.Skip();
    }

    void ApplyBalancedSplitLayout() {
        if (!splitterOuter_ || !centerRightSplit_ || !splitterOuter_->IsSplit() || !centerRightSplit_->IsSplit()) {
            return;
        }

        const int totalW = splitterOuter_->GetClientSize().GetWidth();
        if (totalW <= 0) {
            return;
        }

        int side = totalW / 4;
        side = std::max(side, 260);
        side = std::min(side, (totalW - 300) / 2);
        if (side < 260) {
            return;
        }

        splitterOuter_->SetSashPosition(side);

        const int centerRightW = totalW - side;
        int centerW = centerRightW - side;
        centerW = std::max(centerW, 300);
        centerW = std::min(centerW, centerRightW - 260);
        centerRightSplit_->SetSashPosition(centerW);
    }

    void OnTileCollectionChanged(wxCommandEvent&) {
        CommitActiveCollectionDescriptionFromUi();
        const int selection = tileCollectionChoice_->GetSelection();
        if (selection == wxNOT_FOUND || selection >= static_cast<int>(world_.tileCollections.size())) {
            return;
        }
        world_.activeTileCollectionId = world_.tileCollections[static_cast<size_t>(selection)].id;
        const TileCollection* collection = ActiveTileCollection();
        tilePalette_->SetSelectedTileId(selectedTileId_);
        tilePalette_->SetPreferredColumns(collection ? collection->editorPaletteColumns : 0);
        tilePalette_->SetCollection(collection);
        canvas_->SetTileCollections(&world_.tileCollections);
        canvas_->SetTileOwnerHints(&tileOwnerHints_);
        canvas_->SetTileCollection(collection);
        if (collection && !collection->tiles.empty()) {
            bool foundSelected = false;
            for (const TileDef& tile : collection->tiles) {
                if (tile.id == selectedTileId_) {
                    foundSelected = true;
                    break;
                }
            }
            if (!foundSelected) {
                selectedTileId_ = collection->tiles.front().id;
            }
            canvas_->SetTileSelection(selectedTileId_);
        }
        if (tileCollectionDescriptionCtrl_) {
            tileCollectionDescriptionCtrl_->Enable(collection != nullptr);
            tileCollectionDescriptionCtrl_->ChangeValue(collection ? wxString::FromUTF8(collection->description) : wxString());
        }
        if (tilePaletteColumnsCtrl_) {
            const int uiColumns = collection
                ? (collection->editorPaletteColumns > 0 ? collection->editorPaletteColumns : tilePalette_->ComputedAutoColumns())
                : 1;
            updatingTilePaletteColumnsUi_ = true;
            tilePaletteColumnsCtrl_->Enable(collection != nullptr);
            tilePaletteColumnsCtrl_->SetValue(std::max(1, uiColumns));
            updatingTilePaletteColumnsUi_ = false;
        }
        RefreshSelectedTileControls();
        MarkDirty();
    }

    void OnTileCollectionDescriptionChanged(wxCommandEvent&) {
        CommitActiveCollectionDescriptionFromUi();
    }

    void OnTilePaletteColumnsChanged(wxCommandEvent&) {
        if (updatingTilePaletteColumnsUi_) {
            return;
        }
        TileCollection* collection = ActiveTileCollection();
        if (!collection || !tilePaletteColumnsCtrl_) {
            return;
        }

        const int columns = std::max(1, tilePaletteColumnsCtrl_->GetValue());
        if (collection->editorPaletteColumns == columns) {
            return;
        }

        collection->editorPaletteColumns = columns;
        tilePalette_->SetPreferredColumns(columns);
        MarkDirty();
    }

    void OnEditTileHitbox(TileDef& tile) {
        const TileCollection* collection = ActiveTileCollection();
        if (!collection) {
            return;
        }

        // Get or create atlas bitmap
        wxBitmap atlas;
        if (!collection->imagePath.empty()) {
            wxFileName directPath(wxString::FromUTF8(collection->imagePath));
            if (directPath.FileExists()) {
                atlas.LoadFile(directPath.GetFullPath(), wxBITMAP_TYPE_PNG);
            } else {
                wxFileName relPath(wxString::FromUTF8(collection->imagePath));
                relPath.MakeAbsolute(wxGetCwd());
                if (relPath.FileExists()) {
                    atlas.LoadFile(relPath.GetFullPath(), wxBITMAP_TYPE_PNG);
                }
            }
        }

        TileHitboxEditor dialog(this, tile, atlas, *collection);
        if (dialog.ShowModal() == wxID_OK) {
            MarkDirty();
            RefreshAll();  // Refresh to show updated hitboxes in the tile palette
        }
    }

    void OnPaintLayerChanged(wxCommandEvent&) {
        SetPaintLayerIndex(paintLayerChoice_->GetSelection());
    }

    void OnLayerVisibilityChanged(wxCommandEvent&) {
        layerVisible_[0] = layerVisibleCheck0_->GetValue();
        layerVisible_[1] = layerVisibleCheck1_->GetValue();
        layerVisible_[2] = layerVisibleCheck2_->GetValue();
        for (int layer = 0; layer < kTileLayers; ++layer) {
            canvas_->SetLayerVisibility(layer, layerVisible_[static_cast<size_t>(layer)]);
        }
    }

    void OnHitboxOverlayToggled(wxCommandEvent&) {
        showHitboxOverlay_ = hitboxOverlayCheck_ && hitboxOverlayCheck_->GetValue();
        if (canvas_) {
            canvas_->SetShowHitboxOverlay(showHitboxOverlay_);
        }
    }

    void OnSelectedTileSolidChanged(wxCommandEvent&) {
        TileCollection* collection = ActiveTileCollection();
        if (!collection) {
            return;
        }
        TileDef* tile = FindTileDef(*collection, selectedTileId_);
        if (!tile) {
            return;
        }
        tile->solid = tileSolidCheck_->GetValue();
        tilePalette_->Refresh();
        MarkDirty();
    }

    void OnRemoveTile(wxCommandEvent&) {
        RemoveSelectedTile();
    }

    void OnAddTileFromSheet(wxCommandEvent&) {
        TileCollection* collection = ActiveTileCollection();
        if (!collection) {
            return;
        }

        RefreshSheetSpriteLibraries();
        if (sheetSpriteCollections_.empty()) {
            wxMessageBox("No sprites were found in data/sheets.", "Add Tile", wxOK | wxICON_INFORMATION, this);
            return;
        }

        SpriteLibraryPickerDialog picker(this, sheetSpriteCollections_, "Add Tile From Sprite Library");
        if (picker.ShowModal() != wxID_OK) {
            return;
        }

        SheetSpritePick pick;
        if (!picker.GetSelectedPick(pick)) {
            wxMessageBox("Pick a sprite to continue.", "Add Tile", wxOK | wxICON_INFORMATION, this);
            return;
        }

        if (!AddSheetSpriteToActiveCollection(pick)) {
            return;
        }

        MarkDirty();
        RefreshAll();
        tilePalette_->SetSelectedTileId(selectedTileId_, true);
        canvas_->SetTileSelection(selectedTileId_);
        RefreshSelectedTileControls();
    }

    void OnAddTileCollectionFromSheet(wxCommandEvent&) {
        if (sheetSpriteCollections_.empty()) {
            RefreshSheetSpriteLibraries();
        }
        if (sheetSpriteCollections_.empty()) {
            wxMessageBox("No sprites were found in data/sheets.", "Add Collection", wxOK | wxICON_INFORMATION, this);
            return;
        }

        wxArrayString choices;
        for (const SheetSpriteCollectionDef& collection : sheetSpriteCollections_) {
            const std::string label = collection.name.empty() ? collection.id : collection.name;
            choices.Add(wxString::FromUTF8(label));
        }

        wxSingleChoiceDialog picker(this, "Select a sheet to import as a new tile collection", "Add Collection", choices);
        if (picker.ShowModal() != wxID_OK) {
            return;
        }

        const int selection = picker.GetSelection();
        if (selection < 0 || selection >= static_cast<int>(sheetSpriteCollections_.size())) {
            return;
        }

        const SheetSpriteCollectionDef& source = sheetSpriteCollections_[static_cast<size_t>(selection)];
        TileCollection collection = BuildTileCollectionFromSheetLibrary(source, world_.tileCollections);
        if (collection.tiles.empty()) {
            wxMessageBox("The selected sheet does not contain any usable sprites.", "Add Collection", wxOK | wxICON_INFORMATION, this);
            return;
        }

        const std::string newCollectionId = collection.id;
        world_.tileCollections.push_back(std::move(collection));
        world_.activeTileCollectionId = newCollectionId;
        selectedTileId_ = world_.tileCollections.back().tiles.front().id;
        MarkDirty();
        RefreshAll();
    }

    void OnMoveLayer(wxCommandEvent&) {
        ScreenLoadData* screen = CurrentScreen();
        if (!screen) {
            return;
        }

        const int sourceLayer = std::clamp(paintLayerIndex_, 0, kTileLayers - 1);

        wxArrayString choices;
        for (int i = 0; i < kTileLayers; ++i) {
            choices.Add(wxString::Format("Layer %d", i + 1));
        }

        wxSingleChoiceDialog targetDlg(this, "Move non-empty tiles from active layer to:", "Move Layer", choices);
        if (targetDlg.ShowModal() != wxID_OK) {
            return;
        }

        const int targetLayer = targetDlg.GetSelection();
        if (targetLayer < 0 || targetLayer >= kTileLayers || targetLayer == sourceLayer) {
            return;
        }

        const int confirm = wxMessageBox(
            wxString::Format(
                "Move all non-empty tiles from Layer %d to Layer %d on current screen?\n\n"
                "Destination non-empty tiles will be overwritten where source has tiles, and source tiles will be cleared.",
                sourceLayer + 1,
                targetLayer + 1
            ),
            "Move Layer",
            wxYES_NO | wxICON_QUESTION,
            this
        );
        if (confirm != wxYES) {
            return;
        }

        bool changed = false;
        auto& sourceTiles = screen->screen.tileLayerIds[static_cast<size_t>(sourceLayer)];
        auto& targetTiles = screen->screen.tileLayerIds[static_cast<size_t>(targetLayer)];
        for (size_t i = 0; i < sourceTiles.size(); ++i) {
            if (sourceTiles[i] < 0) {
                continue;
            }
            targetTiles[i] = sourceTiles[i];
            sourceTiles[i] = -1;
            changed = true;
        }

        if (!changed) {
            wxMessageBox("Active layer has no non-empty tiles to move on this screen.", "Move Layer", wxOK | wxICON_INFORMATION, this);
            return;
        }

        MarkDirty();
        RefreshCurrentScreenViews();
        SetStatusText(wxString::Format("Moved layer L%d to L%d on current screen", sourceLayer + 1, targetLayer + 1), 0);
    }

    void OnMapMetadataChanged(wxCommandEvent&) {
        MapLoadData* map = CurrentMap();
        if (!map) {
            return;
        }
        map->id = mapIdCtrl_->GetValue().ToStdString();
        map->name = mapNameCtrl_->GetValue().ToStdString();
        if (world_.defaultMapId.empty()) {
            world_.defaultMapId = map->id;
        }
        MarkDirty();
        RefreshMapList();
        RefreshGlobalStartControls();
    }

    void OnMapStartChanged(wxSpinEvent&) {
        MapLoadData* map = CurrentMap();
        if (!map) {
            return;
        }
        map->defaultStartScreenX = mapStartXCtrl_->GetValue();
        map->defaultStartScreenY = mapStartYCtrl_->GetValue();
        worldGrid_->Refresh();
        MarkDirty();
    }

    void OnGlobalStartMapChanged(wxCommandEvent&) {
        const int selection = globalStartMapChoice_->GetSelection();
        if (selection == wxNOT_FOUND) {
            return;
        }
        world_.defaultMapId = world_.maps[static_cast<size_t>(selection)].id;
        worldGrid_->SetGlobalStart(world_.defaultMapId, world_.defaultStartScreenX, world_.defaultStartScreenY);
        worldGrid_->Refresh();
        MarkDirty();
    }

    void OnGlobalStartChanged(wxSpinEvent&) {
        world_.defaultStartScreenX = globalStartXCtrl_->GetValue();
        world_.defaultStartScreenY = globalStartYCtrl_->GetValue();
        worldGrid_->SetGlobalStart(world_.defaultMapId, world_.defaultStartScreenX, world_.defaultStartScreenY);
        worldGrid_->Refresh();
        MarkDirty();
    }

    void OnGlobalSettingsChanged(wxSpinDoubleEvent&) {
        if (globalKnockbackDistanceCtrl_) {
            world_.globalSettings.knockbackDistanceTiles = static_cast<float>(globalKnockbackDistanceCtrl_->GetValue());
        }
        if (globalInvulnerabilityCtrl_) {
            world_.globalSettings.invulnerabilitySeconds = static_cast<float>(globalInvulnerabilityCtrl_->GetValue());
        }
        if (globalTextSpeedCtrl_) {
            world_.globalSettings.textLettersPerSecond = std::max(1.0f, static_cast<float>(globalTextSpeedCtrl_->GetValue()));
        }
        if (globalDropItemLifetimeCtrl_) {
            world_.globalSettings.dropItemLifetimeSec = std::max(0.1f, static_cast<float>(globalDropItemLifetimeCtrl_->GetValue()));
        }
        if (globalItemPickupDurationCtrl_) {
            world_.globalSettings.itemPickupDurationSec = std::max(0.1f, static_cast<float>(globalItemPickupDurationCtrl_->GetValue()));
        }
        MarkDirty();
    }

    void OnTextGlyphMapChanged(wxCommandEvent&) {
        if (updatingTextGlyphMapUi_ || !textGlyphMapCtrl_) {
            return;
        }

        world_.globalSettings.textGlyphMap = NormalizeTextGlyphMap(ToUtf8String(textGlyphMapCtrl_->GetValue()));
        MarkDirty();
    }

    void OnUseCurrentAsGlobalStart(wxCommandEvent&) {
        const MapLoadData* map = CurrentMap();
        if (!map) {
            return;
        }
        world_.defaultMapId = map->id;
        world_.defaultStartScreenX = currentScreenX_;
        world_.defaultStartScreenY = currentScreenY_;
        RefreshGlobalStartControls();
        worldGrid_->SetGlobalStart(world_.defaultMapId, world_.defaultStartScreenX, world_.defaultStartScreenY);
        worldGrid_->Refresh();
        MarkDirty();
    }

    void OnNewWorld(wxCommandEvent&) {
        const int width = static_cast<int>(wxGetNumberFromUser("Overworld width in screens", "Width", "New World", 5, 1, 20, this));
        const int height = static_cast<int>(wxGetNumberFromUser("Overworld height in screens", "Height", "New World", 4, 1, 20, this));
        if (width <= 0 || height <= 0) {
            return;
        }

        world_ = WorldLoadData{};
        world_.formatVersion = 16;
        world_.globalSettings.textGlyphMap = DefaultTextGlyphMap();
        world_.maps.push_back(MakeBlankMap("overworld", "Overworld", width, height));
        world_.tileCollections.clear();
        world_.tileCollections.push_back(BuildForestCollectionFromImage("data/tiles/Overworld.png"));
        world_.activeTileCollectionId = world_.tileCollections.front().id;
        world_.itemDefinitions.clear();
        world_.enemyDefinitions.clear();
        world_.defaultMapId = "overworld";
        world_.defaultStartScreenX = 0;
        world_.defaultStartScreenY = 0;
        selectedItemDefinitionId_.clear();
        selectedEnemyDefinitionId_.clear();
        selectedWarpDefinitionId_.clear();
        selectedWarpEndpointIndex_ = 0;
        paintLayerIndex_ = 0;
        layerVisible_[0] = true;
        layerVisible_[1] = true;
        layerVisible_[2] = true;
        SetPaintLayerIndex(0);
        layerVisibleCheck0_->SetValue(true);
        layerVisibleCheck1_->SetValue(true);
        layerVisibleCheck2_->SetValue(true);
        currentMapIndex_ = 0;
        currentScreenX_ = 0;
        currentScreenY_ = 0;
        currentPath_.clear();
        if (notebook_) {
            notebook_->SetSelection(0);
        }
        dirty_ = true;
        RefreshAll();
        SyncCanvasInteractionModeFromUi();
        UpdateTitle();
    }

    void OnOpenWorld(wxCommandEvent&) {
        wxFileDialog dlg(this, "Open world", "", "", "World JSON (*.json)|*.json", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        if (dlg.ShowModal() != wxID_OK) {
            return;
        }

        const std::string openPath = ToUtf8String(dlg.GetPath());
        std::string backupPath;
        if (!CreateWorldBackupBeforeLoad(openPath, backupPath)) {
            wxMessageBox("Could not create backup copy before loading world file.", "Open failed", wxOK | wxICON_ERROR, this);
            return;
        }

        WorldLoadData loaded;
        if (!MapLoader::LoadWorldJson(openPath, loaded)) {
            wxMessageBox("Could not parse world file.", "Open failed", wxOK | wxICON_ERROR, this);
            return;
        }

        world_ = loaded;
        if (world_.maps.empty()) {
            wxMessageBox("World file does not contain any maps.", "Open failed", wxOK | wxICON_ERROR, this);
            return;
        }

        for (MapLoadData& map : world_.maps) {
            EnsureMapScreens(map);
        }

        currentPath_ = openPath;
        dirty_ = false;
        SelectDefaultStartLocation();
        UpdateTitle();
        PersistLastOpenedWorld();
    }

    bool SaveToPath(const std::string& path) {
        if (!ValidateBeforeSave()) {
            return false;
        }
        if (!MapLoader::SaveWorldJson(path, world_)) {
            wxMessageBox("Could not save world file.", "Save failed", wxOK | wxICON_ERROR, this);
            return false;
        }

        currentPath_ = path;
        dirty_ = false;
        UpdateTitle();
        PersistLastOpenedWorld();
        return true;
    }

    void OnSaveWorld(wxCommandEvent&) {
        if (currentPath_.empty()) {
            wxCommandEvent dummy;
            OnSaveWorldAs(dummy);
            return;
        }
        SaveToPath(currentPath_);
    }

    void OnSaveWorldAs(wxCommandEvent&) {
        wxFileDialog dlg(this, "Save world", "", "world.json", "World JSON (*.json)|*.json", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
        if (dlg.ShowModal() != wxID_OK) {
            return;
        }
        SaveToPath(dlg.GetPath().ToStdString());
    }

    void OnCanvasModeChanged(wxCommandEvent&) {
        SyncCanvasInteractionModeFromUi();
    }

    void OnDisplayTextToggleChanged(wxCommandEvent&) {
        if (updatingScreenTextUi_) {
            return;
        }
        ScreenLoadData* screen = CurrentScreen();
        if (!screen || !displayTextCheck_) {
            return;
        }
        const bool next = displayTextCheck_->GetValue();
        if (screen->screen.displayTextEnabled == next) {
            return;
        }
        screen->screen.displayTextEnabled = next;
        if (displayTextCtrl_) {
            displayTextCtrl_->Enable(next);
        }
        MarkDirty();
    }

    void OnHideFromMapToggleChanged(wxCommandEvent&) {
        if (updatingScreenTextUi_) {
            return;
        }
        ScreenLoadData* screen = CurrentScreen();
        if (!screen || !hideFromMapCheck_) {
            return;
        }
        const bool next = hideFromMapCheck_->GetValue();
        if (screen->screen.hideFromMap == next) {
            return;
        }
        screen->screen.hideFromMap = next;
        MarkDirty();
    }

    void OnDisplayTextValueChanged(wxCommandEvent&) {
        if (updatingScreenTextUi_) {
            return;
        }
        ScreenLoadData* screen = CurrentScreen();
        if (!screen || !displayTextCtrl_) {
            return;
        }
        const std::string next = ToUtf8String(displayTextCtrl_->GetValue());
        if (screen->screen.displayText == next) {
            return;
        }
        screen->screen.displayText = next;
        MarkDirty();
    }

    void OnNotebookPageChanged(wxBookCtrlEvent&) {
        SyncCanvasInteractionModeFromUi();
    }

    void RefreshScreenTextControls() {
        if (!displayTextCheck_ || !hideFromMapCheck_ || !displayTextCtrl_) {
            return;
        }
        updatingScreenTextUi_ = true;
        ScreenLoadData* screen = CurrentScreen();
        if (!screen) {
            displayTextCheck_->SetValue(false);
            hideFromMapCheck_->SetValue(false);
            displayTextCtrl_->ChangeValue("");
            displayTextCtrl_->Enable(false);
        } else {
            displayTextCheck_->SetValue(screen->screen.displayTextEnabled);
            hideFromMapCheck_->SetValue(screen->screen.hideFromMap);
            displayTextCtrl_->ChangeValue(wxString::FromUTF8(screen->screen.displayText));
            displayTextCtrl_->Enable(screen->screen.displayTextEnabled);
        }
        updatingScreenTextUi_ = false;
    }

    void OnWarpDefinitionSelected(wxCommandEvent&) {
        // Handled via WarpPalettePanel callback
    }

    void OnWarpEndpointChanged(wxCommandEvent&) {
        // Handled via WarpPalettePanel callback
    }

    void OnWarpPlacedOnCanvas(float pixelX, float pixelY) {
        if (selectedWarpDefinitionId_.empty()) {
            wxMessageBox("Select a warp definition first.", "Place Warp Endpoint", wxOK | wxICON_INFORMATION, this);
            return;
        }
        PlaceSelectedWarpEndpointAtCurrentScreen(pixelX, pixelY);
    }

    void OnEdgeLinkClickedOnCanvas(const std::string& edge) {
        ScreenLoadData* screen = CurrentScreen();
        MapLoadData* map = CurrentMap();
        if (!screen || !map) {
            return;
        }

        if (wxMessageBox(wxString::Format("Add edge link to '%s' edge?", edge), "Add Edge Link", wxYES_NO | wxICON_QUESTION, this) != wxYES) {
            return;
        }

        const std::string targetMapId = PromptTargetMapId("Target Map");
        if (targetMapId.empty()) {
            return;
        }

        const int toX = static_cast<int>(wxGetNumberFromUser("Target screen X", "toX", "Add Edge Link", currentScreenX_, 0, 99, this));
        const int toY = static_cast<int>(wxGetNumberFromUser("Target screen Y", "toY", "Add Edge Link", currentScreenY_, 0, 99, this));
        const int spawnX = static_cast<int>(wxGetNumberFromUser("Spawn X in target screen pixels", "spawnX", "Add Edge Link", 8, 0, kScreenPixelWidth - 1, this));
        const int spawnY = static_cast<int>(wxGetNumberFromUser("Spawn Y in target screen pixels", "spawnY", "Add Edge Link", 8, 0, kScreenPixelHeight - 1, this));

        wxArrayString kindChoices;
        kindChoices.Add("fade");
        kindChoices.Add("instant");
        wxSingleChoiceDialog kindDlg(this, "Transition style", "Link Kind", kindChoices);
        if (kindDlg.ShowModal() != wxID_OK) {
            return;
        }

        ScreenTransition tr;
        tr.fromMapId = map->id;
        tr.fromScreenX = currentScreenX_;
        tr.fromScreenY = currentScreenY_;
        tr.edge = edge;
        tr.toMapId = targetMapId;
        tr.toScreenX = toX;
        tr.toScreenY = toY;
        tr.spawnX = spawnX;
        tr.spawnY = spawnY;
        tr.kind = kindDlg.GetStringSelection() == "instant" ? TransitionKind::Instant : TransitionKind::Fade;
        screen->transitions.push_back(tr);

        MarkDirty();
        RefreshTransitionsList();
        worldGrid_->Refresh();
        canvas_->Refresh();
    }

    void OnAddMap(wxCommandEvent&) {
        wxTextEntryDialog idDlg(this, "Map ID", "Add Map", wxString::Format("map_%zu", world_.maps.size()));
        if (idDlg.ShowModal() != wxID_OK) {
            return;
        }
        wxTextEntryDialog nameDlg(this, "Map Name", "Add Map", "New Map");
        if (nameDlg.ShowModal() != wxID_OK) {
            return;
        }
        const int width = static_cast<int>(wxGetNumberFromUser("Map width in screens", "Width", "Add Map", 2, 1, 20, this));
        const int height = static_cast<int>(wxGetNumberFromUser("Map height in screens", "Height", "Add Map", 2, 1, 20, this));
        if (width <= 0 || height <= 0) {
            return;
        }

        world_.maps.push_back(MakeBlankMap(idDlg.GetValue().ToStdString(), nameDlg.GetValue().ToStdString(), width, height));
        currentMapIndex_ = static_cast<int>(world_.maps.size()) - 1;
        currentScreenX_ = 0;
        currentScreenY_ = 0;
        MarkDirty();
        RefreshAll();
    }

    void OnRemoveMap(wxCommandEvent&) {
        if (world_.maps.size() <= 1 || currentMapIndex_ == wxNOT_FOUND) {
            return;
        }

        const std::string removedId = world_.maps[static_cast<size_t>(currentMapIndex_)].id;
        world_.maps.erase(world_.maps.begin() + currentMapIndex_);
        currentMapIndex_ = std::clamp(currentMapIndex_, 0, static_cast<int>(world_.maps.size()) - 1);
        currentScreenX_ = 0;
        currentScreenY_ = 0;
        if (world_.defaultMapId == removedId) {
            world_.defaultMapId = world_.maps.front().id;
            world_.defaultStartScreenX = 0;
            world_.defaultStartScreenY = 0;
        }
        MarkDirty();
        RefreshAll();
    }

    void OnResizeCurrentMap(wxCommandEvent&) {
        MapLoadData* map = CurrentMap();
        if (!map) {
            return;
        }
        map->widthScreens = mapWidthCtrl_->GetValue();
        map->heightScreens = mapHeightCtrl_->GetValue();
        EnsureMapScreens(*map);
        currentScreenX_ = std::clamp(currentScreenX_, 0, std::max(0, map->widthScreens - 1));
        currentScreenY_ = std::clamp(currentScreenY_, 0, std::max(0, map->heightScreens - 1));
        if (world_.defaultMapId == map->id) {
            world_.defaultStartScreenX = std::clamp(world_.defaultStartScreenX, 0, std::max(0, map->widthScreens - 1));
            world_.defaultStartScreenY = std::clamp(world_.defaultStartScreenY, 0, std::max(0, map->heightScreens - 1));
        }
        MarkDirty();
        RefreshAll();
    }

    void OnCopyScreenToScreen(wxCommandEvent&) {
        MapLoadData* map = CurrentMap();
        if (!map) {
            return;
        }

        std::vector<std::pair<int, int>> screenCoords;
        wxArrayString screenChoices;
        screenChoices.reserve(map->screens.size());
        for (const ScreenLoadData& screen : map->screens) {
            screenCoords.push_back({screen.x, screen.y});
            screenChoices.Add(wxString::Format("Screen (%d, %d)", screen.x, screen.y));
        }
        if (screenCoords.size() < 2) {
            wxMessageBox("The current map needs at least two screens to copy between them.", "Copy Screen Tiles", wxOK | wxICON_INFORMATION, this);
            return;
        }

        int sourceDefault = 0;
        for (size_t i = 0; i < screenCoords.size(); ++i) {
            if (screenCoords[i].first == currentScreenX_ && screenCoords[i].second == currentScreenY_) {
                sourceDefault = static_cast<int>(i);
                break;
            }
        }

        wxSingleChoiceDialog sourceDlg(this, "Select source screen", "Copy Screen Tiles", screenChoices);
        sourceDlg.SetSelection(sourceDefault);
        if (sourceDlg.ShowModal() != wxID_OK) {
            return;
        }

        wxSingleChoiceDialog targetDlg(this, "Select destination screen", "Copy Screen Tiles", screenChoices);
        targetDlg.SetSelection(sourceDefault == 0 ? 1 : 0);
        if (targetDlg.ShowModal() != wxID_OK) {
            return;
        }

        const int sourceIndex = sourceDlg.GetSelection();
        const int targetIndex = targetDlg.GetSelection();
        if (sourceIndex < 0 || sourceIndex >= static_cast<int>(screenCoords.size()) || targetIndex < 0 || targetIndex >= static_cast<int>(screenCoords.size()) || sourceIndex == targetIndex) {
            return;
        }

        const auto [sourceX, sourceY] = screenCoords[static_cast<size_t>(sourceIndex)];
        const auto [targetX, targetY] = screenCoords[static_cast<size_t>(targetIndex)];
        ScreenLoadData* source = FindScreen(*map, sourceX, sourceY);
        ScreenLoadData* target = FindScreen(*map, targetX, targetY);
        if (!source || !target) {
            return;
        }

        const int confirm = wxMessageBox(
            wxString::Format(
                "Copy tiles from Screen (%d, %d) to Screen (%d, %d)?\n\n"
                "This replaces all destination screen tiles on every layer.",
                sourceX,
                sourceY,
                targetX,
                targetY
            ),
            "Copy Screen Tiles",
            wxYES_NO | wxICON_QUESTION,
            this
        );
        if (confirm != wxYES) {
            return;
        }

        for (int layer = 0; layer < kTileLayers; ++layer) {
            target->screen.tileLayerIds[static_cast<size_t>(layer)] = source->screen.tileLayerIds[static_cast<size_t>(layer)];
        }

        MarkDirty();
        RefreshCurrentScreenViews();
        if (worldGrid_) {
            worldGrid_->Refresh();
        }
    }

    void AddItem() {
        if (sheetSpriteCollections_.empty()) {
            RefreshSheetSpriteLibraries();
        }
        if (sheetSpriteCollections_.empty()) {
            wxMessageBox("No sprite libraries were found under data/sheets.", "Add Item", wxOK | wxICON_INFORMATION, this);
            return;
        }

        ItemDefinition item;
        item.id = NextItemDefinitionId();
        item.name = item.id;
        item.animationSpeed = 0.0f;
        SetItemTriggerAmount(item, 1);

        ItemEditorDialog dlg(this, item, sheetSpriteCollections_);
        if (dlg.ShowModal() != wxID_OK) {
            return;
        }

        world_.itemDefinitions.push_back(item);
        selectedItemDefinitionId_ = item.id;
        MarkDirty();
        RefreshAll();
    }

    void OnAddItem(wxCommandEvent&) { AddItem(); }

    void OnEditItem(wxCommandEvent&) {
        ItemDefinition* item = FindItemDefinition(world_, selectedItemDefinitionId_);
        if (!item) {
            return;
        }

        ItemEditorDialog dlg(this, *item, sheetSpriteCollections_);
        if (dlg.ShowModal() != wxID_OK) {
            return;
        }

        MarkDirty();
        RefreshAll();
    }

    void OnRemoveItem(wxCommandEvent&) {
        if (selectedItemDefinitionId_.empty()) {
            return;
        }

        auto itemIt = std::find_if(world_.itemDefinitions.begin(), world_.itemDefinitions.end(), [this](const ItemDefinition& item) {
            return item.id == selectedItemDefinitionId_;
        });
        if (itemIt == world_.itemDefinitions.end()) {
            return;
        }

        RemoveItemDefinitionReferences(selectedItemDefinitionId_);
        world_.itemDefinitions.erase(itemIt);
        selectedItemDefinitionId_ = world_.itemDefinitions.empty() ? std::string() : world_.itemDefinitions.front().id;
        MarkDirty();
        RefreshAll();
    }

    void OnAddEnemy(wxCommandEvent&) {
        EnemyDefinition enemy;
        enemy.id = NextEnemyDefinitionId();
        enemy.name = enemy.id;
        enemy.hitpoints = 2;
        enemy.baseDamage = 1;
        enemy.moves.push_back(EnemyMoveDefinition{});
        enemy.moves.front().type = EnemyMoveType::StandStill;
        enemy.moves.front().minSeconds = 1.0f;
        enemy.moves.front().maxSeconds = 1.0f;
        enemy.moves.front().speedTilesPerSecond = 1.0f;
        enemy.moves.front().reappearMode = EnemyReappearMode::SamePlace;
        enemy.moves.front().hitboxes.push_back(TileHitbox{0, 0, 12, 12});

        EnemyDefinitionEditorDialog dlg(this, enemy, sheetSpriteCollections_, world_.projectileDefinitions, world_.dropTables, world_.weaponDefinitions);
        if (dlg.ShowModal() != wxID_OK) {
            return;
        }

        world_.enemyDefinitions.push_back(enemy);
        selectedEnemyDefinitionId_ = enemy.id;
        MarkDirty();
        RefreshAll();
    }

    void OnEditEnemy(wxCommandEvent&) {
        EnemyDefinition* enemy = FindEnemyDefinition(world_, selectedEnemyDefinitionId_);
        if (!enemy || enemy->isNpc) {
            return;
        }

        EnemyDefinitionEditorDialog dlg(this, *enemy, sheetSpriteCollections_, world_.projectileDefinitions, world_.dropTables, world_.weaponDefinitions);
        if (dlg.ShowModal() != wxID_OK) {
            return;
        }

        MarkDirty();
        RefreshAll();
    }

    void OnRemoveEnemy(wxCommandEvent&) {
        if (selectedEnemyDefinitionId_.empty()) {
            return;
        }

        auto enemyIt = std::find_if(world_.enemyDefinitions.begin(), world_.enemyDefinitions.end(), [this](const EnemyDefinition& enemy) {
            return enemy.id == selectedEnemyDefinitionId_;
        });
        if (enemyIt == world_.enemyDefinitions.end()) {
            return;
        }
        if (enemyIt->isNpc) {
            return;
        }

        RemoveEnemyDefinitionReferences(selectedEnemyDefinitionId_);
        world_.enemyDefinitions.erase(enemyIt);
        selectedEnemyDefinitionId_ = world_.enemyDefinitions.empty() ? std::string() : world_.enemyDefinitions.front().id;
        MarkDirty();
        RefreshAll();
    }

    void OnAddNpc(wxCommandEvent&) {
        EnemyDefinition npc;
        npc.id = NextEnemyDefinitionId();
        npc.name = npc.id;
        npc.isNpc = true;
        npc.npcText = "";
        npc.hitpoints = 1;
        npc.baseDamage = 0;
        npc.immuneToKnockback = true;
        npc.moves.push_back(EnemyMoveDefinition{});
        npc.moves.front().type = EnemyMoveType::StandStill;
        npc.moves.front().minSeconds = 1.0f;
        npc.moves.front().maxSeconds = 1.0f;
        npc.moves.front().speedTilesPerSecond = 0.0f;
        npc.moves.front().reappearMode = EnemyReappearMode::SamePlace;
        npc.moves.front().hitboxes.push_back(TileHitbox{0, 0, 12, 12});

        EnemyDefinitionEditorDialog dlg(this, npc, sheetSpriteCollections_, world_.projectileDefinitions, world_.dropTables, world_.weaponDefinitions, true);
        if (dlg.ShowModal() != wxID_OK) {
            return;
        }

        world_.enemyDefinitions.push_back(npc);
        selectedNpcDefinitionId_ = npc.id;
        selectedEnemyDefinitionId_ = npc.id;
        MarkDirty();
        RefreshAll();
    }

    void OnEditNpc(wxCommandEvent&) {
        EnemyDefinition* npc = FindEnemyDefinition(world_, selectedNpcDefinitionId_);
        if (!npc || !npc->isNpc) {
            return;
        }

        EnemyDefinitionEditorDialog dlg(this, *npc, sheetSpriteCollections_, world_.projectileDefinitions, world_.dropTables, world_.weaponDefinitions, true);
        if (dlg.ShowModal() != wxID_OK) {
            return;
        }

        MarkDirty();
        RefreshAll();
    }

    void OnRemoveNpc(wxCommandEvent&) {
        if (selectedNpcDefinitionId_.empty()) {
            return;
        }

        auto npcIt = std::find_if(world_.enemyDefinitions.begin(), world_.enemyDefinitions.end(), [this](const EnemyDefinition& enemy) {
            return enemy.id == selectedNpcDefinitionId_ && enemy.isNpc;
        });
        if (npcIt == world_.enemyDefinitions.end()) {
            return;
        }

        RemoveEnemyDefinitionReferences(selectedNpcDefinitionId_);
        world_.enemyDefinitions.erase(npcIt);
        selectedNpcDefinitionId_.clear();
        for (const EnemyDefinition& enemy : world_.enemyDefinitions) {
            if (enemy.isNpc) {
                selectedNpcDefinitionId_ = enemy.id;
                break;
            }
        }
        if (!selectedNpcDefinitionId_.empty()) {
            selectedEnemyDefinitionId_ = selectedNpcDefinitionId_;
        }

        MarkDirty();
        RefreshAll();
    }

    void OnAddProjectile(wxCommandEvent&) {
        if (sheetSpriteCollections_.empty()) {
            RefreshSheetSpriteLibraries();
        }
        if (sheetSpriteCollections_.empty()) {
            wxMessageBox("No sprite libraries were found under data/sheets.", "Add Projectile", wxOK | wxICON_INFORMATION, this);
            return;
        }

        ProjectileDefinition projectile;
        projectile.id = NextProjectileDefinitionId();
        projectile.name = projectile.id;
        projectile.movementType = ProjectileMovementType::TrackPlayer;
        projectile.speedTilesPerSecond = 1.0f;
        projectile.fixedFunctionA = 0.0f;
        projectile.moveThroughSolid = false;
        projectile.baseDamage = 1;

        ProjectileDefinitionEditorDialog dlg(this, projectile, sheetSpriteCollections_);
        if (dlg.ShowModal() != wxID_OK) {
            return;
        }

        world_.projectileDefinitions.push_back(projectile);
        selectedProjectileDefinitionId_ = projectile.id;
        MarkDirty();
        RefreshAll();
    }

    void OnEditProjectile(wxCommandEvent&) {
        ProjectileDefinition* projectile = FindProjectileDefinition(world_, selectedProjectileDefinitionId_);
        if (!projectile) {
            return;
        }

        ProjectileDefinitionEditorDialog dlg(this, *projectile, sheetSpriteCollections_);
        if (dlg.ShowModal() != wxID_OK) {
            return;
        }

        MarkDirty();
        RefreshAll();
    }

    void OnRemoveProjectile(wxCommandEvent&) {
        if (selectedProjectileDefinitionId_.empty()) {
            return;
        }

        auto projectileIt = std::find_if(world_.projectileDefinitions.begin(), world_.projectileDefinitions.end(), [this](const ProjectileDefinition& projectile) {
            return projectile.id == selectedProjectileDefinitionId_;
        });
        if (projectileIt == world_.projectileDefinitions.end()) {
            return;
        }

        world_.projectileDefinitions.erase(projectileIt);
        selectedProjectileDefinitionId_ = world_.projectileDefinitions.empty() ? std::string() : world_.projectileDefinitions.front().id;
        MarkDirty();
        RefreshAll();
    }

    void OnAddWeapon(wxCommandEvent&) {
        if (sheetSpriteCollections_.empty()) {
            RefreshSheetSpriteLibraries();
        }

        WeaponDefinition weapon;
        weapon.id = NextWeaponDefinitionId();
        weapon.name = weapon.id;
        weapon.damage = 1;

        WeaponDefinitionEditorDialog dlg(this, weapon, sheetSpriteCollections_, world_.projectileDefinitions);
        if (dlg.ShowModal() != wxID_OK) {
            return;
        }

        world_.weaponDefinitions.push_back(weapon);
        selectedWeaponDefinitionId_ = weapon.id;
        MarkDirty();
        RefreshAll();
    }

    void OnEditWeapon(wxCommandEvent&) {
        WeaponDefinition* weapon = FindWeaponDefinition(world_, selectedWeaponDefinitionId_);
        if (!weapon) {
            return;
        }

        WeaponDefinitionEditorDialog dlg(this, *weapon, sheetSpriteCollections_, world_.projectileDefinitions);
        if (dlg.ShowModal() != wxID_OK) {
            return;
        }

        MarkDirty();
        RefreshAll();
    }

    void OnRemoveWeapon(wxCommandEvent&) {
        if (selectedWeaponDefinitionId_.empty()) {
            return;
        }

        auto weaponIt = std::find_if(world_.weaponDefinitions.begin(), world_.weaponDefinitions.end(), [this](const WeaponDefinition& weapon) {
            return weapon.id == selectedWeaponDefinitionId_;
        });
        if (weaponIt == world_.weaponDefinitions.end()) {
            return;
        }

        world_.weaponDefinitions.erase(weaponIt);
        selectedWeaponDefinitionId_ = world_.weaponDefinitions.empty() ? std::string() : world_.weaponDefinitions.front().id;
        MarkDirty();
        RefreshAll();
    }

    std::string PromptTargetMapId(const std::string& title) {
        wxArrayString choices;
        for (const MapLoadData& map : world_.maps) {
            choices.Add(wxString::FromUTF8(map.id));
        }
        wxSingleChoiceDialog dlg(this, "Choose target map", title, choices);
        if (dlg.ShowModal() != wxID_OK) {
            return "";
        }
        return dlg.GetStringSelection().ToStdString();
    }

    void OnAddTransition(wxCommandEvent&) {
        ScreenLoadData* screen = CurrentScreen();
        MapLoadData* map = CurrentMap();
        if (!screen || !map) {
            return;
        }

        wxArrayString edgeChoices;
        edgeChoices.Add("left");
        edgeChoices.Add("right");
        edgeChoices.Add("up");
        edgeChoices.Add("down");
        wxSingleChoiceDialog edgeDlg(this, "Edge that triggers this link", "Edge Link", edgeChoices);
        if (edgeDlg.ShowModal() != wxID_OK) {
            return;
        }

        const std::string targetMapId = PromptTargetMapId("Target Map");
        if (targetMapId.empty()) {
            return;
        }

        const int toX = static_cast<int>(wxGetNumberFromUser("Target screen X", "toX", "Add Edge Link", currentScreenX_, 0, 99, this));
        const int toY = static_cast<int>(wxGetNumberFromUser("Target screen Y", "toY", "Add Edge Link", currentScreenY_, 0, 99, this));
        const int spawnX = static_cast<int>(wxGetNumberFromUser("Spawn X in target screen pixels", "spawnX", "Add Edge Link", 8, 0, kScreenPixelWidth - 1, this));
        const int spawnY = static_cast<int>(wxGetNumberFromUser("Spawn Y in target screen pixels", "spawnY", "Add Edge Link", 8, 0, kScreenPixelHeight - 1, this));

        wxArrayString kindChoices;
        kindChoices.Add("fade");
        kindChoices.Add("instant");
        wxSingleChoiceDialog kindDlg(this, "Transition style", "Link Kind", kindChoices);
        if (kindDlg.ShowModal() != wxID_OK) {
            return;
        }

        ScreenTransition tr;
        tr.fromMapId = map->id;
        tr.fromScreenX = currentScreenX_;
        tr.fromScreenY = currentScreenY_;
        tr.edge = edgeDlg.GetStringSelection().ToStdString();
        tr.toMapId = targetMapId;
        tr.toScreenX = toX;
        tr.toScreenY = toY;
        tr.spawnX = spawnX;
        tr.spawnY = spawnY;
        tr.kind = kindDlg.GetStringSelection() == "instant" ? TransitionKind::Instant : TransitionKind::Fade;
        screen->transitions.push_back(tr);

        MarkDirty();
        RefreshTransitionsList();
        worldGrid_->Refresh();
    }

    void OnRemoveTransition(wxCommandEvent&) {
        ScreenLoadData* screen = CurrentScreen();
        if (!screen) {
            return;
        }
        const int index = transitionList_->GetSelection();
        if (index == wxNOT_FOUND) {
            return;
        }
        screen->transitions.erase(screen->transitions.begin() + index);
        MarkDirty();
        RefreshTransitionsList();
        worldGrid_->Refresh();
    }

    void OnAddWarp(wxCommandEvent&) {
        WarpDefinition warp;
        warp.id = NextWarpDefinitionId();
        warp.name = warp.id;
        warp.endpoints[0].tileId = selectedTileId_;
        warp.endpoints[1].tileId = selectedTileId_;
        warp.endpoints[0].hitboxes = {TileHitbox{0, 0, 16, 16}};
        warp.endpoints[1].hitboxes = {TileHitbox{0, 0, 16, 16}};

        WarpDefinitionEditorDialog editor(this, warp, world_, selectedTileId_);
        if (editor.ShowModal() != wxID_OK) {
            return;
        }

        world_.warpDefinitions.push_back(warp);
        SelectWarpDefinition(warp.id);
        SetSelectedWarpEndpointIndex(0);

        MarkDirty();
        RefreshWarpList();
        worldGrid_->Refresh();
        canvas_->Refresh();
    }

    void OnEditWarp(wxCommandEvent&) {
        const std::string warpId = warpPalette_ ? warpPalette_->SelectedWarpId() : std::string();
        WarpDefinition* warp = warpId.empty() ? nullptr : FindWarpDefinition(world_, warpId);
        if (!warp) {
            return;
        }

        WarpDefinitionEditorDialog editor(this, *warp, world_, selectedTileId_);
        if (editor.ShowModal() != wxID_OK) {
            return;
        }

        MarkDirty();
        RefreshWarpList();
        canvas_->Refresh();
    }

    void OnRemoveWarp(wxCommandEvent&) {
        const std::string warpId = warpPalette_ ? warpPalette_->SelectedWarpId() : std::string();
        if (warpId.empty()) {
            return;
        }

        RemoveWarpDefinitionReferences(warpId);
        auto it = std::find_if(world_.warpDefinitions.begin(), world_.warpDefinitions.end(),
            [&warpId](const WarpDefinition& w) { return w.id == warpId; });
        if (it != world_.warpDefinitions.end()) {
            world_.warpDefinitions.erase(it);
        }
        selectedWarpDefinitionId_ = world_.warpDefinitions.empty() ? std::string() : world_.warpDefinitions.front().id;

        MarkDirty();
        RefreshWarpList();
        worldGrid_->Refresh();
        canvas_->Refresh();
    }

    void OnAddPowerupDef(wxCommandEvent&) {
        wxTextEntryDialog idDlg(this, "Unique ID", "Add Powerup", "new_powerup");
        if (idDlg.ShowModal() != wxID_OK) {
            return;
        }

        wxTextEntryDialog effectDlg(this, "Effect name (speed/heal/max_health)", "Add Powerup", "speed");
        if (effectDlg.ShowModal() != wxID_OK) {
            return;
        }

        const int magnitude = static_cast<int>(wxGetNumberFromUser("Magnitude", "value", "Add Powerup", 1, 0, 200, this));
        const int durationMs = static_cast<int>(wxGetNumberFromUser("Duration (milliseconds)", "ms", "Add Powerup", 8000, 0, 120000, this));

        PowerupDef def;
        def.id = idDlg.GetValue().ToStdString();
        def.name = def.id;
        def.effect = effectDlg.GetValue().ToStdString();
        def.magnitude = magnitude;
        def.durationSeconds = static_cast<float>(durationMs) / 1000.0f;
        world_.powerups.push_back(def);

        MarkDirty();
        RefreshPowerupList();
    }

    void OnEditPowerupDef(wxCommandEvent&) {
        const int index = powerupList_->GetSelection();
        if (index == wxNOT_FOUND) {
            return;
        }

        PowerupDef& def = world_.powerups[static_cast<size_t>(index)];
        wxTextEntryDialog effectDlg(this, "Effect", "Edit Powerup", wxString::FromUTF8(def.effect));
        if (effectDlg.ShowModal() != wxID_OK) {
            return;
        }

        const int magnitude = static_cast<int>(wxGetNumberFromUser("Magnitude", "value", "Edit Powerup", def.magnitude, 0, 200, this));
        const int durationMs = static_cast<int>(wxGetNumberFromUser("Duration (milliseconds)", "ms", "Edit Powerup", static_cast<long>(def.durationSeconds * 1000.0f), 0, 120000, this));

        def.effect = effectDlg.GetValue().ToStdString();
        def.magnitude = magnitude;
        def.durationSeconds = static_cast<float>(durationMs) / 1000.0f;
        MarkDirty();
        RefreshPowerupList();
    }

    void OnRemovePowerupDef(wxCommandEvent&) {
        const int index = powerupList_->GetSelection();
        if (index == wxNOT_FOUND) {
            return;
        }
        world_.powerups.erase(world_.powerups.begin() + index);
        MarkDirty();
        RefreshPowerupList();
    }

    void OnAddDropTableDef(wxCommandEvent&) {
        wxTextEntryDialog idDlg(this, "Unique ID", "Add Drop Table", "drop_table_1");
        if (idDlg.ShowModal() != wxID_OK) {
            return;
        }
        const std::string id = idDlg.GetValue().ToStdString();
        if (id.empty()) {
            return;
        }
        auto existing = std::find_if(world_.dropTables.begin(), world_.dropTables.end(), [&id](const EnemyDropTable& t) {
            return t.id == id;
        });
        if (existing != world_.dropTables.end()) {
            wxMessageBox("Drop table ID already exists.", "Drop Tables", wxOK | wxICON_WARNING, this);
            return;
        }

        EnemyDropTable table;
        table.id = id;
        table.name = id;
        DropTableEditorDialog dlg(this, table, world_.itemDefinitions);
        if (dlg.ShowModal() != wxID_OK) {
            return;
        }

        world_.dropTables.push_back(table);
        MarkDirty();
        RefreshDropTableList();
    }

    void OnEditDropTableDef(wxCommandEvent&) {
        if (!dropTablePalette_) {
            return;
        }
        const std::string tableId = dropTablePalette_->SelectedTableId();
        auto it = std::find_if(world_.dropTables.begin(), world_.dropTables.end(),
            [&tableId](const EnemyDropTable& t) { return t.id == tableId; });
        if (it == world_.dropTables.end()) {
            return;
        }

        DropTableEditorDialog dlg(this, *it, world_.itemDefinitions);
        if (dlg.ShowModal() != wxID_OK) {
            return;
        }

        MarkDirty();
        RefreshDropTableList();
        dropTablePalette_->SetSelectedTableId(tableId);
    }

    void OnRemoveDropTableDef(wxCommandEvent&) {
        if (!dropTablePalette_) {
            return;
        }
        const std::string tableId = dropTablePalette_->SelectedTableId();
        auto it = std::find_if(world_.dropTables.begin(), world_.dropTables.end(),
            [&tableId](const EnemyDropTable& t) { return t.id == tableId; });
        if (it == world_.dropTables.end()) {
            return;
        }

        world_.dropTables.erase(it);
        for (EnemyDefinition& enemy : world_.enemyDefinitions) {
            if (enemy.dropTableId == tableId) {
                enemy.dropTableId.clear();
            }
        }

        MarkDirty();
        RefreshDropTableList();
    }

    void OnAddCharacter(wxCommandEvent&) {
        wxTextEntryDialog idDlg(this, "Unique ID", "Add Character", "player_1");
        if (idDlg.ShowModal() != wxID_OK) {
            return;
        }

        wxTextEntryDialog nameDlg(this, "Character Name", "Add Character", "Player 1");
        if (nameDlg.ShowModal() != wxID_OK) {
            return;
        }

        wxTextEntryDialog descDlg(this, "Description", "Add Character", "");
        descDlg.ShowModal();

        wxFileDialog fileDlg(this, "Select spritesheet image", "", "", "PNG files (*.png)|*.png", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        if (fileDlg.ShowModal() != wxID_OK) {
            return;
        }

        CharacterSpriteset spriteset = BuildDefaultPlayerSpriteset();
        spriteset.id = idDlg.GetValue().ToStdString();
        spriteset.name = nameDlg.GetValue().ToStdString();
        spriteset.description = descDlg.GetValue().ToStdString();
        spriteset.imagePath = fileDlg.GetPath().ToStdString();

        CharacterSpritesetEditorDialog editor(this, spriteset, world_.weaponDefinitions);
        if (editor.ShowModal() != wxID_OK) {
            return;
        }

        world_.characterSpritesets.push_back(spriteset);
        world_.activeCharacterSpritesetId = spriteset.id;
        selectedCharacterSpritesetId_ = spriteset.id;
        MarkDirty();
        RefreshCharactersUi();
    }

    void OnEditCharacter(wxCommandEvent&) {
        const int index = SelectedCharacterSpritesetIndex();
        if (index == wxNOT_FOUND) {
            return;
        }

        CharacterSpriteset& spriteset = world_.characterSpritesets[static_cast<size_t>(index)];
        CharacterSpritesetEditorDialog editor(this, spriteset, world_.weaponDefinitions);
        if (editor.ShowModal() != wxID_OK) {
            return;
        }
        MarkDirty();
        RefreshCharactersUi();
    }

    void OnRemoveCharacter(wxCommandEvent&) {
        const int index = SelectedCharacterSpritesetIndex();
        if (index == wxNOT_FOUND) {
            return;
        }
        selectedCharacterSpritesetId_ = world_.characterSpritesets[static_cast<size_t>(index)].id;
        world_.characterSpritesets.erase(world_.characterSpritesets.begin() + index);
        if (world_.characterSpritesets.empty()) {
            world_.activeCharacterSpritesetId.clear();
            selectedCharacterSpritesetId_.clear();
        } else if (std::none_of(world_.characterSpritesets.begin(), world_.characterSpritesets.end(), [this](const CharacterSpriteset& s) {
                       return s.id == selectedCharacterSpritesetId_;
                   })) {
            selectedCharacterSpritesetId_ = world_.characterSpritesets.front().id;
        }
        if (!world_.characterSpritesets.empty() && std::none_of(world_.characterSpritesets.begin(), world_.characterSpritesets.end(), [this](const CharacterSpriteset& s) {
                       return s.id == world_.activeCharacterSpritesetId;
                   })) {
            world_.activeCharacterSpritesetId = world_.characterSpritesets.front().id;
        }
        MarkDirty();
        RefreshCharactersUi();
    }

    void OnActiveCharacterChanged(wxCommandEvent&) {
        const int selection = activeCharacterChoice_->GetSelection();
        if (selection == wxNOT_FOUND || selection >= static_cast<int>(world_.characterSpritesets.size())) {
            return;
        }
        world_.activeCharacterSpritesetId = world_.characterSpritesets[static_cast<size_t>(selection)].id;
        selectedCharacterSpritesetId_ = world_.activeCharacterSpritesetId;
        MarkDirty();
        RefreshCharactersUi();
    }

    void SetPaintLayerIndex(int layer) {
        paintLayerIndex_ = std::clamp(layer, 0, kTileLayers - 1);
        if (paintLayerChoice_) {
            paintLayerChoice_->SetSelection(paintLayerIndex_);
        }
        if (canvas_) {
            canvas_->SetPaintLayer(paintLayerIndex_);
        }
        if (activeLayerLabel_) {
            activeLayerLabel_->SetLabel(wxString::Format("Paint Layer L%d", paintLayerIndex_ + 1));
        }
    }

    void SelectTileFromCanvas(int tileId) {
        if (tileId < 0) {
            return;
        }

        const TileCollection* owningCollection = FindTileCollectionByTileId(world_, tileId);
        if (owningCollection && world_.activeTileCollectionId != owningCollection->id) {
            world_.activeTileCollectionId = owningCollection->id;
            if (tileCollectionChoice_) {
                for (size_t i = 0; i < world_.tileCollections.size(); ++i) {
                    if (world_.tileCollections[i].id == owningCollection->id) {
                        tileCollectionChoice_->SetSelection(static_cast<int>(i));
                        break;
                    }
                }
            }
        }

        selectedTileId_ = tileId;
        if (notebook_) {
            notebook_->SetSelection(0);
        }

        RefreshTileCollectionsUi();
        tilePalette_->SetSelectedTileId(selectedTileId_, true);
        canvas_->SetTileSelection(selectedTileId_);
        SyncCanvasInteractionModeFromUi();
        RefreshSelectedTileControls();
    }

private:
    WorldLoadData world_;
    std::string currentPath_;
    bool dirty_ = false;
    int currentMapIndex_ = 0;
    int currentScreenX_ = 0;
    int currentScreenY_ = 0;

    wxSplitterWindow* splitterOuter_ = nullptr;
    wxSplitterWindow* centerRightSplit_ = nullptr;

    wxListBox* mapList_ = nullptr;
    wxTextCtrl* mapIdCtrl_ = nullptr;
    wxTextCtrl* mapNameCtrl_ = nullptr;
    wxSpinCtrl* mapWidthCtrl_ = nullptr;
    wxSpinCtrl* mapHeightCtrl_ = nullptr;
    wxSpinCtrl* mapStartXCtrl_ = nullptr;
    wxSpinCtrl* mapStartYCtrl_ = nullptr;
    wxChoice* globalStartMapChoice_ = nullptr;
    wxSpinCtrl* globalStartXCtrl_ = nullptr;
    wxSpinCtrl* globalStartYCtrl_ = nullptr;
    WorldGridCanvas* worldGrid_ = nullptr;

    wxStaticText* currentScreenLabel_ = nullptr;
    wxCheckBox* displayTextCheck_ = nullptr;
    wxCheckBox* hideFromMapCheck_ = nullptr;
    wxTextCtrl* displayTextCtrl_ = nullptr;
    wxStaticText* activeLayerLabel_ = nullptr;
    wxChoice* tileCollectionChoice_ = nullptr;
    wxTextCtrl* tileCollectionDescriptionCtrl_ = nullptr;
    wxSpinCtrl* tilePaletteColumnsCtrl_ = nullptr;
    wxChoice* paintLayerChoice_ = nullptr;
    wxChoice* canvasModeChoice_ = nullptr;
    wxCheckBox* tileSolidCheck_ = nullptr;
    wxCheckBox* layerVisibleCheck0_ = nullptr;
    wxCheckBox* layerVisibleCheck1_ = nullptr;
    wxCheckBox* layerVisibleCheck2_ = nullptr;
    wxCheckBox* hitboxOverlayCheck_ = nullptr;
    TilePalettePanel* tilePalette_ = nullptr;
    TileCanvas* canvas_ = nullptr;
    int selectedTileId_ = 0;
    int paintLayerIndex_ = 0;
    std::array<bool, kTileLayers> layerVisible_{true, true, true};
    bool showHitboxOverlay_ = false;
    bool updatingScreenTextUi_ = false;
    bool updatingTilePaletteColumnsUi_ = false;
    std::unordered_map<int, std::string> tileOwnerHints_;
    std::vector<SheetSpriteCollectionDef> sheetSpriteCollections_;
    std::string selectedItemDefinitionId_;
    std::string selectedEnemyDefinitionId_;
    std::string selectedNpcDefinitionId_;
    std::string selectedProjectileDefinitionId_;
    std::string selectedWeaponDefinitionId_;
    std::string selectedCharacterSpritesetId_;
    std::string selectedWarpDefinitionId_;
    int selectedWarpEndpointIndex_ = 0;

    wxNotebook* notebook_ = nullptr;
    ItemPalettePanel* itemPalette_ = nullptr;
    EnemyPalettePanel* enemyPalette_ = nullptr;
    NpcPalettePanel* npcPalette_ = nullptr;
    ProjectilePalettePanel* projectilePalette_ = nullptr;
    WeaponPalettePanel* weaponPalette_ = nullptr;
    CharacterPalettePanel* characterPalette_ = nullptr;
    WarpPalettePanel* warpPalette_ = nullptr;
    wxListBox* transitionList_ = nullptr;
    wxListBox* powerupList_ = nullptr;
    DropTablePalettePanel* dropTablePalette_ = nullptr;
    wxSpinCtrlDouble* globalKnockbackDistanceCtrl_ = nullptr;
    wxSpinCtrlDouble* globalInvulnerabilityCtrl_ = nullptr;
    wxSpinCtrlDouble* globalTextSpeedCtrl_ = nullptr;
    wxSpinCtrlDouble* globalDropItemLifetimeCtrl_ = nullptr;
    wxSpinCtrlDouble* globalItemPickupDurationCtrl_ = nullptr;
    wxTextCtrl* textGlyphMapCtrl_ = nullptr;
    bool updatingTextGlyphMapUi_ = false;
    wxChoice* activeCharacterChoice_ = nullptr;
};

class EditorApp final : public wxApp {
public:
    bool OnInit() override {
        wxInitAllImageHandlers();
        auto* frame = new EditorFrame();
        frame->Show(true);
        return true;
    }
};

wxIMPLEMENT_APP(EditorApp);
