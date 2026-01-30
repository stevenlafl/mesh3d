#pragma once
#include <vector>
#include <string>
#include <cstdint>

namespace mesh3d {

/* Minimal GeoTIFF parser for single-band float32 elevation tiles.
   Handles uncompressed and deflate-compressed strips/tiles.
   Does NOT handle multi-band, tiled layout, or exotic sample formats. */

struct GeoTiffInfo {
    int width = 0;
    int height = 0;
    int bits_per_sample = 0;
    int sample_format = 0;     // 1=uint, 2=int, 3=float
    int compression = 0;       // 1=none, 8=deflate, 32773=packbits
    int rows_per_strip = 0;

    /* Geo metadata (from GeoKeys or ModelTiepoint/ModelPixelScale) */
    double tie_x = 0, tie_y = 0;       // upper-left corner (lon, lat)
    double scale_x = 0, scale_y = 0;   // pixel size (degrees or meters)
    bool has_geo = false;

    /* Strip offsets and byte counts */
    std::vector<uint64_t> strip_offsets;
    std::vector<uint64_t> strip_byte_counts;
};

/* Parse GeoTIFF header and extract metadata.
   Returns false if the file is not a valid TIFF or cannot be parsed. */
bool geotiff_parse(const uint8_t* data, size_t size, GeoTiffInfo& info);

/* Read elevation data from a parsed GeoTIFF.
   Returns row-major float array (height x width).
   Supports uncompressed float32 and int16 (converts to float).
   For deflate-compressed data, requires zlib. */
std::vector<float> geotiff_read_elevation(const uint8_t* data, size_t size,
                                           const GeoTiffInfo& info);

} // namespace mesh3d
