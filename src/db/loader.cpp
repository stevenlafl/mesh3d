#include "db/loader.h"
#include "db/schema.h"
#include "util/log.h"
#include <cstdlib>
#include <cstring>

namespace mesh3d {

static int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

std::vector<uint8_t> DataLoader::decode_bytea(const char* str, int len) {
    /* libpq text mode returns hex format: \x followed by hex pairs */
    std::vector<uint8_t> out;
    if (!str || len < 2) return out;

    if (str[0] == '\\' && str[1] == 'x') {
        /* Hex encoding */
        int hex_len = len - 2;
        out.resize(hex_len / 2);
        for (int i = 0; i < hex_len; i += 2) {
            out[i / 2] = static_cast<uint8_t>(
                (hex_val(str[2 + i]) << 4) | hex_val(str[2 + i + 1]));
        }
    } else {
        /* Raw / escape encoding */
        out.assign(reinterpret_cast<const uint8_t*>(str),
                   reinterpret_cast<const uint8_t*>(str + len));
    }
    return out;
}

bool DataLoader::load_project(int project_id, Scene& scene) {
    scene.clear();

    if (!load_bounds(project_id, scene)) return false;
    load_elevation(project_id, scene);  // optional
    load_nodes(project_id, scene);
    load_merged_coverage(project_id, scene);

    scene.rebuild_all();
    return true;
}

bool DataLoader::load_bounds(int project_id, Scene& scene) {
    auto r = m_db.exec_params(sql::LOAD_PROJECT, {std::to_string(project_id)});
    if (!r->ok() || r->rows() == 0) {
        LOG_ERROR("Project %d not found", project_id);
        return false;
    }
    scene.bounds.min_lat = std::atof(r->get(0, 2));
    scene.bounds.max_lat = std::atof(r->get(0, 3));
    scene.bounds.min_lon = std::atof(r->get(0, 4));
    scene.bounds.max_lon = std::atof(r->get(0, 5));
    LOG_INFO("Loaded project '%s' bounds: lat[%.4f,%.4f] lon[%.4f,%.4f]",
             r->get(0, 1),
             scene.bounds.min_lat, scene.bounds.max_lat,
             scene.bounds.min_lon, scene.bounds.max_lon);
    return true;
}

bool DataLoader::load_elevation(int project_id, Scene& scene) {
    auto r = m_db.exec_params(sql::LOAD_ELEVATION, {std::to_string(project_id)});
    if (!r->ok() || r->rows() == 0) {
        LOG_WARN("No elevation data for project %d", project_id);
        return false;
    }
    scene.grid_rows = std::atoi(r->get(0, 0));
    scene.grid_cols = std::atoi(r->get(0, 1));

    /* Decode BYTEA elevation data */
    int blen = 0;
    const char* bdata = r->get_binary(0, 2, &blen);
    auto bytes = decode_bytea(bdata, blen);
    int expected = scene.grid_rows * scene.grid_cols * sizeof(float);
    if (static_cast<int>(bytes.size()) != expected) {
        LOG_ERROR("Elevation data size mismatch: got %zu, expected %d", bytes.size(), expected);
        return false;
    }
    scene.elevation.resize(scene.grid_rows * scene.grid_cols);
    std::memcpy(scene.elevation.data(), bytes.data(), bytes.size());

    /* Update bounds from elevation grid if provided */
    if (r->get(0, 3)) {
        scene.bounds.min_lat = std::atof(r->get(0, 3));
        scene.bounds.max_lat = std::atof(r->get(0, 4));
        scene.bounds.min_lon = std::atof(r->get(0, 5));
        scene.bounds.max_lon = std::atof(r->get(0, 6));
    }

    LOG_INFO("Loaded elevation grid: %dx%d", scene.grid_rows, scene.grid_cols);
    return true;
}

bool DataLoader::load_nodes(int project_id, Scene& scene) {
    auto r = m_db.exec_params(sql::LOAD_NODES, {std::to_string(project_id)});
    if (!r->ok()) return false;

    GeoProjection proj;
    proj.init(scene.bounds);

    for (int i = 0; i < r->rows(); ++i) {
        NodeData nd;
        nd.info.id = std::atoi(r->get(i, 0));
        std::strncpy(nd.info.name, r->get(i, 1), sizeof(nd.info.name) - 1);
        nd.info.lat = std::atof(r->get(i, 2));
        nd.info.lon = std::atof(r->get(i, 3));
        nd.info.alt = std::atof(r->get(i, 4));
        nd.info.antenna_height_m = static_cast<float>(std::atof(r->get(i, 5)));
        nd.info.role = std::atoi(r->get(i, 6));
        nd.info.max_range_km = static_cast<float>(std::atof(r->get(i, 7)));
        nd.info.tx_power_dbm = static_cast<float>(std::atof(r->get(i, 8)));
        nd.info.antenna_gain_dbi = static_cast<float>(std::atof(r->get(i, 9)));
        nd.info.rx_sensitivity_dbm = static_cast<float>(std::atof(r->get(i, 10)));
        nd.info.frequency_mhz = static_cast<float>(std::atof(r->get(i, 11)));

        auto lc = proj.project(nd.info.lat, nd.info.lon);
        nd.world_pos = glm::vec3(lc.x,
                                  static_cast<float>(nd.info.alt + nd.info.antenna_height_m),
                                  lc.z);
        scene.nodes.push_back(nd);
    }

    LOG_INFO("Loaded %d nodes", static_cast<int>(scene.nodes.size()));
    return true;
}

bool DataLoader::load_merged_coverage(int project_id, Scene& scene) {
    auto r = m_db.exec_params(sql::LOAD_MERGED_COVERAGE, {std::to_string(project_id)});
    if (!r->ok() || r->rows() == 0) {
        LOG_WARN("No merged coverage for project %d", project_id);
        return false;
    }

    int expected = scene.grid_rows * scene.grid_cols;
    if (expected == 0) return false;

    /* combined_visibility */
    int blen = 0;
    const char* bdata = r->get_binary(0, 0, &blen);
    auto vis_bytes = decode_bytea(bdata, blen);
    if (static_cast<int>(vis_bytes.size()) == expected) {
        scene.viewshed_vis = std::move(vis_bytes);
    }

    /* overlap_count_data */
    bdata = r->get_binary(0, 1, &blen);
    auto ovl_bytes = decode_bytea(bdata, blen);
    if (static_cast<int>(ovl_bytes.size()) == expected) {
        scene.overlap_count = std::move(ovl_bytes);
    }

    LOG_INFO("Loaded merged coverage data");
    return true;
}

} // namespace mesh3d
