#pragma once

constexpr int kTileSize = 16;
constexpr int kTilesWide = 16;
constexpr int kTilesHigh = 11;
constexpr int kTilesPerScreen = kTilesWide * kTilesHigh;
constexpr int kTileLayers = 3;
constexpr int kScreenPixelWidth = kTilesWide * kTileSize;
constexpr int kScreenPixelHeight = kTilesHigh * kTileSize;
constexpr int kWindowScale = 4;

constexpr int kDefaultWorldScreensWide = 5;
constexpr int kDefaultWorldScreensHigh = 4;
