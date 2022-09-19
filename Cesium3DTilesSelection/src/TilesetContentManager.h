#pragma once

#include "RasterOverlayUpsampler.h"
#include "TilesetContentLoaderResult.h"

#include <Cesium3DTilesSelection/CreditSystem.h>
#include <Cesium3DTilesSelection/RasterOverlayCollection.h>
#include <Cesium3DTilesSelection/Tile.h>
#include <Cesium3DTilesSelection/TileContent.h>
#include <Cesium3DTilesSelection/TilesetContentLoader.h>
#include <Cesium3DTilesSelection/TilesetExternals.h>
#include <Cesium3DTilesSelection/TilesetLoadFailureDetails.h>
#include <Cesium3DTilesSelection/TilesetOptions.h>
#include <CesiumAsync/IAssetAccessor.h>

#include <vector>

namespace Cesium3DTilesSelection {
class TilesetContentManager {
public:
  TilesetContentManager(
      const TilesetExternals& externals,
      const TilesetOptions& tilesetOptions,
      RasterOverlayCollection&& overlayCollection,
      std::vector<CesiumAsync::IAssetAccessor::THeader>&& requestHeaders,
      std::unique_ptr<TilesetContentLoader>&& pLoader,
      std::unique_ptr<Tile>&& pRootTile);

  TilesetContentManager(
      const TilesetExternals& externals,
      const TilesetOptions& tilesetOptions,
      RasterOverlayCollection&& overlayCollection,
      const std::string& url);

  TilesetContentManager(
      const TilesetExternals& externals,
      const TilesetOptions& tilesetOptions,
      RasterOverlayCollection&& overlayCollection,
      int64_t ionAssetID,
      const std::string& ionAccessToken,
      const std::string& ionAssetEndpointUrl = "https://api.cesium.com/");

  ~TilesetContentManager() noexcept;

  void loadTileContent(Tile& tile, const TilesetOptions& tilesetOptions);

  void updateTileContent(Tile& tile, const TilesetOptions& tilesetOptions);

  bool unloadTileContent(Tile& tile);

  void waitUntilIdle();

  const Tile* getRootTile() const noexcept;

  Tile* getRootTile() noexcept;

  const std::vector<CesiumAsync::IAssetAccessor::THeader>&
  getRequestHeaders() const noexcept;

  std::vector<CesiumAsync::IAssetAccessor::THeader>&
  getRequestHeaders() noexcept;

  const RasterOverlayCollection& getRasterOverlayCollection() const noexcept;

  RasterOverlayCollection& getRasterOverlayCollection() noexcept;

  const Credit* getUserCredit() const noexcept;

  const std::vector<Credit>& getTilesetCredits() const noexcept;

  int32_t getNumberOfTilesLoading() const noexcept;

  int32_t getNumberOfTilesLoaded() const noexcept;

  int64_t getTotalDataUsed() const noexcept;

  bool tileNeedsLoading(const Tile& tile) const noexcept;

private:
  static void setTileContent(
      Tile& tile,
      TileLoadResult&& result,
      void* pWorkerRenderResources);

  void
  updateContentLoadedState(Tile& tile, const TilesetOptions& tilesetOptions);

  void updateDoneState(Tile& tile, const TilesetOptions& tilesetOptions);

  void unloadContentLoadedState(Tile& tile);

  void unloadDoneState(Tile& tile);

  void notifyTileStartLoading(const Tile* pTile) noexcept;

  void notifyTileDoneLoading(const Tile* pTile) noexcept;

  void notifyTileUnloading(const Tile* pTile) noexcept;

  template <class TilesetContentLoaderType>
  void propagateTilesetContentLoaderResult(
      TilesetLoadType type,
      const std::function<void(const TilesetLoadFailureDetails&)>&
          loadErrorCallback,
      TilesetContentLoaderResult<TilesetContentLoaderType>&& result);

  TilesetExternals _externals;
  std::vector<CesiumAsync::IAssetAccessor::THeader> _requestHeaders;
  std::unique_ptr<TilesetContentLoader> _pLoader;
  std::unique_ptr<Tile> _pRootTile;
  std::optional<Credit> _userCredit;
  std::vector<Credit> _tilesetCredits;
  RasterOverlayUpsampler _upsampler;
  RasterOverlayCollection _overlayCollection;
  int32_t _tilesLoadOnProgress;
  int32_t _loadedTilesCount;
  int64_t _tilesDataUsed;
};
} // namespace Cesium3DTilesSelection
