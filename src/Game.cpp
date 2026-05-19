#include "Game.hpp"

#include <algorithm>
#include <array>
#include <cctype>
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
constexpr int kTextStripHeight = 48;
constexpr float kTextLettersPerSecond = 28.0f;

constexpr int kGlyphSize = 8;
constexpr int kGlyphsPerRow = 30;
constexpr int kGlyphRows = 6;
constexpr int kGlyphAreaHeight = kGlyphRows * kGlyphSize;
constexpr int kGlyphCount = kGlyphsPerRow * kGlyphRows;

constexpr int kLetterColumnsPerRow = 13;
constexpr int kDigitBlockStartCol = 14;
constexpr int kExtraTallRowTop = 4;
constexpr int kExtraTallColumns = 14;

constexpr int kTextPanelSourceX = 0;
constexpr int kTextPanelSourceY = 48;
constexpr int kTextPanelSourceW = 240;
constexpr int kTextPanelSourceH = 56;

std::string DefaultTextGlyphMap() {
    std::string map(static_cast<size_t>(kGlyphCount), ' ');
    auto setGlyph = [&map](int row, int col, char ch) {
        if (row < 0 || row >= kGlyphRows || col < 0 || col >= kGlyphsPerRow) {
            return;
        }
        map[static_cast<size_t>(row * kGlyphsPerRow + col)] = ch;
    };

    // Letters occupy the left side as 8x16 sprites laid out A-M on top band, N-Z on second band.
    for (int i = 0; i < 26; ++i) {
        const int letterRowBand = i / kLetterColumnsPerRow;
        const int letterCol = i % kLetterColumnsPerRow;
        const char upper = static_cast<char>('A' + i);
        const char lower = static_cast<char>('a' + i);
        setGlyph(letterRowBand * 2, letterCol, upper);
        setGlyph(letterRowBand * 2 + 1, letterCol, lower);
    }

    // Digits are in a 3x4 8x8 block on the right; last row only uses the center cell for '9'.
    const int digitRows[10] = {0, 0, 0, 1, 1, 1, 2, 2, 2, 3};
    const int digitCols[10] = {0, 1, 2, 0, 1, 2, 0, 1, 2, 1};
    for (int d = 0; d <= 9; ++d) {
        setGlyph(digitRows[d], kDigitBlockStartCol + digitCols[d], static_cast<char>('0' + d));
    }

    return map;
}

std::string NormalizedGlyphMap(const std::string& rawMap) {
    std::string map;
    map.reserve(rawMap.size());
    for (char ch : rawMap) {
        if (ch != '\n' && ch != '\r') {
            map.push_back(ch == '_' ? ' ' : ch);
        }
    }

    if (map.empty()) {
        return DefaultTextGlyphMap();
    }
    if (map.size() < static_cast<size_t>(kGlyphCount)) {
        map.append(static_cast<size_t>(kGlyphCount) - map.size(), ' ');
    } else if (map.size() > static_cast<size_t>(kGlyphCount)) {
        map.resize(static_cast<size_t>(kGlyphCount));
    }
    return map;
}

bool GlyphSourceForCharacter(char c, const std::string& glyphMap, SDL_FRect& source) {
    if (std::isalpha(static_cast<unsigned char>(c))) {
        const char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        const int alphaIndex = lower - 'a';
        if (alphaIndex >= 0 && alphaIndex < 26) {
            const int letterBand = alphaIndex / kLetterColumnsPerRow;
            const int letterColBase = (alphaIndex % kLetterColumnsPerRow) * 2;
            const int letterCol = std::isupper(static_cast<unsigned char>(c)) ? letterColBase : letterColBase + 1;
            source = SDL_FRect{
                static_cast<float>(letterCol * kGlyphSize),
                static_cast<float>(letterBand * 2 * kGlyphSize),
                static_cast<float>(kGlyphSize),
                static_cast<float>(kGlyphSize * 2)
            };
            return true;
        }
    }

    if (c >= '0' && c <= '9') {
        const int digit = static_cast<int>(c - '0');
        const int digitRow = digit == 9 ? 3 : digit / 3;
        const int digitCol = digit == 9 ? 1 : digit % 3;
        source = SDL_FRect{
            static_cast<float>((kDigitBlockStartCol + digitCol) * kGlyphSize),
            static_cast<float>(digitRow * kGlyphSize),
            static_cast<float>(kGlyphSize),
            static_cast<float>(kGlyphSize)
        };
        return true;
    }

    const size_t glyphIndex = glyphMap.find(c);
    if (glyphIndex == std::string::npos) {
        return false;
    }

    const int row = static_cast<int>(glyphIndex / static_cast<size_t>(kGlyphsPerRow));
    const int col = static_cast<int>(glyphIndex % static_cast<size_t>(kGlyphsPerRow));
    if (row < 0 || row >= kGlyphRows || col < 0 || col >= kGlyphsPerRow) {
        return false;
    }

    // Extra atlas band: one additional 8x16 row in columns 0..13.
    if ((row == kExtraTallRowTop || row == kExtraTallRowTop + 1) && col < kExtraTallColumns) {
        source = SDL_FRect{
            static_cast<float>(col * kGlyphSize),
            static_cast<float>(kExtraTallRowTop * kGlyphSize),
            static_cast<float>(kGlyphSize),
            static_cast<float>(kGlyphSize * 2)
        };
        return true;
    }

    source = SDL_FRect{
        static_cast<float>(col * kGlyphSize),
        static_cast<float>(row * kGlyphSize),
        static_cast<float>(kGlyphSize),
        static_cast<float>(kGlyphSize)
    };
    return true;
}

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

std::vector<SDL_FRect> ActiveItemHitboxesAt(const Item& item) {
    std::vector<SDL_FRect> hitboxes;
    hitboxes.reserve(item.hitboxes.size());
    for (const TileHitbox& hitbox : item.hitboxes) {
        hitboxes.push_back(SDL_FRect{
            item.bounds.x + static_cast<float>(hitbox.x),
            item.bounds.y + static_cast<float>(hitbox.y),
            static_cast<float>(hitbox.w),
            static_cast<float>(hitbox.h)
        });
    }
    return hitboxes;
}

const ItemAnimationFrame* ActiveItemFrame(const Item& item, Uint64 ticks) {
    if (item.frames.empty()) {
        return nullptr;
    }
    if (item.frames.size() == 1 || item.animationSpeed <= 0.0f) {
        return &item.frames.front();
    }

    const float seconds = static_cast<float>(ticks) / 1000.0f;
    const int frameIndex = static_cast<int>(std::floor(seconds * item.animationSpeed)) % static_cast<int>(item.frames.size());
    return &item.frames[frameIndex];
}

int ItemIntParam(const Item& item, const std::string& key, int fallbackValue) {
    for (const ItemTriggerParam& param : item.triggerParams) {
        if (param.key != key) {
            continue;
        }

        try {
            return std::stoi(param.value);
        } catch (...) {
            return fallbackValue;
        }
    }
    return fallbackValue;
}

float ItemFloatParam(const Item& item, const std::string& key, float fallbackValue) {
    for (const ItemTriggerParam& param : item.triggerParams) {
        if (param.key != key) {
            continue;
        }

        try {
            return std::stof(param.value);
        } catch (...) {
            return fallbackValue;
        }
    }
    return fallbackValue;
}

SDL_Color LegacyItemColor(const Item& item) {
    if (item.type == ItemType::Coin) {
        return SDL_Color{220, 180, 32, 255};
    }
    if (item.type == ItemType::Powerup) {
        return SDL_Color{98, 210, 190, 255};
    }
    return SDL_Color{192, 140, 74, 255};
}

const EnemyMoveDefinition* ActiveEnemyMoveDefinition(const Enemy& enemy) {
    if (enemy.moves.empty()) {
        return nullptr;
    }
    const int index = std::clamp(enemy.currentMoveIndex, 0, static_cast<int>(enemy.moves.size()) - 1);
    return &enemy.moves[static_cast<size_t>(index)];
}

std::vector<SDL_FRect> ActiveProjectileHitboxesAt(const Projectile& projectile) {
    std::vector<SDL_FRect> out;
    for (const TileHitbox& hb : projectile.hitboxes) {
        if (hb.w <= 0 || hb.h <= 0) {
            continue;
        }
        out.push_back(SDL_FRect{
            projectile.bounds.x + static_cast<float>(hb.x),
            projectile.bounds.y + static_cast<float>(hb.y),
            static_cast<float>(hb.w),
            static_cast<float>(hb.h)
        });
    }
    if (out.empty()) {
        out.push_back(projectile.bounds);
    }
    return out;
}

const std::vector<ItemAnimationFrame>* ProjectileFramesForPhase(const Projectile& projectile) {
    switch (projectile.phase) {
        case Projectile::Phase::Start:
            return &projectile.startFrames;
        case Projectile::Phase::Flight:
            return &projectile.flightFrames;
        case Projectile::Phase::Impact:
            return &projectile.impactFrames;
        case Projectile::Phase::Done:
        default:
            return nullptr;
    }
}

float ProjectileAnimationSpeedForPhase(const Projectile& projectile) {
    switch (projectile.phase) {
        case Projectile::Phase::Start:
            return projectile.startAnimationSpeed;
        case Projectile::Phase::Flight:
            return projectile.flightAnimationSpeed;
        case Projectile::Phase::Impact:
            return projectile.impactAnimationSpeed;
        case Projectile::Phase::Done:
        default:
            return 0.0f;
    }
}

const std::vector<EnemyMoveDefinition::AnimationFrame>* EnemyFramesForDirection(const EnemyMoveDefinition& move, int directionIndex) {
    const int clamped = std::clamp(directionIndex, 0, 3);
    const auto& preferred = move.directionalFrames[static_cast<size_t>(clamped)];
    if (!preferred.empty()) {
        return &preferred;
    }
    for (int dir = 0; dir < 4; ++dir) {
        const auto& frames = move.directionalFrames[static_cast<size_t>(dir)];
        if (!frames.empty()) {
            return &frames;
        }
    }
    return nullptr;
}

const std::vector<EnemyMoveDefinition::AnimationFrame>* EnemyFramesForDirection(const EnemyReactionAnimation& animation, int directionIndex) {
    const int clamped = std::clamp(directionIndex, 0, 3);
    const auto& preferred = animation.directionalFrames[static_cast<size_t>(clamped)];
    if (!preferred.empty()) {
        return &preferred;
    }
    for (int dir = 0; dir < 4; ++dir) {
        const auto& frames = animation.directionalFrames[static_cast<size_t>(dir)];
        if (!frames.empty()) {
            return &frames;
        }
    }
    return nullptr;
}

SDL_FPoint CardinalDirectionFromVector(const SDL_FPoint& input) {
    if (std::fabs(input.x) >= std::fabs(input.y)) {
        if (input.x < 0.0f) {
            return SDL_FPoint{-1.0f, 0.0f};
        }
        if (input.x > 0.0f) {
            return SDL_FPoint{1.0f, 0.0f};
        }
    }
    if (input.y < 0.0f) {
        return SDL_FPoint{0.0f, -1.0f};
    }
    return SDL_FPoint{0.0f, 1.0f};
}

Direction DirectionFromVector(const SDL_FPoint& input) {
    const SDL_FPoint cardinal = CardinalDirectionFromVector(input);
    if (cardinal.x > 0.0f) {
        return Direction::Right;
    }
    if (cardinal.x < 0.0f) {
        return Direction::Left;
    }
    if (cardinal.y < 0.0f) {
        return Direction::Up;
    }
    return Direction::Down;
}

int MoveDirectionIndexFromVector(const SDL_FPoint& direction) {
    if (direction.x > 0.0f) {
        return static_cast<int>(Direction::Right);
    }
    if (direction.x < 0.0f) {
        return static_cast<int>(Direction::Left);
    }
    if (direction.y < 0.0f) {
        return static_cast<int>(Direction::Up);
    }
    return static_cast<int>(Direction::Down);
}

float RandomEnemyMoveSeconds(std::mt19937& rng, float minSeconds, float maxSeconds) {
    const float lo = std::max(0.0f, std::min(minSeconds, maxSeconds));
    const float hi = std::max(lo, std::max(minSeconds, maxSeconds));
    std::uniform_real_distribution<float> dist(lo, hi);
    return dist(rng);
}

SDL_FPoint RandomCardinalDirection(std::mt19937& rng) {
    std::uniform_int_distribution<int> dist(0, 3);
    switch (dist(rng)) {
        case 0:
            return SDL_FPoint{0.0f, -1.0f};
        case 1:
            return SDL_FPoint{1.0f, 0.0f};
        case 2:
            return SDL_FPoint{0.0f, 1.0f};
        case 3:
        default:
            return SDL_FPoint{-1.0f, 0.0f};
    }
}

bool EnemyMoveHasPlayableAnimation(const EnemyMoveDefinition& move) {
    return move.animationSpeed > 0.0f && EnemyFramesForDirection(move, static_cast<int>(Direction::Down)) != nullptr;
}

int EnemyMoveFrameCount(const EnemyMoveDefinition& move, int directionIndex) {
    const auto* frames = EnemyFramesForDirection(move, directionIndex);
    return frames ? static_cast<int>(frames->size()) : 0;
}

std::pair<float, float> EnemyFramePixelSize(const EnemyMoveDefinition::AnimationFrame& frame) {
    float maxW = static_cast<float>(std::max(1, frame.frameWidth * 16));
    float maxH = static_cast<float>(std::max(1, frame.frameHeight * 16));
    for (const EnemyMoveDefinition::AnimationTile& tile : frame.tiles) {
        const float right = static_cast<float>(tile.tileX * 16 + std::max(1, tile.sourceW));
        const float bottom = static_cast<float>(tile.tileY * 16 + std::max(1, tile.sourceH));
        maxW = std::max(maxW, right);
        maxH = std::max(maxH, bottom);
    }
    return std::pair<float, float>{maxW, maxH};
}

SDL_FRect EnemySpriteRectForDraw(const Enemy& enemy) {
    const std::vector<EnemyMoveDefinition::AnimationFrame>* frames = nullptr;
    int frameIndex = 0;
    if (enemy.knockbackTimer > 0.0f) {
        frames = EnemyFramesForDirection(enemy.knockbackAnimation, enemy.knockbackDirection);
        frameIndex = enemy.knockbackAnimationFrame;
    }
    if (!frames) {
        const EnemyMoveDefinition* move = ActiveEnemyMoveDefinition(enemy);
        frames = move ? EnemyFramesForDirection(*move, enemy.moveDirection) : nullptr;
        frameIndex = enemy.animationFrame;
    }
    if (frames && !frames->empty()) {
        const int clampedFrameIndex = std::clamp(frameIndex, 0, static_cast<int>(frames->size()) - 1);
        const auto [spriteW, spriteH] = EnemyFramePixelSize((*frames)[static_cast<size_t>(clampedFrameIndex)]);
        return SDL_FRect{enemy.bounds.x, enemy.bounds.y, spriteW, spriteH};
    }
    return enemy.bounds;
}

SDL_FPoint ProjectileLaunchPointForDefinition(const SDL_FRect& actorBounds, const ProjectileDefinition& definition, const SDL_FPoint& direction) {
    float projectileW = 8.0f;
    float projectileH = 8.0f;

    const ItemAnimationFrame* initialFrame = nullptr;
    if (!definition.startFrames.empty()) {
        initialFrame = &definition.startFrames.front();
    } else if (!definition.flightFrames.empty()) {
        initialFrame = &definition.flightFrames.front();
    } else if (!definition.impactFrames.empty()) {
        initialFrame = &definition.impactFrames.front();
    }

    if (initialFrame) {
        projectileW = static_cast<float>(std::max(1, initialFrame->sourceW));
        projectileH = static_cast<float>(std::max(1, initialFrame->sourceH));
    } else if (!definition.hitboxes.empty()) {
        projectileW = static_cast<float>(std::max(1, definition.hitboxes.front().w));
        projectileH = static_cast<float>(std::max(1, definition.hitboxes.front().h));
    }

    SDL_FPoint spawn{
        actorBounds.x + actorBounds.w * 0.5f,
        actorBounds.y + actorBounds.h * 0.5f
    };

    if (std::fabs(direction.x) >= std::fabs(direction.y)) {
        spawn.x = direction.x < 0.0f ? actorBounds.x - projectileW * 0.5f : actorBounds.x + actorBounds.w + projectileW * 0.5f;
    } else {
        spawn.y = direction.y < 0.0f ? actorBounds.y - projectileH * 0.5f : actorBounds.y + actorBounds.h + projectileH * 0.5f;
    }

    return spawn;
}

SDL_FRect ForegroundOcclusionRectForActor(const SDL_FRect& spriteRect, float) {
    return spriteRect;
}

bool TileShouldOccludeActor(const SDL_FRect& tileRect, const SDL_FRect& actorRect) {
    const bool overlapsHorizontally =
        actorRect.x < tileRect.x + tileRect.w &&
        actorRect.x + actorRect.w > tileRect.x;
    const bool overlapsVertically =
        actorRect.y < tileRect.y + tileRect.h &&
        actorRect.y + actorRect.h > tileRect.y;
    return overlapsHorizontally && overlapsVertically;
}

void DrawQuarterHeart(SDL_Renderer* renderer, float x, float y, int quarterCount) {
    static constexpr std::array<const char*, 6> kHeartRows = {
        ".11.11.",
        "1111111",
        "1111111",
        ".11111.",
        "..111..",
        "...1..."
    };
    static constexpr std::array<int, 5> kFillColumns = {0, 2, 4, 5, 7};

    quarterCount = std::clamp(quarterCount, 0, 4);
    const int fillColumns = kFillColumns[quarterCount];

    SDL_SetRenderDrawColor(renderer, 54, 14, 18, 255);
    for (int row = 0; row < static_cast<int>(kHeartRows.size()); ++row) {
        for (int col = 0; kHeartRows[row][col] != '\0'; ++col) {
            if (kHeartRows[row][col] == '1') {
                SDL_RenderPoint(renderer, x + static_cast<float>(col), y + static_cast<float>(row));
            }
        }
    }

    SDL_SetRenderDrawColor(renderer, 220, 48, 48, 255);
    for (int row = 0; row < static_cast<int>(kHeartRows.size()); ++row) {
        for (int col = 0; kHeartRows[row][col] != '\0'; ++col) {
            if (kHeartRows[row][col] == '1' && col < fillColumns) {
                SDL_RenderPoint(renderer, x + static_cast<float>(col), y + static_cast<float>(row));
            }
        }
    }
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

SDL_Texture* Game::TextureForItemFrame(const ItemAnimationFrame& frame) {
    if (frame.sourceImagePath.empty()) {
        return nullptr;
    }

    const std::string resolvedPath = ResolveAssetPath(frame.sourceImagePath);
    auto found = itemTextureByPath_.find(resolvedPath);
    if (found != itemTextureByPath_.end()) {
        return found->second;
    }

    SDL_Surface* surface = LoadPngSurface(resolvedPath);
    if (!surface) {
        itemTextureByPath_[resolvedPath] = nullptr;
        return nullptr;
    }

    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer_, surface);
    SDL_DestroySurface(surface);
    itemTextureByPath_[resolvedPath] = texture;
    return texture;
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
        (kScreenPixelHeight + kHudStripHeight + kTextStripHeight) * kWindowScale,
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
        kScreenPixelHeight + kHudStripHeight + kTextStripHeight,
        SDL_LOGICAL_PRESENTATION_INTEGER_SCALE
    );

    if (IsFirstVersionMode()) {
        // Force procedural world to match the original prototype behavior.
        world_.LoadFromJsonOrDefault("__force_procedural__");
        player_.maxHealth = 4;
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

    const auto& weaponDefinitions = world_.WeaponDefinitions();
    if (!weaponDefinitions.empty()) {
        equippedWeaponAId_ = weaponDefinitions.front().id;
        equippedWeaponBId_ = weaponDefinitions.size() > 1 ? weaponDefinitions[1].id : weaponDefinitions.front().id;
    }

    const bool tilesOk = BuildTileTextureAtlas();
    const bool characterOk = BuildSpriteAtlas();
    const std::string textAtlasPath = ResolveAssetPath("data/sprites/text/font.png");
    SDL_Surface* textSurface = LoadPngSurface(textAtlasPath);
    if (textSurface) {
        textAtlas_ = SDL_CreateTextureFromSurface(renderer_, textSurface);
        SDL_DestroySurface(textSurface);
        if (textAtlas_) {
            SDL_SetTextureBlendMode(textAtlas_, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(textAtlas_, SDL_SCALEMODE_NEAREST);
        }
    }

    previousScreenMapId_.clear();
    previousScreenX_ = -1;
    previousScreenY_ = -1;
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

    for (auto& entry : itemTextureByPath_) {
        if (entry.second) {
            SDL_DestroyTexture(entry.second);
        }
    }
    itemTextureByPath_.clear();

    if (characterAtlas_) {
        SDL_DestroyTexture(characterAtlas_);
        characterAtlas_ = nullptr;
    }

    if (textAtlas_) {
        SDL_DestroyTexture(textAtlas_);
        textAtlas_ = nullptr;
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
            } else if (event.key.scancode == SDL_SCANCODE_F4) {
                debugShowOcclusion_ = !debugShowOcclusion_;
                SDL_Log("Debug foreground occlusion %s", debugShowOcclusion_ ? "ON" : "OFF");
            }
        }
    }
}

bool Game::IsRectCollidingWithSolidTiles(const SDL_FRect& rect, const std::string& mapId, int screenX, int screenY) const {
    if (rect.x < 0.0f || rect.y < 0.0f || rect.x + rect.w > static_cast<float>(kScreenPixelWidth) || rect.y + rect.h > static_cast<float>(kScreenPixelHeight)) {
        return true;
    }

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

std::vector<SDL_FRect> Game::ActiveEnemyHitboxesAt(const Enemy& enemy) const {
    std::vector<SDL_FRect> out;

    const EnemyMoveDefinition* move = ActiveEnemyMoveDefinition(enemy);
    if (!move || move->hitboxes.empty()) {
        out.push_back(enemy.bounds);
        return out;
    }

    for (const TileHitbox& hb : move->hitboxes) {
        if (hb.w <= 0 || hb.h <= 0) {
            continue;
        }
        out.push_back(SDL_FRect{
            enemy.bounds.x + static_cast<float>(hb.x),
            enemy.bounds.y + static_cast<float>(hb.y),
            static_cast<float>(hb.w),
            static_cast<float>(hb.h)
        });
    }

    if (out.empty()) {
        out.push_back(enemy.bounds);
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

bool Game::AreEnemyHitboxesCollidingAfterDelta(const Enemy& enemy, float dx, float dy, const std::string& mapId, int screenX, int screenY) const {
    const std::vector<SDL_FRect> hitboxes = ActiveEnemyHitboxesAt(enemy);
    for (const SDL_FRect& currentHitbox : hitboxes) {
        SDL_FRect nextHitbox = currentHitbox;
        nextHitbox.x += dx;
        nextHitbox.y += dy;
        if (IsRectCollidingWithSolidTiles(nextHitbox, mapId, screenX, screenY)) {
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

void Game::ResolveEnemyAxisMovement(Enemy& enemy, float dx, float dy) {
    if (!AreEnemyHitboxesCollidingAfterDelta(enemy, dx, 0.0f, currentMapId_, currentScreenX_, currentScreenY_)) {
        enemy.bounds.x += dx;
    } else {
        enemy.velocity.x = 0.0f;
    }

    if (!AreEnemyHitboxesCollidingAfterDelta(enemy, 0.0f, dy, currentMapId_, currentScreenX_, currentScreenY_)) {
        enemy.bounds.y += dy;
    } else {
        enemy.velocity.y = 0.0f;
    }
}

float Game::ApplyPlayerAxisDeltaClamped(float delta, bool xAxis) {
    if (std::fabs(delta) <= 0.0001f) {
        return 0.0f;
    }

    const float direction = delta < 0.0f ? -1.0f : 1.0f;
    float remaining = std::fabs(delta);
    float applied = 0.0f;

    while (remaining > 0.0001f) {
        const float step = std::min(1.0f, remaining);
        SDL_FRect candidate = player_.bounds;
        if (xAxis) {
            candidate.x += direction * step;
        } else {
            candidate.y += direction * step;
        }

        if (IsPlayerHitboxCollidingAt(candidate, currentMapId_, currentScreenX_, currentScreenY_)) {
            break;
        }

        if (xAxis) {
            player_.bounds.x = candidate.x;
        } else {
            player_.bounds.y = candidate.y;
        }
        applied += step;
        remaining -= step;
    }

    return direction * applied;
}

float Game::ApplyEnemyAxisDeltaClamped(Enemy& enemy, float delta, bool xAxis) {
    if (std::fabs(delta) <= 0.0001f) {
        return 0.0f;
    }

    const float direction = delta < 0.0f ? -1.0f : 1.0f;
    float remaining = std::fabs(delta);
    float applied = 0.0f;

    while (remaining > 0.0001f) {
        const float step = std::min(1.0f, remaining);
        const float dx = xAxis ? direction * step : 0.0f;
        const float dy = xAxis ? 0.0f : direction * step;
        if (AreEnemyHitboxesCollidingAfterDelta(enemy, dx, dy, currentMapId_, currentScreenX_, currentScreenY_)) {
            break;
        }

        if (xAxis) {
            enemy.bounds.x += dx;
        } else {
            enemy.bounds.y += dy;
        }
        applied += step;
        remaining -= step;
    }

    return direction * applied;
}

void Game::ResolveKnockbackMovement(float dx, float dy) {
    const float appliedX = ApplyPlayerAxisDeltaClamped(dx, true);
    const float appliedY = ApplyPlayerAxisDeltaClamped(dy, false);
    if (std::fabs(appliedX - dx) > 0.001f) {
        player_.knockbackVelocity.x = 0.0f;
    }
    if (std::fabs(appliedY - dy) > 0.001f) {
        player_.knockbackVelocity.y = 0.0f;
    }
}

void Game::ResolveEnemyKnockbackMovement(Enemy& enemy, float dx, float dy) {
    const float appliedX = ApplyEnemyAxisDeltaClamped(enemy, dx, true);
    const float appliedY = ApplyEnemyAxisDeltaClamped(enemy, dy, false);
    if (std::fabs(appliedX - dx) > 0.001f) {
        enemy.knockbackVelocity.x = 0.0f;
    }
    if (std::fabs(appliedY - dy) > 0.001f) {
        enemy.knockbackVelocity.y = 0.0f;
    }
}

SDL_FPoint Game::DefaultArrivalPosition(TransitionDirection direction) const {
    const float kEdgeBuffer = 4.0f;
    const float kSouthArrivalInset = 12.0f;
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
            // Spawn slightly farther from the top edge to avoid immediate re-trigger of north transition.
            pos.y = kSouthArrivalInset;
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

void Game::TryDetectEdgeTrigger(float intendedDx, float intendedDy) {
    if (transitionPhase_ != TransitionPhase::None || transitionCooldownTimer_ > 0.0f) {
        return;
    }

    const float kTriggerBandWidth = 2.0f;  // Invisible trigger zone at screen edges
    const float kIntentEpsilon = 0.0001f;
    SDL_FRect projectedBounds = player_.bounds;
    projectedBounds.x += intendedDx;
    projectedBounds.y += intendedDy;
    const std::vector<SDL_FRect> projectedHitboxes = ActivePlayerHitboxesAt(projectedBounds);

    if (projectedHitboxes.empty()) {
        return;
    }

    float minX = projectedHitboxes.front().x;
    float minY = projectedHitboxes.front().y;
    float maxX = projectedHitboxes.front().x + projectedHitboxes.front().w;
    float maxY = projectedHitboxes.front().y + projectedHitboxes.front().h;
    for (const SDL_FRect& hb : projectedHitboxes) {
        minX = std::min(minX, hb.x);
        minY = std::min(minY, hb.y);
        maxX = std::max(maxX, hb.x + hb.w);
        maxY = std::max(maxY, hb.y + hb.h);
    }

    std::string targetMapId = currentMapId_;
    int targetSX = currentScreenX_;
    int targetSY = currentScreenY_;
    TransitionDirection direction = TransitionDirection::Down;
    std::string triggeredEdge;
    bool triggered = false;

    // Only trigger edges the player is actively moving toward.
    // This prevents immediate bounce-back transitions right after arriving on a screen.
    if (intendedDx < -kIntentEpsilon && minX <= kTriggerBandWidth) {
        triggeredEdge = "left";
        direction = TransitionDirection::Left;
        targetSX = currentScreenX_ - 1;
        triggered = true;
    }
    else if (intendedDx > kIntentEpsilon && maxX >= static_cast<float>(kScreenPixelWidth) - kTriggerBandWidth) {
        triggeredEdge = "right";
        direction = TransitionDirection::Right;
        targetSX = currentScreenX_ + 1;
        triggered = true;
    }
    else if (intendedDy < -kIntentEpsilon && minY <= kTriggerBandWidth) {
        triggeredEdge = "up";
        direction = TransitionDirection::Up;
        targetSY = currentScreenY_ - 1;
        triggered = true;
    }
    else if (intendedDy > kIntentEpsilon && maxY >= static_cast<float>(kScreenPixelHeight) - kTriggerBandWidth) {
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

    if (player_.knockbackTimer > 0.0f) {
        player_.knockbackTimer = std::max(0.0f, player_.knockbackTimer - dt);
        player_.moving = false;
        ResolveKnockbackMovement(player_.knockbackVelocity.x * dt, player_.knockbackVelocity.y * dt);
        TryUseWarpPoint();
        TryStartScreenTransition();
        UpdateCharacterAnimation(dt);
        previousWeaponAPressed_ = false;
        previousWeaponBPressed_ = false;
        return;
    }

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
    TryDetectEdgeTrigger(dx * distance, dy * distance);
    ResolveAxisMovement(dx * distance, dy * distance);
    TryUseWarpPoint();
    TryStartScreenTransition();

    UpdateCharacterAnimation(dt);

    if (!IsFirstVersionMode()) {
        const bool weaponAPressed = keys[SDL_SCANCODE_SPACE] || keys[SDL_SCANCODE_RETURN];
        const bool weaponBPressed = keys[SDL_SCANCODE_X] || keys[SDL_SCANCODE_RCTRL];

        if (weaponAPressed && !previousWeaponAPressed_ && !player_.attack.active && player_.attack.cooldownTimer <= 0.0f) {
            if (const WeaponDefinition* weapon = EquippedWeaponForSlotA()) {
                UseWeapon(*weapon);
            }
        }
        if (weaponBPressed && !previousWeaponBPressed_ && !player_.attack.active && player_.attack.cooldownTimer <= 0.0f) {
            if (const WeaponDefinition* weapon = EquippedWeaponForSlotB()) {
                UseWeapon(*weapon);
            }
        }

        previousWeaponAPressed_ = weaponAPressed;
        previousWeaponBPressed_ = weaponBPressed;
    } else {
        previousWeaponAPressed_ = false;
        previousWeaponBPressed_ = false;
    }
}

const WeaponDefinition* Game::FindWeaponDefinitionById(const std::string& weaponId) const {
    for (const WeaponDefinition& weapon : world_.WeaponDefinitions()) {
        if (weapon.id == weaponId) {
            return &weapon;
        }
    }
    return nullptr;
}

const ProjectileDefinition* Game::FindProjectileDefinitionById(const std::string& projectileId) const {
    for (const ProjectileDefinition& projectile : world_.ProjectileDefinitions()) {
        if (projectile.id == projectileId) {
            return &projectile;
        }
    }
    return nullptr;
}

const WeaponDefinition* Game::EquippedWeaponForSlotA() const {
    if (const WeaponDefinition* equipped = FindWeaponDefinitionById(equippedWeaponAId_)) {
        return equipped;
    }
    const auto& weaponDefinitions = world_.WeaponDefinitions();
    return weaponDefinitions.empty() ? nullptr : &weaponDefinitions.front();
}

const WeaponDefinition* Game::EquippedWeaponForSlotB() const {
    if (const WeaponDefinition* equipped = FindWeaponDefinitionById(equippedWeaponBId_)) {
        return equipped;
    }
    const auto& weaponDefinitions = world_.WeaponDefinitions();
    if (weaponDefinitions.size() > 1) {
        return &weaponDefinitions[1];
    }
    return weaponDefinitions.empty() ? nullptr : &weaponDefinitions.front();
}

void Game::UseWeapon(const WeaponDefinition& weapon) {
    player_.attack.active = true;
    player_.attack.activeTimer = player_.attack.activeDuration;
    player_.attack.cooldownTimer = player_.attack.cooldownDuration;
    player_.attack.damage = std::max(0, weapon.damage);

    activeWeaponActionId_ = weapon.id;
    weaponVisualTimer_ = player_.attack.activeDuration;

    if (const CharacterSpriteset* spriteset = world_.ActiveCharacterSpriteset()) {
        for (const CharacterAction& action : spriteset->actions) {
            if (action.id != activeWeaponActionId_) {
                continue;
            }
            const int directionIndex = std::clamp(static_cast<int>(player_.facing), 0, 3);
            const std::vector<CharacterFrame>& frames = action.directionalFrames[static_cast<size_t>(directionIndex)];
            if (!frames.empty()) {
                const float speed = std::max(0.1f, action.animationSpeed);
                weaponVisualTimer_ = std::max(weaponVisualTimer_, static_cast<float>(frames.size()) / speed);
            }
            break;
        }
    }

    if (weapon.isProjectile) {
        const ProjectileDefinition* projectileDefinition = FindProjectileDefinitionById(weapon.projectileDefinitionId);
        if (!projectileDefinition) {
            const auto& projectileDefinitions = world_.ProjectileDefinitions();
            projectileDefinition = projectileDefinitions.empty() ? nullptr : &projectileDefinitions.front();
        }
        if (!projectileDefinition) {
            return;
        }

        SDL_FPoint direction{0.0f, 1.0f};
        if (player_.facing == Direction::Up) {
            direction = SDL_FPoint{0.0f, -1.0f};
        } else if (player_.facing == Direction::Left) {
            direction = SDL_FPoint{-1.0f, 0.0f};
        } else if (player_.facing == Direction::Right) {
            direction = SDL_FPoint{1.0f, 0.0f};
        }

        const std::vector<SDL_FRect> playerHitboxes = ActivePlayerHitboxesAt(player_.bounds);
        SDL_FRect emitBounds = player_.bounds;
        if (!playerHitboxes.empty()) {
            float minX = playerHitboxes.front().x;
            float minY = playerHitboxes.front().y;
            float maxX = playerHitboxes.front().x + playerHitboxes.front().w;
            float maxY = playerHitboxes.front().y + playerHitboxes.front().h;
            for (const SDL_FRect& hb : playerHitboxes) {
                minX = std::min(minX, hb.x);
                minY = std::min(minY, hb.y);
                maxX = std::max(maxX, hb.x + hb.w);
                maxY = std::max(maxY, hb.y + hb.h);
            }
            emitBounds = SDL_FRect{minX, minY, std::max(1.0f, maxX - minX), std::max(1.0f, maxY - minY)};
        }

        const SDL_FPoint facingDirection{
            player_.facing == Direction::Left ? -1.0f : player_.facing == Direction::Right ? 1.0f : 0.0f,
            player_.facing == Direction::Up ? -1.0f : player_.facing == Direction::Down ? 1.0f : 0.0f
        };
        const SDL_FPoint spawn = ProjectileLaunchPointForDefinition(emitBounds, *projectileDefinition, facingDirection);
        SpawnProjectile(*projectileDefinition, ProjectileOwner::Player, spawn, direction);
        player_.attack.hitbox = SDL_FRect{0.0f, 0.0f, 0.0f, 0.0f};
        return;
    }

    if (const CharacterSpriteset* spriteset = world_.ActiveCharacterSpriteset()) {
        for (const CharacterAction& action : spriteset->actions) {
            if (action.id != activeWeaponActionId_) {
                continue;
            }

            const int directionIndex = std::clamp(static_cast<int>(player_.facing), 0, 3);
            const auto& directionalHitboxes = action.directionalHitboxes[static_cast<size_t>(directionIndex)];
            if (!directionalHitboxes.empty()) {
                const std::vector<CharacterFrame>& frames = action.directionalFrames[static_cast<size_t>(directionIndex)];
                const CharacterFrame* frame = frames.empty() ? nullptr : &frames.front();
                SDL_FRect spriteRect = player_.bounds;
                if (frame) {
                    const float spriteW = static_cast<float>(frame->frameWidth * spriteset->tileWidth);
                    const float spriteH = static_cast<float>(frame->frameHeight * spriteset->tileHeight);
                    spriteRect = SDL_FRect{
                        player_.bounds.x + (player_.bounds.w * 0.5f) - (spriteW * 0.5f),
                        player_.bounds.y + player_.bounds.h - spriteH,
                        spriteW,
                        spriteH
                    };
                }

                bool first = true;
                SDL_FRect aggregate{};
                for (const TileHitbox& hb : directionalHitboxes) {
                    if (hb.w <= 0 || hb.h <= 0) {
                        continue;
                    }
                    const SDL_FRect worldHb{
                        spriteRect.x + static_cast<float>(hb.x),
                        spriteRect.y + static_cast<float>(hb.y),
                        static_cast<float>(hb.w),
                        static_cast<float>(hb.h)
                    };
                    if (first) {
                        aggregate = worldHb;
                        first = false;
                    } else {
                        const float minX = std::min(aggregate.x, worldHb.x);
                        const float minY = std::min(aggregate.y, worldHb.y);
                        const float maxX = std::max(aggregate.x + aggregate.w, worldHb.x + worldHb.w);
                        const float maxY = std::max(aggregate.y + aggregate.h, worldHb.y + worldHb.h);
                        aggregate = SDL_FRect{minX, minY, maxX - minX, maxY - minY};
                    }
                }

                if (!first) {
                    player_.attack.hitbox = aggregate;
                    return;
                }
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
    weaponVisualTimer_ = std::max(0.0f, weaponVisualTimer_ - dt);

    std::string nextAction = "standing";
    if (!IsFirstVersionMode() && player_.knockbackTimer > 0.0f) {
        nextAction = "knockback";
    } else if (!IsFirstVersionMode() && weaponVisualTimer_ > 0.0f && !activeWeaponActionId_.empty()) {
        nextAction = activeWeaponActionId_;
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
        if (!activeWeaponActionId_.empty() && activeActionId_ == activeWeaponActionId_) {
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
        player_.health = std::min(player_.maxHealth, player_.health + std::max(1, def.magnitude));
    } else if (def.effect == "heal") {
        player_.health = std::min(player_.maxHealth, player_.health + std::max(1, def.magnitude));
    }
}

void Game::ApplyItemTrigger(const Item& item) {
    switch (item.triggerFunction) {
        case ItemTriggerFunction::IncreaseCoins:
            coins_ += std::max(0, ItemIntParam(item, "amount", 1));
            break;
        case ItemTriggerFunction::IncreaseHealth:
            player_.health = std::min(player_.maxHealth, player_.health + std::max(0, ItemIntParam(item, "amount", 1)));
            break;
        case ItemTriggerFunction::IncreaseMaxHealth: {
            const int amount = std::max(0, ItemIntParam(item, "amount", 4));
            player_.maxHealth = std::max(player_.maxHealth, player_.maxHealth + amount);
            player_.health = std::min(player_.maxHealth, player_.health + amount);
            break;
        }
        case ItemTriggerFunction::ApplySpeedBoost: {
            speedBuffMagnitude_ = std::max(speedBuffMagnitude_, std::max(0, ItemIntParam(item, "amount", 0)));
            speedBuffTimer_ = std::max(speedBuffTimer_, std::max(0.0f, ItemFloatParam(item, "duration", 0.0f)));
            player_.speedPixelsPerSecond = player_.baseSpeedPixelsPerSecond + static_cast<float>(speedBuffMagnitude_);
            break;
        }
        case ItemTriggerFunction::None:
        default:
            break;
    }
}

void Game::ApplyPlayerDamage(int damage, const SDL_FPoint& knockbackDirection) {
    player_.health = std::max(0, player_.health - std::max(0, damage));
    player_.invulnTimer = std::max(0.0f, world_.Settings().invulnerabilitySeconds);
    const SDL_FPoint cardinal = CardinalDirectionFromVector(knockbackDirection);
    constexpr float kKnockbackMoveSeconds = 0.12f;
    const float distancePixels = std::max(0.0f, world_.Settings().knockbackDistanceTiles) * static_cast<float>(kTileSize);
    player_.knockbackVelocity = SDL_FPoint{cardinal.x * distancePixels / kKnockbackMoveSeconds, cardinal.y * distancePixels / kKnockbackMoveSeconds};
    player_.knockbackTimer = distancePixels > 0.0f ? kKnockbackMoveSeconds : 0.0f;
}

void Game::ApplyEnemyDamage(Enemy& enemy, int damage, const SDL_FPoint& knockbackDirection) {
    enemy.health -= std::max(0, damage);
    enemy.invulnTimer = std::max(0.0f, world_.Settings().invulnerabilitySeconds);
    enemy.knockbackDirection = enemy.moveDirection;
    const SDL_FPoint cardinal = CardinalDirectionFromVector(knockbackDirection);
    constexpr float kKnockbackMoveSeconds = 0.12f;
    const float distancePixels = std::max(0.0f, world_.Settings().knockbackDistanceTiles) * static_cast<float>(kTileSize);
    
    if (!enemy.immuneToKnockback) {
        enemy.knockbackVelocity = SDL_FPoint{cardinal.x * distancePixels / kKnockbackMoveSeconds, cardinal.y * distancePixels / kKnockbackMoveSeconds};
        enemy.knockbackTimer = distancePixels > 0.0f ? kKnockbackMoveSeconds : 0.0f;
    }
    
    enemy.knockbackAnimationTimer = 0.0f;
    enemy.knockbackAnimationFrame = 0;
    if (enemy.health <= 0) {
        enemy.deathAnimationPlaying = true;
        enemy.deathAnimationTimer = 0.0f;
        enemy.deathAnimationFrame = 0;
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
        if (!enemy.alive || enemy.disappeared || enemy.mapId != currentMapId_ || enemy.screenX != currentScreenX_ || enemy.screenY != currentScreenY_) {
            continue;
        }

        if (enemy.invulnTimer > 0.0f) {
            enemy.invulnTimer = std::max(0.0f, enemy.invulnTimer - dt);
        }

        if (player_.attack.active && enemy.invulnTimer <= 0.0f) {
            bool hitEnemy = false;
            for (const SDL_FRect& enemyHitbox : ActiveEnemyHitboxesAt(enemy)) {
                if (Intersects(player_.attack.hitbox, enemyHitbox)) {
                    hitEnemy = true;
                    break;
                }
            }

            if (hitEnemy) {
                SDL_FPoint knockbackDirection{0.0f, 1.0f};
                if (player_.facing == Direction::Up) {
                    knockbackDirection = SDL_FPoint{0.0f, -1.0f};
                } else if (player_.facing == Direction::Left) {
                    knockbackDirection = SDL_FPoint{-1.0f, 0.0f};
                } else if (player_.facing == Direction::Right) {
                    knockbackDirection = SDL_FPoint{1.0f, 0.0f};
                }
                ApplyEnemyDamage(enemy, player_.attack.damage, knockbackDirection);
            }
        }

        if (player_.invulnTimer <= 0.0f) {
            bool enemyTouchedPlayer = false;
            for (const SDL_FRect& enemyHitbox : ActiveEnemyHitboxesAt(enemy)) {
                if (PlayerIntersects(enemyHitbox)) {
                    enemyTouchedPlayer = true;
                    break;
                }
            }
            if (enemyTouchedPlayer) {
                SDL_FPoint knockbackDirection{
                    (player_.bounds.x + player_.bounds.w * 0.5f) - (enemy.bounds.x + enemy.bounds.w * 0.5f),
                    (player_.bounds.y + player_.bounds.h * 0.5f) - (enemy.bounds.y + enemy.bounds.h * 0.5f)
                };
                ApplyPlayerDamage(std::max(0, enemy.baseDamage), knockbackDirection);
            }
        }
    }
}

void Game::UpdateEnemies(float dt) {
    static std::mt19937 rng(1337);
    std::uniform_int_distribution<int> randomTileX(0, kTilesWide - 1);
    std::uniform_int_distribution<int> randomTileY(0, kTilesHigh - 1);

    for (Enemy& enemy : world_.Enemies()) {
        if (!enemy.alive || enemy.mapId != currentMapId_ || enemy.screenX != currentScreenX_ || enemy.screenY != currentScreenY_) {
            continue;
        }

        if (enemy.deathAnimationPlaying) {
            const EnemyReactionAnimation& deathAnim = enemy.deathAnimation;
            const auto* deathFrames = EnemyFramesForDirection(deathAnim, enemy.moveDirection);
            const bool hasFrames = deathFrames && !deathFrames->empty() && deathAnim.animationSpeed > 0.0f;
            if (hasFrames) {
                enemy.deathAnimationTimer += dt;
                const float frameDuration = 1.0f / std::max(0.1f, deathAnim.animationSpeed);
                const int lastFrame = static_cast<int>(deathFrames->size()) - 1;
                while (enemy.deathAnimationTimer >= frameDuration) {
                    enemy.deathAnimationTimer -= frameDuration;
                    if (enemy.deathAnimationFrame < lastFrame) {
                        enemy.deathAnimationFrame += 1;
                    } else {
                        enemy.alive = false;
                        enemy.deathAnimationPlaying = false;
                        break;
                    }
                }
            } else {
                enemy.alive = false;
                enemy.deathAnimationPlaying = false;
            }
            continue;
        }

        if (enemy.knockbackTimer > 0.0f) {
            enemy.knockbackTimer = std::max(0.0f, enemy.knockbackTimer - dt);
            ResolveEnemyKnockbackMovement(enemy, enemy.knockbackVelocity.x * dt, enemy.knockbackVelocity.y * dt);
            const auto* knockbackFrames = EnemyFramesForDirection(enemy.knockbackAnimation, enemy.knockbackDirection);
            if (knockbackFrames && !knockbackFrames->empty() && enemy.knockbackAnimation.animationSpeed > 0.0f) {
                enemy.knockbackAnimationTimer += dt;
                const float frameDuration = 1.0f / std::max(0.1f, enemy.knockbackAnimation.animationSpeed);
                while (enemy.knockbackAnimationTimer >= frameDuration) {
                    enemy.knockbackAnimationTimer -= frameDuration;
                    enemy.knockbackAnimationFrame = (enemy.knockbackAnimationFrame + 1) % static_cast<int>(knockbackFrames->size());
                }
            } else {
                enemy.knockbackAnimationFrame = 0;
            }
            continue;
        }

        const EnemyMoveDefinition* move = ActiveEnemyMoveDefinition(enemy);
        if (move) {
            auto advanceToNextMove = [&enemy]() {
                enemy.disappeared = false;
                enemy.disappearPhase = Enemy::DisappearPhase::None;
                enemy.currentMoveIndex = (enemy.currentMoveIndex + 1) % static_cast<int>(enemy.moves.size());
                enemy.moveInitialized = false;
                enemy.moveTimer = 0.0f;
                enemy.animationTimer = 0.0f;
                enemy.moveProjectileSpawned = false;
            };

            auto enemyEmitBounds = [this, &enemy]() {
                SDL_FRect emitBounds = enemy.bounds;
                const std::vector<SDL_FRect> enemyHitboxes = ActiveEnemyHitboxesAt(enemy);
                if (!enemyHitboxes.empty()) {
                    float minX = enemyHitboxes.front().x;
                    float minY = enemyHitboxes.front().y;
                    float maxX = enemyHitboxes.front().x + enemyHitboxes.front().w;
                    float maxY = enemyHitboxes.front().y + enemyHitboxes.front().h;
                    for (const SDL_FRect& hb : enemyHitboxes) {
                        minX = std::min(minX, hb.x);
                        minY = std::min(minY, hb.y);
                        maxX = std::max(maxX, hb.x + hb.w);
                        maxY = std::max(maxY, hb.y + hb.h);
                    }
                    emitBounds = SDL_FRect{minX, minY, std::max(1.0f, maxX - minX), std::max(1.0f, maxY - minY)};
                }
                return emitBounds;
            };

            if (!enemy.moveInitialized) {
                enemy.moveDuration = RandomEnemyMoveSeconds(rng, move->minSeconds, move->maxSeconds);
                enemy.moveTimer = enemy.moveDuration;
                enemy.moveInitialized = true;
                enemy.animationTimer = 0.0f;
                enemy.animationFrame = 0;
                enemy.moveProjectileSpawned = false;
                enemy.disappearPhase = Enemy::DisappearPhase::None;

                if (move->type == EnemyMoveType::MoveRandomDirection) {
                    const SDL_FPoint dir = RandomCardinalDirection(rng);
                    const float speedPixelsPerSecond = std::max(0.0f, move->speedTilesPerSecond) * static_cast<float>(kTileSize);
                    enemy.velocity.x = dir.x * speedPixelsPerSecond;
                    enemy.velocity.y = dir.y * speedPixelsPerSecond;
                    enemy.moveDirection = MoveDirectionIndexFromVector(dir);
                    enemy.disappeared = false;
                } else if (move->type == EnemyMoveType::Disappear) {
                    enemy.disappearOrigin = SDL_FPoint{enemy.bounds.x, enemy.bounds.y};
                    enemy.velocity.x = 0.0f;
                    enemy.velocity.y = 0.0f;
                    if (EnemyMoveHasPlayableAnimation(*move) && EnemyMoveFrameCount(*move, enemy.moveDirection) > 0) {
                        enemy.disappearPhase = Enemy::DisappearPhase::PreDisappear;
                        enemy.disappeared = false;
                        enemy.animationFrame = 0;
                    } else {
                        enemy.disappearPhase = Enemy::DisappearPhase::Hidden;
                        enemy.disappeared = true;
                    }
                } else if (move->type == EnemyMoveType::FireProjectile) {
                    enemy.velocity.x = 0.0f;
                    enemy.velocity.y = 0.0f;
                    enemy.disappeared = false;
                    const SDL_FRect emitBounds = enemyEmitBounds();
                    SDL_FPoint direction{
                        (player_.bounds.x + player_.bounds.w * 0.5f) - (emitBounds.x + emitBounds.w * 0.5f),
                        (player_.bounds.y + player_.bounds.h * 0.5f) - (emitBounds.y + emitBounds.h * 0.5f)
                    };
                    const SDL_FPoint cardinal = CardinalDirectionFromVector(direction);
                    enemy.moveDirection = MoveDirectionIndexFromVector(cardinal);
                    enemy.fireDirection = cardinal;
                } else {
                    enemy.velocity.x = 0.0f;
                    enemy.velocity.y = 0.0f;
                    enemy.disappeared = false;
                }

                if (move->type == EnemyMoveType::FireProjectile) {
                    const int frameCount = std::max(0, EnemyMoveFrameCount(*move, enemy.moveDirection));
                    const float animationSpeed = std::max(0.0f, move->animationSpeed);
                    const float animationDuration = (frameCount > 0 && animationSpeed > 0.0f)
                        ? static_cast<float>(frameCount) / animationSpeed
                        : 0.0f;
                    enemy.moveDuration = std::max(enemy.moveDuration, animationDuration);
                }
            }

            if (move->type == EnemyMoveType::Disappear) {
                const bool hasAnimation = EnemyMoveHasPlayableAnimation(*move) && EnemyMoveFrameCount(*move, enemy.moveDirection) > 0;
                const float frameDuration = hasAnimation ? (1.0f / std::max(0.1f, move->animationSpeed)) : 0.0f;
                const int lastFrame = std::max(0, EnemyMoveFrameCount(*move, enemy.moveDirection) - 1);

                if (enemy.disappearPhase == Enemy::DisappearPhase::PreDisappear) {
                    if (!hasAnimation) {
                        enemy.disappearPhase = Enemy::DisappearPhase::Hidden;
                        enemy.disappeared = true;
                    } else {
                        enemy.animationTimer += dt;
                        while (enemy.animationTimer >= frameDuration) {
                            enemy.animationTimer -= frameDuration;
                            if (enemy.animationFrame < lastFrame) {
                                enemy.animationFrame += 1;
                            } else {
                                enemy.disappearPhase = Enemy::DisappearPhase::Hidden;
                                enemy.disappeared = true;
                                enemy.moveTimer = enemy.moveDuration;
                                break;
                            }
                        }
                    }
                } else if (enemy.disappearPhase == Enemy::DisappearPhase::Hidden) {
                    enemy.moveTimer = std::max(0.0f, enemy.moveTimer - dt);
                    if (enemy.moveTimer <= 0.0f) {
                        if (move->reappearMode == EnemyReappearMode::RandomPosition) {
                            bool placed = false;
                            for (int i = 0; i < 64; ++i) {
                                const int tx = randomTileX(rng);
                                const int ty = randomTileY(rng);
                                if (world_.IsTileSolid(currentMapId_, currentScreenX_, currentScreenY_, tx, ty)) {
                                    continue;
                                }
                                enemy.bounds.x = static_cast<float>(tx * kTileSize);
                                enemy.bounds.y = static_cast<float>(ty * kTileSize);
                                placed = true;
                                break;
                            }
                            if (!placed) {
                                enemy.bounds.x = enemy.disappearOrigin.x;
                                enemy.bounds.y = enemy.disappearOrigin.y;
                            }
                        } else {
                            enemy.bounds.x = enemy.disappearOrigin.x;
                            enemy.bounds.y = enemy.disappearOrigin.y;
                        }

                        enemy.disappeared = false;
                        if (hasAnimation) {
                            enemy.disappearPhase = Enemy::DisappearPhase::Reappear;
                            enemy.animationFrame = lastFrame;
                            enemy.animationTimer = 0.0f;
                        } else {
                            advanceToNextMove();
                        }
                    }
                } else if (enemy.disappearPhase == Enemy::DisappearPhase::Reappear) {
                    if (!hasAnimation) {
                        advanceToNextMove();
                    } else {
                        enemy.animationTimer += dt;
                        while (enemy.animationTimer >= frameDuration) {
                            enemy.animationTimer -= frameDuration;
                            if (enemy.animationFrame > 0) {
                                enemy.animationFrame -= 1;
                            } else {
                                advanceToNextMove();
                                break;
                            }
                        }
                    }
                }

                continue;
            }

            enemy.moveTimer = std::max(0.0f, enemy.moveTimer - dt);

            if (move->type == EnemyMoveType::MoveRandomDirection) {
                const float dx = enemy.velocity.x * dt;
                const float dy = enemy.velocity.y * dt;
                const float beforeX = enemy.bounds.x;
                const float beforeY = enemy.bounds.y;
                ResolveEnemyAxisMovement(enemy, dx, dy);
                constexpr float kMoveEpsilon = 0.0001f;
                const bool attemptedMovement = std::fabs(dx) > kMoveEpsilon || std::fabs(dy) > kMoveEpsilon;
                const bool movedX = std::fabs(enemy.bounds.x - beforeX) > kMoveEpsilon;
                const bool movedY = std::fabs(enemy.bounds.y - beforeY) > kMoveEpsilon;
                if (attemptedMovement && !movedX && !movedY) {
                    std::vector<SDL_FPoint> validDirections;
                    const SDL_FPoint cardinalDirs[] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
                    for (const SDL_FPoint& dir : cardinalDirs) {
                        const float testSpeed = std::max(0.0f, move->speedTilesPerSecond) * static_cast<float>(kTileSize);
                        const float testDx = dir.x * testSpeed * 0.016f;
                        const float testDy = dir.y * testSpeed * 0.016f;
                        SDL_FRect testBounds = enemy.bounds;
                        testBounds.x += testDx;
                        testBounds.y += testDy;
                        if (!AreEnemyHitboxesCollidingAfterDelta(enemy, testDx, testDy, currentMapId_, currentScreenX_, currentScreenY_)) {
                            validDirections.push_back(dir);
                        }
                    }
                    if (!validDirections.empty()) {
                        std::uniform_int_distribution<size_t> selectDir(0, validDirections.size() - 1);
                        const SDL_FPoint newDir = validDirections[selectDir(rng)];
                        const float speedPixelsPerSecond = std::max(0.0f, move->speedTilesPerSecond) * static_cast<float>(kTileSize);
                        enemy.velocity.x = newDir.x * speedPixelsPerSecond;
                        enemy.velocity.y = newDir.y * speedPixelsPerSecond;
                        enemy.moveDirection = MoveDirectionIndexFromVector(newDir);
                    } else {
                        enemy.velocity.x = 0.0f;
                        enemy.velocity.y = 0.0f;
                        enemy.moveTimer = 0.0f;
                    }
                }
            }

            if (move->type == EnemyMoveType::FireProjectile) {
                const int frameCount = EnemyMoveFrameCount(*move, enemy.moveDirection);
                const bool hasAnimation = EnemyMoveHasPlayableAnimation(*move) && frameCount > 0;
                if (hasAnimation) {
                    enemy.animationTimer += dt;
                    const float frameDuration = 1.0f / std::max(0.1f, move->animationSpeed);
                    const int lastFrame = std::max(0, frameCount - 1);

                    while (enemy.animationTimer >= frameDuration && enemy.animationFrame < lastFrame) {
                        enemy.animationTimer -= frameDuration;
                        enemy.animationFrame += 1;
                    }

                    if (!enemy.moveProjectileSpawned && enemy.animationFrame >= lastFrame && enemy.animationTimer >= frameDuration) {
                        const ProjectileDefinition* projectileDefinition = nullptr;
                        for (const ProjectileDefinition& definition : world_.ProjectileDefinitions()) {
                            if (!move->projectileDefinitionId.empty() && definition.id == move->projectileDefinitionId) {
                                projectileDefinition = &definition;
                                break;
                            }
                        }
                        if (!projectileDefinition && !world_.ProjectileDefinitions().empty()) {
                            projectileDefinition = &world_.ProjectileDefinitions().front();
                        }
                        if (projectileDefinition) {
                            const SDL_FRect emitBounds = enemyEmitBounds();
                            const SDL_FPoint spawn = ProjectileLaunchPointForDefinition(emitBounds, *projectileDefinition, enemy.fireDirection);
                            SpawnProjectile(*projectileDefinition, ProjectileOwner::Enemy, spawn, enemy.fireDirection);
                        }
                        enemy.moveProjectileSpawned = true;
                    }
                } else if (!enemy.moveProjectileSpawned) {
                    const ProjectileDefinition* projectileDefinition = nullptr;
                    for (const ProjectileDefinition& definition : world_.ProjectileDefinitions()) {
                        if (!move->projectileDefinitionId.empty() && definition.id == move->projectileDefinitionId) {
                            projectileDefinition = &definition;
                            break;
                        }
                    }
                    if (!projectileDefinition && !world_.ProjectileDefinitions().empty()) {
                        projectileDefinition = &world_.ProjectileDefinitions().front();
                    }
                    if (projectileDefinition) {
                        const SDL_FRect emitBounds = enemyEmitBounds();
                        const SDL_FPoint spawn = ProjectileLaunchPointForDefinition(emitBounds, *projectileDefinition, enemy.fireDirection);
                        SpawnProjectile(*projectileDefinition, ProjectileOwner::Enemy, spawn, enemy.fireDirection);
                    }
                    enemy.moveProjectileSpawned = true;
                }
            } else if (EnemyMoveHasPlayableAnimation(*move)) {
                enemy.animationTimer += dt;
                const float frameDuration = 1.0f / std::max(0.1f, move->animationSpeed);
                while (enemy.animationTimer >= frameDuration) {
                    enemy.animationTimer -= frameDuration;
                    enemy.animationFrame = (enemy.animationFrame + 1) % std::max(1, EnemyMoveFrameCount(*move, enemy.moveDirection));
                }
            } else {
                enemy.animationFrame = 0;
            }

            if (enemy.moveTimer <= 0.0f) {
                advanceToNextMove();
            }

            if (!enemy.disappeared) {
#if 0
                enemy.projectileCooldownTimer = std::max(0.0f, enemy.projectileCooldownTimer - dt);
                const auto& projectileDefinitions = world_.ProjectileDefinitions();
                if (enemy.projectileCooldownTimer <= 0.0f && !projectileDefinitions.empty()) {
                    SDL_FPoint direction{
                        (player_.bounds.x + player_.bounds.w * 0.5f) - (enemy.bounds.x + enemy.bounds.w * 0.5f),
                        (player_.bounds.y + player_.bounds.h * 0.5f) - (enemy.bounds.y + enemy.bounds.h * 0.5f)
                    };
                    const float len = std::sqrt(direction.x * direction.x + direction.y * direction.y);
                    if (len > 0.0001f) {
                        direction.x /= len;
                        direction.y /= len;
                    } else {
                        direction = SDL_FPoint{0.0f, 1.0f};
                    }
                    const SDL_FPoint spawn{
                        enemy.bounds.x + enemy.bounds.w * 0.5f,
                        enemy.bounds.y + enemy.bounds.h * 0.5f
                    };
                    SpawnProjectile(projectileDefinitions.front(), ProjectileOwner::Enemy, spawn, direction);
                    enemy.projectileCooldownTimer = projectileCooldownDist(rng);
                }
#endif
            }

            continue;
        }

        // Legacy fallback behavior for worlds that still use old enemy schema.
        if (enemy.behavior == "static") {
            enemy.velocity.x = 0.0f;
            enemy.velocity.y = 0.0f;
        } else {
            enemy.directionTimer -= dt;
            if (enemy.directionTimer <= 0.0f) {
                const SDL_FPoint dir = RandomCardinalDirection(rng);
                enemy.velocity.x = dir.x * std::max(0.0f, enemy.speed);
                enemy.velocity.y = dir.y * std::max(0.0f, enemy.speed);
                enemy.moveDirection = MoveDirectionIndexFromVector(dir);
                enemy.directionTimer = 1.2f;
            }
        }

        const float beforeX = enemy.bounds.x;
        const float beforeY = enemy.bounds.y;
        const float dx = enemy.velocity.x * dt;
        const float dy = enemy.velocity.y * dt;
        ResolveEnemyAxisMovement(enemy, dx, dy);
        constexpr float kMoveEpsilon = 0.0001f;
        const bool attemptedMovement = std::fabs(dx) > kMoveEpsilon || std::fabs(dy) > kMoveEpsilon;
        const bool movedX = std::fabs(enemy.bounds.x - beforeX) > kMoveEpsilon;
        const bool movedY = std::fabs(enemy.bounds.y - beforeY) > kMoveEpsilon;
        if (attemptedMovement && !movedX && !movedY) {
            enemy.velocity.x = 0.0f;
            enemy.velocity.y = 0.0f;
        }

        // Enemy projectile spawning is disabled so bullets are player-fired only.
    }
}

void Game::SpawnProjectile(const ProjectileDefinition& definition, ProjectileOwner owner, const SDL_FPoint& spawnPos, const SDL_FPoint& initialDirection) {
    Projectile projectile;
    projectile.projectileId = definition.id;
    projectile.name = definition.name;
    projectile.mapId = currentMapId_;
    projectile.screenX = currentScreenX_;
    projectile.screenY = currentScreenY_;
    projectile.owner = owner;
    projectile.movementType = definition.movementType;
    projectile.speedPixelsPerSecond = std::max(0.0f, definition.speedTilesPerSecond) * static_cast<float>(kTileSize);
    projectile.fixedFunctionA = definition.fixedFunctionA;
    projectile.limitedDistancePixels = std::max(0.0f, definition.limitedDistanceTiles) * static_cast<float>(kTileSize);
    projectile.limitedDurationSeconds = std::max(0.0f, definition.limitedDurationSeconds);
    projectile.lifetimeTimer = 0.0f;
    projectile.traveledDistancePixels = 0.0f;
    projectile.trackCorrectionDistanceAccumulator = 0.0f;
    projectile.movementStopped = false;
    projectile.damageConsumed = false;
    projectile.impactAnimationFinished = false;
    projectile.moveThroughSolid = definition.moveThroughSolid;
    projectile.baseDamage = std::max(0, definition.baseDamage);
    projectile.hitboxes = definition.hitboxes;
    projectile.startFrames = definition.startFrames;
    projectile.flightFrames = definition.flightFrames;
    projectile.impactFrames = definition.impactFrames;
    projectile.startAnimationSpeed = definition.startAnimationSpeed;
    projectile.flightAnimationSpeed = definition.flightAnimationSpeed;
    projectile.impactAnimationSpeed = definition.impactAnimationSpeed;
    projectile.phase = projectile.startFrames.empty() ? Projectile::Phase::Flight : Projectile::Phase::Start;

    const auto* initialFrames = ProjectileFramesForPhase(projectile);
    if (initialFrames && !initialFrames->empty()) {
        projectile.bounds.w = static_cast<float>(std::max(1, (*initialFrames)[0].sourceW));
        projectile.bounds.h = static_cast<float>(std::max(1, (*initialFrames)[0].sourceH));
    } else if (!projectile.hitboxes.empty()) {
        projectile.bounds.w = static_cast<float>(std::max(1, projectile.hitboxes.front().w));
        projectile.bounds.h = static_cast<float>(std::max(1, projectile.hitboxes.front().h));
    }
    projectile.bounds.x = spawnPos.x - projectile.bounds.w * 0.5f;
    projectile.bounds.y = spawnPos.y - projectile.bounds.h * 0.5f;

    SDL_FPoint direction = initialDirection;
    const float directionLen = std::sqrt(direction.x * direction.x + direction.y * direction.y);
    if (directionLen > 0.0001f) {
        direction.x /= directionLen;
        direction.y /= directionLen;
    } else {
        direction = SDL_FPoint{1.0f, 0.0f};
    }

    if (projectile.movementType == ProjectileMovementType::FixedFunction) {
        if (std::fabs(direction.x) >= std::fabs(direction.y)) {
            projectile.velocity = SDL_FPoint{direction.x < 0.0f ? -1.0f : 1.0f, 0.0f};
        } else {
            projectile.velocity = SDL_FPoint{0.0f, direction.y < 0.0f ? -1.0f : 1.0f};
        }
    } else if (projectile.movementType == ProjectileMovementType::StraightLimitedDistance) {
        if (std::fabs(direction.x) >= std::fabs(direction.y)) {
            projectile.velocity = SDL_FPoint{direction.x < 0.0f ? -1.0f : 1.0f, 0.0f};
        } else {
            projectile.velocity = SDL_FPoint{0.0f, direction.y < 0.0f ? -1.0f : 1.0f};
        }
    } else {
        projectile.velocity = direction;
    }

    projectiles_.push_back(projectile);
}

void Game::UpdateProjectiles(float dt) {
    auto beginImpact = [](Projectile& projectile) {
        if (projectile.impactFrames.empty()) {
            projectile.impactAnimationFinished = true;
            if (projectile.movementType == ProjectileMovementType::StraightLimitedDistance) {
                projectile.movementStopped = true;
            } else {
                projectile.phase = Projectile::Phase::Done;
                projectile.alive = false;
            }
            return;
        }
        projectile.phase = Projectile::Phase::Impact;
        projectile.animationFrame = 0;
        projectile.animationTimer = 0.0f;
        projectile.impactAnimationFinished = false;
    };

    for (Projectile& projectile : projectiles_) {
        if (!projectile.alive || projectile.mapId != currentMapId_ || projectile.screenX != currentScreenX_ || projectile.screenY != currentScreenY_) {
            continue;
        }

        if (projectile.movementType == ProjectileMovementType::StraightLimitedDistance || projectile.movementType == ProjectileMovementType::Homing) {
            projectile.lifetimeTimer += dt;
            if (projectile.lifetimeTimer >= projectile.limitedDurationSeconds) {
                projectile.phase = Projectile::Phase::Done;
                projectile.alive = false;
                continue;
            }
        }

        if (projectile.phase == Projectile::Phase::Impact) {
            const auto* impactFrames = ProjectileFramesForPhase(projectile);
            if (!impactFrames || impactFrames->empty()) {
                projectile.phase = Projectile::Phase::Done;
                projectile.alive = false;
                continue;
            }

            if (projectile.movementType == ProjectileMovementType::StraightLimitedDistance && projectile.impactAnimationFinished) {
                continue;
            }

            const float authoredSpeed = ProjectileAnimationSpeedForPhase(projectile);
            const float speed = authoredSpeed > 0.0f ? authoredSpeed : 12.0f;
            projectile.animationTimer += dt;
            const float frameDuration = 1.0f / std::max(0.1f, speed);
            while (projectile.animationTimer >= frameDuration) {
                projectile.animationTimer -= frameDuration;
                if (projectile.animationFrame + 1 < static_cast<int>(impactFrames->size())) {
                    projectile.animationFrame += 1;
                } else {
                    if (projectile.movementType == ProjectileMovementType::StraightLimitedDistance) {
                        projectile.animationFrame = static_cast<int>(impactFrames->size()) - 1;
                        projectile.impactAnimationFinished = true;
                    } else {
                        projectile.phase = Projectile::Phase::Done;
                        projectile.alive = false;
                    }
                    break;
                }
            }
            continue;
        }

        if (projectile.phase == Projectile::Phase::Start) {
            const auto* frames = ProjectileFramesForPhase(projectile);
            const float authoredSpeed = ProjectileAnimationSpeedForPhase(projectile);
            const float speed = authoredSpeed > 0.0f ? authoredSpeed : 12.0f;
            if (!frames || frames->empty() || (frames->size() == 1 && ProjectileAnimationSpeedForPhase(projectile) <= 0.0f)) {
                projectile.phase = Projectile::Phase::Flight;
                projectile.animationFrame = 0;
                projectile.animationTimer = 0.0f;
            } else {
                projectile.animationTimer += dt;
                const float frameDuration = 1.0f / speed;
                while (projectile.animationTimer >= frameDuration) {
                    projectile.animationTimer -= frameDuration;
                    projectile.animationFrame += 1;
                    if (projectile.animationFrame >= static_cast<int>(frames->size())) {
                        projectile.phase = Projectile::Phase::Flight;
                        projectile.animationFrame = 0;
                        projectile.animationTimer = 0.0f;
                        break;
                    }
                }
            }
            if (projectile.phase == Projectile::Phase::Done || !projectile.alive) {
                continue;
            }
        }

        if (projectile.phase != Projectile::Phase::Flight && projectile.phase != Projectile::Phase::Start) {
            continue;
        }

        float dx = 0.0f;
        float dy = 0.0f;
        if (projectile.movementType == ProjectileMovementType::TrackPlayer || projectile.movementType == ProjectileMovementType::Homing) {
            auto updateVelocityTowardPlayer = [this, &projectile]() {
                constexpr float kPi = 3.14159265358979323846f;
                constexpr float kMaxTurnRadians = kPi / 18.0f; // 10 degrees
                SDL_FPoint direction{
                    (player_.bounds.x + player_.bounds.w * 0.5f) - (projectile.bounds.x + projectile.bounds.w * 0.5f),
                    (player_.bounds.y + player_.bounds.h * 0.5f) - (projectile.bounds.y + projectile.bounds.h * 0.5f)
                };
                const float len = std::sqrt(direction.x * direction.x + direction.y * direction.y);
                if (len > 0.0001f) {
                    direction.x /= len;
                    direction.y /= len;
                } else {
                    direction = SDL_FPoint{0.0f, 1.0f};
                }

                const float velocityLen = std::sqrt(projectile.velocity.x * projectile.velocity.x + projectile.velocity.y * projectile.velocity.y);
                if (velocityLen <= 0.0001f) {
                    projectile.velocity = direction;
                    return;
                }

                SDL_FPoint currentDir{projectile.velocity.x / velocityLen, projectile.velocity.y / velocityLen};
                float currentAngle = std::atan2(currentDir.y, currentDir.x);
                float targetAngle = std::atan2(direction.y, direction.x);
                float delta = targetAngle - currentAngle;
                while (delta > kPi) {
                    delta -= 2.0f * kPi;
                }
                while (delta < -kPi) {
                    delta += 2.0f * kPi;
                }

                delta = std::clamp(delta, -kMaxTurnRadians, kMaxTurnRadians);
                const float newAngle = currentAngle + delta;
                projectile.velocity = SDL_FPoint{std::cos(newAngle), std::sin(newAngle)};
            };

            if (projectile.movementType == ProjectileMovementType::Homing) {
                updateVelocityTowardPlayer();
            } else {
                const float correctionDistance = std::max(1.0f, projectile.speedPixelsPerSecond / 3.0f);
                if (projectile.trackCorrectionDistanceAccumulator <= 0.0001f || projectile.trackCorrectionDistanceAccumulator >= correctionDistance) {
                    updateVelocityTowardPlayer();
                    projectile.trackCorrectionDistanceAccumulator = 0.0f;
                }
            }
            dx = projectile.velocity.x * projectile.speedPixelsPerSecond * dt;
            dy = projectile.velocity.y * projectile.speedPixelsPerSecond * dt;
        } else if (projectile.movementType == ProjectileMovementType::StraightLimitedDistance) {
            if (!projectile.movementStopped) {
                dx = projectile.velocity.x * projectile.speedPixelsPerSecond * dt;
                dy = projectile.velocity.y * projectile.speedPixelsPerSecond * dt;

                const float stepDistance = std::sqrt(dx * dx + dy * dy);
                const float remainingDistance = std::max(0.0f, projectile.limitedDistancePixels - projectile.traveledDistancePixels);
                if (remainingDistance <= 0.0001f) {
                    projectile.movementStopped = true;
                    dx = 0.0f;
                    dy = 0.0f;
                } else if (stepDistance > remainingDistance && stepDistance > 0.0001f) {
                    const float scale = remainingDistance / stepDistance;
                    dx *= scale;
                    dy *= scale;
                }
            }
        } else {
            if (std::fabs(projectile.velocity.x) >= std::fabs(projectile.velocity.y)) {
                const float signX = projectile.velocity.x < 0.0f ? -1.0f : 1.0f;
                dx = signX * projectile.speedPixelsPerSecond * dt;
                dy = projectile.fixedFunctionA * dx;
                projectile.velocity.x = signX;
                projectile.velocity.y = projectile.fixedFunctionA * signX;
            } else {
                const float signY = projectile.velocity.y < 0.0f ? -1.0f : 1.0f;
                dy = signY * projectile.speedPixelsPerSecond * dt;
                dx = projectile.fixedFunctionA * dy;
                projectile.velocity.y = signY;
                projectile.velocity.x = projectile.fixedFunctionA * signY;
            }
        }

        if (projectile.movementType == ProjectileMovementType::StraightLimitedDistance && !projectile.moveThroughSolid && !projectile.movementStopped) {
            SDL_FRect nextBounds = projectile.bounds;
            nextBounds.x += dx;
            nextBounds.y += dy;

            const bool wouldLeaveScreen =
                nextBounds.x < 0.0f ||
                nextBounds.y < 0.0f ||
                nextBounds.x + nextBounds.w > static_cast<float>(kScreenPixelWidth) ||
                nextBounds.y + nextBounds.h > static_cast<float>(kScreenPixelHeight);
            if (wouldLeaveScreen) {
                nextBounds.x = std::clamp(nextBounds.x, 0.0f, static_cast<float>(kScreenPixelWidth) - nextBounds.w);
                nextBounds.y = std::clamp(nextBounds.y, 0.0f, static_cast<float>(kScreenPixelHeight) - nextBounds.h);
                dx = nextBounds.x - projectile.bounds.x;
                dy = nextBounds.y - projectile.bounds.y;
                projectile.movementStopped = true;
            }

            SDL_FRect movedBounds = projectile.bounds;
            movedBounds.x += dx;
            movedBounds.y += dy;
            Projectile probe = projectile;
            probe.bounds = movedBounds;
            const std::vector<SDL_FRect> nextHitboxes = ActiveProjectileHitboxesAt(probe);
            bool hitSolid = false;
            for (const SDL_FRect& hitbox : nextHitboxes) {
                if (IsRectCollidingWithSolidTiles(hitbox, projectile.mapId, projectile.screenX, projectile.screenY)) {
                    hitSolid = true;
                    break;
                }
            }
            if (hitSolid) {
                dx = 0.0f;
                dy = 0.0f;
                projectile.movementStopped = true;
                projectile.damageConsumed = true;
                beginImpact(projectile);
            }
        }

        projectile.bounds.x += dx;
        projectile.bounds.y += dy;
        if (projectile.movementType == ProjectileMovementType::TrackPlayer) {
            projectile.trackCorrectionDistanceAccumulator += std::sqrt(dx * dx + dy * dy);
        }
        if (projectile.movementType == ProjectileMovementType::StraightLimitedDistance) {
            projectile.traveledDistancePixels += std::sqrt(dx * dx + dy * dy);
            if (!projectile.movementStopped && projectile.traveledDistancePixels >= projectile.limitedDistancePixels - 0.0001f) {
                projectile.movementStopped = true;
            }
            if (projectile.phase == Projectile::Phase::Impact) {
                continue;
            }
        }

        const bool outOfScreen =
            projectile.bounds.x + projectile.bounds.w < 0.0f ||
            projectile.bounds.y + projectile.bounds.h < 0.0f ||
            projectile.bounds.x > static_cast<float>(kScreenPixelWidth) ||
            projectile.bounds.y > static_cast<float>(kScreenPixelHeight);
        if (outOfScreen) {
            if (projectile.movementType == ProjectileMovementType::StraightLimitedDistance) {
                projectile.phase = Projectile::Phase::Done;
                projectile.alive = false;
            } else {
                beginImpact(projectile);
            }
            continue;
        }

        const std::vector<SDL_FRect> hitboxes = ActiveProjectileHitboxesAt(projectile);
        if (!projectile.moveThroughSolid && projectile.movementType != ProjectileMovementType::StraightLimitedDistance) {
            bool hitSolid = false;
            for (const SDL_FRect& hitbox : hitboxes) {
                if (IsRectCollidingWithSolidTiles(hitbox, projectile.mapId, projectile.screenX, projectile.screenY)) {
                    hitSolid = true;
                    break;
                }
            }
            if (hitSolid) {
                beginImpact(projectile);
                continue;
            }
        }

        if (projectile.owner == ProjectileOwner::Enemy && player_.invulnTimer <= 0.0f && !projectile.damageConsumed) {
            bool touchedPlayer = false;
            for (const SDL_FRect& hitbox : hitboxes) {
                if (PlayerIntersects(hitbox)) {
                    touchedPlayer = true;
                    break;
                }
            }
            if (touchedPlayer) {
                ApplyPlayerDamage(std::max(0, projectile.baseDamage), projectile.velocity);
                projectile.damageConsumed = true;
                if (projectile.movementType == ProjectileMovementType::StraightLimitedDistance) {
                    projectile.movementStopped = true;
                    beginImpact(projectile);
                } else {
                    beginImpact(projectile);
                }
                continue;
            }
        }

        if (projectile.owner == ProjectileOwner::Player && !projectile.damageConsumed) {
            bool hitEnemy = false;
            for (Enemy& enemy : world_.Enemies()) {
                if (!enemy.alive || enemy.disappeared || enemy.mapId != currentMapId_ || enemy.screenX != currentScreenX_ || enemy.screenY != currentScreenY_) {
                    continue;
                }
                if (enemy.invulnTimer > 0.0f) {
                    continue;
                }
                const std::vector<SDL_FRect> enemyHitboxes = ActiveEnemyHitboxesAt(enemy);
                for (const SDL_FRect& projectileHitbox : hitboxes) {
                    bool localHit = false;
                    for (const SDL_FRect& enemyHitbox : enemyHitboxes) {
                        if (Intersects(projectileHitbox, enemyHitbox)) {
                            localHit = true;
                            break;
                        }
                    }
                    if (localHit) {
                        ApplyEnemyDamage(enemy, std::max(0, projectile.baseDamage), projectile.velocity);
                        hitEnemy = true;
                        break;
                    }
                }
                if (hitEnemy) {
                    break;
                }
            }

            if (hitEnemy) {
                projectile.damageConsumed = true;
                if (projectile.movementType == ProjectileMovementType::StraightLimitedDistance) {
                    projectile.movementStopped = true;
                    beginImpact(projectile);
                } else {
                    beginImpact(projectile);
                }
                continue;
            }
        }

        const auto* flightFrames = ProjectileFramesForPhase(projectile);
        if (flightFrames && !flightFrames->empty() && projectile.flightAnimationSpeed > 0.0f) {
            projectile.animationTimer += dt;
            const float frameDuration = 1.0f / std::max(0.1f, projectile.flightAnimationSpeed);
            while (projectile.animationTimer >= frameDuration) {
                projectile.animationTimer -= frameDuration;
                projectile.animationFrame = (projectile.animationFrame + 1) % static_cast<int>(flightFrames->size());
            }
        } else {
            projectile.animationFrame = 0;
        }
    }

    projectiles_.erase(
        std::remove_if(projectiles_.begin(), projectiles_.end(), [](const Projectile& projectile) {
            return !projectile.alive || projectile.phase == Projectile::Phase::Done;
        }),
        projectiles_.end());
}

void Game::DrawProjectilesForScreen(const std::string& mapId, int screenX, int screenY, float offsetX, float offsetY) {
    for (const Projectile& projectile : projectiles_) {
        if (!projectile.alive || projectile.mapId != mapId || projectile.screenX != screenX || projectile.screenY != screenY) {
            continue;
        }

        const auto* frames = ProjectileFramesForPhase(projectile);
        if (frames && !frames->empty()) {
            const int frameIndex = std::clamp(projectile.animationFrame, 0, static_cast<int>(frames->size()) - 1);
            const ItemAnimationFrame& frame = (*frames)[static_cast<size_t>(frameIndex)];
            SDL_Texture* texture = TextureForItemFrame(frame);
            if (texture) {
                const SDL_FRect src{
                    static_cast<float>(frame.sourceX),
                    static_cast<float>(frame.sourceY),
                    static_cast<float>(frame.sourceW),
                    static_cast<float>(frame.sourceH)
                };
                SDL_FRect dst = projectile.bounds;
                dst.x += offsetX;
                dst.y += offsetY;
                SDL_RenderTexture(renderer_, texture, &src, &dst);
                continue;
            }
        }

        SDL_SetRenderDrawColor(renderer_, 255, 230, 96, 255);
        SDL_FRect rect = projectile.bounds;
        rect.x += offsetX;
        rect.y += offsetY;
        SDL_RenderFillRect(renderer_, &rect);
    }
}

void Game::UpdateItems() {
    for (Item& item : world_.Items()) {
        if (item.collected || item.mapId != currentMapId_ || item.screenX != currentScreenX_ || item.screenY != currentScreenY_) {
            continue;
        }

        if (item.legacyPickup) {
            if (!PlayerIntersects(item.bounds)) {
                continue;
            }

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
            continue;
        }

        const std::vector<SDL_FRect> hitboxes = ActiveItemHitboxesAt(item);
        if (hitboxes.empty()) {
            continue;
        }

        bool touched = false;
        for (const SDL_FRect& hitbox : hitboxes) {
            if (PlayerIntersects(hitbox)) {
                touched = true;
                break;
            }
        }

        if (!touched) {
            continue;
        }

        item.collected = true;
        ApplyItemTrigger(item);
    }
}

void Game::UpdateRoomText(float dt) {
    if (IsFirstVersionMode()) {
        roomTextContent_.clear();
        roomTextVisibleCharacters_ = 0.0f;
        return;
    }

    const bool screenChanged = previousScreenMapId_ != currentMapId_ || previousScreenX_ != currentScreenX_ || previousScreenY_ != currentScreenY_;
    if (screenChanged) {
        previousScreenMapId_ = currentMapId_;
        previousScreenX_ = currentScreenX_;
        previousScreenY_ = currentScreenY_;

        const Screen& screen = world_.GetScreen(currentMapId_, currentScreenX_, currentScreenY_);
        if (screen.displayTextEnabled && !screen.displayText.empty()) {
            roomTextMapId_ = currentMapId_;
            roomTextScreenX_ = currentScreenX_;
            roomTextScreenY_ = currentScreenY_;
            roomTextContent_ = screen.displayText;
            roomTextVisibleCharacters_ = 0.0f;
        } else {
            roomTextMapId_.clear();
            roomTextScreenX_ = -1;
            roomTextScreenY_ = -1;
            roomTextContent_.clear();
            roomTextVisibleCharacters_ = 0.0f;
        }
    }

    const bool activeForCurrentScreen = roomTextMapId_ == currentMapId_ && roomTextScreenX_ == currentScreenX_ && roomTextScreenY_ == currentScreenY_;
    if (!activeForCurrentScreen || roomTextContent_.empty()) {
        return;
    }

    roomTextVisibleCharacters_ = std::min(static_cast<float>(roomTextContent_.size()), roomTextVisibleCharacters_ + world_.Settings().textLettersPerSecond * dt);
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
        UpdateProjectiles(dt);
        if (!IsFirstVersionMode()) {
            UpdateCombat(dt);
        }
        UpdateItems();
    }

    UpdateTransition(dt);
    UpdateRoomText(dt);
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

void Game::DrawTilesForScreen(const std::string& mapId, int screenX, int screenY, float offsetX, float offsetY, const SDL_FRect* playerBoundsOverride) {
    const Screen& screen = world_.GetScreen(mapId, screenX, screenY);
    const bool classicMode = IsFirstVersionMode();
    SDL_FRect playerRect{};
    bool hasPlayer = false;
    if (playerBoundsOverride != nullptr) {
        playerRect = PlayerSpriteRectForBounds(*playerBoundsOverride);
        playerRect.x += offsetX;
        playerRect.y += offsetY;
        hasPlayer = true;
    }

    for (int ty = 0; ty < kTilesHigh; ++ty) {
        for (int tx = 0; tx < kTilesWide; ++tx) {
            const SDL_FRect rect{
                static_cast<float>(tx * kTileSize) + offsetX,
                static_cast<float>(ty * kTileSize) + offsetY,
                static_cast<float>(kTileSize),
                static_cast<float>(kTileSize)
            };

            const size_t tileIndex = static_cast<size_t>(ty * kTilesWide + tx);
            for (int layer = 0; layer < kTileLayers; ++layer) {
                const int tileId = screen.tileLayerIds[static_cast<size_t>(layer)][tileIndex];
                if (tileId < 0) {
                    continue;
                }

                if (layer > 0) {
                    const std::vector<SDL_FRect> tileHitboxes = world_.GetTileHitboxes(mapId, screenX, screenY, tx, ty);
                    if (!tileHitboxes.empty()) {
                        float minHitboxTopLocal = static_cast<float>(kTileSize);
                        for (const SDL_FRect& hitbox : tileHitboxes) {
                            minHitboxTopLocal = std::min(minHitboxTopLocal, hitbox.y - static_cast<float>(ty * kTileSize));
                        }
                        const int occlusionPixels = std::clamp(static_cast<int>(std::round(minHitboxTopLocal)), 0, kTileSize);
                        const float occlusionHeight = static_cast<float>(occlusionPixels);
                        if (occlusionHeight > 0.01f) {
                            SDL_FRect occlusionRect = rect;
                            occlusionRect.h = occlusionHeight;

                            bool shouldDeferToForeground = false;
                            if (hasPlayer && TileShouldOccludeActor(occlusionRect, playerRect)) {
                                shouldDeferToForeground = true;
                            }
                            if (!shouldDeferToForeground) {
                                for (const Enemy& enemy : world_.Enemies()) {
                                    if (!enemy.alive || enemy.disappeared || enemy.mapId != mapId || enemy.screenX != screenX || enemy.screenY != screenY) {
                                        continue;
                                    }
                                    SDL_FRect enemyRect = EnemySpriteRectForDraw(enemy);
                                    enemyRect.x += offsetX;
                                    enemyRect.y += offsetY;
                                    if (TileShouldOccludeActor(occlusionRect, enemyRect)) {
                                        shouldDeferToForeground = true;
                                        break;
                                    }
                                }
                            }

                            if (shouldDeferToForeground) {
                                continue;
                            }
                        }
                    }
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

void Game::DrawForegroundOcclusionTilesForScreen(const std::string& mapId, int screenX, int screenY, float offsetX, float offsetY, const SDL_FRect* playerBoundsOverride) {
    const Screen& screen = world_.GetScreen(mapId, screenX, screenY);
    const bool classicMode = IsFirstVersionMode();

    SDL_FRect playerRect{};
    const bool hasPlayer = playerBoundsOverride != nullptr;
    if (hasPlayer) {
        playerRect = PlayerSpriteRectForBounds(*playerBoundsOverride);
    }

    if (debugShowOcclusion_) {
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        if (hasPlayer) {
            SDL_SetRenderDrawColor(renderer_, 64, 224, 255, 220);
            SDL_RenderRect(renderer_, &playerRect);
        }

        for (const Enemy& enemy : world_.Enemies()) {
            if (!enemy.alive || enemy.disappeared || enemy.mapId != mapId || enemy.screenX != screenX || enemy.screenY != screenY) {
                continue;
            }

            SDL_FRect enemyRect = EnemySpriteRectForDraw(enemy);
            enemyRect.x += offsetX;
            enemyRect.y += offsetY;
            SDL_SetRenderDrawColor(renderer_, 255, 170, 48, 220);
            SDL_RenderRect(renderer_, &enemyRect);
        }
    }

    for (int ty = 0; ty < kTilesHigh; ++ty) {
        for (int tx = 0; tx < kTilesWide; ++tx) {
            const size_t tileIndex = static_cast<size_t>(ty * kTilesWide + tx);
            const SDL_FRect rect{
                static_cast<float>(tx * kTileSize) + offsetX,
                static_cast<float>(ty * kTileSize) + offsetY,
                static_cast<float>(kTileSize) + 0.02f,
                static_cast<float>(kTileSize) + 0.02f
            };

            for (int layer = 1; layer < kTileLayers; ++layer) {
                const int tileId = screen.tileLayerIds[static_cast<size_t>(layer)][tileIndex];
                if (tileId < 0) {
                    continue;
                }

                const std::vector<SDL_FRect> tileHitboxes = world_.GetTileHitboxes(mapId, screenX, screenY, tx, ty);
                if (tileHitboxes.empty()) {
                    continue;
                }

                float minHitboxTopLocal = static_cast<float>(kTileSize);
                for (const SDL_FRect& hitbox : tileHitboxes) {
                    minHitboxTopLocal = std::min(minHitboxTopLocal, hitbox.y - static_cast<float>(ty * kTileSize));
                }
                const int occlusionPixels = std::clamp(static_cast<int>(std::round(minHitboxTopLocal)), 0, kTileSize);
                const float occlusionHeight = static_cast<float>(occlusionPixels);
                if (occlusionHeight <= 0.01f) {
                    continue;
                }

                SDL_FRect occlusionRect = rect;
                occlusionRect.h = occlusionHeight;

                bool shouldDrawForeground = false;
                if (hasPlayer && TileShouldOccludeActor(occlusionRect, playerRect)) {
                    shouldDrawForeground = true;
                }

                if (!shouldDrawForeground) {
                    for (const Enemy& enemy : world_.Enemies()) {
                        if (!enemy.alive || enemy.disappeared || enemy.mapId != mapId || enemy.screenX != screenX || enemy.screenY != screenY) {
                            continue;
                        }

                        SDL_FRect enemyRect = EnemySpriteRectForDraw(enemy);
                        enemyRect.x += offsetX;
                        enemyRect.y += offsetY;
                        if (TileShouldOccludeActor(occlusionRect, enemyRect)) {
                            shouldDrawForeground = true;
                            break;
                        }
                    }
                }

                if (!shouldDrawForeground) {
                    if (debugShowOcclusion_) {
                        SDL_SetRenderDrawColor(renderer_, 255, 64, 64, 110);
                        SDL_RenderRect(renderer_, &occlusionRect);
                    }
                    continue;
                }

                const auto drawFullTile = [this, classicMode, &rect](int drawTileId) {
                    if (drawTileId < 0) {
                        return;
                    }
                    const auto drawIt = tileRenderById_.find(drawTileId);
                    if (drawIt != tileRenderById_.end() && drawIt->second.texture != nullptr) {
                        SDL_RenderTexture(renderer_, drawIt->second.texture, &drawIt->second.source, &rect);
                        return;
                    }
                    const SDL_Color drawColor = TileColorFromId(drawTileId, classicMode);
                    SDL_SetRenderDrawColor(renderer_, drawColor.r, drawColor.g, drawColor.b, drawColor.a);
                    SDL_RenderFillRect(renderer_, &rect);
                };

                // Overlap is tested against the zone above the hitbox, but once triggered
                // the whole foreground tile must render over the actor.
                drawFullTile(tileId);

                if (debugShowOcclusion_) {
                    SDL_SetRenderDrawColor(renderer_, 80, 255, 80, 220);
                    SDL_RenderRect(renderer_, &occlusionRect);
                }
            }
        }
    }
}

void Game::DrawItemsForScreen(const std::string& mapId, int screenX, int screenY, float offsetX, float offsetY) {
    for (const Item& item : world_.Items()) {
        if (item.collected || item.mapId != mapId || item.screenX != screenX || item.screenY != screenY) {
            continue;
        }

        SDL_FRect rect = item.bounds;
        rect.x += offsetX;
        rect.y += offsetY;

        const ItemAnimationFrame* frame = ActiveItemFrame(item, SDL_GetTicks());
        SDL_Texture* texture = frame ? TextureForItemFrame(*frame) : nullptr;
        if (texture && frame) {
            SDL_FRect src{
                static_cast<float>(frame->sourceX),
                static_cast<float>(frame->sourceY),
                static_cast<float>(frame->sourceW),
                static_cast<float>(frame->sourceH)
            };
            rect.w = static_cast<float>(frame->sourceW);
            rect.h = static_cast<float>(frame->sourceH);
            SDL_RenderTexture(renderer_, texture, &src, &rect);
            continue;
        }

        const SDL_Color color = LegacyItemColor(item);
        SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);
        SDL_RenderFillRect(renderer_, &rect);
    }
}

void Game::DrawEnemiesForScreen(const std::string& mapId, int screenX, int screenY, float offsetX, float offsetY) {
    for (const Enemy& enemy : world_.Enemies()) {
        if ((!enemy.alive && !enemy.deathAnimationPlaying) || enemy.disappeared || enemy.mapId != mapId || enemy.screenX != screenX || enemy.screenY != screenY) {
            continue;
        }

        const EnemyMoveDefinition::AnimationFrame* frame = nullptr;
        const std::vector<EnemyMoveDefinition::AnimationFrame>* frames = nullptr;
        int frameIndex = 0;
        if (enemy.deathAnimationPlaying) {
            const EnemyReactionAnimation& deathAnim = enemy.deathAnimation;
            frames = EnemyFramesForDirection(deathAnim, enemy.moveDirection);
            frameIndex = enemy.deathAnimationFrame;
        }
        if (!frames && enemy.knockbackTimer > 0.0f) {
            frames = EnemyFramesForDirection(enemy.knockbackAnimation, enemy.knockbackDirection);
            frameIndex = enemy.knockbackAnimationFrame;
        }
        if (!frames) {
            const EnemyMoveDefinition* move = ActiveEnemyMoveDefinition(enemy);
            frames = move ? EnemyFramesForDirection(*move, enemy.moveDirection) : nullptr;
            frameIndex = enemy.animationFrame;
        }
        if (frames && !frames->empty()) {
            const int clampedFrameIndex = std::clamp(frameIndex, 0, static_cast<int>(frames->size()) - 1);
            frame = &(*frames)[static_cast<size_t>(clampedFrameIndex)];
        }

        SDL_FRect rect = enemy.bounds;
        rect.x += offsetX;
        rect.y += offsetY;

        if (frame) {
            bool drewAny = false;
            const Uint8 alpha = (enemy.invulnTimer > 0.0f && (static_cast<int>(enemy.invulnTimer * 20.0f) % 2 == 0)) ? 128 : 255;
            for (const EnemyMoveDefinition::AnimationTile& tile : frame->tiles) {
                ItemAnimationFrame srcFrame;
                srcFrame.sourceImagePath = tile.sourceImagePath;
                srcFrame.sourceLabel = tile.sourceLabel;
                srcFrame.sourceX = tile.sourceX;
                srcFrame.sourceY = tile.sourceY;
                srcFrame.sourceW = tile.sourceW;
                srcFrame.sourceH = tile.sourceH;

                SDL_Texture* texture = TextureForItemFrame(srcFrame);
                if (!texture) {
                    continue;
                }

                SDL_SetTextureAlphaMod(texture, alpha);
                const SDL_FRect src{
                    static_cast<float>(tile.sourceX),
                    static_cast<float>(tile.sourceY),
                    static_cast<float>(tile.sourceW),
                    static_cast<float>(tile.sourceH)
                };
                SDL_FRect dst{
                    rect.x + static_cast<float>(tile.tileX * 16),
                    rect.y + static_cast<float>(tile.tileY * 16),
                    static_cast<float>(std::max(1, tile.sourceW)),
                    static_cast<float>(std::max(1, tile.sourceH))
                };
                SDL_RenderTexture(renderer_, texture, &src, &dst);
                SDL_SetTextureAlphaMod(texture, 255);
                drewAny = true;
            }

            if (drewAny) {
                continue;
            }
        }

        if (enemy.invulnTimer > 0.0f) {
            SDL_SetRenderDrawColor(renderer_, 235, 180, 180, 255);
        } else {
            SDL_SetRenderDrawColor(renderer_, 146, 40, 40, 255);
        }
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
        if (!enemy.alive || enemy.disappeared || enemy.mapId != mapId || enemy.screenX != screenX || enemy.screenY != screenY) {
            continue;
        }
        for (SDL_FRect drawRect : ActiveEnemyHitboxesAt(enemy)) {
            drawRect.x += offsetX;
            drawRect.y += offsetY;
            SDL_RenderRect(renderer_, &drawRect);
        }
    }

    SDL_SetRenderDrawColor(renderer_, 255, 220, 80, 190);
    for (const Item& item : world_.Items()) {
        if (item.collected || item.mapId != mapId || item.screenX != screenX || item.screenY != screenY) {
            continue;
        }
        const std::vector<SDL_FRect> hitboxes = ActiveItemHitboxesAt(item);
        if (hitboxes.empty()) {
            SDL_FRect drawRect{item.bounds.x + offsetX, item.bounds.y + offsetY, item.bounds.w, item.bounds.h};
            SDL_RenderRect(renderer_, &drawRect);
            continue;
        }

        for (SDL_FRect drawRect : hitboxes) {
            drawRect.x += offsetX;
            drawRect.y += offsetY;
            SDL_RenderRect(renderer_, &drawRect);
        }
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
    const int totalHearts = std::max(1, (player_.maxHealth + 3) / 4);
    const float coinsBaseX = IsFirstVersionMode() ? 34.0f : 8.0f + totalHearts * 10.0f + 10.0f;
    const float wheatBaseX = IsFirstVersionMode() ? 66.0f : coinsBaseX + 38.0f;
    const float panelWidth = std::max(94.0f, wheatBaseX + 34.0f);
    SDL_FRect panel{4.0f, 2.0f, panelWidth, 20.0f};
    SDL_RenderFillRect(renderer_, &panel);

    for (int i = 0; i < totalHearts; ++i) {
        const int quarterCount = std::clamp(player_.health - i * 4, 0, 4);
        DrawQuarterHeart(renderer_, 6.0f + i * 10.0f, 6.0f, quarterCount);
    }

    SDL_SetRenderDrawColor(renderer_, 220, 180, 32, 255);
    for (int i = 0; i < coins_ && i < 4; ++i) {
        SDL_FRect coin = IsFirstVersionMode()
            ? SDL_FRect{34.0f + i * 8.0f, 6.0f, 6.0f, 6.0f}
            : SDL_FRect{coinsBaseX + i * 8.0f, 6.0f, 6.0f, 6.0f};
        SDL_RenderFillRect(renderer_, &coin);
    }

    SDL_SetRenderDrawColor(renderer_, 192, 140, 74, 255);
    for (int i = 0; i < wheat_ && i < 4; ++i) {
        SDL_FRect grain = IsFirstVersionMode()
            ? SDL_FRect{66.0f + i * 8.0f, 6.0f, 6.0f, 6.0f}
            : SDL_FRect{wheatBaseX + i * 8.0f, 14.0f, 6.0f, 4.0f};
        SDL_RenderFillRect(renderer_, &grain);
    }

    if (!IsFirstVersionMode() && speedBuffTimer_ > 0.0f) {
        SDL_SetRenderDrawColor(renderer_, 80, 222, 190, 255);
        SDL_FRect buff{wheatBaseX + 20.0f, 4.0f, std::min(16.0f, speedBuffTimer_ * 2.0f), 3.0f};
        SDL_RenderFillRect(renderer_, &buff);
    }

    if (!IsFirstVersionMode()) {
        auto drawWeaponSlot = [this](float x, const WeaponDefinition* weapon, bool selected) {
            SDL_SetRenderDrawColor(renderer_, selected ? 236 : 128, selected ? 220 : 140, selected ? 120 : 156, 255);
            SDL_FRect border{x, 2.0f, 20.0f, 20.0f};
            SDL_RenderFillRect(renderer_, &border);

            SDL_SetRenderDrawColor(renderer_, 20, 24, 31, 255);
            SDL_FRect interior{x + 1.0f, 3.0f, 18.0f, 18.0f};
            SDL_RenderFillRect(renderer_, &interior);

            if (!weapon) {
                return;
            }

            SDL_Texture* iconTexture = TextureForItemFrame(weapon->hudSprite);
            if (!iconTexture) {
                return;
            }
            const SDL_FRect src{
                static_cast<float>(weapon->hudSprite.sourceX),
                static_cast<float>(weapon->hudSprite.sourceY),
                static_cast<float>(std::max(1, weapon->hudSprite.sourceW)),
                static_cast<float>(std::max(1, weapon->hudSprite.sourceH))
            };
            const SDL_FRect dst{x + 2.0f, 4.0f, 16.0f, 16.0f};
            SDL_RenderTexture(renderer_, iconTexture, &src, &dst);
        };

        const WeaponDefinition* weaponA = EquippedWeaponForSlotA();
        const WeaponDefinition* weaponB = EquippedWeaponForSlotB();
        const float centerX = static_cast<float>(kScreenPixelWidth) * 0.5f;
        drawWeaponSlot(centerX - 22.0f, weaponA, true);
        drawWeaponSlot(centerX + 2.0f, weaponB, false);
    }
}

void Game::DrawRoomText() {
    const float stripY = static_cast<float>(kHudStripHeight + kScreenPixelHeight);
    SDL_SetRenderDrawColor(renderer_, 9, 10, 13, 255);
    SDL_FRect stripRect{0.0f, stripY, static_cast<float>(kScreenPixelWidth), static_cast<float>(kTextStripHeight)};
    SDL_RenderFillRect(renderer_, &stripRect);

    SDL_SetRenderDrawColor(renderer_, 24, 34, 48, 255);
    SDL_FRect divider{0.0f, stripY, static_cast<float>(kScreenPixelWidth), 1.0f};
    SDL_RenderFillRect(renderer_, &divider);

    const bool activeForCurrentScreen = roomTextMapId_ == currentMapId_ && roomTextScreenX_ == currentScreenX_ && roomTextScreenY_ == currentScreenY_;
    if (!textAtlas_ || roomTextContent_.empty() || !activeForCurrentScreen) {
        return;
    }

    const SDL_FRect panelSrc{
        static_cast<float>(kTextPanelSourceX),
        static_cast<float>(kTextPanelSourceY),
        static_cast<float>(kTextPanelSourceW),
        static_cast<float>(kTextPanelSourceH)
    };
    const float panelMaxW = static_cast<float>(kScreenPixelWidth) - 8.0f;
    const float panelMaxH = static_cast<float>(kTextStripHeight) - 6.0f;
    const float panelScale = std::max(0.1f, std::min(panelMaxW / panelSrc.w, panelMaxH / panelSrc.h));
    const float panelW = panelSrc.w * panelScale;
    const float panelH = panelSrc.h * panelScale;
    const SDL_FRect panelDst{
        (static_cast<float>(kScreenPixelWidth) - panelW) * 0.5f,
        stripY + (static_cast<float>(kTextStripHeight) - panelH) * 0.5f,
        panelW,
        panelH
    };
    SDL_RenderTexture(renderer_, textAtlas_, &panelSrc, &panelDst);

    const int visibleCount = std::clamp(static_cast<int>(std::floor(roomTextVisibleCharacters_)), 0, static_cast<int>(roomTextContent_.size()));
    if (visibleCount <= 0) {
        return;
    }

    const std::string glyphMap = NormalizedGlyphMap(world_.Settings().textGlyphMap);

    const float textHorizontalPadding = std::max(4.0f, 10.0f * panelScale);
    const float textTopPadding = std::max(1.0f, 3.0f * panelScale) + 6.0f;
    const float textBottomPadding = std::max(1.0f, 3.0f * panelScale);

    const float textLeft = panelDst.x + textHorizontalPadding;
    const float textTop = panelDst.y + textTopPadding;
    const float textRight = panelDst.x + panelDst.w - textHorizontalPadding;
    const float textBottom = panelDst.y + panelDst.h - textBottomPadding;
    const float glyphCellWidth = std::max(4.0f, std::floor(static_cast<float>(kGlyphSize) * panelScale));
    const float glyphCellHeight = glyphCellWidth;
    const float lineHeight = glyphCellHeight * 2.0f;

    float cursorX = textLeft;
    float cursorY = textTop;
    const auto nextLine = [&]() {
        cursorX = textLeft;
        cursorY += lineHeight;
    };

    size_t index = 0;
    while (index < static_cast<size_t>(visibleCount)) {
        const char ch = roomTextContent_[index];

        if (ch == '\n') {
            nextLine();
            if (cursorY + lineHeight > textBottom) {
                break;
            }
            ++index;
            continue;
        }

        if (ch == ' ') {
            if (cursorX > textLeft && cursorX + glyphCellWidth <= textRight) {
                cursorX += glyphCellWidth;
            }
            ++index;
            continue;
        }

        size_t wordEnd = index;
        while (wordEnd < static_cast<size_t>(visibleCount)) {
            const char wordChar = roomTextContent_[wordEnd];
            if (wordChar == ' ' || wordChar == '\n') {
                break;
            }
            ++wordEnd;
        }

        const float wordWidth = static_cast<float>(wordEnd - index) * glyphCellWidth;
        if (cursorX > textLeft && cursorX + wordWidth > textRight) {
            nextLine();
        }
        if (cursorY + lineHeight > textBottom) {
            break;
        }

        for (; index < wordEnd; ++index) {
            if (cursorX + glyphCellWidth > textRight) {
                nextLine();
                if (cursorY + lineHeight > textBottom) {
                    break;
                }
            }

            SDL_FRect glyphSrc{};
            if (GlyphSourceForCharacter(roomTextContent_[index], glyphMap, glyphSrc)) {
                const float glyphDstHeight = glyphSrc.h > static_cast<float>(kGlyphSize) ? lineHeight : glyphCellHeight;
                const float glyphDstY = glyphDstHeight < lineHeight ? cursorY + (lineHeight - glyphDstHeight) : cursorY;
                SDL_FRect glyphDst{cursorX, glyphDstY, glyphCellWidth, glyphDstHeight};
                SDL_RenderTexture(renderer_, textAtlas_, &glyphSrc, &glyphDst);
            }
            cursorX += glyphCellWidth;
        }

        if (cursorY + lineHeight > textBottom) {
            break;
        }
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

void Game::DrawScreenLayer(const std::string& mapId, int screenX, int screenY, float offsetX, float offsetY, const SDL_FRect* playerBoundsOverride) {
    DrawTilesForScreen(mapId, screenX, screenY, offsetX, offsetY, playerBoundsOverride);
    DrawItemsForScreen(mapId, screenX, screenY, offsetX, offsetY);
    DrawProjectilesForScreen(mapId, screenX, screenY, offsetX, offsetY);
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

        const SDL_FRect interpolatedPlayer{player_.bounds.x, player_.bounds.y, player_.bounds.w, player_.bounds.h};
        DrawScreenLayer(transitionSourceMapId_, transitionSourceScreenX_, transitionSourceScreenY_, currentOffsetX, currentOffsetY, &interpolatedPlayer);
        DrawScreenLayer(transitionTargetMapId_, transitionTargetScreenX_, transitionTargetScreenY_, targetOffsetX, targetOffsetY, &interpolatedPlayer);

        if (debugShowHitboxes_) {
            DrawDebugHitboxesForScreen(transitionSourceMapId_, transitionSourceScreenX_, transitionSourceScreenY_, currentOffsetX, currentOffsetY);
            DrawDebugHitboxesForScreen(transitionTargetMapId_, transitionTargetScreenX_, transitionTargetScreenY_, targetOffsetX, targetOffsetY);
        }

        DrawPlayerAt(interpolatedPlayer);
        DrawForegroundOcclusionTilesForScreen(transitionSourceMapId_, transitionSourceScreenX_, transitionSourceScreenY_, currentOffsetX, currentOffsetY, &interpolatedPlayer);
        DrawForegroundOcclusionTilesForScreen(transitionTargetMapId_, transitionTargetScreenX_, transitionTargetScreenY_, targetOffsetX, targetOffsetY, &interpolatedPlayer);
        if (debugShowHitboxes_) {
            DrawPlayerDebugHitboxesAt(interpolatedPlayer);
        }
    } else {
        DrawScreenLayer(currentMapId_, currentScreenX_, currentScreenY_, 0.0f, 0.0f, &player_.bounds);
        if (debugShowHitboxes_) {
            DrawDebugHitboxesForScreen(currentMapId_, currentScreenX_, currentScreenY_, 0.0f, 0.0f);
        }
        DrawPlayer();
        DrawForegroundOcclusionTilesForScreen(currentMapId_, currentScreenX_, currentScreenY_, 0.0f, 0.0f, &player_.bounds);
        if (debugShowHitboxes_) {
            DrawPlayerDebugHitboxesAt(player_.bounds);
        }
    }

    DrawTransitionOverlay();

    SDL_SetRenderViewport(renderer_, nullptr);
    DrawAttackHitbox();
    DrawHUD();
    DrawRoomText();

    SDL_RenderPresent(renderer_);
}

