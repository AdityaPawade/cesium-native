#include "CompositeContent.h"

#include "Cesium3DTilesSelection/GltfContent.h"
#include "Cesium3DTilesSelection/TileContentFactory.h"
#include "Cesium3DTilesSelection/spdlog-cesium.h"

#include <CesiumAsync/IAssetRequest.h>
#include <CesiumAsync/IAssetResponse.h>
#include <CesiumUtility/Tracing.h>

#include <cstddef>

namespace {
#pragma pack(push, 1)
struct CmptHeader {
  char magic[4];
  uint32_t version;
  uint32_t byteLength;
  uint32_t tilesLength;
};

struct InnerHeader {
  char magic[4];
  uint32_t version;
  uint32_t byteLength;
};
#pragma pack(pop)

static_assert(sizeof(CmptHeader) == 16);
static_assert(sizeof(InnerHeader) == 12);
} // namespace

namespace Cesium3DTilesSelection {

namespace {

class DerivedInnerResponse : public CesiumAsync::IAssetResponse {
private:
  const IAssetResponse* pOriginalResponse;
  gsl::span<const std::byte> derivedData;

public:
  DerivedInnerResponse(
      const IAssetResponse* pOriginalResponse_,
      const gsl::span<const std::byte> derivedData_)
      : pOriginalResponse(pOriginalResponse_), derivedData(derivedData_) {}

  uint16_t statusCode() const override {
    return pOriginalResponse->statusCode();
  }

  std::string contentType() const override { return ""; }

  const CesiumAsync::HttpHeaders& headers() const override {
    return pOriginalResponse->headers();
  }

  gsl::span<const std::byte> data() const override { return derivedData; }
};

class DerivedInnerRequest : public CesiumAsync::IAssetRequest {
private:
  std::shared_ptr<CesiumAsync::IAssetRequest> pOriginalRequest;
  std::unique_ptr<DerivedInnerResponse> pDerivedResponse;

public:
  DerivedInnerRequest(
      const std::shared_ptr<CesiumAsync::IAssetRequest>& pOriginalRequest_,
      const gsl::span<const std::byte> data)
      : pOriginalRequest(pOriginalRequest_),
        pDerivedResponse(std::make_unique<DerivedInnerResponse>(
            pOriginalRequest->response(),
            data)) {}

  const std::string& method() const override {
    return this->pOriginalRequest->method();
  }

  const std::string& url() const override {
    return this->pOriginalRequest->url();
  }

  const CesiumAsync::HttpHeaders& headers() const override {
    return this->pOriginalRequest->headers();
  }

  const CesiumAsync::IAssetResponse* response() const override {
    return this->pDerivedResponse.get();
  }
};

/**
 * @brief Derive a {@link TileContentLoadInput} from the given one.
 *
 * This will return a new instance where all properies are set to be
 * the same as in the given input, except for the content type (which
 * is set to be the empty string), and the data, which is set to be
 * the given derived data.
 *
 * @param input The original input.
 * @param derivedData The data for the result.
 * @return The result.
 */
TileContentLoadInput derive(
    const TileContentLoadInput& input,
    const gsl::span<const std::byte>& derivedData) {
  return TileContentLoadInput(
      input.pAsyncSystem,
      input.pLogger,
      input.pAssetAccessor,
      std::make_shared<DerivedInnerRequest>(input.pRequest, derivedData),
      input.tileID,
      input.tileBoundingVolume,
      input.tileContentBoundingVolume,
      input.tileRefine,
      input.tileGeometricError,
      input.tileTransform,
      input.contentOptions);
}
} // namespace

CesiumAsync::Future<std::unique_ptr<TileContentLoadResult>>
CompositeContent::load(const TileContentLoadInput& input) {
  CESIUM_TRACE("Cesium3DTilesSelection::CompositeContent::load");
  const std::shared_ptr<spdlog::logger>& pLogger = input.pLogger;
  const gsl::span<const std::byte>& data = input.pRequest->response()->data();
  const std::string& url = input.pRequest->url();

  if (data.size() < sizeof(CmptHeader)) {
    SPDLOG_LOGGER_WARN(
        pLogger,
        "Composite tile {} must be at least 16 bytes.",
        url);
    return input.pAsyncSystem->createResolvedFuture(
        std::unique_ptr<TileContentLoadResult>(nullptr));
  }

  const CmptHeader* pHeader = reinterpret_cast<const CmptHeader*>(data.data());
  if (std::string(pHeader->magic, 4) != "cmpt") {
    SPDLOG_LOGGER_WARN(
        pLogger,
        "Composite tile does not have the expected magic vaue 'cmpt'.");
    return input.pAsyncSystem->createResolvedFuture(
        std::unique_ptr<TileContentLoadResult>(nullptr));
  }

  if (pHeader->version != 1) {
    SPDLOG_LOGGER_WARN(
        pLogger,
        "Unsupported composite tile version {}.",
        pHeader->version);
    return input.pAsyncSystem->createResolvedFuture(
        std::unique_ptr<TileContentLoadResult>(nullptr));
  }

  if (pHeader->byteLength > data.size()) {
    SPDLOG_LOGGER_WARN(
        pLogger,
        "Composite tile byteLength is {} but only {} bytes are available.",
        pHeader->byteLength,
        data.size());
    return input.pAsyncSystem->createResolvedFuture(
        std::unique_ptr<TileContentLoadResult>(nullptr));
  }

  std::vector<CesiumAsync::Future<std::unique_ptr<TileContentLoadResult>>>
      innerTiles;
  uint32_t pos = sizeof(CmptHeader);

  for (uint32_t i = 0; i < pHeader->tilesLength && pos < pHeader->byteLength;
       ++i) {
    if (pos + sizeof(InnerHeader) > pHeader->byteLength) {
      SPDLOG_LOGGER_WARN(
          pLogger,
          "Composite tile ends before all embedded tiles could be read.");
      break;
    }
    const InnerHeader* pInner =
        reinterpret_cast<const InnerHeader*>(data.data() + pos);
    if (pos + pInner->byteLength > pHeader->byteLength) {
      SPDLOG_LOGGER_WARN(
          pLogger,
          "Composite tile ends before all embedded tiles could be read.");
      break;
    }

    const gsl::span<const std::byte> innerData(
        data.data() + pos,
        pInner->byteLength);

    pos += pInner->byteLength;

    innerTiles.push_back(
        TileContentFactory::createContent(derive(input, innerData)));
  }

  return input.pAsyncSystem->all(std::move(innerTiles))
      .thenInWorkerThread(
          [pLogger, tilesLength = pHeader->tilesLength](
              std::vector<std::unique_ptr<TileContentLoadResult>>&&
                  innerTilesResult) -> std::unique_ptr<TileContentLoadResult> {
            if (innerTilesResult.empty()) {
              if (tilesLength > 0) {
                SPDLOG_LOGGER_WARN(
                    pLogger,
                    "Composite tile does not contain any loadable inner "
                    "tiles.");
              }
              return std::unique_ptr<TileContentLoadResult>(nullptr);
            }
            if (innerTilesResult.size() == 1) {
              return std::move(innerTilesResult[0]);
            }

            std::unique_ptr<TileContentLoadResult> pResult =
                std::move(innerTilesResult[0]);

            for (size_t i = 1; i < innerTilesResult.size(); ++i) {
              if (!innerTilesResult[i] || !innerTilesResult[i]->model) {
                continue;
              }

              if (pResult->model) {
                pResult->model.value().merge(
                    std::move(innerTilesResult[i]->model.value()));
              } else {
                pResult->model = std::move(innerTilesResult[i]->model);
              }
            }

            return pResult;
          });
}

} // namespace Cesium3DTilesSelection
