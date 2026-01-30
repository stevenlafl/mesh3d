#pragma once
#include "tile/tile_provider.h"
#include "tile/tile_cache.h"
#include "tile/tile_terrain_builder.h"
#include "tile/tile_selector.h"
#include "tile/tile_data.h"
#include "util/math_util.h"
#include <mesh3d/types.h>
#include <memory>
#include <functional>
#include <vector>

namespace mesh3d {

class Camera;
class HgtProvider;

enum class ImagerySource { SATELLITE, STREET, NONE };

/* Orchestrates the tile system: providers, selector, builder, cache.
   Two providers: elevation (SingleTileProvider) + imagery (UrlTileProvider).
   When HGT provider is set, supports camera-driven dynamic loading. */
class TileManager {
public:
    using DrawFn = std::function<void(const TileRenderable&)>;

    void set_elevation_provider(std::unique_ptr<TileProvider> provider);
    void set_imagery_provider(std::unique_ptr<TileProvider> provider);

    /* Set HGT provider for dynamic elevation loading */
    void set_hgt_provider(std::unique_ptr<HgtProvider> provider);

    /* Switch imagery source at runtime */
    void set_imagery_source(ImagerySource src);
    ImagerySource imagery_source() const { return m_imagery_source; }
    void cycle_imagery_source();

    /* Set project bounds and projection */
    void set_bounds(const mesh3d_bounds_t& bounds);

    /* Update: fetch/upload any missing tiles (static mode).
       Call each frame (does work only when tiles are missing). */
    void update();

    /* Update with camera position for dynamic HGT loading.
       Converts camera world pos -> lat/lon, loads nearby HGT tiles. */
    void update(const Camera& cam, const GeoProjection& proj);

    /* Iterate visible tiles for rendering */
    void render(DrawFn fn) const;

    bool has_terrain() const;
    bool has_hgt_provider() const { return m_hgt_provider != nullptr; }

    /* Access for configuration */
    TileSelector& selector() { return m_selector; }
    TileTerrainBuilder& builder() { return m_builder; }

    void clear();

private:
    std::unique_ptr<TileProvider> m_elev_provider;
    std::unique_ptr<TileProvider> m_imagery_provider;
    std::unique_ptr<HgtProvider> m_hgt_provider;
    ImagerySource m_imagery_source = ImagerySource::NONE;

    TileSelector m_selector;
    TileTerrainBuilder m_builder;
    TileCache m_cache;

    mesh3d_bounds_t m_bounds{};
    GeoProjection m_proj;
    bool m_bounds_set = false;
    bool m_elev_loaded = false;

    /* Currently visible tile coords */
    std::vector<TileCoord> m_visible_elev;
    std::vector<TileCoord> m_visible_imagery;

    void ensure_elevation_tiles();
    void ensure_imagery_tiles();
    void composite_imagery_for_tile(TileRenderable* tr);

    /* Camera-driven dynamic tile selection */
    void update_dynamic_tiles(double cam_lat, double cam_lon);
};

} // namespace mesh3d
