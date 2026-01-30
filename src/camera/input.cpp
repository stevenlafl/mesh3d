#include "camera/input.h"

namespace mesh3d {

void InputHandler::process_event(const SDL_Event& ev, Camera& cam) {
    switch (ev.type) {
    case SDL_QUIT:
        m_quit = true;
        break;

    case SDL_KEYDOWN:
        if (ev.key.repeat) break;
        switch (ev.key.keysym.sym) {
            case SDLK_w: m_fwd   = true; break;
            case SDLK_s: m_back  = true; break;
            case SDLK_a: m_left  = true; break;
            case SDLK_d: m_right = true; break;
            case SDLK_q: m_down  = true; break;
            case SDLK_e: m_up    = true; break;
            case SDLK_LSHIFT:
            case SDLK_RSHIFT: m_sprint = true; break;
            case SDLK_TAB:    m_tab  = true; break;
            case SDLK_1:      m_key1 = true; break;
            case SDLK_2:      m_key2 = true; break;
            case SDLK_t:      m_keyT = true; break;
            case SDLK_f:      m_keyF = true; break;
            case SDLK_ESCAPE:
                if (m_mouse_captured) {
                    m_mouse_captured = false;
                    SDL_SetRelativeMouseMode(SDL_FALSE);
                } else {
                    m_quit = true;
                }
                break;
        }
        break;

    case SDL_KEYUP:
        switch (ev.key.keysym.sym) {
            case SDLK_w: m_fwd   = false; break;
            case SDLK_s: m_back  = false; break;
            case SDLK_a: m_left  = false; break;
            case SDLK_d: m_right = false; break;
            case SDLK_q: m_down  = false; break;
            case SDLK_e: m_up    = false; break;
            case SDLK_LSHIFT:
            case SDLK_RSHIFT: m_sprint = false; break;
        }
        break;

    case SDL_MOUSEBUTTONDOWN:
        if (ev.button.button == SDL_BUTTON_RIGHT && !m_mouse_captured) {
            m_mouse_captured = true;
            SDL_SetRelativeMouseMode(SDL_TRUE);
        }
        break;

    case SDL_MOUSEMOTION:
        if (m_mouse_captured) {
            cam.rotate(static_cast<float>(ev.motion.xrel),
                      -static_cast<float>(ev.motion.yrel));
        }
        break;

    case SDL_MOUSEWHEEL:
        cam.zoom(static_cast<float>(ev.motion.x));
        break;
    }
}

void InputHandler::update(Camera& cam, float dt) {
    if (m_fwd)   cam.move_forward( dt, m_sprint);
    if (m_back)  cam.move_forward(-dt, m_sprint);
    if (m_right) cam.move_right( dt, m_sprint);
    if (m_left)  cam.move_right(-dt, m_sprint);
    if (m_up)    cam.move_up( dt, m_sprint);
    if (m_down)  cam.move_up(-dt, m_sprint);
}

} // namespace mesh3d
