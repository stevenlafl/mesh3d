#include "tile/geotiff.h"
#include "util/log.h"
#include <cstring>
#include <cmath>
#include <algorithm>
#include <zlib.h>

namespace mesh3d {

/* ── TIFF parsing helpers ──────────────────────────────────────────── */

static bool is_little_endian(const uint8_t* data) {
    return data[0] == 'I' && data[1] == 'I';
}

template<typename T>
static T read_val(const uint8_t* p, bool le) {
    T v = 0;
    if (le) {
        for (int i = sizeof(T) - 1; i >= 0; --i)
            v = (v << 8) | p[i];
    } else {
        for (size_t i = 0; i < sizeof(T); ++i)
            v = (v << 8) | p[i];
    }
    return v;
}

static uint16_t r16(const uint8_t* p, bool le) { return read_val<uint16_t>(p, le); }
static uint32_t r32(const uint8_t* p, bool le) { return read_val<uint32_t>(p, le); }

static double r_double(const uint8_t* p, bool le) {
    uint64_t bits = 0;
    if (le) {
        for (int i = 7; i >= 0; --i) bits = (bits << 8) | p[i];
    } else {
        for (int i = 0; i < 8; ++i) bits = (bits << 8) | p[i];
    }
    double v;
    std::memcpy(&v, &bits, 8);
    return v;
}

static float r_float(const uint8_t* p, bool le) {
    uint32_t bits = r32(p, le);
    float v;
    std::memcpy(&v, &bits, 4);
    return v;
}

/* TIFF tag IDs */
enum {
    TAG_WIDTH           = 256,
    TAG_HEIGHT          = 257,
    TAG_BITS_PER_SAMPLE = 258,
    TAG_COMPRESSION     = 259,
    TAG_STRIP_OFFSETS   = 273,
    TAG_ROWS_PER_STRIP  = 278,
    TAG_STRIP_BYTE_CNT  = 279,
    TAG_SAMPLE_FORMAT   = 339,
    TAG_MODEL_TIEPOINT  = 33922,
    TAG_MODEL_PIXSCALE  = 33550,
};

struct IFDEntry {
    uint16_t tag;
    uint16_t type;
    uint32_t count;
    uint32_t value_offset; // value or offset to value
};

static size_t type_size(uint16_t type) {
    switch (type) {
        case 1: return 1; // BYTE
        case 2: return 1; // ASCII
        case 3: return 2; // SHORT
        case 4: return 4; // LONG
        case 5: return 8; // RATIONAL
        case 6: return 1; // SBYTE
        case 7: return 1; // UNDEFINED
        case 8: return 2; // SSHORT
        case 9: return 4; // SLONG
        case 10: return 8; // SRATIONAL
        case 11: return 4; // FLOAT
        case 12: return 8; // DOUBLE
        case 16: return 8; // LONG8 (BigTIFF)
        default: return 1;
    }
}

static std::vector<uint64_t> read_offset_array(const uint8_t* data, size_t size,
                                                 const IFDEntry& e, bool le) {
    std::vector<uint64_t> result;
    size_t ts = type_size(e.type);
    size_t total = ts * e.count;
    const uint8_t* p;

    if (total <= 4) {
        /* Value is inline in the value_offset field */
        uint8_t buf[4];
        if (le) {
            buf[0] = e.value_offset & 0xFF;
            buf[1] = (e.value_offset >> 8) & 0xFF;
            buf[2] = (e.value_offset >> 16) & 0xFF;
            buf[3] = (e.value_offset >> 24) & 0xFF;
        } else {
            buf[3] = e.value_offset & 0xFF;
            buf[2] = (e.value_offset >> 8) & 0xFF;
            buf[1] = (e.value_offset >> 16) & 0xFF;
            buf[0] = (e.value_offset >> 24) & 0xFF;
        }
        p = buf;
        for (uint32_t i = 0; i < e.count; ++i) {
            if (e.type == 3) // SHORT
                result.push_back(r16(p + i * 2, le));
            else if (e.type == 4) // LONG
                result.push_back(r32(p + i * 4, le));
            else
                result.push_back(p[i]);
        }
    } else {
        if (e.value_offset + total > size) return result;
        p = data + e.value_offset;
        for (uint32_t i = 0; i < e.count; ++i) {
            if (e.type == 3)
                result.push_back(r16(p + i * 2, le));
            else if (e.type == 4)
                result.push_back(r32(p + i * 4, le));
            else if (e.type == 16) {
                uint64_t v = 0;
                if (le) for (int b = 7; b >= 0; --b) v = (v << 8) | p[i * 8 + b];
                else for (int b = 0; b < 8; ++b) v = (v << 8) | p[i * 8 + b];
                result.push_back(v);
            }
            else
                result.push_back(p[i]);
        }
    }
    return result;
}

static std::vector<double> read_double_array(const uint8_t* data, size_t size,
                                               const IFDEntry& e, bool le) {
    std::vector<double> result;
    if (e.type != 12) return result; // DOUBLE only
    size_t total = 8 * e.count;
    if (e.value_offset + total > size) return result;
    const uint8_t* p = data + e.value_offset;
    for (uint32_t i = 0; i < e.count; ++i) {
        result.push_back(r_double(p + i * 8, le));
    }
    return result;
}

bool geotiff_parse(const uint8_t* data, size_t size, GeoTiffInfo& info) {
    if (size < 8) return false;

    bool le = is_little_endian(data);
    uint16_t magic = r16(data + 2, le);
    if (magic != 42) {
        LOG_WARN("GeoTIFF: not a TIFF file (magic=%d)", magic);
        return false;
    }

    uint32_t ifd_offset = r32(data + 4, le);
    if (ifd_offset + 2 > size) return false;

    uint16_t num_entries = r16(data + ifd_offset, le);
    const uint8_t* entry_p = data + ifd_offset + 2;

    for (uint16_t i = 0; i < num_entries; ++i) {
        if (entry_p + 12 > data + size) break;

        IFDEntry e;
        e.tag = r16(entry_p, le);
        e.type = r16(entry_p + 2, le);
        e.count = r32(entry_p + 4, le);
        e.value_offset = r32(entry_p + 8, le);
        entry_p += 12;

        /* For single-value tags, extract value directly */
        auto single_val = [&]() -> uint32_t {
            if (e.type == 3 && e.count == 1) return r16(data + ifd_offset + 2 + i * 12 + 8, le);
            return e.value_offset;
        };

        switch (e.tag) {
            case TAG_WIDTH:
                info.width = static_cast<int>(single_val());
                break;
            case TAG_HEIGHT:
                info.height = static_cast<int>(single_val());
                break;
            case TAG_BITS_PER_SAMPLE:
                info.bits_per_sample = static_cast<int>(single_val());
                break;
            case TAG_COMPRESSION:
                info.compression = static_cast<int>(single_val());
                break;
            case TAG_ROWS_PER_STRIP:
                info.rows_per_strip = static_cast<int>(single_val());
                break;
            case TAG_SAMPLE_FORMAT:
                info.sample_format = static_cast<int>(single_val());
                break;
            case TAG_STRIP_OFFSETS:
                info.strip_offsets = read_offset_array(data, size, e, le);
                break;
            case TAG_STRIP_BYTE_CNT:
                info.strip_byte_counts = read_offset_array(data, size, e, le);
                break;
            case TAG_MODEL_TIEPOINT: {
                auto vals = read_double_array(data, size, e, le);
                if (vals.size() >= 6) {
                    info.tie_x = vals[3]; // lon of (0,0) pixel
                    info.tie_y = vals[4]; // lat of (0,0) pixel
                    info.has_geo = true;
                }
                break;
            }
            case TAG_MODEL_PIXSCALE: {
                auto vals = read_double_array(data, size, e, le);
                if (vals.size() >= 2) {
                    info.scale_x = vals[0];
                    info.scale_y = vals[1];
                    info.has_geo = true;
                }
                break;
            }
        }
    }

    if (info.rows_per_strip == 0) info.rows_per_strip = info.height;

    return info.width > 0 && info.height > 0;
}

static std::vector<uint8_t> decompress_deflate(const uint8_t* src, size_t src_len,
                                                 size_t expected_len) {
    std::vector<uint8_t> out(expected_len);
    z_stream strm{};
    strm.next_in = const_cast<Bytef*>(src);
    strm.avail_in = static_cast<uInt>(src_len);
    strm.next_out = out.data();
    strm.avail_out = static_cast<uInt>(out.size());

    if (inflateInit(&strm) != Z_OK) return {};
    int ret = inflate(&strm, Z_FINISH);
    inflateEnd(&strm);

    if (ret != Z_STREAM_END) return {};
    out.resize(strm.total_out);
    return out;
}

std::vector<float> geotiff_read_elevation(const uint8_t* data, size_t size,
                                           const GeoTiffInfo& info) {
    int total = info.width * info.height;
    std::vector<float> elev(total, 0.0f);

    if (info.strip_offsets.empty()) {
        LOG_WARN("GeoTIFF: no strip offsets");
        return elev;
    }

    bool le = is_little_endian(data);
    int bytes_per_sample = info.bits_per_sample / 8;
    int row_bytes = info.width * bytes_per_sample;

    int strip_rows = info.rows_per_strip;
    int row = 0;

    for (size_t s = 0; s < info.strip_offsets.size(); ++s) {
        uint64_t offset = info.strip_offsets[s];
        uint64_t byte_count = (s < info.strip_byte_counts.size()) ?
                               info.strip_byte_counts[s] : 0;

        if (offset + byte_count > size) break;

        const uint8_t* strip_data = data + offset;
        std::vector<uint8_t> decompressed;

        if (info.compression == 8) { // deflate
            int rows_this_strip = std::min(strip_rows, info.height - row);
            size_t expected = rows_this_strip * row_bytes;
            decompressed = decompress_deflate(strip_data, byte_count, expected);
            if (decompressed.empty()) {
                LOG_WARN("GeoTIFF: deflate decompression failed at strip %zu", s);
                row += strip_rows;
                continue;
            }
            strip_data = decompressed.data();
            byte_count = decompressed.size();
        } else if (info.compression != 1) {
            LOG_WARN("GeoTIFF: unsupported compression %d", info.compression);
            return elev;
        }

        /* Read rows from this strip */
        int rows_this_strip = std::min(strip_rows, info.height - row);
        for (int r = 0; r < rows_this_strip && row + r < info.height; ++r) {
            const uint8_t* row_p = strip_data + r * row_bytes;
            if (row_p + row_bytes > strip_data + byte_count) break;

            int out_row = row + r;
            for (int c = 0; c < info.width; ++c) {
                int idx = out_row * info.width + c;
                const uint8_t* p = row_p + c * bytes_per_sample;

                if (info.sample_format == 3 && info.bits_per_sample == 32) {
                    elev[idx] = r_float(p, le);
                } else if (info.sample_format == 2 && info.bits_per_sample == 16) {
                    /* Signed int16 */
                    int16_t v = static_cast<int16_t>(r16(p, le));
                    elev[idx] = static_cast<float>(v);
                } else if (info.bits_per_sample == 16) {
                    /* Unsigned int16 */
                    elev[idx] = static_cast<float>(r16(p, le));
                } else if (info.sample_format == 3 && info.bits_per_sample == 64) {
                    elev[idx] = static_cast<float>(r_double(p, le));
                }
            }
        }
        row += rows_this_strip;
    }

    return elev;
}

} // namespace mesh3d
