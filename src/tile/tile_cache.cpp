#include "tile/tile_cache.h"
#include "util/log.h"

namespace mesh3d {

TileCache::TileCache(int max_tiles) : m_max_tiles(max_tiles) {}

TileRenderable* TileCache::upload(TileRenderable&& tile) {
    TileCoord coord = tile.coord;

    /* If already cached, update */
    auto it = m_map.find(coord);
    if (it != m_map.end()) {
        it->second.tile = std::move(tile);
        /* Move to front of LRU */
        m_lru.erase(it->second.lru_it);
        m_lru.push_front(coord);
        it->second.lru_it = m_lru.begin();
        return &it->second.tile;
    }

    /* Evict if at capacity */
    while (static_cast<int>(m_map.size()) >= m_max_tiles) {
        evict_lru();
    }

    /* Insert */
    m_lru.push_front(coord);
    auto [ins_it, _] = m_map.emplace(coord, CacheEntry{std::move(tile), m_lru.begin()});
    return &ins_it->second.tile;
}

TileRenderable* TileCache::get(const TileCoord& coord) {
    auto it = m_map.find(coord);
    if (it == m_map.end()) return nullptr;

    /* Move to front of LRU */
    m_lru.erase(it->second.lru_it);
    m_lru.push_front(coord);
    it->second.lru_it = m_lru.begin();
    return &it->second.tile;
}

bool TileCache::has(const TileCoord& coord) const {
    return m_map.find(coord) != m_map.end();
}

void TileCache::touch(const TileCoord& coord) {
    auto it = m_map.find(coord);
    if (it == m_map.end()) return;
    m_lru.erase(it->second.lru_it);
    m_lru.push_front(coord);
    it->second.lru_it = m_lru.begin();
}

void TileCache::evict(const TileCoord& coord) {
    auto it = m_map.find(coord);
    if (it == m_map.end()) return;
    m_lru.erase(it->second.lru_it);
    m_map.erase(it);
}

void TileCache::clear() {
    m_map.clear();
    m_lru.clear();
}

void TileCache::evict_lru() {
    if (m_lru.empty()) return;
    TileCoord oldest = m_lru.back();
    m_lru.pop_back();
    m_map.erase(oldest);
    LOG_DEBUG("Evicted tile z=%d x=%d y=%d", oldest.z, oldest.x, oldest.y);
}

} // namespace mesh3d
