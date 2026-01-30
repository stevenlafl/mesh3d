#pragma once
#include <cmath>
#include <mesh3d/types.h>

namespace mesh3d {

/* Approximate meters-per-degree at a given latitude */
inline double meters_per_deg_lat() { return 111320.0; }
inline double meters_per_deg_lon(double lat_rad) {
    return 111320.0 * std::cos(lat_rad);
}

/* Convert lat/lon to local XZ coordinates (meters from bounds center).
   X = east (lon), Z = south (lat inverted so north = -Z), Y = up (elevation). */
struct LocalCoord {
    float x, z;
};

struct LatLon {
    double lat, lon;
};

struct GeoProjection {
    double center_lat, center_lon;
    double m_per_deg_lat;
    double m_per_deg_lon;

    void init(const mesh3d_bounds_t& b) {
        center_lat = (b.min_lat + b.max_lat) * 0.5;
        center_lon = (b.min_lon + b.max_lon) * 0.5;
        m_per_deg_lat = meters_per_deg_lat();
        m_per_deg_lon = meters_per_deg_lon(center_lat * M_PI / 180.0);
    }

    LocalCoord project(double lat, double lon) const {
        float x = static_cast<float>((lon - center_lon) * m_per_deg_lon);
        float z = static_cast<float>((center_lat - lat) * m_per_deg_lat); // north = -Z
        return {x, z};
    }

    float width_m(const mesh3d_bounds_t& b) const {
        return static_cast<float>((b.max_lon - b.min_lon) * m_per_deg_lon);
    }

    float height_m(const mesh3d_bounds_t& b) const {
        return static_cast<float>((b.max_lat - b.min_lat) * m_per_deg_lat);
    }

    LatLon unproject(float world_x, float world_z) const {
        double lon = center_lon + world_x / m_per_deg_lon;
        double lat = center_lat - world_z / m_per_deg_lat; // -Z = north
        return {lat, lon};
    }
};

} // namespace mesh3d
