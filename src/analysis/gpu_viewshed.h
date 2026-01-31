#pragma once
#include "render/compute_shader.h"
#include "scene/scene.h"
#include "util/math_util.h"
#include <mesh3d/types.h>
#include <glad/glad.h>
#include <vector>
#include <string>
#include <cstdint>

namespace mesh3d {

/* Async compute state for non-blocking GPU viewshed */
enum class ComputeState { IDLE, DISPATCHED, READY };

class GpuViewshed {
public:
    GpuViewshed() = default;
    ~GpuViewshed();

    bool init(const std::string& shader_dir);
    void shutdown();

    /* Upload elevation grid to GPU texture */
    void upload_elevation(const float* data, int rows, int cols);

    /* Set grid parameters for coordinate mapping */
    void set_grid_params(const mesh3d_bounds_t& bounds, int rows, int cols);

    /* Set propagation model (FSPL, ITM, or Fresnel) */
    void set_propagation_model(mesh3d_prop_model_t model);
    mesh3d_prop_model_t propagation_model() const { return m_prop_model; }

    /* Set ITM parameters for ITM propagation model */
    void set_itm_params(const mesh3d_itm_params_t& params);

    /* Set receiver / display config */
    void set_rf_config(const mesh3d_rf_config_t& config);

    /* Compute viewshed for all nodes, merging results on GPU (blocking) */
    void compute_all(const std::vector<NodeData>& nodes);

    /* Async compute: dispatch GPU work and place a fence (non-blocking).
       cpu_elevation is the same data passed to upload_elevation(), used to
       look up node heights on the CPU side and avoid per-node glReadPixels
       stalls that would serialize the dispatch loop. */
    void compute_all_async(const std::vector<NodeData>& nodes,
                           const float* cpu_elevation);

    /* Check if GPU work is done (non-blocking). Returns current state. */
    ComputeState poll_state();

    /* Read back merged results to CPU arrays (blocking readback) */
    void read_back(std::vector<uint8_t>& vis,
                   std::vector<float>& signal,
                   std::vector<uint8_t>& overlap);

    /* Read back after async compute completes, resets state to IDLE */
    void read_back_async(std::vector<uint8_t>& vis,
                         std::vector<float>& signal,
                         std::vector<uint8_t>& overlap);

    /* Current async state */
    ComputeState state() const { return m_state; }

    /* Check if GL 4.3 compute shaders are available */
    static bool is_available();

    GpuViewshed(const GpuViewshed&) = delete;
    GpuViewshed& operator=(const GpuViewshed&) = delete;

private:
    ComputeShader m_viewshed_shader;  // FSPL + diffraction
    ComputeShader m_merge_shader;
    ComputeShader m_itm_shader;       // Longley-Rice ITM
    ComputeShader m_fresnel_shader;   // Fresnel-Kirchhoff

    /* GPU textures */
    GLuint m_elevation_tex = 0;   // R32F  (input)
    GLuint m_node_vis_tex  = 0;   // R8UI  (per-node scratch)
    GLuint m_node_sig_tex  = 0;   // R32F  (per-node scratch)
    GLuint m_merged_vis_tex = 0;  // R8UI  (accumulated)
    GLuint m_merged_sig_tex = 0;  // R32F  (accumulated)
    GLuint m_overlap_tex   = 0;   // R8UI  (accumulated)

    /* Grid dimensions */
    int m_rows = 0, m_cols = 0;

    /* Geographic params for coordinate mapping */
    mesh3d_bounds_t m_bounds{};
    float m_cell_meters = 30.0f;

    /* Propagation model selection */
    mesh3d_prop_model_t m_prop_model = MESH3D_PROP_ITM;
    mesh3d_itm_params_t m_itm_params{5, 15.0f, 0.005f, 1, 50.0f, 50.0f, 301.0f, 50.0f, 12};

    /* Receiver / display config */
    mesh3d_rf_config_t m_rf_config{-130.0f, 1.0f, 2.0f, 2.0f, -130.0f, -80.0f};

    bool m_initialized = false;
    bool m_has_itm = false;
    bool m_has_fresnel = false;

    /* Async compute state */
    ComputeState m_state = ComputeState::IDLE;
    GLsync m_fence = nullptr;

    /* Chunked dispatch: breaks each node's viewshed into row-bands so the
       GPU can interleave render work between chunks. */
    static constexpr int ROWS_PER_CHUNK = 128;

    struct ChunkNode {
        NodeData data;
        int col, row;
        float observer_height;
    };

    struct ChunkState {
        std::vector<ChunkNode> nodes;
        size_t current_node = 0;
        int current_row = 0;
        bool merge_pending = false;
        ComputeShader* active_shader = nullptr;
        GLuint groups_x = 0;
    };

    ChunkState m_chunk;

    void create_textures(int rows, int cols);
    void destroy_textures();
    void clear_merge_textures();

    /* Set uniforms that are constant across all nodes (grid, environment, RX).
       Must be called after active_shader->use(). */
    void set_environment_uniforms(ComputeShader* shader);

    /* Set per-node TX uniforms from node hardware profile.
       Must be called after set_environment_uniforms(). */
    void set_node_uniforms(ComputeShader* shader, const NodeData& nd,
                           int nc, int nr, float observer_height);

    /* Dispatch merge pass after each node's viewshed pass. */
    void dispatch_merge(GLuint groups_x, GLuint groups_y);

    /* Dispatch one row-band of the viewshed shader for the current chunk node. */
    void dispatch_viewshed_band();

    /* Advance chunked dispatch state machine (called when fence is signaled). */
    void advance_chunk();

    /* Place a GPU fence and flush. */
    void place_fence();
};

} // namespace mesh3d
