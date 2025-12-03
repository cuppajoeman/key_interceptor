#include "input/linux_input_adapter/linux_input_adapter.hpp"

#include "utility/fixed_frequency_loop/fixed_frequency_loop.hpp"

#include "select_linux_device.hpp"

#include "utility/collection_utils/collection_utils.hpp"
#include "utility/logger/logger.hpp"
#include "utility/timer/timer.hpp"

#include <iostream>

InputState input_state;

/**
 * @brief a class that process keys from the operating system and optionally forwards them to a virtul keyboard device,
 * allows you to determine which keystrokes pass through (forwarding) or do not, and additionally allow you to run
 * intermediate logic to generate other keystrokes
 */
class KeyInterceptor {
  public:
    std::function<void()> logic;

    KeyInterceptor(std::function<void()> logic)
        : device_name(interactively_select_linux_device_name()), ufd(create_virtual_keyboard_device()),
          linux_input_adapter(input_state, device_name, true), logic(logic) {
        key_enum_to_linux_code = collection_utils::invert(linux_input_adapter.linux_code_to_key_enum);

        // NOTE: the reason why this is here is because for some reason just sending over KEY_ENTER to the virtual
        // keyboard doesn't work properly, and this fixes it and I don't exactly know why.
        key_enum_to_linux_code.at(EKey::ENTER) = KEY_KPENTER;
    }

    std::string device_name;
    int ufd;
    LinuxInputAdapter linux_input_adapter;

    std::unordered_map<EKey, int> key_enum_to_linux_code;

    std::vector<EKey> keys_to_ignore_this_update;

    void update() {
        linux_input_adapter.poll_events();
        // global_logger->debug("space just pressed: {}", input_state.is_just_pressed(EKey::SPACE));

        global_logger->info(input_state.get_visual_keyboard_state());

        logic();

        // key forwarding required as we grab exclusive control of the keyboard.
        for (const auto &key_enum : input_state.get_just_pressed_keys()) {
            bool key_should_be_ignored = collection_utils::contains(keys_to_ignore_this_update, key_enum);
            if (key_should_be_ignored)
                continue;
            std::cout << "key press: " << input_state.key_enum_to_object.at(key_enum)->string_repr << std::endl;
            send_key(ufd, key_enum_to_linux_code.at(key_enum), LinuxInputAdapter::press_value);
        }

        for (const auto &key_enum : input_state.get_held_keys()) {
            bool key_should_be_ignored = collection_utils::contains(keys_to_ignore_this_update, key_enum);
            if (key_should_be_ignored)
                continue;
            std::cout << "key repeat: " << input_state.key_enum_to_object.at(key_enum)->string_repr << std::endl;
            send_key(ufd, key_enum_to_linux_code.at(key_enum), LinuxInputAdapter::repeat_value);
        }

        for (const auto &key_enum : input_state.get_just_released_keys()) {
            bool key_should_be_ignored = collection_utils::contains(keys_to_ignore_this_update, key_enum);
            if (key_should_be_ignored)
                continue;
            std::cout << "key release: " << input_state.key_enum_to_object.at(key_enum)->string_repr << std::endl;
            send_key(ufd, key_enum_to_linux_code.at(key_enum), LinuxInputAdapter::release_value);
        }

        keys_to_ignore_this_update.clear();
        input_state.process();
    }
};

class ChordSystem {

  public:
    ChordSystem() : key_interceptor([this]() { per_iteration_logic(); }) {
        add_chord_mapping(EKey::z, EKey::LEFT_SHIFT);
        add_chord_mapping(EKey::x, EKey::LEFT_CONTROL);
        // add_chord_mapping(EKey::c, EKey::LEFT_CONTROL); super
        add_chord_mapping(EKey::v, EKey::LEFT_ALT);

        add_chord_mapping(EKey::a, EKey::ESCAPE);
        add_chord_mapping(EKey::q, EKey::TAB);

        add_chord_mapping(EKey::p, EKey::BACKSPACE);
        add_chord_mapping(EKey::SEMICOLON, EKey::ENTER);
        add_chord_mapping(EKey::SLASH, EKey::RIGHT_SHIFT);
        add_chord_mapping(EKey::PERIOD, EKey::RIGHT_CONTROL);
    }

    KeyInterceptor key_interceptor;

    bool timer_started_at_least_once = false;
    Timer chord_activation_timer{0.2};

    bool chord_started = false;

    bool first_space_pressed = false;

    struct ChordMapping {
        EKey input_key;
        EKey output_key;
    };

    std::vector<ChordMapping> chord_mappings;

    void add_chord_mapping(EKey input_key, EKey output_key) { chord_mappings.emplace_back(input_key, output_key); }

    void per_iteration_logic() {

        GlobalLogSection _("tick");

        global_logger->debug("space signal state: {}",
                             input_state.key_enum_to_object.at(EKey::SPACE)->pressed_signal.to_string());

        std::cout << "0" << std::endl;
        if (input_state.is_just_pressed(EKey::SPACE)) {
            std::cout << "1" << std::endl;
            std::cout << "time up: " << chord_activation_timer.time_up()
                      << " not tsalo: " << not timer_started_at_least_once << std::endl;
            if (chord_activation_timer.time_up() or not timer_started_at_least_once) {
                chord_started = false;
                std::cout << "2" << std::endl;
                chord_activation_timer.start();
                first_space_pressed = true;
                timer_started_at_least_once = true;
            } else { // the timer was not up
                std::cout << "3" << std::endl;
                chord_started = true;
                global_logger->debug("chord started");
            }
            // If you manually press space, it gets ignored
            key_interceptor.keys_to_ignore_this_update.push_back(EKey::SPACE);
        }

        // only if the time for the chord to start elapsed and you had pressed space we do a slightly delayed space
        // emission
        if (not chord_started and chord_activation_timer.time_up() and first_space_pressed) {
            std::cout << "space generated from delay" << std::endl;
            send_key(key_interceptor.ufd, key_interceptor.key_enum_to_linux_code.at(EKey::SPACE),
                     LinuxInputAdapter::press_value);
            send_key(key_interceptor.ufd, key_interceptor.key_enum_to_linux_code.at(EKey::SPACE),
                     LinuxInputAdapter::release_value);
            first_space_pressed = false;
        }

        // chord ends here
        if (input_state.is_just_released(EKey::SPACE) and chord_started) {
            chord_started = false;
            // turn off all possible output keys from the chord mapping so they don't repeat if they were held down
            // when space was released.
            for (const auto &cm : chord_mappings) {
                send_key(key_interceptor.ufd, key_interceptor.key_enum_to_linux_code.at(cm.output_key),
                         LinuxInputAdapter::release_value);
            }
        }

        auto just_pressed_keys = input_state.get_just_pressed_keys();
        bool used_non_space_key =
            not collection_utils::contains(just_pressed_keys, EKey::SPACE) and not just_pressed_keys.empty();
        if (not chord_started and used_non_space_key and first_space_pressed) {
            std::cout << "space generated from <space><nonspace>" << std::endl;
            send_key(key_interceptor.ufd, key_interceptor.key_enum_to_linux_code.at(EKey::SPACE),
                     LinuxInputAdapter::press_value);
            send_key(key_interceptor.ufd, key_interceptor.key_enum_to_linux_code.at(EKey::SPACE),
                     LinuxInputAdapter::release_value);
            first_space_pressed = false;
        }

        // TODO: this doesn't work because it needs to not be reset per iteration because it doesn't have any effect
        // because it cannot effect more than one iteration and chord keys come through on different iterations
        int num_consecutive_keys_to_modify = 1;

        auto chord_mapping = [&](EKey input_key, EKey output_key) {
            // NOTE: these if statements are mutually exclusive

            if (num_consecutive_keys_to_modify == 0)
                return;

            if (input_state.is_just_pressed(input_key)) {
                send_key(key_interceptor.ufd, key_interceptor.key_enum_to_linux_code.at(output_key),
                         LinuxInputAdapter::press_value);
                first_space_pressed = false;
                num_consecutive_keys_to_modify--;
            }
            if (input_state.is_held(input_key)) {
                send_key(key_interceptor.ufd, key_interceptor.key_enum_to_linux_code.at(output_key),
                         LinuxInputAdapter::repeat_value);

                num_consecutive_keys_to_modify--;
            }
            // TODO: in the future we need to fix the problem where this is not reached because you released space
            // before the chord key
            if (input_state.is_just_released(input_key)) {
                send_key(key_interceptor.ufd, key_interceptor.key_enum_to_linux_code.at(output_key),
                         LinuxInputAdapter::release_value);
                num_consecutive_keys_to_modify--;
            }
            key_interceptor.keys_to_ignore_this_update.push_back(input_key);
        };

        // TODO: generalize with more stuff later
        if (chord_started) {
            for (const auto &cm : chord_mappings) {
                chord_mapping(cm.input_key, cm.output_key);
            }
        }
    }
};

int main() {

    global_logger->remove_all_sinks();
    // global_logger->add_file_sink("logs/logs.txt");

    FixedFrequencyLoop ffl;
    ChordSystem chord_system;

    auto term = []() { return false; };
    ffl.logging_enabled = true;

    ffl.start([&](double dt) { chord_system.key_interceptor.update(); }, term);
}
