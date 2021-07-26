#pragma once

#include "Cesium3DTilesPipeline/TileContentLoadInput.h"
#include "Cesium3DTilesPipeline/TileContentLoadResult.h"

namespace Cesium3DTilesPipeline {
/**
 * @brief Can create a {@link TileContentLoadResult} from a
 * {@link TileContentLoadInput}.
 */
class CESIUM3DTILESPIPELINE_API TileContentLoader {

public:
  virtual ~TileContentLoader() = default;

  /**
   * @brief Loads the tile content from the given input.
   *
   * @param input The {@link TileContentLoadInput}
   * @return The {@link TileContentLoadResult}. This may be the `nullptr` if the
   * tile content could not be loaded.
   */
  virtual std::unique_ptr<TileContentLoadResult>
  load(const TileContentLoadInput& input) = 0;
};
} // namespace Cesium3DTilesPipeline