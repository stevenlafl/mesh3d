#pragma once

namespace mesh3d {
namespace sql {

constexpr const char* LOAD_PROJECT =
    "SELECT id, name, "
    "ST_YMin(bounds::geometry) as min_lat, ST_YMax(bounds::geometry) as max_lat, "
    "ST_XMin(bounds::geometry) as min_lon, ST_XMax(bounds::geometry) as max_lon "
    "FROM projects WHERE id = $1";

constexpr const char* LOAD_NODES =
    "SELECT n.id, n.name, "
    "ST_Y(n.location::geometry) as lat, ST_X(n.location::geometry) as lon, "
    "ST_Z(n.location::geometry) as alt, "
    "n.antenna_height_m, n.role, n.max_range_km, "
    "COALESCE(h.tx_power_dbm, 27) as tx_power_dbm, "
    "COALESCE(h.antenna_gain_dbi, 0) as antenna_gain_dbi, "
    "COALESCE(h.rx_sensitivity_dbm, -130) as rx_sensitivity_dbm, "
    "COALESCE(h.frequency_mhz, 906) as frequency_mhz "
    "FROM nodes n "
    "LEFT JOIN hardware_profiles h ON n.hardware_profile_id = h.id "
    "WHERE n.project_id = $1 "
    "ORDER BY n.id";

constexpr const char* LOAD_ELEVATION =
    "SELECT grid_rows, grid_cols, elevation_data, "
    "ST_YMin(bounds::geometry) as min_lat, ST_YMax(bounds::geometry) as max_lat, "
    "ST_XMin(bounds::geometry) as min_lon, ST_XMax(bounds::geometry) as max_lon "
    "FROM elevation_grids WHERE project_id = $1 LIMIT 1";

constexpr const char* LOAD_MERGED_COVERAGE =
    "SELECT combined_visibility, overlap_count_data "
    "FROM merged_coverages WHERE project_id = $1 LIMIT 1";

constexpr const char* LOAD_VIEWSHED_SIGNAL =
    "SELECT v.signal_strength_data, v.grid_rows, v.grid_cols "
    "FROM viewshed_results v "
    "JOIN nodes n ON v.node_id = n.id "
    "WHERE v.project_id = $1 "
    "ORDER BY n.id";

} // namespace sql
} // namespace mesh3d
