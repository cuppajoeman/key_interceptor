
#include "linux_input_adapter.hpp"

#include <fcntl.h>
#include <iostream>
#include <linux/input-event-codes.h>
#include <linux/input.h>
#include <stdexcept>
#include <unistd.h>

LinuxInputAdapter::LinuxInputAdapter(InputState &input_state, const std::string &device_path, bool exclusive_control)
    : input_state(input_state) {

    fd = open(device_path.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        throw std::runtime_error("Failed to open input device: " + device_path);
    }

    // WARN: here we're grabbing the keyboard's input completely so that it will not go to any other program, this is
    // only safe because we forward the keys in the main function, otherwise this could leave you in a state without any
    // keyboard input.
    if (exclusive_control) {
        if (ioctl(fd, EVIOCGRAB, 1) < 0) {
            perror("EVIOCGRAB");
        }
    }

    // Map Linux input codes to your EKey enum
    linux_code_to_key_enum.emplace(KEY_A, EKey::a);
    linux_code_to_key_enum.emplace(KEY_B, EKey::b);
    linux_code_to_key_enum.emplace(KEY_C, EKey::c);
    linux_code_to_key_enum.emplace(KEY_D, EKey::d);
    linux_code_to_key_enum.emplace(KEY_E, EKey::e);
    linux_code_to_key_enum.emplace(KEY_F, EKey::f);
    linux_code_to_key_enum.emplace(KEY_G, EKey::g);
    linux_code_to_key_enum.emplace(KEY_H, EKey::h);
    linux_code_to_key_enum.emplace(KEY_I, EKey::i);
    linux_code_to_key_enum.emplace(KEY_J, EKey::j);
    linux_code_to_key_enum.emplace(KEY_K, EKey::k);
    linux_code_to_key_enum.emplace(KEY_L, EKey::l);
    linux_code_to_key_enum.emplace(KEY_M, EKey::m);
    linux_code_to_key_enum.emplace(KEY_N, EKey::n);
    linux_code_to_key_enum.emplace(KEY_O, EKey::o);
    linux_code_to_key_enum.emplace(KEY_P, EKey::p);
    linux_code_to_key_enum.emplace(KEY_Q, EKey::q);
    linux_code_to_key_enum.emplace(KEY_R, EKey::r);
    linux_code_to_key_enum.emplace(KEY_S, EKey::s);
    linux_code_to_key_enum.emplace(KEY_T, EKey::t);
    linux_code_to_key_enum.emplace(KEY_U, EKey::u);
    linux_code_to_key_enum.emplace(KEY_V, EKey::v);
    linux_code_to_key_enum.emplace(KEY_W, EKey::w);
    linux_code_to_key_enum.emplace(KEY_X, EKey::x);
    linux_code_to_key_enum.emplace(KEY_Y, EKey::y);
    linux_code_to_key_enum.emplace(KEY_Z, EKey::z);

    linux_code_to_key_enum.emplace(KEY_SPACE, EKey::SPACE);
    linux_code_to_key_enum.emplace(KEY_GRAVE, EKey::GRAVE_ACCENT);

    linux_code_to_key_enum.emplace(KEY_1, EKey::ONE);
    linux_code_to_key_enum.emplace(KEY_2, EKey::TWO);
    linux_code_to_key_enum.emplace(KEY_3, EKey::THREE);
    linux_code_to_key_enum.emplace(KEY_4, EKey::FOUR);
    linux_code_to_key_enum.emplace(KEY_5, EKey::FIVE);
    linux_code_to_key_enum.emplace(KEY_6, EKey::SIX);
    linux_code_to_key_enum.emplace(KEY_7, EKey::SEVEN);
    linux_code_to_key_enum.emplace(KEY_8, EKey::EIGHT);
    linux_code_to_key_enum.emplace(KEY_9, EKey::NINE);
    linux_code_to_key_enum.emplace(KEY_0, EKey::ZERO);
    linux_code_to_key_enum.emplace(KEY_MINUS, EKey::MINUS);
    linux_code_to_key_enum.emplace(KEY_EQUAL, EKey::EQUAL);

    linux_code_to_key_enum.emplace(KEY_LEFTBRACE, EKey::LEFT_SQUARE_BRACKET);
    linux_code_to_key_enum.emplace(KEY_RIGHTBRACE, EKey::RIGHT_SQUARE_BRACKET);

    linux_code_to_key_enum.emplace(KEY_COMMA, EKey::COMMA);
    linux_code_to_key_enum.emplace(KEY_DOT, EKey::PERIOD);

    linux_code_to_key_enum.emplace(KEY_CAPSLOCK, EKey::CAPS_LOCK);
    linux_code_to_key_enum.emplace(KEY_ESC, EKey::ESCAPE);
    linux_code_to_key_enum.emplace(KEY_ENTER, EKey::ENTER);
    linux_code_to_key_enum.emplace(KEY_TAB, EKey::TAB);
    linux_code_to_key_enum.emplace(KEY_BACKSPACE, EKey::BACKSPACE);
    linux_code_to_key_enum.emplace(KEY_INSERT, EKey::INSERT);
    linux_code_to_key_enum.emplace(KEY_DELETE, EKey::DELETE_);

    linux_code_to_key_enum.emplace(KEY_RIGHT, EKey::RIGHT);
    linux_code_to_key_enum.emplace(KEY_LEFT, EKey::LEFT);
    linux_code_to_key_enum.emplace(KEY_UP, EKey::UP);
    linux_code_to_key_enum.emplace(KEY_DOWN, EKey::DOWN);

    linux_code_to_key_enum.emplace(KEY_SLASH, EKey::SLASH);
    linux_code_to_key_enum.emplace(KEY_BACKSLASH, EKey::BACKSLASH);

    linux_code_to_key_enum.emplace(KEY_SEMICOLON, EKey::SEMICOLON);
    linux_code_to_key_enum.emplace(KEY_APOSTROPHE, EKey::SINGLE_QUOTE);

    // FUNCTION KEY?
    linux_code_to_key_enum.emplace(KEY_MENU, EKey::MENU_KEY);

    linux_code_to_key_enum.emplace(KEY_LEFTSHIFT, EKey::LEFT_SHIFT);
    linux_code_to_key_enum.emplace(KEY_RIGHTSHIFT, EKey::RIGHT_SHIFT);
    linux_code_to_key_enum.emplace(KEY_LEFTCTRL, EKey::LEFT_CONTROL);
    linux_code_to_key_enum.emplace(KEY_RIGHTCTRL, EKey::RIGHT_CONTROL);
    linux_code_to_key_enum.emplace(KEY_LEFTALT, EKey::LEFT_ALT);
    linux_code_to_key_enum.emplace(KEY_RIGHTALT, EKey::RIGHT_ALT);
    // SUPER KEYS?

    // Mouse buttons
    linux_code_to_key_enum.emplace(BTN_LEFT, EKey::LEFT_MOUSE_BUTTON);
    linux_code_to_key_enum.emplace(BTN_RIGHT, EKey::RIGHT_MOUSE_BUTTON);
    linux_code_to_key_enum.emplace(BTN_MIDDLE, EKey::MIDDLE_MOUSE_BUTTON);
}

LinuxInputAdapter::~LinuxInputAdapter() {
    if (fd >= 0)
        close(fd);
}

void LinuxInputAdapter::poll_events() {
    GlobalLogSection _("poll_events");
    struct input_event ev;

    // Nonblocking
    // WARN: there is a period of time before the keyboard will report that it's being held down. Ie first you will
    // receive an event that the key was pressed, and then some iterations of the outer loop encompassing this logic
    // will occur, and then you'll receive an event that the key is held ie ev.value is 2, because you these updates are
    // not instant it means that the pressed signal will not be getting updated on every tick, therefore we need to do
    // something
    ssize_t n = read(fd, &ev, sizeof(ev));

    while (n == sizeof(ev)) {
        if (ev.type == EV_KEY) {
            auto it = linux_code_to_key_enum.find(ev.code);
            if (it != linux_code_to_key_enum.end()) {
                Key &active_key = *(input_state.key_enum_to_object.at(it->second));
                bool is_pressed = (ev.value != 0); // 0 = release, 1 = press, 2 = repeat
                global_logger->debug("key detect: {} with value: {}", active_key.string_repr, ev.value);
                active_key.pressed_signal.set(is_pressed);
                global_logger->debug("pressed signal: {}", active_key.pressed_signal.to_string());
            }
        } else if (ev.type == EV_REL) {
            // For relative mouse movement
            if (ev.code == REL_X) {
                input_state.prev_mouse_position_x = input_state.mouse_position_x;
                input_state.mouse_position_x += ev.value;
                input_state.mouse_delta_x = ev.value;
            } else if (ev.code == REL_Y) {
                input_state.prev_mouse_position_y = input_state.mouse_position_y;
                input_state.mouse_position_y += ev.value;
                input_state.mouse_delta_y = ev.value;
            } else if (ev.code == REL_WHEEL) {
                // Optional: handle scroll
                // input_state.scroll_delta = ev.value;
            }
        }

        n = read(fd, &ev, sizeof(ev));
    }

    if (n < 0 && errno != EAGAIN) {
        std::cerr << "Error reading from input device\n";
    }
}
