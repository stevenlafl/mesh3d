#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace mesh3d {

/* Simple filesystem cache for downloaded tile data.
   Default cache directory: ~/.cache/mesh3d/tiles/ */
class DiskCache {
public:
    explicit DiskCache(const std::string& cache_dir = "");

    /* Check if a key exists in cache */
    bool has(const std::string& key) const;

    /* Read cached data. Returns empty vector if not found. */
    std::vector<uint8_t> read(const std::string& key) const;

    /* Write data to cache under key */
    bool write(const std::string& key, const uint8_t* data, size_t len);
    bool write(const std::string& key, const std::vector<uint8_t>& data);

    const std::string& cache_dir() const { return m_cache_dir; }

private:
    std::string m_cache_dir;

    std::string key_to_path(const std::string& key) const;
    bool ensure_dir(const std::string& path) const;
};

} // namespace mesh3d
