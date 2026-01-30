#include "camera/input.h"

namespace mesh3d {

void InputHandler::process_event(const SDL_Event& ev, Camera& cam) {
    switch (ev.type) {
    case SDL_QUIT:
        m_quit = true;
        break;

    case SDL_WINDOWEVENT:
        if (ev.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
            m_focused = true;
        } else if (ev.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
            m_focused = false;
            /* Release all held keys so camera doesn't drift */
            m_fwd = m_back = m_left = m_right = m_up = m_down = false;
            m_sprint = false;
            if (m_mouse_captured) {
                m_mouse_captured = false;
                SDL_SetRelativeMouseMode(SDL_FALSE);
            }
        }
        break;

    case SDL_TEXTINPUT:
        if (m_menu_open && ev.text.text[0]) {
            m_text_char = ev.text.text[0];
        }
        break;

    case SDL_KEYDOWN:
        if (ev.key.repeat) {
            /* Allow repeats for menu text input keys */
            if (m_menu_open) {
                switch (ev.key.keysym.sym) {
                    case SDLK_BACKSPACE: m_backspace = true; break;
                    case SDLK_UP:    m_arrow_up = true; break;
                    case SDLK_DOWN:  m_arrow_down = true; break;
                    case SDLK_LEFT:  m_arrow_left = true; break;
                    case SDLK_RIGHT: m_arrow_right = true; break;
                    default: break;
                }
            }
            break;
        }

        if (m_menu_open) {
            /* Menu-specific input */
            switch (ev.key.keysym.sym) {
                case SDLK_ESCAPE:   m_escape = true; break;
                case SDLK_RETURN:
                case SDLK_KP_ENTER: m_enter = true; break;
                case SDLK_UP:       m_arrow_up = true; break;
                case SDLK_DOWN:     m_arrow_down = true; break;
                case SDLK_LEFT:     m_arrow_left = true; break;
                case SDLK_RIGHT:    m_arrow_right = true; break;
                case SDLK_BACKSPACE:m_backspace = true; break;
                case SDLK_DELETE:   m_delete_key = true; break;
                case SDLK_TAB:      m_arrow_down = true; break; // tab acts as down
                default: break;
            }
            break; // don't process camera keys when menu is open
        }

        /* Normal (non-menu) input */
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
            case SDLK_3:      m_key3 = true; break;
            case SDLK_t:      m_keyT = true; break;
            case SDLK_f:      m_keyF = true; break;
            case SDLK_n:      m_keyN = true; break;
            case SDLK_h:      m_keyH = true; break;
            case SDLK_DELETE:  m_delete_key = true; break;
            case SDLK_ESCAPE:
                m_escape = true;
                if (m_mouse_captured) {
                    m_mouse_captured = false;
                    SDL_SetRelativeMouseMode(SDL_FALSE);
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
        if (m_menu_open) break; // ignore clicks when menu open
        if (ev.button.button == SDL_BUTTON_LEFT) {
            m_left_click = true;
        } else if (ev.button.button == SDL_BUTTON_RIGHT) {
            if (!m_mouse_captured) {
                m_mouse_captured = true;
                SDL_SetRelativeMouseMode(SDL_TRUE);
            }
            m_right_click = true;
        }
        break;

    case SDL_MOUSEMOTION:
        if (m_mouse_captured && m_focused && !m_menu_open) {
            cam.rotate(static_cast<float>(ev.motion.xrel),
                      -static_cast<float>(ev.motion.yrel));
        }
        break;

    case SDL_MOUSEWHEEL:
        if (!m_menu_open) {
            cam.zoom(static_cast<float>(ev.motion.x));
        }
        break;
    }
}

void InputHandler::update(Camera& cam, float dt) {
    if (m_menu_open) return; // suppress camera movement when menu open

    if (m_fwd)   cam.move_forward( dt, m_sprint);
    if (m_back)  cam.move_forward(-dt, m_sprint);
    if (m_right) cam.move_right( dt, m_sprint);
    if (m_left)  cam.move_right(-dt, m_sprint);
    if (m_up)    cam.move_up( dt, m_sprint);
    if (m_down)  cam.move_up(-dt, m_sprint);
}

} // namespace mesh3d
