#!/usr/bin/env python3
"""Import mesh-mapgen viewshed data into mesh3d PostGIS database.

Usage:
    # From SRTM .hgt terrain files (generates synthetic nodes + viewsheds):
    python import_viewshed.py --dest-db "host=localhost port=5434 dbname=mesh3d user=mesh3d password=mesh3d" \
        --hgt-dir /path/to/mesh-mapgen/data/terrain \
        --center-lat 38.5 --center-lon -105.5 --radius-km 15 \
        --project-name "Colorado Test"

    # From mesh-mapgen database:
    python import_viewshed.py --source-db "dbname=mesh_mapgen" \
        --dest-db "host=localhost port=5434 dbname=mesh3d user=mesh3d password=mesh3d" \
        --project-name "My Project"

    # From numpy .npz:
    python import_viewshed.py --dest-db "..." --npz data.npz --project-name "My Project"
"""

import argparse
import glob
import math
import os
import struct
import sys

import numpy as np
import psycopg2


# ── DB insert helpers ────────────────────────────────────────────────

def create_project(cur, name, bounds):
    min_lat, max_lat, min_lon, max_lon = bounds
    wkt = (f"POLYGON(({min_lon} {min_lat}, {max_lon} {min_lat}, "
           f"{max_lon} {max_lat}, {min_lon} {max_lat}, {min_lon} {min_lat}))")
    cur.execute(
        "INSERT INTO projects (name, bounds, min_lat, max_lat, min_lon, max_lon) "
        "VALUES (%s, ST_GeomFromText(%s, 4326), %s, %s, %s, %s) RETURNING id",
        (name, wkt, min_lat, max_lat, min_lon, max_lon))
    return cur.fetchone()[0]


def insert_hardware_profile(cur, profile):
    cur.execute(
        "INSERT INTO hardware_profiles (name, tx_power_dbm, antenna_gain_dbi, "
        "cable_loss_db, rx_sensitivity_dbm, frequency_mhz, spreading_factor) "
        "VALUES (%s, %s, %s, %s, %s, %s, %s) RETURNING id",
        (profile.get('name', 'default'),
         profile.get('tx_power_dbm', 27),
         profile.get('antenna_gain_dbi', 0),
         profile.get('cable_loss_db', 0),
         profile.get('rx_sensitivity_dbm', -130),
         profile.get('frequency_mhz', 906),
         profile.get('spreading_factor', 12)))
    return cur.fetchone()[0]


def insert_node(cur, project_id, node, hw_profile_id=None):
    point_wkt = f"POINTZ({node['lon']} {node['lat']} {node.get('alt', 0)})"
    cur.execute(
        "INSERT INTO nodes (project_id, name, location, antenna_height_m, role, "
        "hardware_profile_id, max_range_km) "
        "VALUES (%s, %s, ST_GeomFromText(%s, 4326), %s, %s, %s, %s) RETURNING id",
        (project_id, node['name'], point_wkt,
         node.get('antenna_height_m', 2),
         node.get('role', 2),
         hw_profile_id,
         node.get('max_range_km', 5)))
    return cur.fetchone()[0]


def insert_elevation(cur, project_id, elevation, bounds):
    rows, cols = elevation.shape
    min_lat, max_lat, min_lon, max_lon = bounds
    wkt = (f"POLYGON(({min_lon} {min_lat}, {max_lon} {min_lat}, "
           f"{max_lon} {max_lat}, {min_lon} {max_lat}, {min_lon} {min_lat}))")
    cur.execute(
        "INSERT INTO elevation_grids (project_id, grid_rows, grid_cols, bounds, elevation_data) "
        "VALUES (%s, %s, %s, ST_GeomFromText(%s, 4326), %s)",
        (project_id, rows, cols, wkt, psycopg2.Binary(elevation.astype(np.float32).tobytes())))


def insert_viewshed(cur, node_id, project_id, visibility, signal_strength, bounds):
    rows, cols = visibility.shape
    min_lat, max_lat, min_lon, max_lon = bounds
    wkt = (f"POLYGON(({min_lon} {min_lat}, {max_lon} {min_lat}, "
           f"{max_lon} {max_lat}, {min_lon} {max_lat}, {min_lon} {min_lat}))")
    vis_data = psycopg2.Binary(visibility.astype(np.uint8).tobytes())
    sig_data = psycopg2.Binary(signal_strength.astype(np.float32).tobytes()) if signal_strength is not None else None
    cur.execute(
        "INSERT INTO viewshed_results (node_id, project_id, grid_rows, grid_cols, "
        "visibility_data, signal_strength_data, bounds) "
        "VALUES (%s, %s, %s, %s, %s, %s, ST_GeomFromText(%s, 4326))",
        (node_id, project_id, rows, cols, vis_data, sig_data, wkt))


def insert_merged_coverage(cur, project_id, combined_vis, overlap_count,
                           coverage_pct=None, overlap_pct=None):
    cur.execute(
        "INSERT INTO merged_coverages (project_id, combined_visibility, "
        "overlap_count_data, coverage_percentage, overlap_percentage) "
        "VALUES (%s, %s, %s, %s, %s)",
        (project_id,
         psycopg2.Binary(combined_vis.astype(np.uint8).tobytes()),
         psycopg2.Binary(overlap_count.astype(np.uint8).tobytes()),
         coverage_pct, overlap_pct))


# ── SRTM HGT reader ─────────────────────────────────────────────────

def parse_hgt_filename(path):
    """Parse N38W106.hgt -> (lat, lon) of SW corner."""
    name = os.path.basename(path).split('.')[0].upper()
    lat_ch, lat_str = name[0], name[1:3]
    lon_ch, lon_str = name[3], name[4:7]
    lat = int(lat_str) * (1 if lat_ch == 'N' else -1)
    lon = int(lon_str) * (1 if lon_ch == 'E' else -1)
    return lat, lon


def read_hgt(path):
    """Read SRTM HGT file -> (elevation_array, lat_sw, lon_sw).
    Returns float32 array, shape (nrows, ncols), north-up (row 0 = north edge)."""
    size = os.path.getsize(path)
    if size == 2 * 3601 * 3601:
        n = 3601  # SRTM1
    elif size == 2 * 1201 * 1201:
        n = 1201  # SRTM3
    else:
        raise ValueError(f"Unknown HGT size: {size} bytes for {path}")
    with open(path, 'rb') as f:
        data = np.frombuffer(f.read(), dtype='>i2').reshape(n, n).astype(np.float32)
    # Replace voids (-32768) with 0
    data[data < -1000] = 0
    lat, lon = parse_hgt_filename(path)
    return data, lat, lon


def load_hgt_region(hgt_dir, min_lat, max_lat, min_lon, max_lon):
    """Load and stitch HGT tiles covering the requested bounds.
    Returns (elevation_grid, actual_bounds) subsampled to reasonable size."""
    # Find which tiles we need
    tile_lat_min = int(math.floor(min_lat))
    tile_lat_max = int(math.floor(max_lat))
    tile_lon_min = int(math.floor(min_lon))
    tile_lon_max = int(math.floor(max_lon))

    tiles = {}
    for f in glob.glob(os.path.join(hgt_dir, '*.hgt')):
        data, lat, lon = read_hgt(f)
        if tile_lat_min <= lat <= tile_lat_max and tile_lon_min <= lon <= tile_lon_max:
            tiles[(lat, lon)] = data
            print(f"  Loaded tile: {os.path.basename(f)} ({data.shape[0]}x{data.shape[1]})")

    if not tiles:
        print(f"  No HGT tiles found for region lat[{min_lat},{max_lat}] lon[{min_lon},{max_lon}]")
        return None, None

    # Determine tile size (all tiles same size)
    sample = next(iter(tiles.values()))
    n = sample.shape[0]  # 3601 or 1201

    # Stitch into big grid
    lat_range = range(tile_lat_max, tile_lat_min - 1, -1)  # north to south
    lon_range = range(tile_lon_min, tile_lon_max + 1)
    n_lat_tiles = len(lat_range)
    n_lon_tiles = len(lon_range)

    big = np.zeros((n_lat_tiles * (n - 1) + 1, n_lon_tiles * (n - 1) + 1), dtype=np.float32)
    for i, lat in enumerate(lat_range):
        for j, lon in enumerate(lon_range):
            tile = tiles.get((lat, lon))
            if tile is not None:
                r0 = i * (n - 1)
                c0 = j * (n - 1)
                big[r0:r0 + n, c0:c0 + n] = tile

    # The full stitched grid covers:
    full_min_lat = tile_lat_min
    full_max_lat = tile_lat_max + 1
    full_min_lon = tile_lon_min
    full_max_lon = tile_lon_max + 1

    # Crop to requested bounds
    total_rows, total_cols = big.shape
    lat_res = (full_max_lat - full_min_lat) / (total_rows - 1)
    lon_res = (full_max_lon - full_min_lon) / (total_cols - 1)

    r0 = int((full_max_lat - max_lat) / lat_res)
    r1 = int((full_max_lat - min_lat) / lat_res) + 1
    c0 = int((min_lon - full_min_lon) / lon_res)
    c1 = int((max_lon - full_min_lon) / lon_res) + 1

    r0 = max(0, r0)
    r1 = min(total_rows, r1)
    c0 = max(0, c0)
    c1 = min(total_cols, c1)

    cropped = big[r0:r1, c0:c1]

    # Subsample if too large (keep under ~512x512 for reasonable mesh size)
    max_dim = 512
    if cropped.shape[0] > max_dim or cropped.shape[1] > max_dim:
        factor = max(cropped.shape[0], cropped.shape[1]) // max_dim + 1
        cropped = cropped[::factor, ::factor]
        print(f"  Subsampled by {factor}x -> {cropped.shape}")

    actual_bounds = (min_lat, max_lat, min_lon, max_lon)
    print(f"  Elevation grid: {cropped.shape[0]}x{cropped.shape[1]}, "
          f"range [{cropped.min():.0f}, {cropped.max():.0f}]m")
    return cropped, actual_bounds


# ── Simple viewshed + signal simulation ──────────────────────────────

def simple_viewshed(elevation, node_row, node_col, node_elev, max_range_cells,
                    antenna_h=10.0):
    """Simple line-of-sight viewshed. Returns (visibility, signal_strength) grids."""
    rows, cols = elevation.shape
    vis = np.zeros((rows, cols), dtype=np.uint8)
    sig = np.full((rows, cols), -999.0, dtype=np.float32)

    obs_h = node_elev + antenna_h

    for r in range(rows):
        for c in range(cols):
            dr = r - node_row
            dc = c - node_col
            dist_cells = math.sqrt(dr * dr + dc * dc)
            if dist_cells > max_range_cells or dist_cells < 0.5:
                if dist_cells < 0.5:
                    vis[r, c] = 1
                    sig[r, c] = -60.0
                continue

            # Walk along the ray and check if any cell blocks LOS
            steps = int(dist_cells * 1.5) + 1
            blocked = False
            for s in range(1, steps):
                t = s / steps
                sr = node_row + dr * t
                sc = node_col + dc * t
                si, sj = int(sr), int(sc)
                if 0 <= si < rows and 0 <= sj < cols:
                    # Interpolate required clearance height
                    frac = t
                    needed_h = obs_h + (elevation[r, c] - obs_h) * frac
                    if elevation[si, sj] > needed_h + 1.0:
                        blocked = True
                        break

            if not blocked:
                vis[r, c] = 1
                # Simple log-distance path loss for signal strength
                dist_km = dist_cells * 0.03  # rough: 30m per cell
                if dist_km < 0.01:
                    dist_km = 0.01
                # Free-space path loss at 906 MHz
                fspl = 20 * math.log10(dist_km) + 20 * math.log10(906) + 32.44
                sig[r, c] = 27.0 - fspl  # tx_power - path_loss

    return vis, sig


def generate_viewsheds(elevation, bounds, nodes):
    """Generate viewshed for each node. Returns list of (vis, sig) arrays
    and merged coverage arrays."""
    rows, cols = elevation.shape
    min_lat, max_lat, min_lon, max_lon = bounds
    lat_res = (max_lat - min_lat) / (rows - 1)
    lon_res = (max_lon - min_lon) / (cols - 1)

    combined_vis = np.zeros((rows, cols), dtype=np.uint8)
    overlap_count = np.zeros((rows, cols), dtype=np.uint8)
    results = []

    for node in nodes:
        # Map node lat/lon to grid cell
        nr = int((max_lat - node['lat']) / lat_res)
        nc = int((node['lon'] - min_lon) / lon_res)
        nr = max(0, min(rows - 1, nr))
        nc = max(0, min(cols - 1, nc))

        node_elev = elevation[nr, nc]
        node['alt'] = float(node_elev)
        max_range_km = node.get('max_range_km', 5)
        max_range_cells = int(max_range_km * 1000 / 30)  # ~30m per cell

        print(f"  Computing viewshed for {node['name']} at ({nr},{nc}), "
              f"elev={node_elev:.0f}m, range={max_range_km}km...")

        vis, sig = simple_viewshed(elevation, nr, nc, node_elev,
                                   max_range_cells, node.get('antenna_height_m', 10))

        results.append((vis, sig))
        mask = vis > 0
        overlap_count[mask] += 1
        combined_vis[mask] = 1

    total_cells = rows * cols
    vis_cells = int(np.sum(combined_vis > 0))
    overlap_cells = int(np.sum(overlap_count > 1))
    coverage_pct = 100.0 * vis_cells / total_cells
    overlap_pct = 100.0 * overlap_cells / total_cells if vis_cells > 0 else 0

    print(f"  Coverage: {coverage_pct:.1f}%, Overlap: {overlap_pct:.1f}%")
    return results, combined_vis, overlap_count, coverage_pct, overlap_pct


# ── Import modes ─────────────────────────────────────────────────────

def import_from_hgt(dest_conn, project_name, hgt_dir, center_lat, center_lon, radius_km):
    """Import from SRTM HGT files with synthetic nodes and computed viewsheds."""
    cur = dest_conn.cursor()

    # Compute bounds from center + radius
    deg_lat = radius_km / 111.32
    deg_lon = radius_km / (111.32 * math.cos(math.radians(center_lat)))
    bounds = (center_lat - deg_lat, center_lat + deg_lat,
              center_lon - deg_lon, center_lon + deg_lon)

    print(f"Loading terrain from {hgt_dir}...")
    elevation, actual_bounds = load_hgt_region(hgt_dir, *bounds)
    if elevation is None:
        print("Failed to load elevation data", file=sys.stderr)
        sys.exit(1)

    project_id = create_project(cur, project_name, actual_bounds)
    print(f"Created project {project_id}: {project_name}")

    insert_elevation(cur, project_id, elevation, actual_bounds)

    # Create a default hardware profile
    hw_id = insert_hardware_profile(cur, {'name': 'Meshtastic 906MHz'})

    # Place synthetic nodes at interesting terrain points
    rows, cols = elevation.shape
    min_lat, max_lat, min_lon, max_lon = actual_bounds
    nodes = [
        {'name': 'Gateway', 'lat': center_lat + deg_lat * 0.3,
         'lon': center_lon - deg_lon * 0.1,
         'role': 0, 'max_range_km': 10, 'antenna_height_m': 15},
        {'name': 'Relay-North', 'lat': center_lat + deg_lat * 0.6,
         'lon': center_lon + deg_lon * 0.3,
         'role': 1, 'max_range_km': 7, 'antenna_height_m': 10},
        {'name': 'Relay-South', 'lat': center_lat - deg_lat * 0.4,
         'lon': center_lon - deg_lon * 0.3,
         'role': 1, 'max_range_km': 7, 'antenna_height_m': 10},
        {'name': 'Leaf-East', 'lat': center_lat + deg_lat * 0.1,
         'lon': center_lon + deg_lon * 0.6,
         'role': 2, 'max_range_km': 4, 'antenna_height_m': 5},
        {'name': 'Leaf-West', 'lat': center_lat - deg_lat * 0.2,
         'lon': center_lon - deg_lon * 0.5,
         'role': 2, 'max_range_km': 4, 'antenna_height_m': 5},
    ]

    # Compute viewsheds
    print("Computing viewsheds...")
    viewshed_results, combined_vis, overlap_count, cov_pct, ovl_pct = \
        generate_viewsheds(elevation, actual_bounds, nodes)

    # Insert nodes and viewsheds
    for i, node in enumerate(nodes):
        node_id = insert_node(cur, project_id, node, hw_id)
        vis, sig = viewshed_results[i]
        insert_viewshed(cur, node_id, project_id, vis, sig, actual_bounds)
        print(f"  Inserted node {node['name']} (id={node_id})")

    insert_merged_coverage(cur, project_id, combined_vis, overlap_count, cov_pct, ovl_pct)

    dest_conn.commit()
    print(f"\nDone! Project ID: {project_id}")
    print(f"View with: ./build/mesh3d --db \"host=localhost port=5434 dbname=mesh3d "
          f"user=mesh3d password=mesh3d\" --project {project_id}")


def import_from_mesh_mapgen(source_conn, dest_conn, project_name):
    """Import data from a mesh-mapgen database."""
    src = source_conn.cursor()
    dst = dest_conn.cursor()

    src.execute("SELECT name, min_lat, max_lat, min_lon, max_lon FROM projects LIMIT 1")
    row = src.fetchone()
    if not row:
        print("No project found in source database")
        return

    bounds = (row[1], row[2], row[3], row[4])
    project_id = create_project(dst, project_name or row[0], bounds)
    print(f"Created project {project_id}: {project_name or row[0]}")

    src.execute("SELECT name, ST_Y(location::geometry), ST_X(location::geometry), "
                "ST_Z(location::geometry), antenna_height_m, role, max_range_km FROM nodes")
    for nrow in src.fetchall():
        node = {
            'name': nrow[0], 'lat': nrow[1], 'lon': nrow[2],
            'alt': nrow[3] or 0, 'antenna_height_m': nrow[4] or 2,
            'role': nrow[5] or 2, 'max_range_km': nrow[6] or 5
        }
        nid = insert_node(dst, project_id, node)
        print(f"  Imported node: {node['name']} (id={nid})")

    src.execute("SELECT grid_rows, grid_cols, elevation_data, "
                "min_lat, max_lat, min_lon, max_lon FROM elevation_grids LIMIT 1")
    erow = src.fetchone()
    if erow:
        elev = np.frombuffer(erow[2], dtype=np.float32).reshape(erow[0], erow[1])
        ebounds = (erow[3], erow[4], erow[5], erow[6])
        insert_elevation(dst, project_id, elev, ebounds)
        print(f"  Imported elevation grid: {erow[0]}x{erow[1]}")

    dest_conn.commit()
    print("Import complete!")


def import_from_npz(dest_conn, project_name, npz_path):
    """Import from numpy .npz archive."""
    data = np.load(npz_path, allow_pickle=True)
    dst = dest_conn.cursor()

    bounds = tuple(data['bounds'])
    project_id = create_project(dst, project_name, bounds)

    if 'elevation' in data:
        insert_elevation(dst, project_id, data['elevation'], bounds)

    if 'nodes' in data:
        nodes = data['nodes'].item() if data['nodes'].ndim == 0 else data['nodes']
        if isinstance(nodes, dict):
            nodes = [nodes]
        for n in nodes:
            insert_node(dst, project_id, n)

    if 'merged_visibility' in data:
        vis = data['merged_visibility']
        ovl = data.get('overlap_count', np.zeros_like(vis))
        insert_merged_coverage(dst, project_id, vis, ovl)

    dest_conn.commit()
    print(f"Imported from {npz_path} as project {project_id}")


# ── Main ─────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="Import viewshed data to mesh3d DB")
    parser.add_argument("--dest-db", required=True, help="mesh3d DB connection string")
    parser.add_argument("--project-name", default="Imported Project")

    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--source-db", help="mesh-mapgen DB connection string")
    group.add_argument("--npz", help="Import from .npz file")
    group.add_argument("--hgt-dir", help="SRTM .hgt terrain directory")

    parser.add_argument("--center-lat", type=float, default=38.5,
                        help="Center latitude for HGT import (default: 38.5)")
    parser.add_argument("--center-lon", type=float, default=-105.5,
                        help="Center longitude for HGT import (default: -105.5)")
    parser.add_argument("--radius-km", type=float, default=15,
                        help="Radius in km for HGT import (default: 15)")
    args = parser.parse_args()

    dest_conn = psycopg2.connect(args.dest_db)

    if args.hgt_dir:
        import_from_hgt(dest_conn, args.project_name, args.hgt_dir,
                        args.center_lat, args.center_lon, args.radius_km)
    elif args.npz:
        import_from_npz(dest_conn, args.project_name, args.npz)
    elif args.source_db:
        source_conn = psycopg2.connect(args.source_db)
        import_from_mesh_mapgen(source_conn, dest_conn, args.project_name)
        source_conn.close()

    dest_conn.close()


if __name__ == "__main__":
    main()
