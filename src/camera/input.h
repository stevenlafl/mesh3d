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

    /* Menu/mode awareness */
    void set_menu_open(bool open) { m_menu_open = open; }
    bool menu_open() const { return m_menu_open; }

    /* Keyboard toggles — read and clear */
    bool consume_tab()   { bool v = m_tab; m_tab = false; return v; }
    bool consume_key1()  { bool v = m_key1; m_key1 = false; return v; }
    bool consume_key3()  { bool v = m_key3; m_key3 = false; return v; }
    bool consume_keyT()  { bool v = m_keyT; m_keyT = false; return v; }
    bool consume_keyF()  { bool v = m_keyF; m_keyF = false; return v; }
    bool consume_keyN()  { bool v = m_keyN; m_keyN = false; return v; }
    bool consume_keyH()  { bool v = m_keyH; m_keyH = false; return v; }
    bool consume_escape(){ bool v = m_escape; m_escape = false; return v; }

    /* Mouse click events — read and clear */
    bool consume_left_click()  { bool v = m_left_click; m_left_click = false; return v; }
    bool consume_right_click() { bool v = m_right_click; m_right_click = false; return v; }
    bool consume_delete_key()  { bool v = m_delete_key; m_delete_key = false; return v; }

    /* Menu-specific input */
    bool consume_enter()    { bool v = m_enter; m_enter = false; return v; }
    bool consume_arrow_up() { bool v = m_arrow_up; m_arrow_up = false; return v; }
    bool consume_arrow_down() { bool v = m_arrow_down; m_arrow_down = false; return v; }
    bool consume_arrow_left() { bool v = m_arrow_left; m_arrow_left = false; return v; }
    bool consume_arrow_right(){ bool v = m_arrow_right; m_arrow_right = false; return v; }
    bool consume_backspace() { bool v = m_backspace; m_backspace = false; return v; }

    /* Text input */
    char consume_text_char() { char c = m_text_char; m_text_char = 0; return c; }

private:
    bool m_quit = false;
    bool m_mouse_captured = false;
    bool m_sprint = false;
    bool m_menu_open = false;

    /* movement keys held */
    bool m_fwd = false, m_back = false;
    bool m_left = false, m_right = false;
    bool m_up = false, m_down = false;

    /* toggle keys (edge-triggered) */
    bool m_tab = false, m_key1 = false, m_key3 = false;
    bool m_keyT = false, m_keyF = false;
    bool m_keyN = false, m_keyH = false;
    bool m_escape = false;

    /* Mouse clicks (edge-triggered) */
    bool m_left_click = false, m_right_click = false;
    bool m_delete_key = false;

    /* Menu input (edge-triggered) */
    bool m_enter = false;
    bool m_arrow_up = false, m_arrow_down = false;
    bool m_arrow_left = false, m_arrow_right = false;
    bool m_backspace = false;
    char m_text_char = 0;

    bool m_focused = true;
};

} // namespace mesh3d
