#pragma once
#include "scene/scene.h"
#include "util/math_util.h"
#include <vector>
#include <cstdint>

namespace mesh3d {

class GpuViewshed;

/* Compute line-of-sight viewshed and signal strength for a single node
   on an elevation grid.

   elevation: row-major float array, rows x cols
   bounds:    geographic bounds of the grid
   node:      node to compute viewshed for

   Outputs:
     visibility: rows x cols uint8 (0=not visible, 1=visible)
     signal:     rows x cols float (dBm, -999 = no signal)
*/
void compute_viewshed(const float* elevation, int rows, int cols,
                      const mesh3d_bounds_t& bounds,
                      const NodeData& node,
                      std::vector<uint8_t>& visibility,
                      std::vector<float>& signal);

/* Recompute merged viewshed/signal for all nodes in the scene,
   then rebuild the terrain mesh. Uses scene.elevation grid.
   For tile-based scenes, does nothing (no scene-level grid). */
void recompute_all_viewsheds(Scene& scene, const GeoProjection& proj);

/* GPU-accelerated version. Falls back to CPU if gpu is null or unavailable. */
void recompute_all_viewsheds_gpu(Scene& scene, const GeoProjection& proj,
                                  GpuViewshed* gpu);

} // namespace mesh3d
