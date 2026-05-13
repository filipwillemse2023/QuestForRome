#include "Game.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cmath>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

#include <png.h>

#include "Constants.hpp"

namespace {

constexpr int kHudStripHeight = 24;

bool Intersects(const SDL_FRect& a, const SDL_FRect& b) {
    return !(a.x + a.w <= b.x || b.x + b.w <= a.x || a.y + a.h <= b.y || b.y + b.h <= a.y);
}

int DirectionToRow(Direction dir) {
    switch (dir) {
        case Direction::Down:
            return 0;
        case Direction::Left:
            return 1;
        case Direction::Right:
            return 2;
        case Direction::Up:
            return 3;
    }
    return 0;
}

SDL_Color TileColorFromId(int tileId, bool classicMode) {
    if (tileId == 0) {
        return classicMode ? SDL_Color{88, 146, 72, 255} : SDL_Color{102, 148, 74, 255};
    }
    if (tileId == 1) {
        return classicMode ? SDL_Color{128, 122, 114, 255} : SDL_Color{126, 120, 108, 255};
    }
    if (tileId == 2) {
        return classicMode ? SDL_Color{64, 116, 168, 255} : SDL_Color{58, 108, 166, 255};
    }
    if (tileId == 3) {
        return classicMode ? SDL_Color{188, 170, 102, 255} : SDL_Color{194, 174, 106, 255};
    }

    const Uint8 r = static_cast<Uint8>(60 + ((tileId * 37) % 140));
    const Uint8 g = static_cast<Uint8>(60 + ((tileId * 67) % 140));
    const Uint8 b = static_cast<Uint8>(60 + ((tileId * 97) % 140));
    return SDL_Color{r, g, b, 255};
}

std::string ResolveMapPath() {
    std::vector<std::filesystem::path> candidates;

    // Prefer paths relative to the executable so launching from build folders still finds project data.
    const char* basePathRaw = SDL_GetBasePath();
    if (basePathRaw != nullptr) {
        const std::filesystem::path basePath(basePathRaw);

        candidates.push_back(basePath / "../../data/world.json");
        candidates.push_back(basePath / "../data/world.json");
        candidates.push_back(basePath / "data/world.json");
        candidates.push_back(basePath / "../../../data/world.json");
    }

    // Also support launching from repository root or other working directories.
    candidates.push_back("data/world.json");
    candidates.push_back("../data/world.json");
    candidates.push_back("../../data/world.json");
    candidates.push_back("../../../data/world.json");

    for (const std::filesystem::path& candidate : candidates) {
        if (std::filesystem::exists(candidate)) {
            return candidate.lexically_normal().string();
        }
    }

    return "data/world.json";
}

bool CanScrollBetweenScreens(
    const std::string& sourceMapId,
    int sourceX,
    int sourceY,
    const std::string& targetMapId,
    int targetX,
    int targetY,
    TransitionDirection direction
) {
    if (sourceMapId != targetMapId) {
        return false;
    }

    switch (direction) {
        case TransitionDirection::Left:
            return targetX == sourceX - 1 && targetY == sourceY;
        case TransitionDirection::Right:
            return targetX == sourceX + 1 && targetY == sourceY;
        case TransitionDirection::Up:
            return targetX == sourceX && targetY == sourceY - 1;
        case TransitionDirection::Down:
            return targetX == sourceX && targetY == sourceY + 1;
    }

    return false;
}

}  // namespace

bool Game::IsFirstVersionMode() const {
#if defined(QUEST_FIRST_VERSION_MODE)
    return true;
#else
    return false;
#endif
}

float Game::Lerp(float start, float end, float t) {
    return start + (end - start) * t;
}

std::string Game::ResolveAssetPath(const std::string& sourcePath) const {
    if (sourcePath.empty()) {
        return sourcePath;
    }

    const std::filesystem::path direct(sourcePath);
    if (std::filesystem::exists(direct)) {
        return direct.lexically_normal().string();
    }

    std::vector<std::filesystem::path> candidates;
    const char* basePathRaw = SDL_GetBasePath();
    if (basePathRaw != nullptr) {
        const std::filesystem::path basePath(basePathRaw);
        candidates.push_back(basePath / sourcePath);
        candidates.push_back(basePath / "../" / sourcePath);
        candidates.push_back(basePath / "../../" / sourcePath);
        candidates.push_back(basePath / "../../../" / sourcePath);
    }
    candidates.push_back(std::filesystem::path(sourcePath));
    candidates.push_back(std::filesystem::path("../") / sourcePath);
    candidates.push_back(std::filesystem::path("../../") / sourcePath);

    for (const std::filesystem::path& p : candidates) {
        if (std::filesystem::exists(p)) {
            return p.lexically_normal().string();
        }
    }
    return sourcePath;
}

SDL_Surface* Game::LoadPngSurface(const std::string& path) const {
    FILE* fp = nullptr;
#if defined(_WIN32)
    fopen_s(&fp, path.c_str(), "rb");
#else
    fp = std::fopen(path.c_str(), "rb");
#endif
    if (!fp) {
        SDL_Log("Failed to open PNG '%s'", path.c_str());
        return nullptr;
    }

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png) {
        std::fclose(fp);
        return nullptr;
    }

    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_read_struct(&png, nullptr, nullptr);
        std::fclose(fp);
        return nullptr;
    }

    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, nullptr);
        std::fclose(fp);
        return nullptr;
    }

    png_init_io(png, fp);
    png_read_info(png, info);

    png_uint_32 width = 0;
    png_uint_32 height = 0;
    int bitDepth = 0;
    int colorType = 0;
    png_get_IHDR(png, info, &width, &height, &bitDepth, &colorType, nullptr, nullptr, nullptr);

    if (bitDepth == 16) {
        png_set_strip_16(png);
    }
    if (colorType == PNG_COLOR_TYPE_PALETTE) {
        png_set_palette_to_rgb(png);
    }
    if (colorType == PNG_COLOR_TYPE_GRAY && bitDepth < 8) {
        png_set_expand_gray_1_2_4_to_8(png);
    }
    if (png_get_valid(png, info, PNG_INFO_tRNS)) {
        png_set_tRNS_to_alpha(png);
    }
    if (colorType == PNG_COLOR_TYPE_RGB || colorType == PNG_COLOR_TYPE_GRAY || colorType == PNG_COLOR_TYPE_PALETTE) {
        png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
    }
    if (colorType == PNG_COLOR_TYPE_GRAY || colorType == PNG_COLOR_TYPE_GRAY_ALPHA) {
        png_set_gray_to_rgb(png);
    }

    png_read_update_info(png, info);

    SDL_Surface* surface = SDL_CreateSurface(static_cast<int>(width), static_cast<int>(height), SDL_PIXELFORMAT_RGBA32);
    if (!surface) {
        png_destroy_read_struct(&png, &info, nullptr);
        std::fclose(fp);
        return nullptr;
    }

    std::vector<png_bytep> rows(height);
    for (png_uint_32 y = 0; y < height; ++y) {
        rows[y] = static_cast<png_bytep>(static_cast<unsigned char*>(surface->pixels) + (y * surface->pitch));
    }

    png_read_image(png, rows.data());
    png_read_end(png, nullptr);

    png_destroy_read_struct(&png, &info, nullptr);
    std::fclose(fp);
    return surface;
}

bool Game::Initialize() {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return false;
    }

    window_ = SDL_CreateWindow(
        "Quest for Rome - SDL3 Prototype",
        kScreenPixelWidth * kWindowScale,
        (kScreenPixelHeight + kHudStripHeight) * kWindowScale,
        SDL_WINDOW_RESIZABLE
    );
    if (!window_) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        return false;
    }

    renderer_ = SDL_CreateRenderer(window_, nullptr);
    if (!renderer_) {
        SDL_Log("SDL_CreateRenderer failed: %s", SDL_GetError());
        return false;
    }

    SDL_SetRenderLogicalPresentation(
        renderer_,
        kScreenPixelWidth,
        kScreenPixelHeight + kHudStripHeight,
        SDL_LOGICAL_PRESENTATION_INTEGER_SCALE
    );

    if (IsFirstVersionMode()) {
        // Force procedural world to match the original prototype behavior.
        world_.LoadFromJsonOrDefault("__force_procedural__");
        player_.health = 3;
    } else {
        const std::string mapPath = ResolveMapPath();
        SDL_Log("Loading world map from '%s'", mapPath.c_str());
        if (!world_.LoadFromJsonOrDefault(mapPath)) {
            SDL_Log("Map file not loaded from '%s'; using procedural fallback.", mapPath.c_str());
        }
    }

    currentMapId_ = world_.DefaultMapId();
    currentScreenX_ = world_.DefaultStartScreenX();
    currentScreenY_ = world_.DefaultStartScreenY();
    if (!world_.InBounds(currentMapId_, currentScreenX_, currentScreenY_)) {
        currentMapId_ = "overworld";
        currentScreenX_ = 0;
        currentScreenY_ = 0;
    }

    if (IsFirstVersionMode()) {
        return true;
    }

    const bool tilesOk = BuildTileTextureAtlas();
    const bool characterOk = BuildSpriteAtlas();
    return tilesOk && characterOk;
}

void Game::Run() {
    bool running = true;
    Uint64 previous = SDL_GetTicks();

    while (running) {
        const Uint64 now = SDL_GetTicks();
        float dt = static_cast<float>(now - previous) / 1000.0f;
        previous = now;
        dt = std::min(dt, 0.05f);

        HandleEvents(running);
        Update(dt);
        Draw();
    }
}

Game::~Game() {
    for (SDL_Texture* texture : ownedTileAtlases_) {
        if (texture) {
            SDL_DestroyTexture(texture);
        }
    }
    ownedTileAtlases_.clear();

    if (characterAtlas_) {
        SDL_DestroyTexture(characterAtlas_);
        characterAtlas_ = nullptr;
    }

    if (renderer_) {
        SDL_DestroyRenderer(renderer_);
        renderer_ = nullptr;
    }

    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }

    SDL_Quit();
}

void Game::HandleEvents(bool& running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) {
            running = false;
        } else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
            if (event.key.scancode == SDL_SCANCODE_F3) {
                debugShowHitboxes_ = !debugShowHitboxes_;
                SDL_Log("Debug hitboxes %s", debugShowHitboxes_ ? "ON" : "OFF");
            }
        }
    }
}

bool Game::IsRectCollidingWithSolidTiles(const SDL_FRect& rect, const std::string& mapId, int screenX, int screenY) const {
    const int left = static_cast<int>(std::floor(rect.x / kTileSize));
    const int right = static_cast<int>(std::floor((rect.x + rect.w - 0.001f) / kTileSize));
    const int top = static_cast<int>(std::floor(rect.y / kTileSize));
    const int bottom = static_cast<int>(std::floor((rect.y + rect.h - 0.001f) / kTileSize));

    for (int ty = top; ty <= bottom; ++ty) {
        for (int tx = left; tx <= right; ++tx) {
            const std::vector<SDL_FRect> tileHitboxes = world_.GetTileHitboxes(mapId, screenX, screenY, tx, ty);
            for (const SDL_FRect& tileHitbox : tileHitboxes) {
                if (tileHitbox.w > 0 && tileHitbox.h > 0) {
                    if (!(rect.x + rect.w <= tileHitbox.x || 
                          tileHitbox.x + tileHitbox.w <= rect.x || 
                          rect.y + rect.h <= tileHitbox.y || 
                          tileHitbox.y + tileHitbox.h <= rect.y)) {
                        return true;
                    }
                }
            }
        }
    }

    return false;
}

std::vector<SDL_FRect> Game::ActivePlayerHitboxesAt(const SDL_FRect& candidateBounds) const {
    std::vector<SDL_FRect> out;

    const CharacterAction* action = ActiveCharacterAction();
    if (!action || action->hitboxes.empty()) {
        out.push_back(candidateBounds);
        return out;
    }

    const SDL_FRect spriteRect = PlayerSpriteRectForBounds(candidateBounds);

    for (const TileHitbox& hb : action->hitboxes) {
        if (hb.w <= 0 || hb.h <= 0) {
            continue;
        }
        out.push_back(SDL_FRect{
            spriteRect.x + static_cast<float>(hb.x),
            spriteRect.y + static_cast<float>(hb.y),
            static_cast<float>(hb.w),
            static_cast<float>(hb.h)
        });
    }

    if (out.empty()) {
        out.push_back(candidateBounds);
    }
    return out;
}

SDL_FRect Game::PlayerSpriteRectForBounds(const SDL_FRect& bounds) const {
    if (IsFirstVersionMode()) {
        return bounds;
    }

    const CharacterSpriteset* spriteset = world_.ActiveCharacterSpriteset();
    const CharacterFrame* frame = ActiveCharacterFrame();
    if (!spriteset || !frame) {
        return bounds;
    }

    const float spriteW = static_cast<float>(frame->frameWidth * spriteset->tileWidth);
    const float spriteH = static_cast<float>(frame->frameHeight * spriteset->tileHeight);
    return SDL_FRect{
        bounds.x + (bounds.w * 0.5f) - (spriteW * 0.5f),
        bounds.y + bounds.h - spriteH,
        spriteW,
        spriteH
    };
}

bool Game::IsPlayerHitboxCollidingAt(const SDL_FRect& candidateBounds, const std::string& mapId, int screenX, int screenY) const {
    const std::vector<SDL_FRect> hitboxes = ActivePlayerHitboxesAt(candidateBounds);
    for (const SDL_FRect& hitbox : hitboxes) {
        if (IsRectCollidingWithSolidTiles(hitbox, mapId, screenX, screenY)) {
            return true;
        }
    }
    return false;
}

bool Game::PlayerIntersects(const SDL_FRect& other) const {
    const std::vector<SDL_FRect> hitboxes = ActivePlayerHitboxesAt(player_.bounds);
    for (const SDL_FRect& hitbox : hitboxes) {
        if (Intersects(hitbox, other)) {
            return true;
        }
    }
    return false;
}

void Game::ResolveAxisMovement(float dx, float dy) {
    SDL_FRect tryX = player_.bounds;
    tryX.x += dx;
    if (!IsPlayerHitboxCollidingAt(tryX, currentMapId_, currentScreenX_, currentScreenY_)) {
        player_.bounds.x = tryX.x;
    }

    SDL_FRect tryY = player_.bounds;
    tryY.y += dy;
    if (!IsPlayerHitboxCollidingAt(tryY, currentMapId_, currentScreenX_, currentScreenY_)) {
        player_.bounds.y = tryY.y;
    }
}

SDL_FPoint Game::DefaultArrivalPosition(TransitionDirection direction) const {
    const float kEdgeBuffer = 4.0f;
    SDL_FPoint pos{player_.bounds.x, player_.bounds.y};

    switch (direction) {
        case TransitionDirection::Left:
            pos.x = static_cast<float>(kScreenPixelWidth) - player_.bounds.w - kEdgeBuffer;
            break;
        case TransitionDirection::Right:
            pos.x = kEdgeBuffer;
            break;
        case TransitionDirection::Up:
            pos.y = static_cast<float>(kScreenPixelHeight) - player_.bounds.h - kEdgeBuffer;
            break;
        case TransitionDirection::Down:
            pos.y = kEdgeBuffer;
            break;
    }

    pos.x = std::clamp(pos.x, 0.0f, static_cast<float>(kScreenPixelWidth) - player_.bounds.w);
    pos.y = std::clamp(pos.y, 0.0f, static_cast<float>(kScreenPixelHeight) - player_.bounds.h);
    return pos;
}

void Game::BeginScreenTransition(
    const std::string& nextMapId,
    int nextScreenX,
    int nextScreenY,
    TransitionDirection direction,
    SDL_FPoint destinationPos,
    bool scrolling
) {
    transitionDirection_ = direction;
    transitionTimer_ = 0.0f;
    transitionSourceMapId_ = currentMapId_;
    transitionSourceScreenX_ = currentScreenX_;
    transitionSourceScreenY_ = currentScreenY_;
    transitionTargetMapId_ = nextMapId;
    transitionTargetScreenX_ = nextScreenX;
    transitionTargetScreenY_ = nextScreenY;
    transitionEndPos_ = FindNearbyFreePosition(destinationPos, nextMapId, nextScreenX, nextScreenY);

    if (!scrolling) {
        currentMapId_ = nextMapId;
        currentScreenX_ = nextScreenX;
        currentScreenY_ = nextScreenY;
        player_.bounds.x = transitionEndPos_.x;
        player_.bounds.y = transitionEndPos_.y;
        transitionPhase_ = TransitionPhase::None;
        transitionTimer_ = 0.0f;
        transitionCooldownTimer_ = 0.3f;
        return;
    }

    transitionPhase_ = TransitionPhase::Scrolling;

    switch (transitionDirection_) {
        case TransitionDirection::Up:
            transitionStartPos_ = SDL_FPoint{player_.bounds.x, 0.0f};
            break;
        case TransitionDirection::Down:
            transitionStartPos_ = SDL_FPoint{player_.bounds.x, static_cast<float>(kScreenPixelHeight) - player_.bounds.h};
            break;
        case TransitionDirection::Left:
            transitionStartPos_ = SDL_FPoint{0.0f, player_.bounds.y};
            break;
        case TransitionDirection::Right:
            transitionStartPos_ = SDL_FPoint{static_cast<float>(kScreenPixelWidth) - player_.bounds.w, player_.bounds.y};
            break;
    }

    player_.bounds.x = transitionStartPos_.x;
    player_.bounds.y = transitionStartPos_.y;
}

SDL_FPoint Game::FindNearbyFreePosition(SDL_FPoint candidate, const std::string& mapId, int screenX, int screenY) const {
    SDL_FRect probe = player_.bounds;

    for (int radius = 0; radius < 8; ++radius) {
        for (int oy = -radius; oy <= radius; ++oy) {
            for (int ox = -radius; ox <= radius; ++ox) {
                probe.x = candidate.x + static_cast<float>(ox * 2);
                probe.y = candidate.y + static_cast<float>(oy * 2);
                if (!IsRectCollidingWithSolidTiles(probe, mapId, screenX, screenY)) {
                    return SDL_FPoint{probe.x, probe.y};
                }
            }
        }
    }

    return candidate;
}

void Game::TryDetectEdgeTrigger() {
    if (transitionPhase_ != TransitionPhase::None || transitionCooldownTimer_ > 0.0f) {
        return;
    }

    const float kTriggerBandWidth = 2.0f;  // Invisible trigger zone at screen edges

    std::string targetMapId = currentMapId_;
    int targetSX = currentScreenX_;
    int targetSY = currentScreenY_;
    TransitionDirection direction = TransitionDirection::Down;
    std::string triggeredEdge;
    bool triggered = false;

    // Check left edge
    if (player_.bounds.x <= kTriggerBandWidth) {
        triggeredEdge = "left";
        direction = TransitionDirection::Left;
        targetSX = currentScreenX_ - 1;
        triggered = true;
    }
    // Check right edge
    else if (player_.bounds.x + player_.bounds.w >= static_cast<float>(kScreenPixelWidth) - kTriggerBandWidth) {
        triggeredEdge = "right";
        direction = TransitionDirection::Right;
        targetSX = currentScreenX_ + 1;
        triggered = true;
    }
    // Check top edge
    else if (player_.bounds.y <= kTriggerBandWidth) {
        triggeredEdge = "up";
        direction = TransitionDirection::Up;
        targetSY = currentScreenY_ - 1;
        triggered = true;
    }
    // Check bottom edge
    else if (player_.bounds.y + player_.bounds.h >= static_cast<float>(kScreenPixelHeight) - kTriggerBandWidth) {
        triggeredEdge = "down";
        direction = TransitionDirection::Down;
        targetSY = currentScreenY_ + 1;
        triggered = true;
    }

    if (!triggered) {
        return;
    }

    SDL_FPoint destinationPos = DefaultArrivalPosition(direction);
    bool scrolling = true;

    if (const ScreenTransition* tr = world_.FindTransition(currentMapId_, currentScreenX_, currentScreenY_, triggeredEdge)) {
        targetMapId = tr->toMapId;
        targetSX = tr->toScreenX;
        targetSY = tr->toScreenY;
        destinationPos = SDL_FPoint{static_cast<float>(tr->spawnX), static_cast<float>(tr->spawnY)};
        scrolling = tr->kind != TransitionKind::Instant &&
            CanScrollBetweenScreens(currentMapId_, currentScreenX_, currentScreenY_, targetMapId, targetSX, targetSY, direction);
    }

    if (!world_.InBounds(targetMapId, targetSX, targetSY)) {
        return;
    }

    BeginScreenTransition(targetMapId, targetSX, targetSY, direction, destinationPos, scrolling);
}

void Game::TryUseWarpPoint() {
    if (transitionPhase_ != TransitionPhase::None || transitionCooldownTimer_ > 0.0f) {
        return;
    }

    const WarpPoint* warp = world_.FindWarpAt(currentMapId_, currentScreenX_, currentScreenY_, player_.bounds);
    if (!warp) {
        return;
    }

    if (!world_.InBounds(warp->targetMapId, warp->targetScreenX, warp->targetScreenY)) {
        return;
    }

    BeginScreenTransition(
        warp->targetMapId,
        warp->targetScreenX,
        warp->targetScreenY,
        transitionDirection_,
        SDL_FPoint{static_cast<float>(warp->spawnX), static_cast<float>(warp->spawnY)},
        false
    );
}

void Game::TryStartScreenTransition() {
    if (transitionPhase_ != TransitionPhase::None) {
        return;
    }

    std::string targetMapId = currentMapId_;
    int targetSX = currentScreenX_;
    int targetSY = currentScreenY_;
    TransitionDirection direction = TransitionDirection::Down;
    std::string crossedEdge;

    if (player_.bounds.x <= 0.0f) {
        crossedEdge = "left";
        direction = TransitionDirection::Left;
        targetSX = currentScreenX_ - 1;
    } else if (player_.bounds.x + player_.bounds.w >= static_cast<float>(kScreenPixelWidth)) {
        crossedEdge = "right";
        direction = TransitionDirection::Right;
        targetSX = currentScreenX_ + 1;
    } else if (player_.bounds.y <= 0.0f) {
        crossedEdge = "up";
        direction = TransitionDirection::Up;
        targetSY = currentScreenY_ - 1;
    } else if (player_.bounds.y + player_.bounds.h >= static_cast<float>(kScreenPixelHeight)) {
        crossedEdge = "down";
        direction = TransitionDirection::Down;
        targetSY = currentScreenY_ + 1;
    } else {
        return;
    }

    SDL_FPoint destinationPos = DefaultArrivalPosition(direction);
    bool scrolling = true;

    if (const ScreenTransition* tr = world_.FindTransition(currentMapId_, currentScreenX_, currentScreenY_, crossedEdge)) {
        targetMapId = tr->toMapId;
        targetSX = tr->toScreenX;
        targetSY = tr->toScreenY;
        destinationPos = SDL_FPoint{static_cast<float>(tr->spawnX), static_cast<float>(tr->spawnY)};
        scrolling = tr->kind != TransitionKind::Instant &&
            CanScrollBetweenScreens(currentMapId_, currentScreenX_, currentScreenY_, targetMapId, targetSX, targetSY, direction);
    }

    if (!world_.InBounds(targetMapId, targetSX, targetSY)) {
        player_.bounds.x = std::clamp(player_.bounds.x, 0.0f, static_cast<float>(kScreenPixelWidth) - player_.bounds.w);
        player_.bounds.y = std::clamp(player_.bounds.y, 0.0f, static_cast<float>(kScreenPixelHeight) - player_.bounds.h);
        return;
    }

    BeginScreenTransition(targetMapId, targetSX, targetSY, direction, destinationPos, scrolling);
}

void Game::UpdateTransition(float dt) {
    if (transitionCooldownTimer_ > 0.0f) {
        transitionCooldownTimer_ = std::max(0.0f, transitionCooldownTimer_ - dt);
    }

    if (transitionPhase_ == TransitionPhase::None) {
        return;
    }

    transitionTimer_ += dt;
    const float progress = std::clamp(transitionTimer_ / transitionDuration_, 0.0f, 1.0f);

    switch (transitionDirection_) {
        case TransitionDirection::Up:
            player_.bounds.x = transitionStartPos_.x;
            player_.bounds.y = Lerp(transitionStartPos_.y, transitionEndPos_.y, progress);
            break;
        case TransitionDirection::Down:
            player_.bounds.x = transitionStartPos_.x;
            player_.bounds.y = Lerp(transitionStartPos_.y, transitionEndPos_.y, progress);
            break;
        case TransitionDirection::Left:
            player_.bounds.x = Lerp(transitionStartPos_.x, transitionEndPos_.x, progress);
            player_.bounds.y = transitionStartPos_.y;
            break;
        case TransitionDirection::Right:
            player_.bounds.x = Lerp(transitionStartPos_.x, transitionEndPos_.x, progress);
            player_.bounds.y = transitionStartPos_.y;
            break;
    }

    if (progress >= 1.0f) {
        currentMapId_ = transitionTargetMapId_;
        currentScreenX_ = transitionTargetScreenX_;
        currentScreenY_ = transitionTargetScreenY_;
        player_.bounds.x = transitionEndPos_.x;
        player_.bounds.y = transitionEndPos_.y;

        transitionPhase_ = TransitionPhase::None;
        transitionTimer_ = 0.0f;
        transitionCooldownTimer_ = 0.3f;  // Prevent immediate re-trigger
    }
}

void Game::UpdatePlayerInputAndAnimation(float dt) {
    const bool* keys = SDL_GetKeyboardState(nullptr);
    float dx = 0.0f;
    float dy = 0.0f;

    if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP]) {
        dy -= 1.0f;
        player_.facing = Direction::Up;
    }
    if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN]) {
        dy += 1.0f;
        player_.facing = Direction::Down;
    }
    if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT]) {
        dx -= 1.0f;
        player_.facing = Direction::Left;
    }
    if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) {
        dx += 1.0f;
        player_.facing = Direction::Right;
    }

    float length = std::sqrt(dx * dx + dy * dy);
    player_.moving = length > 0.0f;
    if (length > 0.0f) {
        dx /= length;
        dy /= length;
    }

    const float distance = player_.speedPixelsPerSecond * dt;
    
    // Check for edge proximity triggers BEFORE collision resolution
    // This allows transitions to fire before the player hits solid border tiles
    TryDetectEdgeTrigger();
    
    ResolveAxisMovement(dx * distance, dy * distance);
    TryUseWarpPoint();
    TryStartScreenTransition();

    UpdateCharacterAnimation(dt);

    if (!IsFirstVersionMode()) {
        const bool attackPressed = keys[SDL_SCANCODE_SPACE] || keys[SDL_SCANCODE_RETURN];
        if (attackPressed && !previousAttackPressed_ && !player_.attack.active && player_.attack.cooldownTimer <= 0.0f) {
            player_.attack.active = true;
            player_.attack.activeTimer = player_.attack.activeDuration;
            player_.attack.cooldownTimer = player_.attack.cooldownDuration;

            // Let the authored sword-slash animation define how long the visual lasts.
            slashVisualTimer_ = player_.attack.activeDuration;
            if (const CharacterSpriteset* spriteset = world_.ActiveCharacterSpriteset()) {
                for (const CharacterAction& action : spriteset->actions) {
                    if (action.id != "sword_slash") {
                        continue;
                    }
                    const int directionIndex = std::clamp(static_cast<int>(player_.facing), 0, 3);
                    const std::vector<CharacterFrame>& frames = action.directionalFrames[static_cast<size_t>(directionIndex)];
                    if (!frames.empty()) {
                        const float speed = std::max(0.1f, action.animationSpeed);
                        slashVisualTimer_ = std::max(slashVisualTimer_, static_cast<float>(frames.size()) / speed);
                    }
                    break;
                }
            }

            SDL_FRect hit = player_.bounds;
            if (player_.facing == Direction::Down) {
                hit.y += hit.h;
                hit.h = 8.0f;
            } else if (player_.facing == Direction::Up) {
                hit.y -= 8.0f;
                hit.h = 8.0f;
            } else if (player_.facing == Direction::Left) {
                hit.x -= 8.0f;
                hit.w = 8.0f;
            } else {
                hit.x += hit.w;
                hit.w = 8.0f;
            }
            player_.attack.hitbox = hit;
        }
        previousAttackPressed_ = attackPressed;
    } else {
        previousAttackPressed_ = false;
    }
}

const CharacterAction* Game::ActiveCharacterAction() const {
    const CharacterSpriteset* spriteset = world_.ActiveCharacterSpriteset();
    if (!spriteset) {
        return nullptr;
    }
    for (const CharacterAction& action : spriteset->actions) {
        if (action.id == activeActionId_) {
            return &action;
        }
    }
    if (!spriteset->actions.empty()) {
        return &spriteset->actions.front();
    }
    return nullptr;
}

const CharacterFrame* Game::ActiveCharacterFrame() const {
    const CharacterAction* action = ActiveCharacterAction();
    if (!action) {
        return nullptr;
    }
    const int directionIndex = std::clamp(static_cast<int>(player_.facing), 0, 3);
    const std::vector<CharacterFrame>& frames = action->directionalFrames[static_cast<size_t>(directionIndex)];
    if (frames.empty()) {
        return nullptr;
    }
    const int index = std::clamp(activeActionFrame_, 0, static_cast<int>(frames.size()) - 1);
    return &frames[static_cast<size_t>(index)];
}

void Game::UpdateCharacterAnimation(float dt) {
    slashVisualTimer_ = std::max(0.0f, slashVisualTimer_ - dt);

    std::string nextAction = "standing";
    if (!IsFirstVersionMode() && slashVisualTimer_ > 0.0f) {
        nextAction = "sword_slash";
    } else if (player_.moving) {
        nextAction = "walking";
    }

    if (activeActionId_ != nextAction) {
        activeActionId_ = nextAction;
        activeActionFrame_ = 0;
        activeActionTimer_ = 0.0f;
    }

    const CharacterAction* action = ActiveCharacterAction();
    if (!action) {
        player_.animFrame = player_.moving ? 0 : 1;
        return;
    }
    const int directionIndex = std::clamp(static_cast<int>(player_.facing), 0, 3);
    const std::vector<CharacterFrame>& frames = action->directionalFrames[static_cast<size_t>(directionIndex)];
    if (frames.empty()) {
        player_.animFrame = player_.moving ? 0 : 1;
        return;
    }

    const float speed = std::max(0.1f, action->animationSpeed);

    const float frameDuration = 1.0f / speed;
    activeActionTimer_ += dt;
    if (activeActionTimer_ >= frameDuration) {
        activeActionTimer_ = 0.0f;
        if (activeActionId_ == "sword_slash") {
            activeActionFrame_ = std::min(activeActionFrame_ + 1, static_cast<int>(frames.size()) - 1);
        } else {
            activeActionFrame_ = (activeActionFrame_ + 1) % static_cast<int>(frames.size());
        }
    }

    player_.animFrame = activeActionFrame_;
}

void Game::ApplyPowerup(const PowerupDef& def) {
    if (def.effect == "speed") {
        speedBuffMagnitude_ = std::max(speedBuffMagnitude_, def.magnitude);
        speedBuffTimer_ = std::max(speedBuffTimer_, def.durationSeconds);
        player_.speedPixelsPerSecond = player_.baseSpeedPixelsPerSecond + static_cast<float>(speedBuffMagnitude_);
    } else if (def.effect == "max_health") {
        player_.health = std::min(8, player_.health + std::max(1, def.magnitude));
    } else if (def.effect == "heal") {
        player_.health = std::min(8, player_.health + std::max(1, def.magnitude));
    }
}

void Game::UpdateCombat(float dt) {
    if (player_.attack.cooldownTimer > 0.0f) {
        player_.attack.cooldownTimer = std::max(0.0f, player_.attack.cooldownTimer - dt);
    }

    if (player_.attack.active) {
        player_.attack.activeTimer -= dt;
        if (player_.attack.activeTimer <= 0.0f) {
            player_.attack.active = false;
        }
    }

    if (player_.invulnTimer > 0.0f) {
        player_.invulnTimer = std::max(0.0f, player_.invulnTimer - dt);
    }

    for (Enemy& enemy : world_.Enemies()) {
        if (!enemy.alive || enemy.mapId != currentMapId_ || enemy.screenX != currentScreenX_ || enemy.screenY != currentScreenY_) {
            continue;
        }

        if (enemy.invulnTimer > 0.0f) {
            enemy.invulnTimer = std::max(0.0f, enemy.invulnTimer - dt);
        }

        if (player_.attack.active && enemy.invulnTimer <= 0.0f && Intersects(player_.attack.hitbox, enemy.bounds)) {
            enemy.health -= player_.attack.damage;
            enemy.invulnTimer = 0.20f;
            enemy.velocity.x *= -1.2f;
            enemy.velocity.y *= -1.2f;
            if (enemy.health <= 0) {
                enemy.alive = false;
            }
        }

        if (player_.invulnTimer <= 0.0f && PlayerIntersects(enemy.bounds)) {
            player_.health = std::max(0, player_.health - 1);
            player_.invulnTimer = 0.75f;
        }
    }
}

void Game::UpdateEnemies(float dt) {
    static std::mt19937 rng(1337);
    std::uniform_real_distribution<float> randomVel(-28.0f, 28.0f);

    for (Enemy& enemy : world_.Enemies()) {
        if (!enemy.alive || enemy.mapId != currentMapId_ || enemy.screenX != currentScreenX_ || enemy.screenY != currentScreenY_) {
            continue;
        }

        if (enemy.behavior == "static") {
            enemy.velocity.x = 0.0f;
            enemy.velocity.y = 0.0f;
        } else if (enemy.behavior == "chase") {
            const float dx = player_.bounds.x - enemy.bounds.x;
            const float dy = player_.bounds.y - enemy.bounds.y;
            const float len = std::sqrt(dx * dx + dy * dy);
            if (len > 0.001f) {
                enemy.velocity.x = (dx / len) * enemy.speed;
                enemy.velocity.y = (dy / len) * enemy.speed;
            }
        } else {
            enemy.directionTimer -= dt;
            if (enemy.directionTimer <= 0.0f) {
                if (enemy.behavior == "patrol") {
                    if (std::abs(enemy.velocity.x) > std::abs(enemy.velocity.y)) {
                        enemy.velocity.x *= -1.0f;
                        enemy.velocity.y = 0.0f;
                    } else {
                        enemy.velocity.y *= -1.0f;
                        enemy.velocity.x = 0.0f;
                    }
                } else {
                    enemy.velocity.x = randomVel(rng);
                    enemy.velocity.y = randomVel(rng);
                }
                enemy.directionTimer = 1.2f;
            }
        }

        SDL_FRect next = enemy.bounds;
        next.x += enemy.velocity.x * dt;
        next.y += enemy.velocity.y * dt;

        if (!IsRectCollidingWithSolidTiles(next, currentMapId_, currentScreenX_, currentScreenY_)) {
            enemy.bounds = next;
        } else {
            enemy.velocity.x *= -1.0f;
            enemy.velocity.y *= -1.0f;
        }
    }
}

void Game::UpdateItems() {
    for (Item& item : world_.Items()) {
        if (item.collected || item.mapId != currentMapId_ || item.screenX != currentScreenX_ || item.screenY != currentScreenY_) {
            continue;
        }

        if (PlayerIntersects(item.bounds)) {
            item.collected = true;
            if (item.type == ItemType::Coin) {
                coins_ += 1;
            } else if (item.type == ItemType::Wheat) {
                wheat_ += 1;
            } else if (!item.powerupId.empty()) {
                if (const PowerupDef* def = world_.FindPowerupById(item.powerupId)) {
                    ApplyPowerup(*def);
                }
            }
        }
    }
}

void Game::Update(float dt) {
    if (speedBuffTimer_ > 0.0f) {
        speedBuffTimer_ = std::max(0.0f, speedBuffTimer_ - dt);
        if (speedBuffTimer_ <= 0.0f) {
            speedBuffMagnitude_ = 0;
            player_.speedPixelsPerSecond = player_.baseSpeedPixelsPerSecond;
        }
    }

    if (transitionPhase_ == TransitionPhase::None) {
        UpdatePlayerInputAndAnimation(dt);
        UpdateEnemies(dt);
        if (!IsFirstVersionMode()) {
            UpdateCombat(dt);
        }
        UpdateItems();
    }

    UpdateTransition(dt);
}

bool Game::BuildTileTextureAtlas() {
    for (SDL_Texture* texture : ownedTileAtlases_) {
        if (texture) {
            SDL_DestroyTexture(texture);
        }
    }
    ownedTileAtlases_.clear();
    tileRenderById_.clear();

    for (const TileCollection& collection : world_.TileCollections()) {
        if (collection.imagePath.empty()) {
            continue;
        }
        const std::string resolvedPath = ResolveAssetPath(collection.imagePath);
        SDL_Surface* surface = LoadPngSurface(resolvedPath);
        if (!surface) {
            SDL_Log("Tile atlas load failed for '%s'", resolvedPath.c_str());
            continue;
        }

        SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer_, surface);
        SDL_DestroySurface(surface);
        if (!texture) {
            SDL_Log("Failed to create tile atlas texture for '%s': %s", resolvedPath.c_str(), SDL_GetError());
            continue;
        }

        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
        ownedTileAtlases_.push_back(texture);

        for (const TileDef& tile : collection.tiles) {
            TileRenderInfo info;
            info.texture = texture;
            info.source = SDL_FRect{
                static_cast<float>(tile.sourceX),
                static_cast<float>(tile.sourceY),
                static_cast<float>(collection.tileWidth),
                static_cast<float>(collection.tileHeight)
            };
            tileRenderById_[tile.id] = info;
        }
    }

    SDL_Log("Loaded tile rendering atlas entries: %d", static_cast<int>(tileRenderById_.size()));
    return true;
}

bool Game::BuildSpriteAtlas() {
    if (characterAtlas_) {
        SDL_DestroyTexture(characterAtlas_);
        characterAtlas_ = nullptr;
    }

    const CharacterSpriteset* spriteset = world_.ActiveCharacterSpriteset();
    if (!spriteset || spriteset->imagePath.empty()) {
        SDL_Log("No character spriteset configured. Falling back to rectangle player rendering.");
        return true;
    }

    const std::string resolvedPath = ResolveAssetPath(spriteset->imagePath);
    SDL_Surface* surface = LoadPngSurface(resolvedPath);
    if (!surface) {
        SDL_Log("Failed to load character spritesheet '%s'.", resolvedPath.c_str());
        return true;
    }

    characterAtlas_ = SDL_CreateTextureFromSurface(renderer_, surface);
    SDL_DestroySurface(surface);
    if (!characterAtlas_) {
        SDL_Log("Failed to create character texture '%s': %s", resolvedPath.c_str(), SDL_GetError());
        return true;
    }

    SDL_SetTextureBlendMode(characterAtlas_, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(characterAtlas_, SDL_SCALEMODE_NEAREST);
    SDL_Log("Loaded character spritesheet '%s'", resolvedPath.c_str());
    activeActionId_ = "standing";
    activeActionFrame_ = 0;
    activeActionTimer_ = 0.0f;
    return true;
}

void Game::DrawTilesForScreen(const std::string& mapId, int screenX, int screenY, float offsetX, float offsetY) {
    const Screen& screen = world_.GetScreen(mapId, screenX, screenY);
    const bool classicMode = IsFirstVersionMode();

    for (int ty = 0; ty < kTilesHigh; ++ty) {
        for (int tx = 0; tx < kTilesWide; ++tx) {
            const SDL_FRect rect{
                static_cast<float>(tx * kTileSize) + offsetX,
                static_cast<float>(ty * kTileSize) + offsetY,
                static_cast<float>(kTileSize) + 0.02f,
                static_cast<float>(kTileSize) + 0.02f
            };

            const size_t tileIndex = static_cast<size_t>(ty * kTilesWide + tx);
            for (int layer = 0; layer < kTileLayers; ++layer) {
                const int tileId = screen.tileLayerIds[static_cast<size_t>(layer)][tileIndex];
                if (tileId < 0) {
                    continue;
                }
                const auto it = tileRenderById_.find(tileId);
                if (it != tileRenderById_.end() && it->second.texture != nullptr) {
                    SDL_RenderTexture(renderer_, it->second.texture, &it->second.source, &rect);
                } else {
                    const SDL_Color color = TileColorFromId(tileId, classicMode);
                    SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);
                    SDL_RenderFillRect(renderer_, &rect);
                }
            }
        }
    }

    const std::string dungeonId = world_.DungeonIdForScreen(mapId, screenX, screenY);
    if (!dungeonId.empty()) {
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer_, 28, 18, 8, 52);
        SDL_FRect overlay{0.0f, 0.0f, static_cast<float>(kScreenPixelWidth), static_cast<float>(kScreenPixelHeight)};
        SDL_RenderFillRect(renderer_, &overlay);
    }
}

void Game::DrawItemsForScreen(const std::string& mapId, int screenX, int screenY, float offsetX, float offsetY) {
    for (const Item& item : world_.Items()) {
        if (item.collected || item.mapId != mapId || item.screenX != screenX || item.screenY != screenY) {
            continue;
        }

        if (item.type == ItemType::Coin) {
            SDL_SetRenderDrawColor(renderer_, 220, 180, 32, 255);
        } else if (item.type == ItemType::Powerup) {
            SDL_SetRenderDrawColor(renderer_, 98, 210, 190, 255);
        } else {
            SDL_SetRenderDrawColor(renderer_, 192, 140, 74, 255);
        }
        SDL_FRect rect = item.bounds;
        rect.x += offsetX;
        rect.y += offsetY;
        SDL_RenderFillRect(renderer_, &rect);
    }
}

void Game::DrawEnemiesForScreen(const std::string& mapId, int screenX, int screenY, float offsetX, float offsetY) {
    for (const Enemy& enemy : world_.Enemies()) {
        if (!enemy.alive || enemy.mapId != mapId || enemy.screenX != screenX || enemy.screenY != screenY) {
            continue;
        }

        if (enemy.invulnTimer > 0.0f) {
            SDL_SetRenderDrawColor(renderer_, 235, 180, 180, 255);
        } else {
            SDL_SetRenderDrawColor(renderer_, 146, 40, 40, 255);
        }
        SDL_FRect rect = enemy.bounds;
        rect.x += offsetX;
        rect.y += offsetY;
        SDL_RenderFillRect(renderer_, &rect);
    }
}

void Game::DrawPlayer() {
    if (IsFirstVersionMode()) {
        DrawPlayerClassic();
        return;
    }

    const CharacterSpriteset* spriteset = world_.ActiveCharacterSpriteset();
    const CharacterFrame* frame = ActiveCharacterFrame();
    if (!characterAtlas_ || !spriteset || !frame) {
        SDL_SetRenderDrawColor(renderer_, 210, 210, 224, 255);
        SDL_RenderFillRect(renderer_, &player_.bounds);
        return;
    }

    SDL_FRect src{
        static_cast<float>(frame->tileX * spriteset->tileWidth),
        static_cast<float>(frame->tileY * spriteset->tileHeight),
        static_cast<float>(frame->frameWidth * spriteset->tileWidth),
        static_cast<float>(frame->frameHeight * spriteset->tileHeight)
    };

    SDL_FRect dst = PlayerSpriteRectForBounds(player_.bounds);

    if (player_.invulnTimer > 0.0f) {
        const Uint8 alpha = (static_cast<int>(player_.invulnTimer * 20.0f) % 2 == 0) ? 128 : 255;
        SDL_SetTextureAlphaMod(characterAtlas_, alpha);
    } else {
        SDL_SetTextureAlphaMod(characterAtlas_, 255);
    }

    SDL_RenderTexture(renderer_, characterAtlas_, &src, &dst);
}

void Game::DrawPlayerAt(const SDL_FRect& bounds) {
    if (IsFirstVersionMode() || !characterAtlas_) {
        SDL_SetRenderDrawColor(renderer_, 210, 210, 224, 255);
        SDL_RenderFillRect(renderer_, &bounds);
        return;
    }

    const CharacterSpriteset* spriteset = world_.ActiveCharacterSpriteset();
    const CharacterFrame* frame = ActiveCharacterFrame();
    if (!spriteset || !frame) {
        SDL_SetRenderDrawColor(renderer_, 210, 210, 224, 255);
        SDL_RenderFillRect(renderer_, &bounds);
        return;
    }

    SDL_FRect src{
        static_cast<float>(frame->tileX * spriteset->tileWidth),
        static_cast<float>(frame->tileY * spriteset->tileHeight),
        static_cast<float>(frame->frameWidth * spriteset->tileWidth),
        static_cast<float>(frame->frameHeight * spriteset->tileHeight)
    };

    SDL_FRect dst = PlayerSpriteRectForBounds(bounds);

    if (player_.invulnTimer > 0.0f) {
        const Uint8 alpha = (static_cast<int>(player_.invulnTimer * 20.0f) % 2 == 0) ? 128 : 255;
        SDL_SetTextureAlphaMod(characterAtlas_, alpha);
    } else {
        SDL_SetTextureAlphaMod(characterAtlas_, 255);
    }

    SDL_RenderTexture(renderer_, characterAtlas_, &src, &dst);
}

void Game::DrawPlayerClassic() {
    SDL_SetRenderDrawColor(renderer_, 210, 210, 224, 255);
    SDL_RenderFillRect(renderer_, &player_.bounds);
}

void Game::DrawAttackHitbox() {
    // Hidden in normal gameplay; keep function for optional debug instrumentation.
}

void Game::DrawDebugHitboxesForScreen(const std::string& mapId, int screenX, int screenY, float offsetX, float offsetY) {
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);

    // Tile collision hitboxes.
    SDL_SetRenderDrawColor(renderer_, 64, 220, 255, 190);
    for (int ty = 0; ty < kTilesHigh; ++ty) {
        for (int tx = 0; tx < kTilesWide; ++tx) {
            const std::vector<SDL_FRect> hitboxes = world_.GetTileHitboxes(mapId, screenX, screenY, tx, ty);
            for (const SDL_FRect& hb : hitboxes) {
                SDL_FRect drawRect{hb.x + offsetX, hb.y + offsetY, hb.w, hb.h};
                SDL_RenderRect(renderer_, &drawRect);
            }
        }
    }

    // Enemy and item bounds as part of collision/interaction debugging.
    SDL_SetRenderDrawColor(renderer_, 255, 96, 96, 190);
    for (const Enemy& enemy : world_.Enemies()) {
        if (!enemy.alive || enemy.mapId != mapId || enemy.screenX != screenX || enemy.screenY != screenY) {
            continue;
        }
        SDL_FRect drawRect{enemy.bounds.x + offsetX, enemy.bounds.y + offsetY, enemy.bounds.w, enemy.bounds.h};
        SDL_RenderRect(renderer_, &drawRect);
    }

    SDL_SetRenderDrawColor(renderer_, 255, 220, 80, 190);
    for (const Item& item : world_.Items()) {
        if (item.collected || item.mapId != mapId || item.screenX != screenX || item.screenY != screenY) {
            continue;
        }
        SDL_FRect drawRect{item.bounds.x + offsetX, item.bounds.y + offsetY, item.bounds.w, item.bounds.h};
        SDL_RenderRect(renderer_, &drawRect);
    }
}

void Game::DrawPlayerDebugHitboxesAt(const SDL_FRect& bounds) {
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, 80, 255, 120, 210);

    const std::vector<SDL_FRect> hitboxes = ActivePlayerHitboxesAt(bounds);
    for (const SDL_FRect& hb : hitboxes) {
        SDL_RenderRect(renderer_, &hb);
    }

    if (player_.attack.active) {
        SDL_SetRenderDrawColor(renderer_, 255, 120, 80, 210);
        SDL_RenderRect(renderer_, &player_.attack.hitbox);
    }
}

void Game::DrawHUD() {
    SDL_SetRenderDrawColor(renderer_, 7, 10, 14, 255);
    SDL_FRect topBand{0.0f, 0.0f, static_cast<float>(kScreenPixelWidth), static_cast<float>(kHudStripHeight)};
    SDL_RenderFillRect(renderer_, &topBand);

    SDL_SetRenderDrawColor(renderer_, 24, 34, 48, 255);
    SDL_FRect divider{0.0f, static_cast<float>(kHudStripHeight - 1), static_cast<float>(kScreenPixelWidth), 1.0f};
    SDL_RenderFillRect(renderer_, &divider);

    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 180);
    SDL_FRect panel = IsFirstVersionMode() ? SDL_FRect{4.0f, 2.0f, 94.0f, 20.0f} : SDL_FRect{4.0f, 2.0f, 114.0f, 20.0f};
    SDL_RenderFillRect(renderer_, &panel);

    SDL_SetRenderDrawColor(renderer_, 220, 48, 48, 255);
    const int maxHealthPips = IsFirstVersionMode() ? 4 : 8;
    for (int i = 0; i < player_.health && i < maxHealthPips; ++i) {
        SDL_FRect heart{6.0f + i * 8.0f, 6.0f, 6.0f, 6.0f};
        SDL_RenderFillRect(renderer_, &heart);
    }

    SDL_SetRenderDrawColor(renderer_, 220, 180, 32, 255);
    for (int i = 0; i < coins_ && i < 4; ++i) {
        SDL_FRect coin = IsFirstVersionMode()
            ? SDL_FRect{34.0f + i * 8.0f, 6.0f, 6.0f, 6.0f}
            : SDL_FRect{74.0f + i * 8.0f, 6.0f, 6.0f, 6.0f};
        SDL_RenderFillRect(renderer_, &coin);
    }

    SDL_SetRenderDrawColor(renderer_, 192, 140, 74, 255);
    for (int i = 0; i < wheat_ && i < 4; ++i) {
        SDL_FRect grain = IsFirstVersionMode()
            ? SDL_FRect{66.0f + i * 8.0f, 6.0f, 6.0f, 6.0f}
            : SDL_FRect{74.0f + i * 8.0f, 14.0f, 6.0f, 4.0f};
        SDL_RenderFillRect(renderer_, &grain);
    }

    if (!IsFirstVersionMode() && speedBuffTimer_ > 0.0f) {
        SDL_SetRenderDrawColor(renderer_, 80, 222, 190, 255);
        SDL_FRect buff{94.0f, 4.0f, std::min(16.0f, speedBuffTimer_ * 2.0f), 3.0f};
        SDL_RenderFillRect(renderer_, &buff);
    }
}

void Game::DrawTransitionOverlay() {
    if (transitionPhase_ == TransitionPhase::None) {
        return;
    }

    const float t = std::clamp(transitionTimer_ / transitionDuration_, 0.0f, 1.0f);
    const float alpha = 0.25f * (1.0f - std::abs(t - 0.5f) * 2.0f);

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, static_cast<Uint8>(alpha * 255.0f));
    SDL_FRect full{0.0f, 0.0f, static_cast<float>(kScreenPixelWidth), static_cast<float>(kScreenPixelHeight)};
    SDL_RenderFillRect(renderer_, &full);
}

void Game::DrawScreenLayer(const std::string& mapId, int screenX, int screenY, float offsetX, float offsetY) {
    DrawTilesForScreen(mapId, screenX, screenY, offsetX, offsetY);
    DrawItemsForScreen(mapId, screenX, screenY, offsetX, offsetY);
    DrawEnemiesForScreen(mapId, screenX, screenY, offsetX, offsetY);
}

void Game::Draw() {
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);

    const SDL_Rect mapViewport{0, kHudStripHeight, kScreenPixelWidth, kScreenPixelHeight};
    SDL_SetRenderViewport(renderer_, &mapViewport);

    if (transitionPhase_ == TransitionPhase::Scrolling) {
        const float progress = std::clamp(transitionTimer_ / transitionDuration_, 0.0f, 1.0f);
        float currentOffsetX = 0.0f;
        float currentOffsetY = 0.0f;
        float targetOffsetX = 0.0f;
        float targetOffsetY = 0.0f;

        switch (transitionDirection_) {
            case TransitionDirection::Up:
                currentOffsetY = progress * static_cast<float>(kScreenPixelHeight);
                targetOffsetY = -static_cast<float>(kScreenPixelHeight) + progress * static_cast<float>(kScreenPixelHeight);
                break;
            case TransitionDirection::Down:
                currentOffsetY = -progress * static_cast<float>(kScreenPixelHeight);
                targetOffsetY = static_cast<float>(kScreenPixelHeight) - progress * static_cast<float>(kScreenPixelHeight);
                break;
            case TransitionDirection::Left:
                currentOffsetX = progress * static_cast<float>(kScreenPixelWidth);
                targetOffsetX = -static_cast<float>(kScreenPixelWidth) + progress * static_cast<float>(kScreenPixelWidth);
                break;
            case TransitionDirection::Right:
                currentOffsetX = -progress * static_cast<float>(kScreenPixelWidth);
                targetOffsetX = static_cast<float>(kScreenPixelWidth) - progress * static_cast<float>(kScreenPixelWidth);
                break;
        }

        DrawScreenLayer(transitionSourceMapId_, transitionSourceScreenX_, transitionSourceScreenY_, currentOffsetX, currentOffsetY);
        DrawScreenLayer(transitionTargetMapId_, transitionTargetScreenX_, transitionTargetScreenY_, targetOffsetX, targetOffsetY);

        if (debugShowHitboxes_) {
            DrawDebugHitboxesForScreen(transitionSourceMapId_, transitionSourceScreenX_, transitionSourceScreenY_, currentOffsetX, currentOffsetY);
            DrawDebugHitboxesForScreen(transitionTargetMapId_, transitionTargetScreenX_, transitionTargetScreenY_, targetOffsetX, targetOffsetY);
        }

        const SDL_FRect interpolatedPlayer{player_.bounds.x, player_.bounds.y, player_.bounds.w, player_.bounds.h};
        DrawPlayerAt(interpolatedPlayer);
        if (debugShowHitboxes_) {
            DrawPlayerDebugHitboxesAt(interpolatedPlayer);
        }
    } else {
        DrawScreenLayer(currentMapId_, currentScreenX_, currentScreenY_, 0.0f, 0.0f);
        if (debugShowHitboxes_) {
            DrawDebugHitboxesForScreen(currentMapId_, currentScreenX_, currentScreenY_, 0.0f, 0.0f);
        }
        DrawPlayer();
        if (debugShowHitboxes_) {
            DrawPlayerDebugHitboxesAt(player_.bounds);
        }
    }

    DrawTransitionOverlay();

    SDL_SetRenderViewport(renderer_, nullptr);
    DrawAttackHitbox();
    DrawHUD();

    SDL_RenderPresent(renderer_);
}
