#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace mesh3d {

class Camera {
public:
    glm::vec3 position{0.0f, 500.0f, 0.0f};
    float yaw   = -90.0f;  // degrees, -90 = looking along -Z
    float pitch  = -30.0f;  // degrees, negative = looking down
    float fov    = 60.0f;
    float near_plane = 1.0f;
    float far_plane  = 100000.0f; // 100 km
    float move_speed = 200.0f;    // m/s base
    float mouse_sensitivity = 0.1f;
    float sprint_multiplier = 4.0f;

    glm::vec3 front() const;
    glm::vec3 right() const;
    glm::vec3 up() const;

    glm::mat4 view_matrix() const;
    glm::mat4 projection_matrix(float aspect) const;

    void move_forward(float dt, bool sprint);
    void move_right(float dt, bool sprint);
    void move_up(float dt, bool sprint);
    void rotate(float dx, float dy);
    void zoom(float delta);

private:
    void update_vectors();
    glm::vec3 m_front{0.0f, -0.5f, -0.866f};
    glm::vec3 m_right{1.0f, 0.0f, 0.0f};
    glm::vec3 m_up{0.0f, 1.0f, 0.0f};
};

} // namespace mesh3d
