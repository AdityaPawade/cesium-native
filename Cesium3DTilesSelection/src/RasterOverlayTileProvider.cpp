#include "Cesium3DTilesSelection/RasterOverlayTileProvider.h"

#include "Cesium3DTilesSelection/IPrepareRendererResources.h"
#include "Cesium3DTilesSelection/RasterOverlay.h"
#include "Cesium3DTilesSelection/RasterOverlayTile.h"
#include "Cesium3DTilesSelection/TileID.h"
#include "Cesium3DTilesSelection/TilesetExternals.h"
#include "Cesium3DTilesSelection/spdlog-cesium.h"

#include <CesiumAsync/IAssetResponse.h>
#include <CesiumGltfReader/GltfReader.h>
#include <CesiumUtility/Tracing.h>
#include <CesiumUtility/joinToString.h>

using namespace CesiumAsync;
using namespace CesiumGeometry;
using namespace CesiumGeospatial;
using namespace CesiumGltf;
using namespace CesiumGltfReader;
using namespace CesiumUtility;

namespace Cesium3DTilesSelection {

/*static*/ CesiumGltfReader::GltfReader
    RasterOverlayTileProvider::_gltfReader{};

RasterOverlayTileProvider::RasterOverlayTileProvider(
    RasterOverlay& owner,
    const std::shared_ptr<CesiumAsync::AsyncSystem>& pAsyncSystem,
    const std::shared_ptr<IAssetAccessor>& pAssetAccessor) noexcept
    : _pOwner(&owner),
      _pAsyncSystem(pAsyncSystem),
      _pAssetAccessor(pAssetAccessor),
      _credit(std::nullopt),
      _pPrepareRendererResources(nullptr),
      _pLogger(nullptr),
      _projection(CesiumGeospatial::GeographicProjection()),
      _coverageRectangle(CesiumGeospatial::GeographicProjection::
                             computeMaximumProjectedRectangle()),
      _pPlaceholder(std::make_unique<RasterOverlayTile>(owner)),
      _tileDataBytes(0),
      _totalTilesCurrentlyLoading(0),
      _throttledTilesCurrentlyLoading(0) {
  // Placeholders should never be removed.
  this->_pPlaceholder->addReference();
}

RasterOverlayTileProvider::RasterOverlayTileProvider(
    RasterOverlay& owner,
    const std::shared_ptr<CesiumAsync::AsyncSystem>& pAsyncSystem,
    const std::shared_ptr<IAssetAccessor>& pAssetAccessor,
    std::optional<Credit> credit,
    const std::shared_ptr<IPrepareRendererResources>& pPrepareRendererResources,
    const std::shared_ptr<spdlog::logger>& pLogger,
    const CesiumGeospatial::Projection& projection,
    const Rectangle& coverageRectangle) noexcept
    : _pOwner(&owner),
      _pAsyncSystem(pAsyncSystem),
      _pAssetAccessor(pAssetAccessor),
      _credit(credit),
      _pPrepareRendererResources(pPrepareRendererResources),
      _pLogger(pLogger),
      _projection(projection),
      _coverageRectangle(coverageRectangle),
      _pPlaceholder(nullptr),
      _tileDataBytes(0),
      _totalTilesCurrentlyLoading(0),
      _throttledTilesCurrentlyLoading(0) {}

CesiumUtility::IntrusivePointer<RasterOverlayTile>
RasterOverlayTileProvider::getTile(
    const CesiumGeometry::Rectangle& rectangle,
    const glm::dvec2& targetScreenPixels) {
  if (this->_pPlaceholder) {
    return this->_pPlaceholder.get();
  }

  if (!rectangle.overlaps(this->_coverageRectangle)) {
    return nullptr;
  }

  return {
      new RasterOverlayTile(this->getOwner(), targetScreenPixels, rectangle)};
}

void RasterOverlayTileProvider::removeTile(RasterOverlayTile* pTile) noexcept {
  assert(pTile->getReferenceCount() == 0);

  this->_tileDataBytes -= int64_t(pTile->getImage().pixelData.size());

  RasterOverlay& overlay = pTile->getOverlay();
  delete pTile;

  if (overlay.isBeingDestroyed()) {
    overlay.destroySafely(nullptr);
  }
}

void RasterOverlayTileProvider::loadTile(RasterOverlayTile& tile) {
  if (this->_pPlaceholder) {
    // Refuse to load placeholders.
    return;
  }

  this->doLoad(tile, false);
}

bool RasterOverlayTileProvider::loadTileThrottled(RasterOverlayTile& tile) {
  if (tile.getState() != RasterOverlayTile::LoadState::Unloaded) {
    return true;
  }

  if (this->_throttledTilesCurrentlyLoading >=
      this->getOwner().getOptions().maximumSimultaneousTileLoads) {
    return false;
  }

  this->doLoad(tile, true);
  return true;
}

CesiumAsync::Future<LoadedRasterOverlayImage>
RasterOverlayTileProvider::loadTileImageFromUrl(
    const std::string& url,
    const std::vector<IAssetAccessor::THeader>& headers,
    LoadTileImageFromUrlOptions&& options) const {

  return this->getAssetAccessor()
      ->get(this->getAsyncSystem(), url, headers)
      .thenInWorkerThread(
          [options = std::move(options),
           Ktx2TranscodeTargets =
               this->getOwner().getOptions().ktx2TranscodeTargets](
              std::shared_ptr<IAssetRequest>&& pRequest) mutable {
            CESIUM_TRACE("load image");
            const IAssetResponse* pResponse = pRequest->response();
            if (pResponse == nullptr) {
              return LoadedRasterOverlayImage{
                  std::nullopt,
                  options.rectangle,
                  std::move(options.credits),
                  {"Image request for " + pRequest->url() + " failed."},
                  {},
                  options.moreDetailAvailable};
            }

            if (pResponse->statusCode() < 200 ||
                pResponse->statusCode() >= 300) {
              std::string message = "Image response code " +
                                    std::to_string(pResponse->statusCode()) +
                                    " for " + pRequest->url();
              return LoadedRasterOverlayImage{
                  std::nullopt,
                  options.rectangle,
                  std::move(options.credits),
                  {message},
                  {},
                  options.moreDetailAvailable};
            }

            if (pResponse->data().empty()) {
              if (options.allowEmptyImages) {
                return LoadedRasterOverlayImage{
                    CesiumGltf::ImageCesium(),
                    options.rectangle,
                    std::move(options.credits),
                    {},
                    {},
                    options.moreDetailAvailable};
              }
              return LoadedRasterOverlayImage{
                  std::nullopt,
                  options.rectangle,
                  std::move(options.credits),
                  {"Image response for " + pRequest->url() + " is empty."},
                  {},
                  options.moreDetailAvailable};
            }

            const gsl::span<const std::byte> data = pResponse->data();

            CesiumGltfReader::ImageReaderResult loadedImage =
                RasterOverlayTileProvider::_gltfReader.readImage(
                    data,
                    Ktx2TranscodeTargets);

            if (!loadedImage.errors.empty()) {
              loadedImage.errors.push_back("Image url: " + pRequest->url());
            }
            if (!loadedImage.warnings.empty()) {
              loadedImage.warnings.push_back("Image url: " + pRequest->url());
            }

            return LoadedRasterOverlayImage{
                loadedImage.image,
                options.rectangle,
                std::move(options.credits),
                std::move(loadedImage.errors),
                std::move(loadedImage.warnings),
                options.moreDetailAvailable};
          });
}

namespace {
struct LoadResult {
  RasterOverlayTile::LoadState state = RasterOverlayTile::LoadState::Unloaded;
  CesiumGltf::ImageCesium image = {};
  CesiumGeometry::Rectangle rectangle = {};
  std::vector<Credit> credits = {};
  void* pRendererResources = nullptr;
  bool moreDetailAvailable = true;
};

/**
 * @brief Processes the given `LoadedRasterOverlayImage`, producing a
 * `LoadResult`.
 *
 * This function is intended to be called on the worker thread.
 *
 * If the given `loadedImage` contains no valid image data, then a
 * `LoadResult` with the state `RasterOverlayTile::LoadState::Failed` will be
 * returned.
 *
 * Otherwise, the image data will be passed to
 * `IPrepareRendererResources::prepareRasterInLoadThread`, and the function
 * will return a `LoadResult` with the image, the prepared renderer resources,
 * and the state `RasterOverlayTile::LoadState::Loaded`.
 *
 * @param tileId The {@link TileID} - only used for logging
 * @param pPrepareRendererResources The `IPrepareRendererResources`
 * @param pLogger The logger
 * @param loadedImage The `LoadedRasterOverlayImage`
 * @param rendererOptions Renderer options
 * @return The `LoadResult`
 */
static LoadResult createLoadResultFromLoadedImage(
    const std::shared_ptr<IPrepareRendererResources>& pPrepareRendererResources,
    const std::shared_ptr<spdlog::logger>& pLogger,
    LoadedRasterOverlayImage&& loadedImage,
    const std::any& rendererOptions) {
  if (!loadedImage.image.has_value()) {
    SPDLOG_LOGGER_ERROR(
        pLogger,
        "Failed to load image for tile {}:\n- {}",
        "TODO",
        // Cesium3DTilesSelection::TileIdUtilities::createTileIdString(tileId),
        CesiumUtility::joinToString(loadedImage.errors, "\n- "));
    LoadResult result;
    result.state = RasterOverlayTile::LoadState::Failed;
    return result;
  }

  if (!loadedImage.warnings.empty()) {
    SPDLOG_LOGGER_WARN(
        pLogger,
        "Warnings while loading image for tile {}:\n- {}",
        "TODO",
        // Cesium3DTilesSelection::TileIdUtilities::createTileIdString(tileId),
        CesiumUtility::joinToString(loadedImage.warnings, "\n- "));
  }

  CesiumGltf::ImageCesium& image = loadedImage.image.value();

  const int32_t bytesPerPixel = image.channels * image.bytesPerChannel;
  const int64_t requiredBytes =
      static_cast<int64_t>(image.width) * image.height * bytesPerPixel;
  if (image.width > 0 && image.height > 0 &&
      image.pixelData.size() >= static_cast<size_t>(requiredBytes)) {
    CESIUM_TRACE(
        "Prepare Raster " + std::to_string(image.width) + "x" +
        std::to_string(image.height) + "x" + std::to_string(image.channels) +
        "x" + std::to_string(image.bytesPerChannel));

    void* pRendererResources = nullptr;
    if (pPrepareRendererResources) {
      pRendererResources = pPrepareRendererResources->prepareRasterInLoadThread(
          image,
          rendererOptions);
    }

    LoadResult result;
    result.state = RasterOverlayTile::LoadState::Loaded;
    result.image = std::move(image);
    result.rectangle = loadedImage.rectangle;
    result.credits = std::move(loadedImage.credits);
    result.pRendererResources = pRendererResources;
    result.moreDetailAvailable = loadedImage.moreDetailAvailable;
    return result;
  }
  LoadResult result;
  result.pRendererResources = nullptr;
  result.state = RasterOverlayTile::LoadState::Failed;
  result.moreDetailAvailable = false;
  return result;
}

} // namespace

void RasterOverlayTileProvider::doLoad(
    RasterOverlayTile& tile,
    bool isThrottledLoad) {
  if (tile.getState() != RasterOverlayTile::LoadState::Unloaded) {
    // Already loading or loaded, do nothing.
    return;
  }

  CESIUM_TRACE_USE_TRACK_SET(this->_loadingSlots);

  // Don't let this tile be destroyed while it's loading.
  tile.setState(RasterOverlayTile::LoadState::Loading);

  this->beginTileLoad(tile, isThrottledLoad);

  this->loadTileImage(tile)
      .thenInWorkerThread(
          [pPrepareRendererResources = this->getPrepareRendererResources(),
           pLogger = this->getLogger(),
           rendererOptions = this->_pOwner->getOptions().rendererOptions](
              LoadedRasterOverlayImage&& loadedImage) {
            return createLoadResultFromLoadedImage(
                pPrepareRendererResources,
                pLogger,
                std::move(loadedImage),
                rendererOptions);
          })
      .thenInMainThread(
          [this, &tile, isThrottledLoad](LoadResult&& result) noexcept {
            tile._rectangle = result.rectangle;
            tile._pRendererResources = result.pRendererResources;
            tile._image = std::move(result.image);
            tile._tileCredits = std::move(result.credits);
            tile._moreDetailAvailable =
                result.moreDetailAvailable
                    ? RasterOverlayTile::MoreDetailAvailable::Yes
                    : RasterOverlayTile::MoreDetailAvailable::No;
            tile.setState(result.state);

            this->_tileDataBytes += int64_t(tile.getImage().pixelData.size());

            this->finalizeTileLoad(tile, isThrottledLoad);
          })
      .catchInMainThread([this, &tile, isThrottledLoad](
                             const std::exception& /*e*/) {
        tile._pRendererResources = nullptr;
        tile._image = {};
        tile._tileCredits = {};
        tile._moreDetailAvailable = RasterOverlayTile::MoreDetailAvailable::No;
        tile.setState(RasterOverlayTile::LoadState::Failed);

        this->finalizeTileLoad(tile, isThrottledLoad);
      });
}

void RasterOverlayTileProvider::beginTileLoad(
    RasterOverlayTile& tile,
    bool isThrottledLoad) noexcept {
  // Keep this tile from being destroyed while it's loading.
  tile.addReference();

  ++this->_totalTilesCurrentlyLoading;
  if (isThrottledLoad) {
    ++this->_throttledTilesCurrentlyLoading;
  }
}

void RasterOverlayTileProvider::finalizeTileLoad(
    RasterOverlayTile& tile,
    bool isThrottledLoad) noexcept {
  --this->_totalTilesCurrentlyLoading;
  if (isThrottledLoad) {
    --this->_throttledTilesCurrentlyLoading;
  }

  // Release the reference we held during load to prevent
  // the tile from disappearing out from under us. This could cause
  // it to immediately be deleted.
  tile.releaseReference();
}
} // namespace Cesium3DTilesSelection
