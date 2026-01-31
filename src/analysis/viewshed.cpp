#include "analysis/viewshed.h"
#include "analysis/gpu_viewshed.h"
#include "util/log.h"
#include <cmath>
#include <algorithm>
#include <chrono>

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

    /* Node may be off-grid (on an adjacent tile); use nearest edge cell for elevation */
    int nr_elev = std::clamp(nr, 0, rows - 1);
    int nc_elev = std::clamp(nc, 0, cols - 1);
    float node_elev = elevation[nr_elev * cols + nc_elev];
    float antenna_h = node.info.antenna_height_m;
    if (antenna_h < 1.0f) antenna_h = 2.0f;
    float obs_h = node_elev + antenna_h;

    /* Approximate cell size in meters (~30m for SRTM1 at mid-latitudes) */
    double center_lat = (bounds.min_lat + bounds.max_lat) * 0.5;
    double m_per_deg_lat = 111320.0;
    double m_per_deg_lon = 111320.0 * std::cos(center_lat * M_PI / 180.0);
    float cell_m_lat = static_cast<float>(lat_res * m_per_deg_lat);
    float cell_m_lon = static_cast<float>(lon_res * m_per_deg_lon);
    float cell_m = (cell_m_lat + cell_m_lon) * 0.5f;

    /* TX power for signal calculation */
    float tx_power_dbm = node.info.tx_power_dbm;
    if (tx_power_dbm <= 0) tx_power_dbm = 22.0f; // default heltec
    float antenna_gain = node.info.antenna_gain_dbi;
    float freq_mhz = node.info.frequency_mhz;
    if (freq_mhz <= 0) freq_mhz = 906.875f;
    float cable_loss = node.info.cable_loss_db;
    float rx_sens = node.info.rx_sensitivity_dbm;
    if (rx_sens >= 0) rx_sens = -132.0f; // default

    /* Max range: full grid diagonal — let signal attenuation handle clipping */
    float eirp = tx_power_dbm + antenna_gain - cable_loss;
    int max_range_cells = static_cast<int>(
        std::sqrt(static_cast<float>(rows * rows + cols * cols)));

    /* Earth curvature factor: 1 / (2 * k * Re) where k=4/3, Re=6371000m */
    const float earth_curve_factor = 1.0f / (2.0f * (4.0f / 3.0f) * 6371000.0f);

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

            /* Walk along the ray with earth curvature and diffraction */
            int steps = static_cast<int>(dist_cells * 1.5f) + 1;
            float target_elev = elevation[r * cols + c];
            float d_total = dist_cells * cell_m;

            /* Find maximum obstruction above LOS line (Deygout method) */
            float max_violation = 0.0f;
            float best_t = 0.0f;

            for (int s = 1; s < steps; ++s) {
                float t = static_cast<float>(s) / steps;
                float sr = nr + dr * t;
                float sc = nc + dc * t;
                int si = static_cast<int>(sr);
                int sj = static_cast<int>(sc);

                if (si < 0 || si >= rows || sj < 0 || sj >= cols) continue;

                /* LOS height with 4/3 earth curvature correction */
                float d_along = d_total * t;
                float d_remain = d_total * (1.0f - t);
                float earth_curve = d_along * d_remain * earth_curve_factor;
                float needed_h = obs_h + (target_elev - obs_h) * t - earth_curve;
                float terrain_h = elevation[si * cols + sj];
                float violation = terrain_h - needed_h;
                if (violation > max_violation) {
                    max_violation = violation;
                    best_t = t;
                }
            }

            /* Free-space path loss */
            float dist_km = d_total / 1000.0f;
            if (dist_km < 0.01f) dist_km = 0.01f;
            float fspl = 20.0f * std::log10(dist_km)
                       + 20.0f * std::log10(freq_mhz)
                       + 32.44f;

            /* EIRP with cable loss */
            float eirp = tx_power_dbm + antenna_gain - cable_loss;

            /* Knife-edge diffraction loss (ITU-R P.526) */
            float diff_loss_db = 0.0f;
            if (max_violation > 0.0f) {
                float lambda = 299.792458f / freq_mhz;
                float d1 = d_total * best_t;
                float d2 = d_total * (1.0f - best_t);
                float d_harmonic = d1 * d2 / (d1 + d2);
                float v = max_violation * std::sqrt(2.0f / (lambda * d_harmonic));
                if (v > -0.78f) {
                    diff_loss_db = 6.9f + 20.0f * std::log10(
                        std::sqrt((v - 0.1f) * (v - 0.1f) + 1.0f) + v - 0.1f);
                }
            }

            float received = eirp - fspl - diff_loss_db;

            /* Visibility based on RX sensitivity threshold */
            if (received >= rx_sens) {
                visibility[r * cols + c] = 1;
            }
            signal[r * cols + c] = received;
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

void recompute_all_viewsheds_gpu(Scene& scene, const GeoProjection& proj,
                                  GpuViewshed* gpu) {
    /* Fall back to CPU if GPU is not available */
    if (!gpu || !GpuViewshed::is_available()) {
        recompute_all_viewsheds(scene, proj);
        return;
    }

    /* Scene-level elevation grid path */
    if (!scene.elevation.empty() && scene.grid_rows >= 2 && scene.grid_cols >= 2) {
        int rows = scene.grid_rows;
        int cols = scene.grid_cols;
        int total = rows * cols;

        if (scene.nodes.empty()) {
            scene.viewshed_vis.assign(total, 0);
            scene.signal_strength.assign(total, -999.0f);
            scene.overlap_count.assign(total, 0);
            scene.build_terrain();
            LOG_INFO("Viewshed cleared (no nodes)");
            return;
        }

        /* Upload elevation and compute on GPU */
        gpu->upload_elevation(scene.elevation.data(), rows, cols);
        gpu->set_grid_params(scene.bounds, rows, cols);
        gpu->compute_all(scene.nodes);
        gpu->read_back(scene.viewshed_vis, scene.signal_strength, scene.overlap_count);

        scene.build_terrain();

        int vis_count = 0;
        for (auto v : scene.viewshed_vis) vis_count += v;
        float pct = 100.0f * vis_count / total;
        LOG_INFO("GPU viewshed computed for %zu nodes: %.1f%% coverage",
                 scene.nodes.size(), pct);
        return;
    }

    /* Tile-based elevation path */
    if (scene.use_tile_system) {
        scene.tile_manager.apply_viewshed_overlays_gpu(scene.nodes, proj, gpu);
        return;
    }

    LOG_WARN("No elevation data available for viewshed computation");
}

void kick_viewshed_recompute(Scene& scene, const GeoProjection& proj,
                              GpuViewshed* gpu) {
    /* Fall back to blocking CPU path if GPU not available */
    if (!gpu || !GpuViewshed::is_available()) {
        LOG_INFO("kick_viewshed: CPU fallback (gpu=%p, available=%s)",
                 (void*)gpu, (gpu && GpuViewshed::is_available()) ? "yes" : "no");
        auto t0 = std::chrono::steady_clock::now();
        recompute_all_viewsheds(scene, proj);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count();
        LOG_INFO("kick_viewshed: CPU fallback took %lld ms", (long long)ms);
        return;
    }

    /* Scene-level elevation grid path */
    if (!scene.elevation.empty() && scene.grid_rows >= 2 && scene.grid_cols >= 2) {
        int rows = scene.grid_rows;
        int cols = scene.grid_cols;
        int total = rows * cols;

        if (scene.nodes.empty()) {
            scene.viewshed_vis.assign(total, 0);
            scene.signal_strength.assign(total, -999.0f);
            scene.overlap_count.assign(total, 0);
            scene.build_terrain();
            LOG_INFO("Viewshed cleared (no nodes)");
            return;
        }

        LOG_INFO("kick_viewshed: GPU async scene-level (%dx%d, %zu nodes)",
                 cols, rows, scene.nodes.size());
        gpu->upload_elevation(scene.elevation.data(), rows, cols);
        gpu->set_grid_params(scene.bounds, rows, cols);
        gpu->compute_all_async(scene.nodes, scene.elevation.data());
        return;
    }

    /* Tile-based elevation path — dispatch per-tile GPU viewshed */
    if (scene.use_tile_system) {
        LOG_INFO("kick_viewshed: GPU async tile path (%zu nodes)",
                 scene.nodes.size());
        scene.tile_manager.kick_viewshed_gpu(scene.nodes, proj, gpu);
        return;
    }

    LOG_WARN("No elevation data available for viewshed computation");
}

void poll_viewshed_recompute(Scene& scene, const GeoProjection& proj,
                              GpuViewshed* gpu) {
    if (!gpu) return;

    /* Scene-level grid path */
    if (!scene.elevation.empty() && scene.grid_rows >= 2 && scene.grid_cols >= 2) {
        if (gpu->poll_state() != ComputeState::READY) return;

        gpu->read_back_async(scene.viewshed_vis, scene.signal_strength,
                              scene.overlap_count);
        scene.build_terrain();

        int vis_count = 0;
        for (auto v : scene.viewshed_vis) vis_count += v;
        int total = scene.grid_rows * scene.grid_cols;
        float pct = 100.0f * vis_count / total;
        LOG_INFO("Async GPU viewshed computed for %zu nodes: %.1f%% coverage",
                 scene.nodes.size(), pct);
        return;
    }

    /* Tile-based path */
    if (scene.use_tile_system) {
        scene.tile_manager.poll_viewshed_gpu(scene.nodes, proj, gpu);
    }
}

} // namespace mesh3d
