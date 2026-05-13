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
#include <filesystem>
#include <fstream>
#include <functional>
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

    for (int ty = 0; ty < kTilesHigh; ++ty) {
        for (int tx = 0; tx < kTilesWide; ++tx) {
            const bool border = tx == 0 || tx == kTilesWide - 1 || ty == 0 || ty == kTilesHigh - 1;
            out.screen.tileLayerIds[0][static_cast<size_t>(ty * kTilesWide + tx)] = border ? 1 : 0;
        }
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

    CharacterAction slash;
    slash.id = "sword_slash";
    slash.name = "Sword Slash";
    slash.animationSpeed = 10.0f;
    slash.directionalFrames[0].push_back(CharacterFrame{0, 8, 2, 2});
    slash.directionalFrames[1].push_back(CharacterFrame{2, 8, 2, 2});
    slash.directionalFrames[2].push_back(CharacterFrame{4, 8, 2, 2});
    slash.directionalFrames[3].push_back(CharacterFrame{6, 8, 2, 2});

    spriteset.actions.push_back(standing);
    spriteset.actions.push_back(walking);
    spriteset.actions.push_back(slash);
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

TileDef* FindTileDef(TileCollection& collection, int id) {
    for (TileDef& tile : collection.tiles) {
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

TileCollection BuildForestCollectionFromImage(const std::string& imagePath) {
    TileCollection collection;
    collection.id = "forest_1";
    collection.name = "Forest 1";
    collection.description = "Imported from Overworld.png";
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

wxString TransitionKindLabel(TransitionKind kind) {
    return kind == TransitionKind::Instant ? "instant" : "fade";
}

}  // namespace

enum class CanvasMode { PaintTile, DrawWarp, EdgeLink };

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
        warpDragging_ = false;
        Refresh();
    }

    void SetTileSelection(int tile) {
        selectedTile_ = tile;
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
        warpDragging_ = false;
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

    void SetWarpDrawCallback(std::function<void(SDL_FRect)> cb) {
        onWarpDraw_ = std::move(cb);
    }

    void SetEdgeClickCallback(std::function<void(const std::string&)> cb) {
        onEdgeClick_ = std::move(cb);
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
        } else if (mode_ == CanvasMode::DrawWarp) {
            if (screen_ && IsInCanvas(pt)) {
                warpDragging_ = true;
                warpDragStart_ = pt;
                warpDragCurrent_ = pt;
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
        } else if (mode_ == CanvasMode::DrawWarp) {
            if (warpDragging_) {
                warpDragging_ = false;
                const SDL_FRect rect = ComputeWarpRect(warpDragStart_, event.GetPosition());
                if (rect.w > 2.0f && rect.h > 2.0f && onWarpDraw_) {
                    onWarpDraw_(rect);
                }
                Refresh();
            }
        }
    }

    void OnMouseMove(wxMouseEvent& event) {
        if (mode_ == CanvasMode::PaintTile) {
            if (dragging_ && event.LeftIsDown()) {
                PaintAt(event.GetPosition());
            }
        } else if (mode_ == CanvasMode::DrawWarp) {
            if (warpDragging_ && event.LeftIsDown()) {
                warpDragCurrent_ = event.GetPosition();
                Refresh();
            }
        }
    }

    void OnRightMouseDown(wxMouseEvent& event) {
        TryPickTileAtPoint(event.GetPosition());
    }

    void OnRightMouseUp(wxMouseEvent& event) {
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

    SDL_FRect ComputeWarpRect(const wxPoint& p1, const wxPoint& p2) const {
        const wxSize size = GetClientSize();
        const int scale = std::max(1, std::min(size.GetWidth() / kTilesWide, size.GetHeight() / kTilesHigh));
        const int ox = (size.GetWidth() - kTilesWide * scale) / 2;
        const int oy = (size.GetHeight() - kTilesHigh * scale) / 2;
        const float gx1 = std::max(0.0f, std::min(static_cast<float>(kScreenPixelWidth),  static_cast<float>(p1.x - ox) * kTileSize / scale));
        const float gy1 = std::max(0.0f, std::min(static_cast<float>(kScreenPixelHeight), static_cast<float>(p1.y - oy) * kTileSize / scale));
        const float gx2 = std::max(0.0f, std::min(static_cast<float>(kScreenPixelWidth),  static_cast<float>(p2.x - ox) * kTileSize / scale));
        const float gy2 = std::max(0.0f, std::min(static_cast<float>(kScreenPixelHeight), static_cast<float>(p2.y - oy) * kTileSize / scale));
        const float minX = gx1 < gx2 ? gx1 : gx2;
        const float minY = gy1 < gy2 ? gy1 : gy2;
        const float maxX = gx1 > gx2 ? gx1 : gx2;
        const float maxY = gy1 > gy2 ? gy1 : gy2;
        return SDL_FRect{minX, minY, maxX - minX, maxY - minY};
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

        // Warp trigger overlays
        if (screen_ && !screen_->warps.empty()) {
            dc.SetFont(wxFont(7, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Segoe UI"));
            for (const WarpPoint& warp : screen_->warps) {
                const int wx = ox + static_cast<int>(warp.trigger.x * scale / kTileSize);
                const int wy = oy + static_cast<int>(warp.trigger.y * scale / kTileSize);
                const int ww = std::max(4, static_cast<int>(warp.trigger.w * scale / kTileSize));
                const int wh = std::max(4, static_cast<int>(warp.trigger.h * scale / kTileSize));
                dc.SetPen(wxPen(wxColour(255, 200, 40), 2));
                dc.SetBrush(*wxTRANSPARENT_BRUSH);
                dc.DrawRectangle(wx, wy, ww, wh);
                dc.SetTextForeground(wxColour(255, 230, 80));
                dc.DrawText(wxString::FromUTF8(warp.label), wx + 2, wy + 1);
            }
        }

        // Warp drag preview
        if (warpDragging_) {
            dc.SetPen(wxPen(wxColour(255, 240, 100), 2, wxPENSTYLE_SHORT_DASH));
            dc.SetBrush(*wxTRANSPARENT_BRUSH);
            const int x1 = std::min(warpDragStart_.x, warpDragCurrent_.x);
            const int y1 = std::min(warpDragStart_.y, warpDragCurrent_.y);
            const int x2 = std::max(warpDragStart_.x, warpDragCurrent_.x);
            const int y2 = std::max(warpDragStart_.y, warpDragCurrent_.y);
            dc.DrawRectangle(x1, y1, x2 - x1, y2 - y1);
        }

        // Edge link mode hover hint
        if (mode_ == CanvasMode::EdgeLink) {
            dc.SetPen(wxPen(wxColour(80, 210, 120, 160), 2, wxPENSTYLE_DOT));
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
    const std::unordered_map<int, std::string>* tileOwnerHints_ = nullptr;
    std::unordered_map<std::string, wxBitmap> collectionAtlasCache_;
    int selectedTile_ = 0;
    int paintLayer_ = 0;
    std::array<bool, kTileLayers> layerVisible_{true, true, true};
    bool showHitboxOverlay_ = false;
    bool dragging_ = false;
    CanvasMode mode_ = CanvasMode::PaintTile;
    bool warpDragging_ = false;
    wxPoint warpDragStart_;
    wxPoint warpDragCurrent_;
    std::function<void()> onDirty_;
    std::function<void(SDL_FRect)> onWarpDraw_;
    std::function<void(const std::string&)> onEdgeClick_;
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

                if (screen && !screen->warps.empty()) {
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
    TileHitboxEditor(wxWindow* parent, TileDef& tile, const wxBitmap& atlas, const TileCollection& collection)
        : wxDialog(parent, wxID_ANY, "Edit Tile Hitbox", wxDefaultPosition, wxSize(520, 560),
                   wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
          tile_(tile),
          atlas_(atlas),
          collection_(collection) {
        if (!tile_.hitboxes.empty()) {
            hitboxes_ = tile_.hitboxes;
        } else {
            hitboxes_.push_back(TileHitbox{tile_.hitboxX, tile_.hitboxY, tile_.hitboxW, tile_.hitboxH});
        }
        if (hitboxes_.empty()) {
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

        canvasPanel_ = new wxPanel(contentParent, wxID_ANY, wxDefaultPosition, wxSize(120, 120));
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

        mainSizer->Add(canvasPanel_, 0, wxALL | wxEXPAND, 10);

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
        spinX_ = new wxSpinCtrl(contentParent, wxID_ANY, "0", wxDefaultPosition, wxSize(64, -1), 0, 0, tileW);
        gridSizer->Add(spinX_, 0);

        gridSizer->Add(new wxStaticText(contentParent, wxID_ANY, "Y:"), 0, wxALIGN_CENTER_VERTICAL);
        spinY_ = new wxSpinCtrl(contentParent, wxID_ANY, "0", wxDefaultPosition, wxSize(64, -1), 0, 0, tileH);
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
        SelectHitbox(0);

        addBtn->Bind(wxEVT_BUTTON, [this, tileW, tileH](wxCommandEvent&) {
            hitboxes_.push_back(TileHitbox{0, 0, tileW, tileH});
            RebuildHitboxList();
            SelectHitbox(static_cast<int>(hitboxes_.size()) - 1);
            RefreshCanvas();
        });

        removeBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            if (hitboxes_.size() <= 1 || selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(hitboxes_.size())) {
                return;
            }
            hitboxes_.erase(hitboxes_.begin() + selectedIndex_);
            RebuildHitboxList();
            SelectHitbox(std::min(selectedIndex_, static_cast<int>(hitboxes_.size()) - 1));
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
        if (tile_.hitboxes.empty()) {
            tile_.hitboxes.push_back(TileHitbox{});
        }
        tile_.hitboxX = tile_.hitboxes.front().x;
        tile_.hitboxY = tile_.hitboxes.front().y;
        tile_.hitboxW = tile_.hitboxes.front().w;
        tile_.hitboxH = tile_.hitboxes.front().h;
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
            return;
        }
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
        const int tileScale = 3;
        const int baseX = 8;
        const int baseY = 8;
        const int tileDisplayW = std::max(1, collection_.tileWidth) * tileScale;
        const int tileDisplayH = std::max(1, collection_.tileHeight) * tileScale;
        if (pt.x < baseX || pt.y < baseY || pt.x >= baseX + tileDisplayW || pt.y >= baseY + tileDisplayH) {
            return -1;
        }
        const int x = (pt.x - baseX) / tileScale;
        const int y = (pt.y - baseY) / tileScale;
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

        // Draw preview magnified 3x while preserving original frame aspect ratio.
        const int tileScale = 3;
        const int baseX = 8;
        const int baseY = 8;
        const int tileDisplayW = std::max(1, collection_.tileWidth) * tileScale;
        const int tileDisplayH = std::max(1, collection_.tileHeight) * tileScale;

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
            const int hx = baseX + (hitbox.x * tileScale);
            const int hy = baseY + (hitbox.y * tileScale);
            const int hw = hitbox.w * tileScale;
            const int hh = hitbox.h * tileScale;
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

        const int baseX = 8;
        const int baseY = 8;
        const int tileScale = 3;
        const int tileW = std::max(1, collection_.tileWidth);
        const int tileH = std::max(1, collection_.tileHeight);

        int newX = (event.GetX() - baseX) / tileScale;
        int newY = (event.GetY() - baseY) / tileScale;
        int oldX = (dragStartX_ - baseX) / tileScale;
        int oldY = (dragStartY_ - baseY) / tileScale;

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
        : wxScrolledWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_SIMPLE | wxVSCROLL) {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetScrollRate(0, 12);
        SetMinSize(wxSize(620, 220));
        Bind(wxEVT_PAINT, &TilePalettePanel::OnPaint, this);
        Bind(wxEVT_LEFT_DOWN, &TilePalettePanel::OnLeftDown, this);
        Bind(wxEVT_LEFT_DCLICK, &TilePalettePanel::OnLeftDClick, this);
        Bind(wxEVT_RIGHT_DOWN, &TilePalettePanel::OnRightDown, this);
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

private:
    int GetPaletteColumns() const {
        // Calculate how many columns fit in the available width
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
    std::function<void(int)> onSelect_;
    std::function<void(TileDef&)> onEditTile_;
    std::function<void(int, const std::string&)> onMoveTile_;
    std::vector<std::pair<std::string, std::string>> moveTargets_;
};

class CharacterSpritesetEditorDialog final : public wxDialog {
public:
    CharacterSpritesetEditorDialog(wxWindow* parent, CharacterSpriteset& spriteset)
        : wxDialog(parent, wxID_ANY, "Edit Character Spriteset", wxDefaultPosition, wxSize(980, 720), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
          spriteset_(spriteset) {
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
        actionHitboxBtn_ = new wxButton(this, wxID_ANY, "Edit Hitboxes...");
        actionRow->Add(actionHitboxBtn_, 0, wxLEFT, 10);
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
        ensureAction("sword_slash", "Sword Slash", 10.0f);
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

    void RefreshActionControls() {
        CharacterAction* action = CurrentAction();
        if (!action || !speedCtrl_) {
            return;
        }
        speedCtrl_->SetValue(action->animationSpeed);
        if (actionHitboxBtn_) {
            actionHitboxBtn_->SetLabel(wxString::Format("Edit Hitboxes... (%d)", static_cast<int>(action->hitboxes.size())));
        }
    }

    void EditCurrentActionHitboxes() {
        CharacterAction* action = CurrentAction();
        if (!action) {
            return;
        }

        CharacterFrame previewFrame{};
        bool havePreviewFrame = false;
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
    wxBitmap atlas_;
    wxTextCtrl* nameCtrl_ = nullptr;
    wxTextCtrl* descCtrl_ = nullptr;
    wxChoice* actionChoice_ = nullptr;
    wxChoice* directionChoice_ = nullptr;
    wxSpinCtrlDouble* speedCtrl_ = nullptr;
    wxButton* actionHitboxBtn_ = nullptr;
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
        IdAddCoin = 2001,
        IdAddWheat,
        IdAddPowerupItem,
        IdRemoveItem,
        IdAddEnemy = 2101,
        IdRemoveEnemy,
        IdAddTransition = 2201,
        IdRemoveTransition,
        IdAddWarp = 2251,
        IdRemoveWarp,
        IdAddPowerupDef = 2301,
        IdEditPowerupDef,
        IdRemovePowerupDef,
        IdAddCharacter = 2401,
        IdEditCharacter,
        IdRemoveCharacter,
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
        centerPanel->SetSizer(centerSizer);

        auto* rightSizer = new wxBoxSizer(wxVERTICAL);
        notebook_ = new wxNotebook(rightPanel, wxID_ANY);
        wxPanel* tilesPage = new wxPanel(notebook_);
        wxPanel* itemsPage = new wxPanel(notebook_);
        wxPanel* enemiesPage = new wxPanel(notebook_);
        wxPanel* transitionsPage = new wxPanel(notebook_);
        wxPanel* warpsPage = new wxPanel(notebook_);
        wxPanel* powerupsPage = new wxPanel(notebook_);
        wxPanel* charactersPage = new wxPanel(notebook_);
        notebook_->AddPage(tilesPage, "Tiles");
        notebook_->AddPage(itemsPage, "Items");
        notebook_->AddPage(enemiesPage, "Enemies");
        notebook_->AddPage(transitionsPage, "Edge Links");
        notebook_->AddPage(warpsPage, "Warps");
        notebook_->AddPage(powerupsPage, "Powerups");
        notebook_->AddPage(charactersPage, "Characters");
        rightSizer->Add(notebook_, 1, wxEXPAND | wxALL, 8);
        rightPanel->SetSizer(rightSizer);

        auto* itemSizer = new wxBoxSizer(wxVERTICAL);
        itemList_ = new wxListBox(itemsPage, wxID_ANY);
        itemSizer->Add(itemList_, 1, wxEXPAND | wxALL, 8);
        auto* itemButtons = new wxBoxSizer(wxHORIZONTAL);
        itemButtons->Add(new wxButton(itemsPage, IdAddCoin, "Add Coin"), 1, wxRIGHT, 4);
        itemButtons->Add(new wxButton(itemsPage, IdAddWheat, "Add Wheat"), 1, wxRIGHT, 4);
        itemButtons->Add(new wxButton(itemsPage, IdAddPowerupItem, "Add Powerup"), 1, wxRIGHT, 4);
        itemButtons->Add(new wxButton(itemsPage, IdRemoveItem, "Remove"), 1);
        itemSizer->Add(itemButtons, 0, wxEXPAND | wxALL, 8);
        itemsPage->SetSizer(itemSizer);

        auto* enemySizer = new wxBoxSizer(wxVERTICAL);
        enemyList_ = new wxListBox(enemiesPage, wxID_ANY);
        enemySizer->Add(enemyList_, 1, wxEXPAND | wxALL, 8);
        auto* enemyButtons = new wxBoxSizer(wxHORIZONTAL);
        enemyButtons->Add(new wxButton(enemiesPage, IdAddEnemy, "Add Enemy"), 1, wxRIGHT, 6);
        enemyButtons->Add(new wxButton(enemiesPage, IdRemoveEnemy, "Remove"), 1);
        enemySizer->Add(enemyButtons, 0, wxEXPAND | wxALL, 8);
        enemiesPage->SetSizer(enemySizer);

        auto* transitionSizer = new wxBoxSizer(wxVERTICAL);
        transitionList_ = new wxListBox(transitionsPage, wxID_ANY);
        transitionSizer->Add(transitionList_, 1, wxEXPAND | wxALL, 8);
        auto* transitionButtons = new wxBoxSizer(wxHORIZONTAL);
        transitionButtons->Add(new wxButton(transitionsPage, IdAddTransition, "Add Link"), 1, wxRIGHT, 6);
        transitionButtons->Add(new wxButton(transitionsPage, IdRemoveTransition, "Remove"), 1);
        transitionSizer->Add(transitionButtons, 0, wxEXPAND | wxALL, 8);
        transitionsPage->SetSizer(transitionSizer);

        auto* warpSizer = new wxBoxSizer(wxVERTICAL);
        warpList_ = new wxListBox(warpsPage, wxID_ANY);
        warpSizer->Add(warpList_, 1, wxEXPAND | wxALL, 8);
        auto* warpButtons = new wxBoxSizer(wxHORIZONTAL);
        warpButtons->Add(new wxButton(warpsPage, IdAddWarp, "Add Warp"), 1, wxRIGHT, 6);
        warpButtons->Add(new wxButton(warpsPage, IdRemoveWarp, "Remove"), 1);
        warpSizer->Add(warpButtons, 0, wxEXPAND | wxALL, 8);
        warpsPage->SetSizer(warpSizer);

        auto* powerupSizer = new wxBoxSizer(wxVERTICAL);
        powerupList_ = new wxListBox(powerupsPage, wxID_ANY);
        powerupSizer->Add(powerupList_, 1, wxEXPAND | wxALL, 8);
        auto* powerupButtons = new wxBoxSizer(wxHORIZONTAL);
        powerupButtons->Add(new wxButton(powerupsPage, IdAddPowerupDef, "Add"), 1, wxRIGHT, 6);
        powerupButtons->Add(new wxButton(powerupsPage, IdEditPowerupDef, "Edit"), 1, wxRIGHT, 6);
        powerupButtons->Add(new wxButton(powerupsPage, IdRemovePowerupDef, "Remove"), 1);
        powerupSizer->Add(powerupButtons, 0, wxEXPAND | wxALL, 8);
        powerupsPage->SetSizer(powerupSizer);

        auto* characterSizer = new wxBoxSizer(wxVERTICAL);
        auto* characterTop = new wxBoxSizer(wxHORIZONTAL);
        characterTop->Add(new wxStaticText(charactersPage, wxID_ANY, "Active In-Game"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
        activeCharacterChoice_ = new wxChoice(charactersPage, wxID_ANY);
        characterTop->Add(activeCharacterChoice_, 1, wxEXPAND);
        characterSizer->Add(characterTop, 0, wxEXPAND | wxALL, 8);
        characterList_ = new wxListBox(charactersPage, wxID_ANY);
        characterSizer->Add(characterList_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
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
        tilesSizer->Add(collectionRow, 0, wxEXPAND | wxALL, 8);

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
        Bind(wxEVT_CHOICE, &EditorFrame::OnPaintLayerChanged, this, paintLayerChoice_->GetId());
        Bind(wxEVT_CHECKBOX, &EditorFrame::OnSelectedTileSolidChanged, this, tileSolidCheck_->GetId());
        Bind(wxEVT_CHECKBOX, &EditorFrame::OnLayerVisibilityChanged, this, layerVisibleCheck0_->GetId());
        Bind(wxEVT_CHECKBOX, &EditorFrame::OnLayerVisibilityChanged, this, layerVisibleCheck1_->GetId());
        Bind(wxEVT_CHECKBOX, &EditorFrame::OnLayerVisibilityChanged, this, layerVisibleCheck2_->GetId());
        Bind(wxEVT_CHECKBOX, &EditorFrame::OnHitboxOverlayToggled, this, hitboxOverlayCheck_->GetId());
        Bind(wxEVT_CHOICE, &EditorFrame::OnCanvasModeChanged, this, canvasModeChoice_->GetId());
        Bind(wxEVT_TEXT, &EditorFrame::OnMapMetadataChanged, this, mapIdCtrl_->GetId());
        Bind(wxEVT_TEXT, &EditorFrame::OnMapMetadataChanged, this, mapNameCtrl_->GetId());
        Bind(wxEVT_SPINCTRL, &EditorFrame::OnMapStartChanged, this, mapStartXCtrl_->GetId());
        Bind(wxEVT_SPINCTRL, &EditorFrame::OnMapStartChanged, this, mapStartYCtrl_->GetId());
        Bind(wxEVT_SPINCTRL, &EditorFrame::OnGlobalStartChanged, this, globalStartXCtrl_->GetId());
        Bind(wxEVT_SPINCTRL, &EditorFrame::OnGlobalStartChanged, this, globalStartYCtrl_->GetId());
        Bind(wxEVT_CHOICE, &EditorFrame::OnGlobalStartMapChanged, this, globalStartMapChoice_->GetId());
        Bind(wxEVT_CHOICE, &EditorFrame::OnActiveCharacterChanged, this, activeCharacterChoice_->GetId());

        Bind(wxEVT_BUTTON, &EditorFrame::OnAddMap, this, IdAddMap);
        Bind(wxEVT_BUTTON, &EditorFrame::OnRemoveMap, this, IdRemoveMap);
        Bind(wxEVT_BUTTON, &EditorFrame::OnResizeCurrentMap, this, IdResizeMap);
        Bind(wxEVT_BUTTON, &EditorFrame::OnUseCurrentAsGlobalStart, this, IdUseCurrentAsGlobalStart);

        Bind(wxEVT_BUTTON, &EditorFrame::OnAddCoin, this, IdAddCoin);
        Bind(wxEVT_BUTTON, &EditorFrame::OnAddWheat, this, IdAddWheat);
        Bind(wxEVT_BUTTON, &EditorFrame::OnAddPowerupItem, this, IdAddPowerupItem);
        Bind(wxEVT_BUTTON, &EditorFrame::OnRemoveItem, this, IdRemoveItem);
        Bind(wxEVT_BUTTON, &EditorFrame::OnAddEnemy, this, IdAddEnemy);
        Bind(wxEVT_BUTTON, &EditorFrame::OnRemoveEnemy, this, IdRemoveEnemy);
        Bind(wxEVT_BUTTON, &EditorFrame::OnAddTransition, this, IdAddTransition);
        Bind(wxEVT_BUTTON, &EditorFrame::OnRemoveTransition, this, IdRemoveTransition);
        Bind(wxEVT_BUTTON, &EditorFrame::OnAddWarp, this, IdAddWarp);
        Bind(wxEVT_BUTTON, &EditorFrame::OnRemoveWarp, this, IdRemoveWarp);
        Bind(wxEVT_BUTTON, &EditorFrame::OnRemoveTile, this, IdRemoveTile);
        Bind(wxEVT_BUTTON, &EditorFrame::OnMoveLayer, this, IdMoveLayer);
        Bind(wxEVT_BUTTON, &EditorFrame::OnAddPowerupDef, this, IdAddPowerupDef);
        Bind(wxEVT_BUTTON, &EditorFrame::OnEditPowerupDef, this, IdEditPowerupDef);
        Bind(wxEVT_BUTTON, &EditorFrame::OnRemovePowerupDef, this, IdRemovePowerupDef);
        Bind(wxEVT_BUTTON, &EditorFrame::OnAddCharacter, this, IdAddCharacter);
        Bind(wxEVT_BUTTON, &EditorFrame::OnEditCharacter, this, IdEditCharacter);
        Bind(wxEVT_BUTTON, &EditorFrame::OnRemoveCharacter, this, IdRemoveCharacter);

        worldGrid_->SetSelectCallback([this](int x, int y) { SelectScreen(x, y); });
        canvas_->SetDirtyCallback([this]() { MarkDirty(); });
        canvas_->SetWarpDrawCallback([this](SDL_FRect rect) { OnWarpDrawnOnCanvas(rect); });
        canvas_->SetEdgeClickCallback([this](const std::string& edge) { OnEdgeLinkClickedOnCanvas(edge); });
        canvas_->SetTilePickCallback([this](int tileId) { SelectTileFromCanvas(tileId); });
        canvas_->SetLayerChangedCallback([this](int layer) { SetPaintLayerIndex(layer); });
        tilePalette_->SetSelectionChangedCallback([this](int tileId) {
            selectedTileId_ = tileId;
            canvas_->SetTileSelection(selectedTileId_);
            RefreshSelectedTileControls();
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
        world_.formatVersion = 5;
        world_.powerups.push_back(PowerupDef{"speed_tonic", "Speed Tonic", "speed", 40, 8.0f});
        world_.maps.push_back(MakeBlankMap("overworld", "Overworld", 5, 4));
        world_.tileCollections.clear();
        world_.tileCollections.push_back(BuildForestCollectionFromImage("data/tiles/Overworld.png"));
        world_.activeTileCollectionId = world_.tileCollections.front().id;
        world_.characterSpritesets.clear();
        world_.characterSpritesets.push_back(BuildDefaultPlayerSpriteset());
        world_.activeCharacterSpritesetId = world_.characterSpritesets.front().id;
        world_.defaultMapId = "overworld";
        world_.defaultStartScreenX = 0;
        world_.defaultStartScreenY = 0;
        selectedTileId_ = 0;
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

        characterList_->Clear();
        activeCharacterChoice_->Clear();
        int activeChoice = 0;
        for (size_t i = 0; i < world_.characterSpritesets.size(); ++i) {
            const CharacterSpriteset& spriteset = world_.characterSpritesets[i];
            characterList_->Append(wxString::Format("%s - %s", spriteset.name, spriteset.description));
            activeCharacterChoice_->Append(wxString::FromUTF8(spriteset.name));
            if (spriteset.id == world_.activeCharacterSpritesetId) {
                activeChoice = static_cast<int>(i);
            }
        }
        if (!world_.characterSpritesets.empty()) {
            activeCharacterChoice_->SetSelection(activeChoice);
            if (characterList_->GetSelection() == wxNOT_FOUND) {
                characterList_->SetSelection(activeChoice);
            }
        }
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
        itemList_->Clear();
        ScreenLoadData* screen = CurrentScreen();
        if (!screen) {
            return;
        }

        for (const Item& item : screen->items) {
            wxString line = wxString::Format("%s @ (%d,%d)", ItemTypeLabel(item.type), static_cast<int>(item.bounds.x), static_cast<int>(item.bounds.y));
            if (item.type == ItemType::Powerup && !item.powerupId.empty()) {
                line += wxString::Format(" id=%s", item.powerupId);
            }
            itemList_->Append(line);
        }
    }

    void RefreshEnemiesList() {
        enemyList_->Clear();
        ScreenLoadData* screen = CurrentScreen();
        if (!screen) {
            return;
        }

        for (const Enemy& enemy : screen->enemies) {
            enemyList_->Append(wxString::Format("%s hp=%d speed=%.0f @ (%d,%d)", enemy.behavior, enemy.health, enemy.speed, static_cast<int>(enemy.bounds.x), static_cast<int>(enemy.bounds.y)));
        }
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
        warpList_->Clear();
        ScreenLoadData* screen = CurrentScreen();
        if (!screen) {
            return;
        }

        for (const WarpPoint& warp : screen->warps) {
            warpList_->Append(wxString::Format("%s [%d,%d %dx%d] -> %s (%d,%d)", warp.label, static_cast<int>(warp.trigger.x), static_cast<int>(warp.trigger.y), static_cast<int>(warp.trigger.w), static_cast<int>(warp.trigger.h), warp.targetMapId, warp.targetScreenX, warp.targetScreenY));
        }
    }

    void RefreshPowerupList() {
        powerupList_->Clear();
        for (const PowerupDef& powerup : world_.powerups) {
            powerupList_->Append(wxString::Format("%s | %s | +%d %.1fs", powerup.id, powerup.effect, powerup.magnitude, powerup.durationSeconds));
        }
    }

    void RefreshCurrentScreenViews() {
        ScreenLoadData* screen = CurrentScreen();
        canvas_->SetScreen(screen);
        currentScreenLabel_->SetLabel(wxString::Format("Map %s - Screen (%d, %d)", CurrentMap() ? CurrentMap()->id : std::string("-"), currentScreenX_, currentScreenY_));
        RefreshItemsList();
        RefreshEnemiesList();
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
        EnsureTileCollectionsInitialized();
        EnsureCharacterSpritesetsInitialized();
        for (MapLoadData& map : world_.maps) {
            EnsureMapScreens(map);
        }
        RefreshMapList();
        RefreshGlobalStartControls();
        RefreshTileCollectionsUi();
        RefreshMapProperties();
        RefreshCurrentScreenViews();
        RefreshPowerupList();
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
        RefreshSelectedTileControls();
        MarkDirty();
    }

    void OnTileCollectionDescriptionChanged(wxCommandEvent&) {
        CommitActiveCollectionDescriptionFromUi();
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
        world_.formatVersion = 5;
        world_.maps.push_back(MakeBlankMap("overworld", "Overworld", width, height));
        world_.tileCollections.clear();
        world_.tileCollections.push_back(BuildForestCollectionFromImage("data/tiles/Overworld.png"));
        world_.activeTileCollectionId = world_.tileCollections.front().id;
        world_.defaultMapId = "overworld";
        world_.defaultStartScreenX = 0;
        world_.defaultStartScreenY = 0;
        paintLayerIndex_ = 0;
        layerVisible_[0] = true;
        layerVisible_[1] = true;
        layerVisible_[2] = true;
        SetPaintLayerIndex(0);
        layerVisibleCheck0_->SetValue(true);
        layerVisibleCheck1_->SetValue(true);
        layerVisibleCheck2_->SetValue(true);
        world_.powerups.clear();
        world_.powerups.push_back(PowerupDef{"speed_tonic", "Speed Tonic", "speed", 40, 8.0f});
        currentMapIndex_ = 0;
        currentScreenX_ = 0;
        currentScreenY_ = 0;
        currentPath_.clear();
        dirty_ = true;
        RefreshAll();
        UpdateTitle();
    }

    void OnOpenWorld(wxCommandEvent&) {
        wxFileDialog dlg(this, "Open world", "", "", "World JSON (*.json)|*.json", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        if (dlg.ShowModal() != wxID_OK) {
            return;
        }

        WorldLoadData loaded;
        if (!MapLoader::LoadWorldJson(dlg.GetPath().ToStdString(), loaded)) {
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

        currentPath_ = dlg.GetPath().ToStdString();
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
        const int sel = canvasModeChoice_->GetSelection();
        CanvasMode mode = CanvasMode::PaintTile;
        if (sel == 1) mode = CanvasMode::DrawWarp;
        else if (sel == 2) mode = CanvasMode::EdgeLink;
        canvas_->SetMode(mode);
    }

    void OnWarpDrawnOnCanvas(SDL_FRect rect) {
        ScreenLoadData* screen = CurrentScreen();
        MapLoadData* map = CurrentMap();
        if (!screen || !map) {
            return;
        }

        wxTextEntryDialog labelDlg(this, "Warp label", "Add Warp", "doorway");
        if (labelDlg.ShowModal() != wxID_OK) {
            return;
        }

        const std::string targetMapId = PromptTargetMapId("Warp Target Map");
        if (targetMapId.empty()) {
            return;
        }

        const int toX = static_cast<int>(wxGetNumberFromUser("Target screen X", "toX", "Add Warp", 0, 0, 99, this));
        const int toY = static_cast<int>(wxGetNumberFromUser("Target screen Y", "toY", "Add Warp", 0, 0, 99, this));
        const int spawnX = static_cast<int>(wxGetNumberFromUser("Spawn X in target pixels", "spawnX", "Add Warp", 120, 0, kScreenPixelWidth - 1, this));
        const int spawnY = static_cast<int>(wxGetNumberFromUser("Spawn Y in target pixels", "spawnY", "Add Warp", 80, 0, kScreenPixelHeight - 1, this));

        wxArrayString kindChoices;
        kindChoices.Add("instant");
        kindChoices.Add("fade");
        wxSingleChoiceDialog kindDlg(this, "Warp style", "Warp Kind", kindChoices);
        if (kindDlg.ShowModal() != wxID_OK) {
            return;
        }

        WarpPoint warp;
        warp.label = labelDlg.GetValue().ToStdString();
        warp.fromMapId = map->id;
        warp.fromScreenX = currentScreenX_;
        warp.fromScreenY = currentScreenY_;
        warp.trigger = rect;
        warp.targetMapId = targetMapId;
        warp.targetScreenX = toX;
        warp.targetScreenY = toY;
        warp.spawnX = spawnX;
        warp.spawnY = spawnY;
        warp.kind = kindDlg.GetStringSelection() == "fade" ? TransitionKind::Fade : TransitionKind::Instant;
        screen->warps.push_back(warp);

        MarkDirty();
        RefreshWarpList();
        worldGrid_->Refresh();
        canvas_->Refresh();
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

    void AddItem(ItemType type) {
        ScreenLoadData* screen = CurrentScreen();
        MapLoadData* map = CurrentMap();
        if (!screen || !map) {
            return;
        }

        const int x = static_cast<int>(wxGetNumberFromUser("Item X in pixels", "x", "Add Item", 80, 0, kScreenPixelWidth - 1, this));
        const int y = static_cast<int>(wxGetNumberFromUser("Item Y in pixels", "y", "Add Item", 80, 0, kScreenPixelHeight - 1, this));

        Item item;
        item.mapId = map->id;
        item.screenX = currentScreenX_;
        item.screenY = currentScreenY_;
        item.bounds = SDL_FRect{static_cast<float>(x), static_cast<float>(y), 10.0f, 10.0f};
        item.type = type;

        if (type == ItemType::Powerup) {
            wxTextEntryDialog dlg(this, "Powerup ID", "Add Powerup Item", "speed_tonic");
            if (dlg.ShowModal() != wxID_OK) {
                return;
            }
            item.powerupId = dlg.GetValue().ToStdString();
        }

        screen->items.push_back(item);
        MarkDirty();
        RefreshItemsList();
    }

    void OnAddCoin(wxCommandEvent&) { AddItem(ItemType::Coin); }
    void OnAddWheat(wxCommandEvent&) { AddItem(ItemType::Wheat); }
    void OnAddPowerupItem(wxCommandEvent&) { AddItem(ItemType::Powerup); }

    void OnRemoveItem(wxCommandEvent&) {
        ScreenLoadData* screen = CurrentScreen();
        if (!screen) {
            return;
        }
        const int index = itemList_->GetSelection();
        if (index == wxNOT_FOUND) {
            return;
        }
        screen->items.erase(screen->items.begin() + index);
        MarkDirty();
        RefreshItemsList();
    }

    void OnAddEnemy(wxCommandEvent&) {
        ScreenLoadData* screen = CurrentScreen();
        MapLoadData* map = CurrentMap();
        if (!screen || !map) {
            return;
        }

        const int x = static_cast<int>(wxGetNumberFromUser("Enemy X in pixels", "x", "Add Enemy", 120, 0, kScreenPixelWidth - 1, this));
        const int y = static_cast<int>(wxGetNumberFromUser("Enemy Y in pixels", "y", "Add Enemy", 80, 0, kScreenPixelHeight - 1, this));
        const int hp = static_cast<int>(wxGetNumberFromUser("Enemy HP", "hp", "Add Enemy", 2, 1, 50, this));

        wxArrayString choices;
        choices.Add("wander");
        choices.Add("chase");
        choices.Add("static");
        choices.Add("patrol");
        wxSingleChoiceDialog behaviorDlg(this, "Behavior", "Enemy Behavior", choices);
        if (behaviorDlg.ShowModal() != wxID_OK) {
            return;
        }

        const int speed = static_cast<int>(wxGetNumberFromUser("Enemy speed", "speed", "Add Enemy", 24, 0, 150, this));

        Enemy enemy;
        enemy.mapId = map->id;
        enemy.screenX = currentScreenX_;
        enemy.screenY = currentScreenY_;
        enemy.bounds = SDL_FRect{static_cast<float>(x), static_cast<float>(y), 12.0f, 12.0f};
        enemy.health = hp;
        enemy.behavior = behaviorDlg.GetStringSelection().ToStdString();
        enemy.speed = static_cast<float>(speed);
        enemy.velocity = SDL_FPoint{enemy.speed, 0.0f};
        screen->enemies.push_back(enemy);

        MarkDirty();
        RefreshEnemiesList();
    }

    void OnRemoveEnemy(wxCommandEvent&) {
        ScreenLoadData* screen = CurrentScreen();
        if (!screen) {
            return;
        }
        const int index = enemyList_->GetSelection();
        if (index == wxNOT_FOUND) {
            return;
        }
        screen->enemies.erase(screen->enemies.begin() + index);
        MarkDirty();
        RefreshEnemiesList();
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
        ScreenLoadData* screen = CurrentScreen();
        MapLoadData* map = CurrentMap();
        if (!screen || !map) {
            return;
        }

        wxTextEntryDialog labelDlg(this, "Warp label", "Add Warp", "doorway");
        if (labelDlg.ShowModal() != wxID_OK) {
            return;
        }

        const int x = static_cast<int>(wxGetNumberFromUser("Trigger X in pixels", "x", "Add Warp", 104, 0, kScreenPixelWidth - 1, this));
        const int y = static_cast<int>(wxGetNumberFromUser("Trigger Y in pixels", "y", "Add Warp", 64, 0, kScreenPixelHeight - 1, this));
        const int w = static_cast<int>(wxGetNumberFromUser("Trigger width", "w", "Add Warp", 32, 1, kScreenPixelWidth, this));
        const int h = static_cast<int>(wxGetNumberFromUser("Trigger height", "h", "Add Warp", 32, 1, kScreenPixelHeight, this));

        const std::string targetMapId = PromptTargetMapId("Warp Target Map");
        if (targetMapId.empty()) {
            return;
        }

        const int toX = static_cast<int>(wxGetNumberFromUser("Target screen X", "toX", "Add Warp", 0, 0, 99, this));
        const int toY = static_cast<int>(wxGetNumberFromUser("Target screen Y", "toY", "Add Warp", 0, 0, 99, this));
        const int spawnX = static_cast<int>(wxGetNumberFromUser("Spawn X in target screen pixels", "spawnX", "Add Warp", 120, 0, kScreenPixelWidth - 1, this));
        const int spawnY = static_cast<int>(wxGetNumberFromUser("Spawn Y in target screen pixels", "spawnY", "Add Warp", 80, 0, kScreenPixelHeight - 1, this));

        wxArrayString kindChoices;
        kindChoices.Add("instant");
        kindChoices.Add("fade");
        wxSingleChoiceDialog kindDlg(this, "Warp style", "Warp Kind", kindChoices);
        if (kindDlg.ShowModal() != wxID_OK) {
            return;
        }

        WarpPoint warp;
        warp.label = labelDlg.GetValue().ToStdString();
        warp.fromMapId = map->id;
        warp.fromScreenX = currentScreenX_;
        warp.fromScreenY = currentScreenY_;
        warp.trigger = SDL_FRect{static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h)};
        warp.targetMapId = targetMapId;
        warp.targetScreenX = toX;
        warp.targetScreenY = toY;
        warp.spawnX = spawnX;
        warp.spawnY = spawnY;
        warp.kind = kindDlg.GetStringSelection() == "fade" ? TransitionKind::Fade : TransitionKind::Instant;
        screen->warps.push_back(warp);

        MarkDirty();
        RefreshWarpList();
        worldGrid_->Refresh();
    }

    void OnRemoveWarp(wxCommandEvent&) {
        ScreenLoadData* screen = CurrentScreen();
        if (!screen) {
            return;
        }
        const int index = warpList_->GetSelection();
        if (index == wxNOT_FOUND) {
            return;
        }
        screen->warps.erase(screen->warps.begin() + index);
        MarkDirty();
        RefreshWarpList();
        worldGrid_->Refresh();
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

        CharacterSpritesetEditorDialog editor(this, spriteset);
        if (editor.ShowModal() != wxID_OK) {
            return;
        }

        world_.characterSpritesets.push_back(spriteset);
        world_.activeCharacterSpritesetId = spriteset.id;
        MarkDirty();
        RefreshCharactersUi();
    }

    void OnEditCharacter(wxCommandEvent&) {
        const int index = characterList_->GetSelection();
        if (index == wxNOT_FOUND) {
            return;
        }

        CharacterSpriteset& spriteset = world_.characterSpritesets[static_cast<size_t>(index)];
        CharacterSpritesetEditorDialog editor(this, spriteset);
        if (editor.ShowModal() != wxID_OK) {
            return;
        }
        MarkDirty();
        RefreshCharactersUi();
    }

    void OnRemoveCharacter(wxCommandEvent&) {
        const int index = characterList_->GetSelection();
        if (index == wxNOT_FOUND) {
            return;
        }
        world_.characterSpritesets.erase(world_.characterSpritesets.begin() + index);
        if (world_.characterSpritesets.empty()) {
            world_.activeCharacterSpritesetId.clear();
        } else if (std::none_of(world_.characterSpritesets.begin(), world_.characterSpritesets.end(), [this](const CharacterSpriteset& s) {
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
    wxStaticText* activeLayerLabel_ = nullptr;
    wxChoice* tileCollectionChoice_ = nullptr;
    wxTextCtrl* tileCollectionDescriptionCtrl_ = nullptr;
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
    std::unordered_map<int, std::string> tileOwnerHints_;

    wxNotebook* notebook_ = nullptr;
    wxListBox* itemList_ = nullptr;
    wxListBox* enemyList_ = nullptr;
    wxListBox* transitionList_ = nullptr;
    wxListBox* warpList_ = nullptr;
    wxListBox* powerupList_ = nullptr;
    wxListBox* characterList_ = nullptr;
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
