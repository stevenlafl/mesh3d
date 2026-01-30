#include "tile/tile_manager.h"
#include "tile/hgt_provider.h"
#include "tile/url_tile_provider.h"
#include "analysis/viewshed.h"
#include "scene/scene.h"
#include "camera/camera.h"
#include "util/log.h"
#include <cstring>
#include <algorithm>

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

    for (auto& coord : m_visible_elev) {
        if (m_cache.has(coord)) continue;

        auto data = m_elev_provider->fetch_tile(coord);
        if (!data) continue;

        TileRenderable tr = m_builder.build(*data, m_proj);
        m_cache.upload(std::move(tr));
        LOG_DEBUG("Uploaded elevation tile z=%d x=%d y=%d", coord.z, coord.x, coord.y);
    }

    m_elev_loaded = true;
}

void TileManager::ensure_imagery_tiles() {
    if (!m_imagery_provider || !m_bounds_set) return;
    if (m_imagery_source == ImagerySource::NONE) return;

    m_visible_imagery = m_selector.select(m_bounds);

    for (auto& coord : m_visible_imagery) {
        /* Check if we already have imagery for this tile in cache */
        /* Imagery tiles are separate from elevation tiles — we merge them */
        /* For single-tile elevation mode, we apply imagery as texture on the elevation tile */
    }

    /* For each elevation tile, try to apply imagery texture */
    for (auto& elev_coord : m_visible_elev) {
        TileRenderable* tr = m_cache.get(elev_coord);
        if (!tr) continue;

        /* If tile already has a texture, skip (already merged) */
        if (tr->texture.valid()) continue;

        /* Fetch the best imagery tile covering this elevation tile's bounds */
        /* For single-tile mode, we composite all imagery tiles into one texture */
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

    LOG_INFO("Composited %d/%zu imagery tiles (%dx%d px)",
             fetched, imagery_coords.size(), comp_w, comp_h);

    Texture tex;
    tex.load_rgba(composite.data(), comp_w, comp_h);
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

    /* Don't evict — let LRU cache handle expiry naturally.
       This prevents thrashing when the camera oscillates near a tile edge. */

    /* Fetch and upload new tiles */
    for (auto& coord : needed) {
        if (m_cache.has(coord)) {
            m_cache.touch(coord);
            continue;
        }

        auto data = m_hgt_provider->fetch_tile(coord);
        if (!data) continue;

        TileRenderable tr = m_builder.build(*data, m_proj);
        m_cache.upload(std::move(tr));
        LOG_INFO("Uploaded HGT tile %s", HgtProvider::coord_to_filename(coord).c_str());
    }

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

void TileManager::apply_viewshed_overlays(const std::vector<NodeData>& nodes,
                                           const GeoProjection& proj) {
    /* Iterate all cached tiles, compute viewshed per tile, rebuild mesh */
    m_cache.for_each_mut([&](TileRenderable& tr) {
        if (tr.elevation.empty() || tr.elev_rows < 2 || tr.elev_cols < 2)
            return;

        int total = tr.elev_rows * tr.elev_cols;
        tr.viewshed.assign(total, 0);
        tr.signal.assign(total, -999.0f);

        /* Compute per-node viewshed on this tile's elevation grid */
        for (auto& nd : nodes) {
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

void TileManager::clear() {
    m_cache.clear();
    m_elev_loaded = false;
    m_visible_elev.clear();
    m_visible_imagery.clear();
}

} // namespace mesh3d
