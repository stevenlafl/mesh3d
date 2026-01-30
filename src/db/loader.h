#pragma once
#include "db/db.h"
#include "scene/scene.h"
#include "util/math_util.h"

namespace mesh3d {

class DataLoader {
public:
    explicit DataLoader(Database& db) : m_db(db) {}

    /* Load a full project into the scene. Returns false on error. */
    bool load_project(int project_id, Scene& scene);

private:
    Database& m_db;

    bool load_bounds(int project_id, Scene& scene);
    bool load_elevation(int project_id, Scene& scene);
    bool load_nodes(int project_id, Scene& scene);
    bool load_merged_coverage(int project_id, Scene& scene);

    /* Decode hex-encoded BYTEA (libpq text mode returns \\x... format) */
    static std::vector<uint8_t> decode_bytea(const char* hex_str, int len);
};

} // namespace mesh3d
