#pragma once

namespace mesh3d {

struct HardwareProfile {
    const char* id;
    const char* name;
    float tx_power_dbm;
    float antenna_gain_dbi;
    float cable_loss_db;
    float rx_sensitivity_dbm;
    float frequency_mhz;
    float bandwidth_khz;
    int   spreading_factor;
    float max_range_km;
};

static constexpr int HARDWARE_PROFILE_COUNT = 8;

static const HardwareProfile HARDWARE_PROFILES[HARDWARE_PROFILE_COUNT] = {
    {"heltec_v3",              "Heltec V3",           22.0f, 2.0f,  0.5f, -132.0f, 906.875f, 250.0f, 11, 5.0f},
    {"tbeam_v1_1",             "T-Beam V1.1",         22.0f, 2.15f, 0.5f, -132.0f, 906.875f, 250.0f, 11, 5.0f},
    {"tbeam_1w",               "T-Beam 1W",           30.0f, 3.0f,  0.5f, -132.0f, 906.875f, 250.0f, 11, 15.0f},
    {"rak4631",                "RAK4631",             22.0f, 2.5f,  0.5f, -132.0f, 906.875f, 250.0f, 11, 5.0f},
    {"station_g2",             "Station G2",          30.0f, 3.0f,  0.5f, -136.0f, 906.875f, 250.0f, 11, 20.0f},
    {"nano_g2_ultra",          "Nano G2 Ultra",       30.0f, 2.0f,  0.5f, -136.0f, 906.875f, 250.0f, 11, 15.0f},
    {"base_station_high_gain", "Base Station HG",     30.0f, 6.0f,  0.5f, -136.0f, 906.875f, 250.0f, 11, 25.0f},
    {"handheld_compact",       "Handheld Compact",    22.0f, 1.0f,  0.5f, -132.0f, 906.875f, 250.0f, 11, 3.0f},
};

} // namespace mesh3d
