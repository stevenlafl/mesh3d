#include "analysis/viewshed.h"
#include "util/log.h"
#include <cmath>
#include <algorithm>

namespace mesh3d {

void compute_viewshed(const float* elevation, int rows, int cols,
                      const mesh3d_bounds_t& bounds,
                      const NodeData& node,
                      std::vector<uint8_t>& visibility,
                      std::vector<float>& signal) {
    int total = rows * cols;
    visibility.assign(total, 0);
    signal.assign(total, -999.0f);

    /* Map node lat/lon to grid cell */
    double lat_res = (bounds.max_lat - bounds.min_lat) / (rows - 1);
    double lon_res = (bounds.max_lon - bounds.min_lon) / (cols - 1);

    int nr = static_cast<int>((bounds.max_lat - node.info.lat) / lat_res);
    int nc = static_cast<int>((node.info.lon - bounds.min_lon) / lon_res);
    nr = std::clamp(nr, 0, rows - 1);
    nc = std::clamp(nc, 0, cols - 1);

    float node_elev = elevation[nr * cols + nc];
    float antenna_h = node.info.antenna_height_m;
    if (antenna_h < 1.0f) antenna_h = 2.0f;
    float obs_h = node_elev + antenna_h;

    float max_range_km = node.info.max_range_km;
    if (max_range_km <= 0) max_range_km = 5.0f;

    /* Approximate cell size in meters (~30m for SRTM1 at mid-latitudes) */
    double center_lat = (bounds.min_lat + bounds.max_lat) * 0.5;
    double m_per_deg_lat = 111320.0;
    double m_per_deg_lon = 111320.0 * std::cos(center_lat * M_PI / 180.0);
    float cell_m_lat = static_cast<float>(lat_res * m_per_deg_lat);
    float cell_m_lon = static_cast<float>(lon_res * m_per_deg_lon);
    float cell_m = (cell_m_lat + cell_m_lon) * 0.5f;

    int max_range_cells = static_cast<int>(max_range_km * 1000.0f / cell_m);
    if (max_range_cells < 1) max_range_cells = 1;

    /* TX power for signal calculation */
    float tx_power_dbm = node.info.tx_power_dbm;
    if (tx_power_dbm <= 0) tx_power_dbm = 22.0f; // default heltec
    float antenna_gain = node.info.antenna_gain_dbi;
    float freq_mhz = node.info.frequency_mhz;
    if (freq_mhz <= 0) freq_mhz = 906.875f;

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            int dr = r - nr;
            int dc = c - nc;
            float dist_cells = std::sqrt(static_cast<float>(dr * dr + dc * dc));

            if (dist_cells < 0.5f) {
                /* Node's own cell */
                visibility[r * cols + c] = 1;
                signal[r * cols + c] = -60.0f;
                continue;
            }

            if (dist_cells > max_range_cells) continue;

            /* Walk along the ray, check line-of-sight */
            int steps = static_cast<int>(dist_cells * 1.5f) + 1;
            bool blocked = false;
            float target_elev = elevation[r * cols + c];

            for (int s = 1; s < steps; ++s) {
                float t = static_cast<float>(s) / steps;
                float sr = nr + dr * t;
                float sc = nc + dc * t;
                int si = static_cast<int>(sr);
                int sj = static_cast<int>(sc);

                if (si < 0 || si >= rows || sj < 0 || sj >= cols) continue;

                /* Height the LOS ray passes at this point */
                float needed_h = obs_h + (target_elev - obs_h) * t;
                if (elevation[si * cols + sj] > needed_h + 1.0f) {
                    blocked = true;
                    break;
                }
            }

            if (!blocked) {
                visibility[r * cols + c] = 1;

                /* Free-space path loss */
                float dist_km = dist_cells * cell_m / 1000.0f;
                if (dist_km < 0.01f) dist_km = 0.01f;
                float fspl = 20.0f * std::log10(dist_km)
                           + 20.0f * std::log10(freq_mhz)
                           + 32.44f;
                signal[r * cols + c] = tx_power_dbm + antenna_gain - fspl;
            }
        }
    }
}

void recompute_all_viewsheds(Scene& scene, const GeoProjection& proj) {
    /* Scene-level elevation grid path */
    if (!scene.elevation.empty() && scene.grid_rows >= 2 && scene.grid_cols >= 2) {
        int rows = scene.grid_rows;
        int cols = scene.grid_cols;
        int total = rows * cols;

        scene.viewshed_vis.assign(total, 0);
        scene.signal_strength.assign(total, -999.0f);
        scene.overlap_count.assign(total, 0);

        if (scene.nodes.empty()) {
            scene.build_terrain();
            LOG_INFO("Viewshed cleared (no nodes)");
            return;
        }

        for (auto& nd : scene.nodes) {
            std::vector<uint8_t> vis;
            std::vector<float> sig;
            compute_viewshed(scene.elevation.data(), rows, cols,
                             scene.bounds, nd, vis, sig);

            for (int i = 0; i < total; ++i) {
                if (vis[i]) {
                    scene.viewshed_vis[i] = 1;
                    scene.overlap_count[i]++;
                    if (sig[i] > scene.signal_strength[i])
                        scene.signal_strength[i] = sig[i];
                }
            }
        }

        scene.build_terrain();

        int vis_count = 0;
        for (auto v : scene.viewshed_vis) vis_count += v;
        float pct = 100.0f * vis_count / total;
        LOG_INFO("Viewshed computed for %zu nodes: %.1f%% coverage",
                 scene.nodes.size(), pct);
        return;
    }

    /* Tile-based elevation path — compute overlays per cached tile */
    if (scene.use_tile_system) {
        scene.tile_manager.apply_viewshed_overlays(scene.nodes, proj);
        return;
    }

    LOG_WARN("No elevation data available for viewshed computation");
}

} // namespace mesh3d
