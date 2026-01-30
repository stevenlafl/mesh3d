#include "camera/camera.h"
#include <algorithm>
#include <cmath>

namespace mesh3d {

glm::vec3 Camera::front() const { return m_front; }
glm::vec3 Camera::right() const { return m_right; }
glm::vec3 Camera::up()    const { return m_up; }

void Camera::update_vectors() {
    float yaw_r   = glm::radians(yaw);
    float pitch_r = glm::radians(pitch);
    m_front = glm::normalize(glm::vec3(
        std::cos(yaw_r) * std::cos(pitch_r),
        std::sin(pitch_r),
        std::sin(yaw_r) * std::cos(pitch_r)
    ));
    m_right = glm::normalize(glm::cross(m_front, glm::vec3(0, 1, 0)));
    m_up    = glm::normalize(glm::cross(m_right, m_front));
}

glm::mat4 Camera::view_matrix() const {
    return glm::lookAt(position, position + m_front, glm::vec3(0, 1, 0));
}

glm::mat4 Camera::projection_matrix(float aspect) const {
    return glm::perspective(glm::radians(fov), aspect, near_plane, far_plane);
}

void Camera::move_forward(float dt, bool sprint) {
    float speed = move_speed * (sprint ? sprint_multiplier : 1.0f);
    position += m_front * speed * dt;
}

void Camera::move_right(float dt, bool sprint) {
    float speed = move_speed * (sprint ? sprint_multiplier : 1.0f);
    position += m_right * speed * dt;
}

void Camera::move_up(float dt, bool sprint) {
    float speed = move_speed * (sprint ? sprint_multiplier : 1.0f);
    position += glm::vec3(0, 1, 0) * speed * dt;
}

void Camera::rotate(float dx, float dy) {
    yaw   += dx * mouse_sensitivity;
    pitch += dy * mouse_sensitivity;
    pitch = std::clamp(pitch, -89.0f, 89.0f);
    update_vectors();
}

void Camera::zoom(float delta) {
    fov -= delta;
    fov = std::clamp(fov, 10.0f, 120.0f);
}

} // namespace mesh3d
