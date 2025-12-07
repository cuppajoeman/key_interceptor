#include "input/linux_input_adapter/linux_input_adapter.hpp"

#include "utility/fixed_frequency_loop/fixed_frequency_loop.hpp"

#include "select_linux_device.hpp"

#include "utility/collection_utils/collection_utils.hpp"
#include "utility/logger/logger.hpp"
#include "utility/temporal_binary_switch/temporal_binary_switch.hpp"
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

/**
 *
 * Motivation:
 *
 * vim taught us that we can avoid reaching for our mouse. Once mastered, we should continue this pattern in memory of
 * vim. Moreover if you are a programmer or someone who usese the computer for a lot of time then you should be
 * investing in the ability to continue doing this. If one day you got a repetitive strain injury and were no longer
 * able to type anymore it would be a sad day.
 *
 * Ok so applying the same reasoning that moving your hand is bad, then we just have to see how we currently move our
 * hand. Most vimmers already rebind escape to caps lock because they understand this concept, but thats usually where
 * it ends. Let's not stop there, instead we should realize that anytime we have to move away from the homerow this is
 * usually a hand movement, the worst offenders are those where we have to slightly adjust the hand to reach our pinky
 * out to grab a key, such as caps lock, delete, tab, enter etc.
 *
 * Our solution to this problem is to move this keys inward so that no hand adjustments have to made. This is a sort of
 * mapping layer, but how do we activate this mapping layer?
 *
 * Clearly we want to adhere to the principle of not moving your hand and so we don't have the option of enabling this
 * mapping mode with traditional keys like ctrl/alt/super etc so instead we have to try something new.
 *
 * Space is the biggest key on the keyboard and pressed by the strongest finger on your hand, so leveraging this key
 * would be good, simply mapping space to activate the mapping mode would be bad, because we use space for other thing
 * as well.
 *
 * One area that we can take advantage of is timing. If we get the user to press space in fast succession we'll enable
 * the mapping layer, if the second space is not emitted within the time frame then we can the mode is not activated,
 * and if you press any key other than space after hitting space, then the mod will also not be activated. This keeps
 * the regular behavior while adding the mapping layer if you know the timing and go fast.
 *
 * TLDR:
 *
 * space tap, space hold in fast succession activates the mapping mode, in this mode, certain keys are remapped to other
 * keys
 *
 * when space is released the mapping mode turns off and keys go back to their regular function unless the following is
 * true
 *
 * if a key is remapped by the mode and it is continually held down even when space is released then it continues to
 * repeat the mapped key.
 *
 * the purpose of this extra functionality is to allow you to combine mapped and unmapped keys, so for example if / is
 * mapped to right shift in the mapped state and a is mapped to escape in the mapped state, then it's impossible to type
 * shift-a in the mapped state because a is already being mapped to something. In order to allow for such situation we
 * need to be able to conditionally keep mapped keys active, and to do this we use above method. In this way we can
 * activate the mapping mode, and then hold down / to keep shift active
 *
 */
// TODO: this needs to be renamed, and then the one with these specific mappings is the homebody keyboard mappings
class ChordSystem {

  public:
    ChordSystem() : key_interceptor([this]() { per_iteration_logic(); }) {
        // homesick

        add_chord_mapping(EKey::q, EKey::TAB);
        add_chord_mapping(EKey::w, EKey::GRAVE_ACCENT);

        add_chord_mapping(EKey::a, EKey::ESCAPE);

        add_chord_mapping(EKey::z, EKey::LEFT_SHIFT);
        add_chord_mapping(EKey::x, EKey::LEFT_CONTROL);
        add_chord_mapping(EKey::c, EKey::LEFT_SUPER);
        add_chord_mapping(EKey::v, EKey::LEFT_ALT);

        add_chord_mapping(EKey::u, EKey::BACKSPACE);
        add_chord_mapping(EKey::i, EKey::LEFT_SQUARE_BRACKET);
        add_chord_mapping(EKey::o, EKey::RIGHT_SQUARE_BRACKET);
        add_chord_mapping(EKey::p, EKey::BACKSLASH);

        add_chord_mapping(EKey::l, EKey::SINGLE_QUOTE);
        add_chord_mapping(EKey::SEMICOLON, EKey::ENTER);

        // add_chord_mapping(EKey::n, EKey::FUNCTION_KEY);
        add_chord_mapping(EKey::m, EKey::MENU_KEY);
        add_chord_mapping(EKey::COMMA, EKey::RIGHT_ALT);
        add_chord_mapping(EKey::PERIOD, EKey::RIGHT_CONTROL);
        add_chord_mapping(EKey::SLASH, EKey::RIGHT_SHIFT);
    }

    KeyInterceptor key_interceptor;

    bool timer_started_at_least_once = false;
    Timer mapping_mode_activation_timer{0.2};

    bool mapping_mode_active = false;

    bool possibly_going_into_mapping_mode = false;

    struct ChordMapping {
        EKey input_key;
        EKey output_key;
        bool active = false;
        TemporalBinarySwitch active_tbs;
    };

    std::vector<ChordMapping> chord_mappings;

    void add_chord_mapping(EKey input_key, EKey output_key) { chord_mappings.emplace_back(input_key, output_key); }

    void per_iteration_logic() {

        GlobalLogSection _("tick");

        global_logger->debug("space signal state: {}",
                             input_state.key_enum_to_object.at(EKey::SPACE)->pressed_signal.to_string());

        if (input_state.is_just_pressed(EKey::SPACE)) {
            if (mapping_mode_activation_timer.time_up() or not timer_started_at_least_once) {
                mapping_mode_active = false;
                mapping_mode_activation_timer.start();
                possibly_going_into_mapping_mode = true;
                timer_started_at_least_once = true;
            } else { // the timer was not up
                mapping_mode_active = true;
                global_logger->debug("chord started");
            }
            // If you manually press space, it gets ignored
            key_interceptor.keys_to_ignore_this_update.push_back(EKey::SPACE);
        }

        // only if the time for the chord to start elapsed and you had pressed space we do a slightly delayed space
        // emission
        if (not mapping_mode_active and mapping_mode_activation_timer.time_up() and possibly_going_into_mapping_mode) {
            send_key(key_interceptor.ufd, key_interceptor.key_enum_to_linux_code.at(EKey::SPACE),
                     LinuxInputAdapter::press_value);
            send_key(key_interceptor.ufd, key_interceptor.key_enum_to_linux_code.at(EKey::SPACE),
                     LinuxInputAdapter::release_value);
            // you took too long so we're longer trying to
            possibly_going_into_mapping_mode = false;
        }

        // TODO: this doesn't work because it needs to not be reset per iteration because it doesn't have any effect
        // because it cannot effect more than one iteration and chord keys come through on different iterations
        int num_consecutive_keys_to_modify = 1;

        auto transform_input_key_to_output_key = [&](EKey input_key, EKey output_key) {
            // NOTE: these if statements are mutually exclusive

            if (num_consecutive_keys_to_modify == 0)
                return;

            if (input_state.is_just_pressed(input_key)) {
                send_key(key_interceptor.ufd, key_interceptor.key_enum_to_linux_code.at(output_key),
                         LinuxInputAdapter::press_value);
                possibly_going_into_mapping_mode = false;
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

        // chord ends here
        if (input_state.is_just_released(EKey::SPACE) and mapping_mode_active) {
            mapping_mode_active = false;
            // turn off all possible output keys from the chord mapping so they don't repeat if they were held down
            // when space was released.
            for (auto &cm : chord_mappings) {
                // leave actively pressed keys on.
                if (input_state.is_pressed(cm.input_key))
                    continue;

                // release all other keys
                cm.active = false;

                global_logger->info("about to turn off key: {}",
                                    input_state.key_enum_to_object.at(cm.input_key)->string_repr);

                send_key(key_interceptor.ufd, key_interceptor.key_enum_to_linux_code.at(cm.output_key),
                         LinuxInputAdapter::release_value);
            }
        }

        auto just_pressed_keys = input_state.get_just_pressed_keys();
        bool used_non_space_key =
            not collection_utils::contains(just_pressed_keys, EKey::SPACE) and not just_pressed_keys.empty();
        // when you type somethign like  "<space>a" we immediately emit the space key before this key so that you can
        // type at full speed.
        if (not mapping_mode_active and used_non_space_key and possibly_going_into_mapping_mode) {
            send_key(key_interceptor.ufd, key_interceptor.key_enum_to_linux_code.at(EKey::SPACE),
                     LinuxInputAdapter::press_value);
            send_key(key_interceptor.ufd, key_interceptor.key_enum_to_linux_code.at(EKey::SPACE),
                     LinuxInputAdapter::release_value);
            possibly_going_into_mapping_mode = false;
        }

        // TODO: generalize with more stuff later
        if (mapping_mode_active) {
            for (auto &cm : chord_mappings) {
                cm.active = true;
                // transform_input_key_to_output_key(cm.input_key, cm.output_key);
            }
        }

        // this does the mappings
        for (auto &cm : chord_mappings) {

            if (not cm.active)
                continue;

            int value;
            switch (input_state.get_current_state(cm.input_key)) {
            case TemporalBinarySwitch::State::just_switched_on:
                possibly_going_into_mapping_mode = false; // WTF WHAT IS THAT
                value = LinuxInputAdapter::press_value;
                break;
            case TemporalBinarySwitch::State::sustained_on:
                value = LinuxInputAdapter::repeat_value;
                break;
            case TemporalBinarySwitch::State::just_switched_off:
                value = LinuxInputAdapter::release_value;
                cm.active = false;
                break;
            case TemporalBinarySwitch::State::sustained_off:
                // doesn't need to be modeled by exclusion
                continue; // we don't even send akey in this case
                break;
            }

            global_logger->info("about to turn on key: {}",
                                input_state.key_enum_to_object.at(cm.input_key)->string_repr);

            key_interceptor.keys_to_ignore_this_update.push_back(cm.input_key);
            send_key(key_interceptor.ufd, key_interceptor.key_enum_to_linux_code.at(cm.output_key), value);
            // transform_input_key_to_output_key(cm.input_key, cm.output_key);
        }
    }
};

int main() {

    // global_logger->remove_all_sinks();
    // global_logger->add_file_sink("logs/logs.txt");

    FixedFrequencyLoop ffl;
    ChordSystem chord_system;

    auto term = []() { return false; };
    ffl.logging_enabled = true;

    ffl.start([&](double dt) { chord_system.key_interceptor.update(); }, term);
}
