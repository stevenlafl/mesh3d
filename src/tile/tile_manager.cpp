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

/* Filter nodes to only those within the tile's geographic bounds.
   Nodes outside the tile get clamped to the edge by the compute shader,
   producing false signal strips along tile boundaries. */
static std::vector<NodeData> nodes_in_bounds(const std::vector<NodeData>& nodes,
                                              const mesh3d_bounds_t& bounds) {
    std::vector<NodeData> result;
    for (auto& nd : nodes) {
        if (nd.info.lat >= bounds.min_lat && nd.info.lat <= bounds.max_lat &&
            nd.info.lon >= bounds.min_lon && nd.info.lon <= bounds.max_lon) {
            result.push_back(nd);
        }
    }
    return result;
}

void TileManager::apply_viewshed_overlays(const std::vector<NodeData>& nodes,
                                           const GeoProjection& proj) {
    /* Iterate all cached tiles, compute viewshed per tile, rebuild mesh */
    m_cache.for_each_mut([&](TileRenderable& tr) {
        if (tr.elevation.empty() || tr.elev_rows < 2 || tr.elev_cols < 2)
            return;

        int total = tr.elev_rows * tr.elev_cols;
        tr.viewshed.assign(total, 0);
        tr.signal.assign(total, -999.0f);

        /* Compute per-node viewshed on this tile's elevation grid
           (only for nodes within this tile's bounds) */
        auto tile_nodes = nodes_in_bounds(nodes, tr.bounds);
        for (auto& nd : tile_nodes) {
            std::vector<uint8_t> vis;
            std::vector<float> sig;
            compute_viewshed(tr.elevation.data(), tr.elev_rows, tr.elev_cols,
                             tr.bounds, nd, vis, sig);

            for (int i = 0; i < total; ++i) {
                if (vis[i]) {
                    tr.viewshed[i] = 1;
                    if (sig[i] > tr.signal[i])
                        tr.signal[i] = sig[i];
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
                                                GpuViewshed* gpu) {
    if (!gpu) {
        apply_viewshed_overlays(nodes, proj);
        return;
    }

    m_cache.for_each_mut([&](TileRenderable& tr) {
        if (tr.elevation.empty() || tr.elev_rows < 2 || tr.elev_cols < 2)
            return;

        int total = tr.elev_rows * tr.elev_cols;

        /* Upload tile elevation and compute on GPU */
        auto tile_nodes = nodes_in_bounds(nodes, tr.bounds);
        gpu->upload_elevation(tr.elevation.data(), tr.elev_rows, tr.elev_cols);
        gpu->set_grid_params(tr.bounds, tr.elev_rows, tr.elev_cols);
        gpu->compute_all(tile_nodes);

        std::vector<uint8_t> overlap;
        gpu->read_back(tr.viewshed, tr.signal, overlap);

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

void TileManager::kick_viewshed_gpu(const std::vector<NodeData>& nodes,
                                      const GeoProjection& proj,
                                      GpuViewshed* gpu) {
    if (!gpu) return;

    /* Collect all tiles that have elevation data */
    m_tile_vs.tile_list.clear();
    m_cache.for_each([&](const TileRenderable& tr) {
        if (!tr.elevation.empty() && tr.elev_rows >= 2 && tr.elev_cols >= 2)
            m_tile_vs.tile_list.push_back(tr.coord);
    });

    if (m_tile_vs.tile_list.empty()) return;

    m_tile_vs.current_tile = 0;
    m_tile_vs.active = true;

    /* Dispatch first tile — only with nodes that fall within it */
    TileRenderable* tr = m_cache.get(m_tile_vs.tile_list[0]);
    if (tr) {
        auto tile_nodes = nodes_in_bounds(nodes, tr->bounds);
        gpu->upload_elevation(tr->elevation.data(), tr->elev_rows, tr->elev_cols);
        gpu->set_grid_params(tr->bounds, tr->elev_rows, tr->elev_cols);
        gpu->compute_all_async(tile_nodes, tr->elevation.data());
    }
}

void TileManager::poll_viewshed_gpu(const std::vector<NodeData>& nodes,
                                      const GeoProjection& proj,
                                      GpuViewshed* gpu) {
    if (!gpu || !m_tile_vs.active) return;
    if (gpu->poll_state() != ComputeState::READY) return;

    /* Read back results for current tile and upload as overlay textures */
    size_t idx = m_tile_vs.current_tile;
    if (idx < m_tile_vs.tile_list.size()) {
        TileRenderable* tr = m_cache.get(m_tile_vs.tile_list[idx]);
        if (tr) {
            auto t0 = std::chrono::steady_clock::now();
            std::vector<uint8_t> overlap;
            gpu->read_back_async(tr->viewshed, tr->signal, overlap);
            auto t1 = std::chrono::steady_clock::now();

            /* Upload as GPU overlay textures (skips full mesh rebuild) */
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
        TileRenderable* tr = m_cache.get(m_tile_vs.tile_list[m_tile_vs.current_tile]);
        if (tr) {
            auto tile_nodes = nodes_in_bounds(nodes, tr->bounds);
            gpu->upload_elevation(tr->elevation.data(), tr->elev_rows, tr->elev_cols);
            gpu->set_grid_params(tr->bounds, tr->elev_rows, tr->elev_cols);
            gpu->compute_all_async(tile_nodes, tr->elevation.data());
        }
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
