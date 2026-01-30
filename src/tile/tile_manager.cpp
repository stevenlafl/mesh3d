#include "tile/tile_manager.h"
#include "tile/url_tile_provider.h"
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

    /* Clear cache so tiles get rebuilt with new imagery (or without) */
    m_cache.clear();
    m_elev_loaded = false;

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

    /* For the single-tile case, we need to composite multiple imagery tiles
       into one texture matching the elevation tile's UV space.
       The elevation tile covers m_bounds, and each imagery tile covers a sub-region. */

    auto imagery_coords = m_selector.select(tr->bounds);
    if (imagery_coords.empty()) return;

    /* Determine composite image size based on number of tiles */
    int tiles_x = 0, tiles_y = 0;
    int min_x = imagery_coords[0].x, max_x = imagery_coords[0].x;
    int min_y = imagery_coords[0].y, max_y = imagery_coords[0].y;
    for (auto& c : imagery_coords) {
        if (c.x < min_x) min_x = c.x;
        if (c.x > max_x) max_x = c.x;
        if (c.y < min_y) min_y = c.y;
        if (c.y > max_y) max_y = c.y;
    }
    tiles_x = max_x - min_x + 1;
    tiles_y = max_y - min_y + 1;

    /* Limit composite size to avoid huge textures */
    static constexpr int MAX_COMPOSITE_DIM = 16; // 16x16 tiles = 4096x4096
    if (tiles_x > MAX_COMPOSITE_DIM || tiles_y > MAX_COMPOSITE_DIM) {
        LOG_WARN("Too many imagery tiles (%dx%d), skipping composite", tiles_x, tiles_y);
        return;
    }

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
    for (auto& coord : m_visible_elev) {
        auto it_check = const_cast<TileCache&>(m_cache).get(coord);
        if (it_check && it_check->mesh.valid()) {
            fn(*it_check);
        }
    }
}

bool TileManager::has_terrain() const {
    return m_elev_loaded && !m_visible_elev.empty();
}

void TileManager::clear() {
    m_cache.clear();
    m_elev_loaded = false;
    m_visible_elev.clear();
    m_visible_imagery.clear();
}

} // namespace mesh3d
