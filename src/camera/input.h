#pragma once
#include <SDL2/SDL.h>
#include "camera/camera.h"

namespace mesh3d {

class InputHandler {
public:
    void process_event(const SDL_Event& ev, Camera& cam);
    void update(Camera& cam, float dt);

    bool quit_requested() const { return m_quit; }
    bool mouse_captured() const { return m_mouse_captured; }
    void set_mouse_captured(bool c) { m_mouse_captured = c; }

    /* Keyboard toggles — read and clear */
    bool consume_tab()   { bool v = m_tab; m_tab = false; return v; }
    bool consume_key1()  { bool v = m_key1; m_key1 = false; return v; }
    bool consume_key2()  { bool v = m_key2; m_key2 = false; return v; }
    bool consume_key3()  { bool v = m_key3; m_key3 = false; return v; }
    bool consume_keyT()  { bool v = m_keyT; m_keyT = false; return v; }
    bool consume_keyF()  { bool v = m_keyF; m_keyF = false; return v; }

private:
    bool m_quit = false;
    bool m_mouse_captured = false;
    bool m_sprint = false;

    /* movement keys held */
    bool m_fwd = false, m_back = false;
    bool m_left = false, m_right = false;
    bool m_up = false, m_down = false;

    /* toggle keys (edge-triggered) */
    bool m_tab = false, m_key1 = false, m_key2 = false, m_key3 = false;
    bool m_keyT = false, m_keyF = false;
};

} // namespace mesh3d
