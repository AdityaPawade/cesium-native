#include "Cesium3DTilesSelection/IonRasterOverlay.h"

#include "Cesium3DTilesSelection/BingMapsRasterOverlay.h"
#include "Cesium3DTilesSelection/RasterOverlayLoadFailureDetails.h"
#include "Cesium3DTilesSelection/RasterOverlayTile.h"
#include "Cesium3DTilesSelection/RasterOverlayTileProvider.h"
#include "Cesium3DTilesSelection/TileMapServiceRasterOverlay.h"
#include "Cesium3DTilesSelection/TilesetExternals.h"
#include "Cesium3DTilesSelection/spdlog-cesium.h"

#include <CesiumAsync/IAssetAccessor.h>
#include <CesiumAsync/IAssetResponse.h>
#include <CesiumUtility/JsonHelpers.h>
#include <CesiumUtility/Uri.h>

#include <rapidjson/document.h>

using namespace CesiumAsync;
using namespace CesiumUtility;

namespace Cesium3DTilesSelection {

IonRasterOverlay::IonRasterOverlay(
    const std::string& name,
    uint32_t ionAssetID,
    const std::string& ionAccessToken,
    const RasterOverlayOptions& overlayOptions)
    : RasterOverlay(name, overlayOptions),
      _ionAssetID(ionAssetID),
      _ionAccessToken(ionAccessToken) {}

IonRasterOverlay::~IonRasterOverlay() {}

Future<std::unique_ptr<RasterOverlayTileProvider>>
IonRasterOverlay::createTileProvider(
    const CesiumAsync::AsyncSystem& asyncSystem,
    const std::shared_ptr<CesiumAsync::IAssetAccessor>& pAssetAccessor,
    const std::shared_ptr<CreditSystem>& pCreditSystem,
    const std::shared_ptr<IPrepareRendererResources>& pPrepareRendererResources,
    const std::shared_ptr<spdlog::logger>& pLogger,
    RasterOverlay* pOwner) {
  std::string ionUrl = "https://api.cesium.com/v1/assets/" +
                       std::to_string(this->_ionAssetID) + "/endpoint";
  ionUrl = CesiumUtility::Uri::addQuery(
      ionUrl,
      "access_token",
      this->_ionAccessToken);

  pOwner = pOwner ? pOwner : this;

  auto reportError = [this, asyncSystem, pLogger](
                         std::shared_ptr<IAssetRequest>&& pRequest,
                         const std::string& message) {
    this->reportError(
        asyncSystem,
        pLogger,
        RasterOverlayLoadFailureDetails{
            this,
            RasterOverlayLoadType::CesiumIon,
            std::move(pRequest),
            message});
  };

  return pAssetAccessor->get(asyncSystem, ionUrl)
      .thenInWorkerThread(
          [name = this->getName(), pLogger, reportError](
              std::shared_ptr<IAssetRequest>&& pRequest)
              -> std::unique_ptr<RasterOverlay> {
            const IAssetResponse* pResponse = pRequest->response();

            rapidjson::Document response;
            response.Parse(
                reinterpret_cast<const char*>(pResponse->data().data()),
                pResponse->data().size());

            if (response.HasParseError()) {
              reportError(
                  std::move(pRequest),
                  fmt::format(
                      "Error when parsing ion raster overlay response, error "
                      "code {} at byte offset {}",
                      response.GetParseError(),
                      response.GetErrorOffset()));
              return nullptr;
            }

            std::string type =
                JsonHelpers::getStringOrDefault(response, "type", "unknown");
            if (type != "IMAGERY") {
              reportError(
                  std::move(pRequest),
                  fmt::format(
                      "Ion raster overlay metadata response type is not "
                      "'IMAGERY', but {}",
                      type));
              return nullptr;
            }

            std::string externalType = JsonHelpers::getStringOrDefault(
                response,
                "externalType",
                "unknown");
            if (externalType == "BING") {
              const auto optionsIt = response.FindMember("options");
              if (optionsIt == response.MemberEnd() ||
                  !optionsIt->value.IsObject()) {
                reportError(
                    std::move(pRequest),
                    fmt::format(
                        "Cesium ion Bing Maps raster overlay metadata response "
                        "does not contain 'options' or it is not an object."));
                return nullptr;
              }

              const auto& options = optionsIt->value;
              std::string url =
                  JsonHelpers::getStringOrDefault(options, "url", "");
              std::string key =
                  JsonHelpers::getStringOrDefault(options, "key", "");
              std::string mapStyle = JsonHelpers::getStringOrDefault(
                  options,
                  "mapStyle",
                  "AERIAL");
              std::string culture =
                  JsonHelpers::getStringOrDefault(options, "culture", "");

              return std::make_unique<BingMapsRasterOverlay>(
                  name,
                  url,
                  key,
                  mapStyle,
                  culture);
            }
            std::string url =
                JsonHelpers::getStringOrDefault(response, "url", "");
            return std::make_unique<TileMapServiceRasterOverlay>(
                name,
                url,
                std::vector<CesiumAsync::IAssetAccessor::THeader>{
                    std::make_pair(
                        "Authorization",
                        "Bearer " + JsonHelpers::getStringOrDefault(
                                        response,
                                        "accessToken",
                                        ""))});
          })
      .thenInMainThread(
          [asyncSystem,
           pOwner,
           pAssetAccessor,
           pCreditSystem,
           pPrepareRendererResources,
           pLogger](std::unique_ptr<RasterOverlay>&& pAggregatedOverlay) {
            // Handle the case that the code above bails out with an error,
            // returning a nullptr.
            if (pAggregatedOverlay) {
              return pAggregatedOverlay->createTileProvider(
                  asyncSystem,
                  pAssetAccessor,
                  pCreditSystem,
                  pPrepareRendererResources,
                  pLogger,
                  pOwner);
            }
            return asyncSystem.createResolvedFuture<
                std::unique_ptr<RasterOverlayTileProvider>>(nullptr);
          });
}

} // namespace Cesium3DTilesSelection
