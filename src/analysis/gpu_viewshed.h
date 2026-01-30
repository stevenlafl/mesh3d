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

    /* Compute viewshed for all nodes, merging results on GPU */
    void compute_all(const std::vector<NodeData>& nodes);

    /* Read back merged results to CPU arrays */
    void read_back(std::vector<uint8_t>& vis,
                   std::vector<float>& signal,
                   std::vector<uint8_t>& overlap);

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
    mesh3d_prop_model_t m_prop_model = MESH3D_PROP_FSPL;
    mesh3d_itm_params_t m_itm_params{5, 15.0f, 0.005f, 1, 50.0f, 50.0f};

    bool m_initialized = false;
    bool m_has_itm = false;
    bool m_has_fresnel = false;

    void create_textures(int rows, int cols);
    void destroy_textures();
    void clear_merge_textures();
};

} // namespace mesh3d
