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
    int64_t ionAssetID,
    const std::string& ionAccessToken,
    const RasterOverlayOptions& overlayOptions)
    : RasterOverlay(name, overlayOptions),
      _ionAssetID(ionAssetID),
      _ionAccessToken(ionAccessToken) {}

IonRasterOverlay::~IonRasterOverlay() {}

std::unordered_map<std::string, IonRasterOverlay::ExternalAssetEndpoint>
    IonRasterOverlay::endpointCache;

Future<std::unique_ptr<RasterOverlayTileProvider>>
IonRasterOverlay::createTileProvider(
    const ExternalAssetEndpoint& endpoint,
    const std::shared_ptr<CesiumAsync::AsyncSystem>& pAsyncSystem,
    const std::shared_ptr<CesiumAsync::IAssetAccessor>& pAssetAccessor,
    const std::shared_ptr<CreditSystem>& pCreditSystem,
    const std::shared_ptr<IPrepareRendererResources>& pPrepareRendererResources,
    const std::shared_ptr<spdlog::logger>& pLogger,
    RasterOverlay* pOwner) {
  if (pCreditSystem) {
    for (const auto& attribution : endpoint.attributions) {
      _credits.push_back(pCreditSystem->createCredit(
          attribution.html,
          !attribution.collapsible || this->getOptions().showCreditsOnScreen));
    }
  }
  if (endpoint.externalType == "BING") {
    _pAggregatedOverlay = std::make_unique<BingMapsRasterOverlay>(
        this->getName(),
        endpoint.url,
        endpoint.key,
        endpoint.mapStyle,
        endpoint.culture);
  } else {
    _pAggregatedOverlay = std::make_unique<TileMapServiceRasterOverlay>(
        this->getName(),
        endpoint.url,
        std::vector<CesiumAsync::IAssetAccessor::THeader>{
            std::make_pair("Authorization", "Bearer " + endpoint.accessToken)});
  }
  return _pAggregatedOverlay->createTileProvider(
      pAsyncSystem,
      pAssetAccessor,
      pCreditSystem,
      pPrepareRendererResources,
      pLogger,
      pOwner);
}

Future<std::unique_ptr<RasterOverlayTileProvider>>
IonRasterOverlay::createTileProvider(
    const std::shared_ptr<CesiumAsync::AsyncSystem>& pAsyncSystem,
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

  auto cacheIt = IonRasterOverlay::endpointCache.find(ionUrl);
  if (cacheIt != IonRasterOverlay::endpointCache.end()) {
    return createTileProvider(
        cacheIt->second,
        pAsyncSystem,
        pAssetAccessor,
        pCreditSystem,
        pPrepareRendererResources,
        pLogger,
        pOwner);
  }

  auto reportError = [this, pAsyncSystem, pLogger](
                         std::shared_ptr<IAssetRequest>&& pRequest,
                         const std::string& message) {
    this->reportError(
        pAsyncSystem,
        pLogger,
        RasterOverlayLoadFailureDetails{
            this,
            RasterOverlayLoadType::CesiumIon,
            std::move(pRequest),
            message});
  };

  return pAssetAccessor->get(pAsyncSystem, ionUrl)
      .thenInWorkerThread(
          [name = this->getName(), pLogger, reportError](
              std::shared_ptr<IAssetRequest>&& pRequest)
              -> std::optional<ExternalAssetEndpoint> {
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
              return std::nullopt;
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
              return std::nullopt;
            }

            ExternalAssetEndpoint endpoint;
            endpoint.externalType = JsonHelpers::getStringOrDefault(
                response,
                "externalType",
                "unknown");
            if (endpoint.externalType == "BING") {
              const auto optionsIt = response.FindMember("options");
              if (optionsIt == response.MemberEnd() ||
                  !optionsIt->value.IsObject()) {
                reportError(
                    std::move(pRequest),
                    fmt::format(
                        "Cesium ion Bing Maps raster overlay metadata response "
                        "does not contain 'options' or it is not an object."));
                return std::nullopt;
              }

              const auto attributionsIt = response.FindMember("attributions");
              if (attributionsIt != response.MemberEnd() &&
                  attributionsIt->value.IsArray()) {

                for (const rapidjson::Value& attribution :
                     attributionsIt->value.GetArray()) {
                  AssetEndpointAttribution& endpointAttribution =
                      endpoint.attributions.emplace_back();
                  const auto html = attribution.FindMember("html");
                  if (html != attribution.MemberEnd() &&
                      html->value.IsString()) {
                    endpointAttribution.html = html->value.GetString();
                  }
                  auto collapsible = attribution.FindMember("collapsible");
                  if (collapsible != attribution.MemberEnd() &&
                      collapsible->value.IsBool()) {
                    endpointAttribution.collapsible =
                        collapsible->value.GetBool();
                  }
                }
              }

              const auto& options = optionsIt->value;
              endpoint.url =
                  JsonHelpers::getStringOrDefault(options, "url", "");
              endpoint.key =
                  JsonHelpers::getStringOrDefault(options, "key", "");
              endpoint.mapStyle = JsonHelpers::getStringOrDefault(
                  options,
                  "mapStyle",
                  "AERIAL");
              endpoint.culture =
                  JsonHelpers::getStringOrDefault(options, "culture", "");
            } else {
              endpoint.url =
                  JsonHelpers::getStringOrDefault(response, "url", "");
              endpoint.accessToken =
                  JsonHelpers::getStringOrDefault(response, "accessToken", "");
            }
            return endpoint;
          })
      .thenInMainThread(
          [pAsyncSystem,
           pOwner,
           pAssetAccessor,
           pCreditSystem,
           pPrepareRendererResources,
           ionUrl,
           this,
           pLogger](const std::optional<ExternalAssetEndpoint>& endpoint) {
            if (endpoint) {
              IonRasterOverlay::endpointCache[ionUrl] = *endpoint;
              return createTileProvider(
                  *endpoint,
                  pAsyncSystem,
                  pAssetAccessor,
                  pCreditSystem,
                  pPrepareRendererResources,
                  pLogger,
                  pOwner);
            }
            return pAsyncSystem->createResolvedFuture<
                std::unique_ptr<RasterOverlayTileProvider>>(nullptr);
          });
}
} // namespace Cesium3DTilesSelection
