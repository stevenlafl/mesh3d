#pragma once
#include "tile/tile_coord.h"
#include "tile/tile_data.h"
#include <unordered_map>
#include <list>
#include <memory>

namespace mesh3d {

/* LRU cache of GPU-uploaded TileRenderable objects.
   All GL operations (upload/evict) must happen on the main thread. */
class TileCache {
public:
    static constexpr int DEFAULT_MAX_TILES = 128;

    explicit TileCache(int max_tiles = DEFAULT_MAX_TILES);

    /* Upload a TileRenderable and store in cache. Returns pointer to cached entry. */
    TileRenderable* upload(TileRenderable&& tile);

    /* Get cached tile, or nullptr if not present. Touches (marks as recently used). */
    TileRenderable* get(const TileCoord& coord);

    /* Check if tile is cached */
    bool has(const TileCoord& coord) const;

    /* Mark tile as recently used */
    void touch(const TileCoord& coord);

    /* Remove a specific tile */
    void evict(const TileCoord& coord);

    /* Clear all cached tiles */
    void clear();

    /* Iterate all cached tiles */
    template<typename Fn>
    void for_each(Fn fn) const {
        for (auto& [coord, entry] : m_map) {
            fn(entry.tile);
        }
    }

    template<typename Fn>
    void for_each_mut(Fn fn) {
        for (auto& [coord, entry] : m_map) {
            fn(entry.tile);
        }
    }

    int size() const { return static_cast<int>(m_map.size()); }
    int max_tiles() const { return m_max_tiles; }

private:
    int m_max_tiles;

    /* LRU list: front = most recently used, back = least recently used */
    using LRUList = std::list<TileCoord>;
    LRUList m_lru;

    struct CacheEntry {
        TileRenderable tile;
        LRUList::iterator lru_it;
    };

    std::unordered_map<TileCoord, CacheEntry> m_map;

    void evict_lru();
};

} // namespace mesh3d
