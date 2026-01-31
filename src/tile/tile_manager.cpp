#include "tile/tile_manager.h"
#include "tile/hgt_provider.h"
#include "tile/url_tile_provider.h"
#include "analysis/viewshed.h"
#include "analysis/gpu_viewshed.h"
#include "scene/scene.h"
#include "camera/camera.h"
#include "util/log.h"
#include <cstring>
#include <algorithm>
#include <chrono>

namespace mesh3d {

void TileManager::set_elevation_provider(std::unique_ptr<TileProvider> provider) {
    m_elev_provider = std::move(provider);
    m_elev_loaded = false;
}

void TileManager::set_imagery_provider(std::unique_ptr<TileProvider> provider) {
    m_imagery_provider = std::move(provider);
    /* Clear cached imagery textures — geometry stays */
}

void TileManager::set_imagery_source(ImagerySource src) {
    if (src == m_imagery_source) return;
    m_imagery_source = src;

    /* Replace the imagery provider */
    switch (src) {
    case ImagerySource::SATELLITE:
        m_imagery_provider = UrlTileProvider::satellite();
        break;
    case ImagerySource::STREET:
        m_imagery_provider = UrlTileProvider::street();
        break;
    case ImagerySource::NONE:
        m_imagery_provider.reset();
        break;
    }

    /* Strip textures from ALL cached tiles so they get new imagery,
       but keep the geometry (meshes) intact to avoid re-reading HGT data. */
    m_cache.for_each_mut([](TileRenderable& tr) {
        tr.texture = Texture();
    });

    const char* names[] = {"satellite", "street", "none"};
    LOG_INFO("Imagery source: %s", names[static_cast<int>(src)]);
}

void TileManager::cycle_imagery_source() {
    int next = (static_cast<int>(m_imagery_source) + 1) % 3;
    set_imagery_source(static_cast<ImagerySource>(next));
}

void TileManager::set_bounds(const mesh3d_bounds_t& bounds) {
    m_bounds = bounds;
    m_proj.init(bounds);
    m_bounds_set = true;
    m_elev_loaded = false;
}

void TileManager::update() {
    if (!m_bounds_set) return;

    ensure_elevation_tiles();
    ensure_imagery_tiles();
}

void TileManager::ensure_elevation_tiles() {
    if (!m_elev_provider || m_elev_loaded) return;

    m_visible_elev = m_elev_provider->tiles_in_bounds(m_bounds, 0);

    bool all_loaded = true;
    for (auto& coord : m_visible_elev) {
        if (m_cache.has(coord)) continue;
        if (m_loader.is_pending(coord)) {
            all_loaded = false;
            continue;
        }
        m_loader.request(coord, m_elev_provider.get());
        all_loaded = false;
    }

    drain_ready_tiles();

    /* Only mark as loaded once all tiles are in cache */
    if (all_loaded) {
        bool really_done = true;
        for (auto& coord : m_visible_elev) {
            if (!m_cache.has(coord)) { really_done = false; break; }
        }
        m_elev_loaded = really_done;
    }
}

void TileManager::ensure_imagery_tiles() {
    if (!m_imagery_provider || !m_bounds_set) return;
    if (m_imagery_source == ImagerySource::NONE) return;

    m_visible_imagery = m_selector.select(m_bounds);

    /* Composite imagery for tiles that still need textures.
       composite_imagery_for_tile calls fetch_tile synchronously per imagery
       sub-tile; the provider's disk cache prevents redundant downloads. */
    for (auto& elev_coord : m_visible_elev) {
        TileRenderable* tr = m_cache.get(elev_coord);
        if (!tr || tr->texture.valid()) continue;
        composite_imagery_for_tile(tr);
    }
}

void TileManager::composite_imagery_for_tile(TileRenderable* tr) {
    if (!m_imagery_provider || !tr) return;

    /* Find a zoom level where the tile fits within MAX_COMPOSITE_DIM.
       Start from the selector's preferred zoom and reduce if needed. */
    static constexpr int MAX_COMPOSITE_DIM = 16; // 16x16 tiles = 4096x4096

    int zoom = m_selector.fixed_zoom;
    std::vector<TileCoord> imagery_coords;
    int min_x, max_x, min_y, max_y, tiles_x, tiles_y;

    while (zoom >= 0) {
        imagery_coords = bounds_to_tile_range(tr->bounds, zoom);
        if (imagery_coords.empty()) return;

        min_x = imagery_coords[0].x; max_x = min_x;
        min_y = imagery_coords[0].y; max_y = min_y;
        for (auto& c : imagery_coords) {
            if (c.x < min_x) min_x = c.x;
            if (c.x > max_x) max_x = c.x;
            if (c.y < min_y) min_y = c.y;
            if (c.y > max_y) max_y = c.y;
        }
        tiles_x = max_x - min_x + 1;
        tiles_y = max_y - min_y + 1;

        if (tiles_x <= MAX_COMPOSITE_DIM && tiles_y <= MAX_COMPOSITE_DIM)
            break;
        --zoom;
    }
    if (zoom < 0) return;

    int tile_px = 256; // standard slippy tile size
    int comp_w = tiles_x * tile_px;
    int comp_h = tiles_y * tile_px;
    std::vector<uint8_t> composite(comp_w * comp_h * 4, 0);

    int fetched = 0;
    for (auto& coord : imagery_coords) {
        auto data = m_imagery_provider->fetch_tile(coord);
        if (!data || data->imagery.empty()) continue;

        int ox = (coord.x - min_x) * tile_px;
        int oy = (coord.y - min_y) * tile_px;

        /* Copy tile pixels into composite */
        int tw = std::min(data->img_width, tile_px);
        int th = std::min(data->img_height, tile_px);
        for (int row = 0; row < th; ++row) {
            int src_off = row * data->img_width * 4;
            int dst_off = ((oy + row) * comp_w + ox) * 4;
            if (dst_off + tw * 4 <= static_cast<int>(composite.size()) &&
                src_off + tw * 4 <= static_cast<int>(data->imagery.size())) {
                std::memcpy(&composite[dst_off], &data->imagery[src_off], tw * 4);
            }
        }
        ++fetched;
    }

    if (fetched == 0) return;

    /* Crop composite to match the elevation tile bounds exactly.
       The composite covers the full slippy map tile grid (min_x..max_x+1,
       min_y..max_y+1) which is typically larger than the HGT tile.
       Use fractional tile coordinates to find the pixel region. */
    double fx0 = lon_to_tile_x_frac(tr->bounds.min_lon, zoom) - min_x;
    double fx1 = lon_to_tile_x_frac(tr->bounds.max_lon, zoom) - min_x;
    double fy0 = lat_to_tile_y_frac(tr->bounds.max_lat, zoom) - min_y; // north = top
    double fy1 = lat_to_tile_y_frac(tr->bounds.min_lat, zoom) - min_y; // south = bottom

    int cx0 = std::max(0, static_cast<int>(std::round(fx0 * tile_px)));
    int cy0 = std::max(0, static_cast<int>(std::round(fy0 * tile_px)));
    int cx1 = std::min(comp_w, static_cast<int>(std::round(fx1 * tile_px)));
    int cy1 = std::min(comp_h, static_cast<int>(std::round(fy1 * tile_px)));
    int crop_w = cx1 - cx0;
    int crop_h = cy1 - cy0;

    if (crop_w <= 0 || crop_h <= 0) return;

    std::vector<uint8_t> cropped(crop_w * crop_h * 4);
    for (int row = 0; row < crop_h; ++row) {
        int src = ((cy0 + row) * comp_w + cx0) * 4;
        int dst = row * crop_w * 4;
        std::memcpy(&cropped[dst], &composite[src], crop_w * 4);
    }

    LOG_INFO("Composited %d/%zu imagery tiles, cropped %dx%d -> %dx%d px",
             fetched, imagery_coords.size(), comp_w, comp_h, crop_w, crop_h);

    Texture tex;
    tex.load_rgba(cropped.data(), crop_w, crop_h);
    tr->texture = std::move(tex);
}

void TileManager::render(DrawFn fn) const {
    if (m_hgt_provider) {
        /* HGT mode: render everything in the cache */
        m_cache.for_each([&](const TileRenderable& tr) {
            if (tr.mesh.valid()) fn(tr);
        });
    } else {
        for (auto& coord : m_visible_elev) {
            auto it_check = const_cast<TileCache&>(m_cache).get(coord);
            if (it_check && it_check->mesh.valid()) {
                fn(*it_check);
            }
        }
    }
}

bool TileManager::has_terrain() const {
    return m_elev_loaded && !m_visible_elev.empty();
}

void TileManager::set_hgt_provider(std::unique_ptr<HgtProvider> provider) {
    m_hgt_provider = std::move(provider);
    m_elev_loaded = false;
    LOG_INFO("HGT provider set on tile manager");
}

void TileManager::set_dsm_provider(std::unique_ptr<DSMProvider> provider) {
    m_dsm_provider = std::move(provider);
    m_elev_loaded = false;
    LOG_INFO("DSM provider set on tile manager");
}

void TileManager::update(const Camera& cam, const GeoProjection& proj) {
    if (!m_hgt_provider) {
        update(); // fallback to static mode
        return;
    }

    LatLon ll = proj.unproject(cam.position.x, cam.position.z);
    update_dynamic_tiles(ll.lat, ll.lon);
    ensure_imagery_tiles();
}

void TileManager::update_dynamic_tiles(double cam_lat, double cam_lon) {
    auto needed = m_hgt_provider->tiles_in_view(cam_lat, cam_lon);

    /* Enqueue missing tiles to async loader */
    for (auto& coord : needed) {
        if (m_cache.has(coord)) {
            m_cache.touch(coord);
            continue;
        }
        if (m_loader.is_pending(coord)) continue;
        m_loader.request(coord, m_hgt_provider.get());
    }

    /* Drain completed tiles from the loader */
    drain_ready_tiles();

    m_visible_elev = needed;
    m_elev_loaded = true;

    /* Update bounds to cover all visible tiles */
    if (!m_visible_elev.empty()) {
        mesh3d_bounds_t total;
        auto first_b = HgtProvider::hgt_tile_bounds(m_visible_elev[0]);
        total = first_b;
        for (size_t i = 1; i < m_visible_elev.size(); ++i) {
            auto b = HgtProvider::hgt_tile_bounds(m_visible_elev[i]);
            total.min_lat = std::min(total.min_lat, b.min_lat);
            total.max_lat = std::max(total.max_lat, b.max_lat);
            total.min_lon = std::min(total.min_lon, b.min_lon);
            total.max_lon = std::max(total.max_lon, b.max_lon);
        }
        m_bounds = total;
        m_bounds_set = true;
    }
}

float TileManager::get_elevation_at(float world_x, float world_z,
                                     const GeoProjection& proj) const {
    LatLon ll = proj.unproject(world_x, world_z);

    /* Determine which tile covers this point */
    TileCoord coord;
    if (m_hgt_provider) {
        coord = HgtProvider::latlon_to_hgt_coord(ll.lat, ll.lon);
    } else if (!m_visible_elev.empty()) {
        coord = m_visible_elev[0]; // single-tile mode
    } else {
        return 0.0f;
    }

    /* Look up cached tile */
    TileRenderable* tr = const_cast<TileCache&>(m_cache).get(coord);
    if (!tr || tr->elevation.empty() || tr->elev_rows < 2 || tr->elev_cols < 2)
        return 0.0f;

    /* Map lat/lon to normalized [0,1] within tile bounds */
    double u = (ll.lon - tr->bounds.min_lon) / (tr->bounds.max_lon - tr->bounds.min_lon);
    double v = (tr->bounds.max_lat - ll.lat) / (tr->bounds.max_lat - tr->bounds.min_lat);

    /* Clamp to valid range */
    u = std::clamp(u, 0.0, 1.0);
    v = std::clamp(v, 0.0, 1.0);

    /* Convert to grid coordinates */
    float gc = static_cast<float>(u * (tr->elev_cols - 1));
    float gr = static_cast<float>(v * (tr->elev_rows - 1));

    int c0 = std::clamp(static_cast<int>(gc), 0, tr->elev_cols - 2);
    int r0 = std::clamp(static_cast<int>(gr), 0, tr->elev_rows - 2);
    int c1 = c0 + 1;
    int r1 = r0 + 1;

    float fc = gc - c0;
    float fr = gr - r0;

    /* Bilinear interpolation */
    float h00 = tr->elevation[r0 * tr->elev_cols + c0];
    float h10 = tr->elevation[r1 * tr->elev_cols + c0];
    float h01 = tr->elevation[r0 * tr->elev_cols + c1];
    float h11 = tr->elevation[r1 * tr->elev_cols + c1];

    float h0 = h00 + fc * (h01 - h00);
    float h1 = h10 + fc * (h11 - h10);

    return h0 + fr * (h1 - h0);
}

/* Build a composite elevation grid from a center tile + its cached neighbors.
   The center tile occupies a sub-region within the larger composite grid,
   so ray marching can traverse terrain on neighboring tiles. */
struct CompositeElevation {
    std::vector<float> data;
    mesh3d_bounds_t bounds;
    int rows = 0, cols = 0;
    int center_row_start = 0, center_col_start = 0;
    int center_rows = 0, center_cols = 0;
};

static CompositeElevation build_composite_elevation(
    const TileRenderable& center, TileCache& cache)
{
    CompositeElevation ce;
    ce.center_rows = center.elev_rows;
    ce.center_cols = center.elev_cols;

    const int cr = center.elev_rows;
    const int cc = center.elev_cols;

    /* Check 3x3 neighborhood for cached tiles with matching resolution.
       nb[grid_row][grid_col]: grid_row 0 = north (max_lat), 2 = south (min_lat).
       Tile coord systems:
         - HGT (z=-1): y = floor(lat), so dy=+1 means NORTH → grid row 0
         - Slippy map:  y increases southward, so dy=-1 means NORTH → grid row 0 */
    const TileRenderable* nb[3][3] = {};
    nb[1][1] = &center;
    bool hgt_mode = (center.coord.z == -1);

    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dy == 0 && dx == 0) continue;
            TileCoord nc = {center.coord.z, center.coord.x + dx, center.coord.y + dy};
            TileRenderable* n = cache.get(nc);
            if (n && n->elev_rows == cr && n->elev_cols == cc && !n->elevation.empty()) {
                /* Map tile offset to grid position (north=row 0) */
                int gr = hgt_mode ? (1 - dy) : (1 + dy); // HGT: +dy=north=0, slippy: -dy=north=0
                int gc = dx + 1;
                nb[gr][gc] = n;
            }
        }
    }

    /* Only expand in directions where neighbors exist */
    int top_rows    = nb[0][0] || nb[0][1] || nb[0][2] ? cr : 0;
    int bottom_rows = nb[2][0] || nb[2][1] || nb[2][2] ? cr : 0;
    int left_cols   = nb[0][0] || nb[1][0] || nb[2][0] ? cc : 0;
    int right_cols  = nb[0][2] || nb[1][2] || nb[2][2] ? cc : 0;

    ce.rows = top_rows + cr + bottom_rows;
    ce.cols = left_cols + cc + right_cols;
    ce.center_row_start = top_rows;
    ce.center_col_start = left_cols;

    ce.data.assign(ce.rows * ce.cols, 0.0f);

    /* Blit each neighbor's elevation into the composite */
    for (int gr = 0; gr < 3; ++gr) {
        for (int gc = 0; gc < 3; ++gc) {
            if (!nb[gr][gc]) continue;
            int dst_r = (gr == 0) ? 0 : (gr == 1 ? top_rows : top_rows + cr);
            int dst_c = (gc == 0) ? 0 : (gc == 1 ? left_cols : left_cols + cc);
            const auto& elev = nb[gr][gc]->elevation;
            for (int r = 0; r < cr; ++r) {
                std::memcpy(&ce.data[(dst_r + r) * ce.cols + dst_c],
                            &elev[r * cc],
                            cc * sizeof(float));
            }
        }
    }

    /* Expanded geographic bounds. Grid row 0 = north (max_lat). */
    double lat_span = center.bounds.max_lat - center.bounds.min_lat;
    double lon_span = center.bounds.max_lon - center.bounds.min_lon;

    ce.bounds.max_lat = center.bounds.max_lat + (top_rows > 0 ? lat_span : 0.0);
    ce.bounds.min_lat = center.bounds.min_lat - (bottom_rows > 0 ? lat_span : 0.0);
    ce.bounds.min_lon = center.bounds.min_lon - (left_cols > 0 ? lon_span : 0.0);
    ce.bounds.max_lon = center.bounds.max_lon + (right_cols > 0 ? lon_span : 0.0);

    return ce;
}

/* Extract center-tile results from a composite-grid viewshed computation */
static void extract_center_results(const CompositeElevation& ce,
                                    const std::vector<uint8_t>& comp_vis,
                                    const std::vector<float>& comp_sig,
                                    std::vector<uint8_t>& tile_vis,
                                    std::vector<float>& tile_sig)
{
    int cr = ce.center_rows;
    int cc = ce.center_cols;
    int total = cr * cc;
    tile_vis.resize(total);
    tile_sig.resize(total);

    for (int r = 0; r < cr; ++r) {
        int src_row = ce.center_row_start + r;
        int src_off = src_row * ce.cols + ce.center_col_start;
        int dst_off = r * cc;
        for (int c = 0; c < cc; ++c) {
            tile_vis[dst_off + c] = comp_vis[src_off + c];
            tile_sig[dst_off + c] = comp_sig[src_off + c];
        }
    }
}

void TileManager::apply_viewshed_overlays(const std::vector<NodeData>& nodes,
                                           const GeoProjection& proj,
                                           const mesh3d_rf_config_t& rf_config) {
    /* Iterate all cached tiles, compute viewshed on composite (tile+neighbors) */
    m_cache.for_each_mut([&](TileRenderable& tr) {
        if (tr.elevation.empty() || tr.elev_rows < 2 || tr.elev_cols < 2)
            return;

        int total = tr.elev_rows * tr.elev_cols;
        tr.viewshed.assign(total, 0);
        tr.signal.assign(total, -999.0f);

        /* Build composite elevation including neighbor tiles */
        auto ce = build_composite_elevation(tr, m_cache);

        for (auto& nd : nodes) {
            std::vector<uint8_t> vis;
            std::vector<float> sig;
            compute_viewshed(ce.data.data(), ce.rows, ce.cols,
                             ce.bounds, nd, vis, sig, rf_config);

            /* Extract center tile results and merge */
            std::vector<uint8_t> tile_vis;
            std::vector<float> tile_sig;
            extract_center_results(ce, vis, sig, tile_vis, tile_sig);

            for (int i = 0; i < total; ++i) {
                if (tile_vis[i]) {
                    tr.viewshed[i] = 1;
                    if (tile_sig[i] > tr.signal[i])
                        tr.signal[i] = tile_sig[i];
                }
            }
        }

        /* Rebuild mesh with overlay data (preserves texture) */
        Texture saved_tex = std::move(tr.texture);
        tr.mesh = m_builder.rebuild_mesh(tr, m_proj);
        tr.texture = std::move(saved_tex);
    });

    LOG_INFO("Applied viewshed overlays to cached tiles for %zu nodes", nodes.size());
}

void TileManager::apply_viewshed_overlays_gpu(const std::vector<NodeData>& nodes,
                                                const GeoProjection& proj,
                                                GpuViewshed* gpu,
                                                const mesh3d_rf_config_t& rf_config) {
    if (!gpu) {
        apply_viewshed_overlays(nodes, proj, rf_config);
        return;
    }

    m_cache.for_each_mut([&](TileRenderable& tr) {
        if (tr.elevation.empty() || tr.elev_rows < 2 || tr.elev_cols < 2)
            return;

        /* Build composite elevation including neighbor tiles */
        auto ce = build_composite_elevation(tr, m_cache);

        /* Upload composite elevation and compute on GPU */
        gpu->upload_elevation(ce.data.data(), ce.rows, ce.cols);
        gpu->set_grid_params(ce.bounds, ce.rows, ce.cols);
        gpu->compute_all(nodes);

        std::vector<uint8_t> comp_vis, comp_overlap;
        std::vector<float> comp_sig;
        gpu->read_back(comp_vis, comp_sig, comp_overlap);

        /* Extract center tile results */
        extract_center_results(ce, comp_vis, comp_sig, tr.viewshed, tr.signal);

        /* Rebuild mesh with overlay data (preserves texture) */
        Texture saved_tex = std::move(tr.texture);
        tr.mesh = m_builder.rebuild_mesh(tr, m_proj);
        tr.texture = std::move(saved_tex);
    });

    LOG_INFO("Applied GPU viewshed overlays to cached tiles for %zu nodes", nodes.size());
}

void TileManager::start_loader() {
    m_loader.start();
}

void TileManager::stop_loader() {
    m_loader.stop();
}

void TileManager::drain_ready_tiles() {
    auto t0 = std::chrono::steady_clock::now();
    constexpr auto BUDGET = std::chrono::milliseconds(4);

    TileData data;
    while (m_loader.poll_result(data)) {
        if (data.coord.z == -1 || !data.elevation.empty()) {
            /* Skip if already in GPU cache (race guard) */
            if (m_cache.has(data.coord)) {
                LOG_DEBUG("Async: tile z=%d x=%d y=%d already in cache, skipping",
                          data.coord.z, data.coord.x, data.coord.y);
            } else {
                TileRenderable tr = m_builder.build(data, m_proj);
                m_cache.upload(std::move(tr));
                LOG_INFO("Async: uploaded tile z=%d x=%d y=%d",
                         data.coord.z, data.coord.x, data.coord.y);
            }
        }

        if (std::chrono::steady_clock::now() - t0 > BUDGET) break;
    }
}

void TileManager::dispatch_tile_viewshed(size_t tile_idx,
                                           const std::vector<NodeData>& nodes,
                                           GpuViewshed* gpu) {
    TileRenderable* tr = m_cache.get(m_tile_vs.tile_list[tile_idx]);
    if (!tr) return;

    auto ce = build_composite_elevation(*tr, m_cache);

    /* Store composite metadata for extraction at readback time */
    if (m_tile_vs.comp_info.size() <= tile_idx)
        m_tile_vs.comp_info.resize(tile_idx + 1);
    m_tile_vs.comp_info[tile_idx] = {
        ce.rows, ce.cols,
        ce.center_row_start, ce.center_col_start,
        ce.center_rows, ce.center_cols
    };

    gpu->upload_elevation(ce.data.data(), ce.rows, ce.cols);
    gpu->set_grid_params(ce.bounds, ce.rows, ce.cols);
    gpu->compute_all_async(nodes, ce.data.data());
}

void TileManager::kick_viewshed_gpu(const std::vector<NodeData>& nodes,
                                      const GeoProjection& proj,
                                      GpuViewshed* gpu) {
    if (!gpu) return;

    /* Clear stale overlay textures so tiles don't show old data during recompute */
    m_cache.for_each_mut([](TileRenderable& tr) {
        tr.destroy_overlay_textures();
    });

    /* Collect all tiles that have elevation data */
    m_tile_vs.tile_list.clear();
    m_tile_vs.comp_info.clear();
    m_cache.for_each([&](const TileRenderable& tr) {
        if (!tr.elevation.empty() && tr.elev_rows >= 2 && tr.elev_cols >= 2)
            m_tile_vs.tile_list.push_back(tr.coord);
    });

    if (m_tile_vs.tile_list.empty()) return;

    m_tile_vs.current_tile = 0;
    m_tile_vs.active = true;
    m_tile_vs.comp_info.resize(m_tile_vs.tile_list.size());

    /* Dispatch first tile with composite elevation */
    dispatch_tile_viewshed(0, nodes, gpu);
}

void TileManager::poll_viewshed_gpu(const std::vector<NodeData>& nodes,
                                      const GeoProjection& proj,
                                      GpuViewshed* gpu) {
    if (!gpu || !m_tile_vs.active) return;
    if (gpu->poll_state() != ComputeState::READY) return;

    /* Read back composite results and extract center tile portion */
    size_t idx = m_tile_vs.current_tile;
    if (idx < m_tile_vs.tile_list.size()) {
        TileRenderable* tr = m_cache.get(m_tile_vs.tile_list[idx]);
        if (tr) {
            auto t0 = std::chrono::steady_clock::now();

            /* Read back full composite results */
            std::vector<uint8_t> comp_vis, comp_overlap;
            std::vector<float> comp_sig;
            gpu->read_back_async(comp_vis, comp_sig, comp_overlap);
            auto t1 = std::chrono::steady_clock::now();

            /* Extract center tile portion */
            auto& ci = m_tile_vs.comp_info[idx];
            CompositeElevation ce;
            ce.rows = ci.comp_rows;
            ce.cols = ci.comp_cols;
            ce.center_row_start = ci.center_row_start;
            ce.center_col_start = ci.center_col_start;
            ce.center_rows = ci.center_rows;
            ce.center_cols = ci.center_cols;
            extract_center_results(ce, comp_vis, comp_sig,
                                    tr->viewshed, tr->signal);

            /* Upload as GPU overlay textures */
            tr->upload_overlay_textures(tr->viewshed.data(), tr->signal.data(),
                                         tr->elev_rows, tr->elev_cols);
            auto t2 = std::chrono::steady_clock::now();

            auto ms = [](auto a, auto b) {
                return std::chrono::duration_cast<std::chrono::milliseconds>(b - a).count();
            };
            LOG_INFO("poll_viewshed_gpu tile %zu: readback=%lldms upload=%lldms",
                     idx, (long long)ms(t0,t1), (long long)ms(t1,t2));
        }
    }

    /* Advance to next tile */
    m_tile_vs.current_tile++;
    if (m_tile_vs.current_tile < m_tile_vs.tile_list.size()) {
        dispatch_tile_viewshed(m_tile_vs.current_tile, nodes, gpu);
    } else {
        /* All tiles done */
        m_tile_vs.active = false;
        LOG_INFO("Async tile viewshed complete for %zu tiles, %zu nodes",
                 m_tile_vs.tile_list.size(), nodes.size());
    }
}

void TileManager::clear() {
    m_loader.stop();
    m_cache.clear();
    m_elev_loaded = false;
    m_visible_elev.clear();
    m_visible_imagery.clear();
    m_tile_vs.active = false;
}

} // namespace mesh3d
