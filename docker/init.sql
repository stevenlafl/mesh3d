-- mesh3d PostGIS schema
CREATE EXTENSION IF NOT EXISTS postgis;

-- Projects
CREATE TABLE projects (
    id SERIAL PRIMARY KEY,
    name TEXT NOT NULL,
    bounds GEOMETRY(Polygon, 4326),
    min_lat DOUBLE PRECISION,
    max_lat DOUBLE PRECISION,
    min_lon DOUBLE PRECISION,
    max_lon DOUBLE PRECISION,
    created_at TIMESTAMPTZ DEFAULT NOW()
);

-- Hardware profiles
CREATE TABLE hardware_profiles (
    id SERIAL PRIMARY KEY,
    name TEXT NOT NULL,
    tx_power_dbm REAL DEFAULT 27,
    antenna_gain_dbi REAL DEFAULT 0,
    cable_loss_db REAL DEFAULT 0,
    rx_sensitivity_dbm REAL DEFAULT -130,
    frequency_mhz REAL DEFAULT 906,
    spreading_factor INTEGER DEFAULT 12
);

-- Nodes
CREATE TABLE nodes (
    id SERIAL PRIMARY KEY,
    project_id INTEGER REFERENCES projects(id) ON DELETE CASCADE,
    name TEXT NOT NULL,
    location GEOMETRY(PointZ, 4326),
    antenna_height_m REAL DEFAULT 2,
    role INTEGER DEFAULT 2,  -- 0=backbone, 1=relay, 2=leaf
    hardware_profile_id INTEGER REFERENCES hardware_profiles(id),
    max_range_km REAL DEFAULT 5
);

CREATE INDEX idx_nodes_project ON nodes(project_id);

-- Elevation grids
CREATE TABLE elevation_grids (
    id SERIAL PRIMARY KEY,
    project_id INTEGER REFERENCES projects(id) ON DELETE CASCADE,
    grid_rows INTEGER NOT NULL,
    grid_cols INTEGER NOT NULL,
    bounds GEOMETRY(Polygon, 4326),
    elevation_data BYTEA NOT NULL  -- row-major float32
);

CREATE INDEX idx_elevation_project ON elevation_grids(project_id);

-- Viewshed results (per-node)
CREATE TABLE viewshed_results (
    id SERIAL PRIMARY KEY,
    node_id INTEGER REFERENCES nodes(id) ON DELETE CASCADE,
    project_id INTEGER REFERENCES projects(id) ON DELETE CASCADE,
    grid_rows INTEGER NOT NULL,
    grid_cols INTEGER NOT NULL,
    visibility_data BYTEA,           -- row-major uint8
    signal_strength_data BYTEA,      -- row-major float32 (dBm)
    bounds GEOMETRY(Polygon, 4326)
);

CREATE INDEX idx_viewshed_project ON viewshed_results(project_id);
CREATE INDEX idx_viewshed_node ON viewshed_results(node_id);

-- Merged coverage
CREATE TABLE merged_coverages (
    id SERIAL PRIMARY KEY,
    project_id INTEGER REFERENCES projects(id) ON DELETE CASCADE,
    combined_visibility BYTEA,       -- row-major uint8
    overlap_count_data BYTEA,        -- row-major uint8 (per-cell node count)
    coverage_percentage REAL,
    overlap_percentage REAL
);

CREATE INDEX idx_merged_project ON merged_coverages(project_id);
