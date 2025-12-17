#include "input/input_state/input_state.hpp"
#include "input/linux_input_adapter/linux_input_adapter.hpp"

#include "select_linux_device.hpp"

#include "utility/temporal_binary_switch/temporal_binary_switch.hpp"
#include "utility/fixed_frequency_loop/fixed_frequency_loop.hpp"
#include "utility/collection_utils/collection_utils.hpp"
#include "utility/text_utils/text_utils.hpp"
#include "utility/logger/logger.hpp"
#include "utility/timer/timer.hpp"

#include <chrono>
#include <iostream>
#include <ratio>

InputState input_state;
InputState virtual_input_state;

/**
 * @brief a class that process keys from the operating system and optionally forwards them to a virtul keyboard device,
 * allows you to determine which keystrokes pass through (forwarding) or do not, and additionally allow you to run
 * intermediate logic to generate other keystrokes
 */
class KeyInterceptor {
  public:
    std::function<void()> logic;

    KeyInterceptor(std::function<void()> logic)
        : device_name(interactively_select_linux_device_name()),
          virtual_keyboard_file_descriptor(create_virtual_keyboard_device()),
          linux_input_adapter(input_state, device_name, true), logic(logic) {
        key_enum_to_linux_code = collection_utils::invert(linux_input_adapter.linux_code_to_key_enum);

        // NOTE: the reason why this is here is because for some reason just sending over KEY_ENTER to the virtual
        // keyboard doesn't work properly, and this fixes it and I don't exactly know why.
        key_enum_to_linux_code.at(EKey::ENTER) = KEY_KPENTER;
    }

    std::string device_name;
    int virtual_keyboard_file_descriptor;
    LinuxInputAdapter linux_input_adapter;

    std::unordered_map<EKey, int> key_enum_to_linux_code;

    std::vector<EKey> keys_to_ignore_this_update;

    bool logging_enabled = false;

    // will make the key occur on the virtual keyboard and also go through the virtual input state for analysis
    void send_key_to_virtual_keyboard(EKey key_enum, int press_value) {

        bool pressed = press_value > 0;

        Key &active_key = *(virtual_input_state.key_enum_to_object.at(key_enum));
        if (active_key.requires_modifer_to_be_typed) {

            Key &active_key_unshifted =
                *(virtual_input_state.key_enum_to_object.at(active_key.key_enum_of_unshifted_version));
            Key &shift_key = *(virtual_input_state.key_enum_to_object.at(EKey::LEFT_SHIFT));
            // SHIFT-KEY PRESS
            if (pressed) {
                send_key(virtual_keyboard_file_descriptor, key_enum_to_linux_code.at(EKey::LEFT_SHIFT), press_value);

                send_key(virtual_keyboard_file_descriptor,
                         key_enum_to_linux_code.at(active_key.key_enum_of_unshifted_version), press_value);

            } else { // KEY-SHIFT RELEASE
                send_key(virtual_keyboard_file_descriptor,
                         key_enum_to_linux_code.at(active_key.key_enum_of_unshifted_version), press_value);
                send_key(virtual_keyboard_file_descriptor, key_enum_to_linux_code.at(EKey::LEFT_SHIFT), press_value);
            }

            active_key_unshifted.pressed_signal.set(pressed);
            shift_key.pressed_signal.set(pressed);
        } else {
            send_key(virtual_keyboard_file_descriptor, key_enum_to_linux_code.at(key_enum), press_value);
            active_key.pressed_signal.set(pressed);
        }
    }

    void update() {
        GlobalLogSection _("update", logging_enabled);

        linux_input_adapter.poll_events();
        // global_logger->debug("space just pressed: {}", input_state.is_just_pressed(EKey::SPACE));

        global_logger->info(input_state.get_visual_keyboard_state());

        logic();

        // key forwarding required as we grab exclusive control of the keyboard.
        for (const auto &key_enum : input_state.get_just_pressed_keys()) {
            bool key_should_be_ignored = collection_utils::contains(keys_to_ignore_this_update, key_enum);
            if (key_should_be_ignored)
                continue;

            send_key_to_virtual_keyboard(key_enum, LinuxInputAdapter::press_value);
        }

        for (const auto &key_enum : input_state.get_held_keys()) {
            bool key_should_be_ignored = collection_utils::contains(keys_to_ignore_this_update, key_enum);
            if (key_should_be_ignored)
                continue;

            send_key_to_virtual_keyboard(key_enum, LinuxInputAdapter::repeat_value);
        }

        for (const auto &key_enum : input_state.get_just_released_keys()) {
            bool key_should_be_ignored = collection_utils::contains(keys_to_ignore_this_update, key_enum);
            if (key_should_be_ignored)
                continue;

            send_key_to_virtual_keyboard(key_enum, LinuxInputAdapter::release_value);
        }

        keys_to_ignore_this_update.clear();
        input_state.process();
        virtual_input_state.process();
    }
};

struct SimultaneousKeypresses {
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    KeyInterceptor &key_interceptor;

    SimultaneousKeypresses(std::chrono::milliseconds t, KeyInterceptor &key_interceptor)
        : threshold(t), key_interceptor(key_interceptor) {}

    struct Combo {
        EKey key1;
        EKey key2;
        std::function<void()> callback;
    };

    std::chrono::milliseconds threshold;
    std::unordered_map<EKey, TimePoint> key_pressed_times;
    std::vector<Combo> combos;

    std::chrono::milliseconds last_duration;

    void register_combo(EKey key1, EKey key2, std::function<void()> callback) {
        combos.push_back({key1, key2, callback});
    }

    // call this every update
    void process() {
        // record timestamps for keys that were just pressed
        for (auto &combo : combos) {
            for (EKey key : {combo.key1, combo.key2}) {
                if (input_state.get_current_state(key) == TemporalBinarySwitch::State::just_switched_on) {
                    key_pressed_times[key] = Clock::now();
                }
            }
        }

        // check all combos
        for (auto &combo : combos) {
            if (input_state.is_pressed(combo.key1) && input_state.is_pressed(combo.key2)) {
                auto it1 = key_pressed_times.find(combo.key1);
                auto it2 = key_pressed_times.find(combo.key2);

                if (it1 != key_pressed_times.end() && it2 != key_pressed_times.end()) {
                    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(it2->second - it1->second);
                    auto abs_duration = std::chrono::milliseconds(std::abs(duration.count()));
                    last_duration = abs_duration;

                    if (abs_duration.count() >= 0 && abs_duration < threshold) {
                        combo.callback();

                        // Optionally ignore keys for this update
                        key_interceptor.keys_to_ignore_this_update.push_back(combo.key1);
                        key_interceptor.keys_to_ignore_this_update.push_back(combo.key2);
                    }
                }
            }
        }
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
 * TODO: another feature that I want to add is the ability to do punch through toggling, what this means is that you hit
 * space space to enter the mode, and while this is active there should be a way to temporarliy just toggle the mode so
 * maybe akey where you press it down nd it will temporarily disable the mapping mode.
 *
 * the point is that you can type things like this_thing_here, without having to spam space so much
 *
 */
// TODO: this needs to be renamed, and then the one with these specific mappings is the homebody keyboard mappings
class ChordSystem {

  public:
    struct SingleKeyMap {
        EKey input_key;
        EKey output_key;
        bool active = false;
        TemporalBinarySwitch active_tbs;
    };

    enum class MapName {
        homesick,
        number_pulldown,
        programming,
        shift_lock,
        vim_arrows,
    };

    struct KeyMap {
        MapName map_name;
        std::vector<SingleKeyMap> key_mappings;

        void add_key_mapping(EKey input_key, EKey output_key) { key_mappings.emplace_back(input_key, output_key); }
    };

    KeyMap key_map;

    std::unordered_map<MapName, KeyMap> map_name_to_key_map = {
        {MapName::homesick, KeyMap()},   {MapName::number_pulldown, KeyMap()}, {MapName::programming, KeyMap()},
        {MapName::shift_lock, KeyMap()}, {MapName::vim_arrows, KeyMap()},
    };

    MapName current_mapping = MapName::homesick;

    void add_chord_mapping(EKey input_key, EKey output_key) {
        key_map.key_mappings.emplace_back(input_key, output_key);
    }

    SimultaneousKeypresses simultaneous_keypresses{std::chrono::milliseconds(35), key_interceptor};
    ChordSystem() : key_interceptor([this]() { per_iteration_logic(); }) {
        // homesick

        auto &homesick_mapping = map_name_to_key_map.at(MapName::homesick);

        homesick_mapping.add_key_mapping(EKey::q, EKey::TAB);
        homesick_mapping.add_key_mapping(EKey::w, EKey::GRAVE_ACCENT);

        homesick_mapping.add_key_mapping(EKey::a, EKey::ESCAPE);

        homesick_mapping.add_key_mapping(EKey::z, EKey::LEFT_SHIFT);
        homesick_mapping.add_key_mapping(EKey::x, EKey::LEFT_CONTROL);
        homesick_mapping.add_key_mapping(EKey::c, EKey::LEFT_SUPER);
        homesick_mapping.add_key_mapping(EKey::v, EKey::LEFT_ALT);

        homesick_mapping.add_key_mapping(EKey::u, EKey::BACKSPACE);
        homesick_mapping.add_key_mapping(EKey::i, EKey::LEFT_SQUARE_BRACKET);
        homesick_mapping.add_key_mapping(EKey::o, EKey::RIGHT_SQUARE_BRACKET);
        homesick_mapping.add_key_mapping(EKey::p, EKey::BACKSLASH);

        homesick_mapping.add_key_mapping(EKey::l, EKey::SINGLE_QUOTE);
        homesick_mapping.add_key_mapping(EKey::SEMICOLON, EKey::ENTER);

        // add_chord_mapping(EKey::n, EKey::FUNCTION_KEY);
        // homesick_mapping.add_key_mapping(EKey::m, EKey::MENU_KEY);
        homesick_mapping.add_key_mapping(EKey::COMMA, EKey::RIGHT_ALT);
        homesick_mapping.add_key_mapping(EKey::PERIOD, EKey::RIGHT_CONTROL);
        homesick_mapping.add_key_mapping(EKey::SLASH, EKey::RIGHT_SHIFT);

        auto &number_pulldown_mapping = map_name_to_key_map.at(MapName::number_pulldown);
        number_pulldown_mapping.add_key_mapping(EKey::a, EKey::ONE);
        number_pulldown_mapping.add_key_mapping(EKey::s, EKey::TWO);
        number_pulldown_mapping.add_key_mapping(EKey::d, EKey::THREE);
        number_pulldown_mapping.add_key_mapping(EKey::f, EKey::FOUR);
        number_pulldown_mapping.add_key_mapping(EKey::g, EKey::FIVE);
        number_pulldown_mapping.add_key_mapping(EKey::h, EKey::SIX);
        number_pulldown_mapping.add_key_mapping(EKey::j, EKey::SEVEN);
        number_pulldown_mapping.add_key_mapping(EKey::k, EKey::EIGHT);
        number_pulldown_mapping.add_key_mapping(EKey::l, EKey::NINE);
        number_pulldown_mapping.add_key_mapping(EKey::SEMICOLON, EKey::ZERO);

        // TODO: there's a problem right now when you try and send something like exclamation point because that's not a
        // valid key in the context of the virtual keyboard instead we need to do a shift 1 or something of that form,
        // this also has to be done when the mode is over and we're clearing stuff out.

        number_pulldown_mapping.add_key_mapping(EKey::q, EKey::EXCLAMATION_POINT);
        number_pulldown_mapping.add_key_mapping(EKey::w, EKey::AT_SIGN);
        number_pulldown_mapping.add_key_mapping(EKey::e, EKey::NUMBER_SIGN);
        number_pulldown_mapping.add_key_mapping(EKey::r, EKey::DOLLAR_SIGN);
        number_pulldown_mapping.add_key_mapping(EKey::t, EKey::PERCENT_SIGN);
        number_pulldown_mapping.add_key_mapping(EKey::y, EKey::CARET);
        number_pulldown_mapping.add_key_mapping(EKey::u, EKey::AMPERSAND);
        number_pulldown_mapping.add_key_mapping(EKey::i, EKey::ASTERISK);
        number_pulldown_mapping.add_key_mapping(EKey::o, EKey::LEFT_PARENTHESIS);
        number_pulldown_mapping.add_key_mapping(EKey::p, EKey::RIGHT_PARENTHESIS);

        auto &programming_mapping = map_name_to_key_map.at(MapName::programming);

        programming_mapping.add_key_mapping(EKey::f, EKey::LEFT_PARENTHESIS);  // (
        programming_mapping.add_key_mapping(EKey::j, EKey::RIGHT_PARENTHESIS); // )

        programming_mapping.add_key_mapping(EKey::d, EKey::LEFT_SQUARE_BRACKET);  // [
        programming_mapping.add_key_mapping(EKey::k, EKey::RIGHT_SQUARE_BRACKET); // ]

        programming_mapping.add_key_mapping(EKey::s, EKey::LESS_THAN);    // <
        programming_mapping.add_key_mapping(EKey::l, EKey::GREATER_THAN); // >

        programming_mapping.add_key_mapping(EKey::a, EKey::LEFT_CURLY_BRACKET);          // {
        programming_mapping.add_key_mapping(EKey::SEMICOLON, EKey::RIGHT_CURLY_BRACKET); // }

        programming_mapping.add_key_mapping(EKey::q, EKey::AMPERSAND);
        programming_mapping.add_key_mapping(EKey::w, EKey::UNDERSCORE);
        programming_mapping.add_key_mapping(EKey::e, EKey::EQUAL);

        programming_mapping.add_key_mapping(EKey::u, EKey::PLUS);
        programming_mapping.add_key_mapping(EKey::i, EKey::MINUS);
        programming_mapping.add_key_mapping(EKey::o, EKey::ASTERISK);
        programming_mapping.add_key_mapping(EKey::p, EKey::SLASH);

        programming_mapping.add_key_mapping(EKey::x, EKey::COLON);

        auto &vim_arrows = map_name_to_key_map.at(MapName::vim_arrows);
        vim_arrows.add_key_mapping(EKey::h, EKey::LEFT);
        vim_arrows.add_key_mapping(EKey::l, EKey::RIGHT);
        vim_arrows.add_key_mapping(EKey::j, EKey::DOWN);
        vim_arrows.add_key_mapping(EKey::k, EKey::UP);

        auto &shift_lock = map_name_to_key_map.at(MapName::shift_lock);

        for (auto &key : input_state.all_keys) {
            if (key.shiftable) {
                shift_lock.add_key_mapping(key.key_enum, key.key_enum_of_shifted_version);
            }
        }

        if (not space_tap_mapping_activation_mode) {

            simultaneous_keypresses.register_combo(EKey::SPACE, EKey::f, [&]() {
                mapping_mode_active = true;
                current_mapping = MapName::homesick;
                key_used_to_start_mapping = EKey::f;
            });
            simultaneous_keypresses.register_combo(EKey::SPACE, EKey::j, [&]() {
                mapping_mode_active = true;
                current_mapping = MapName::homesick;
                key_used_to_start_mapping = EKey::j;
            });

            simultaneous_keypresses.register_combo(EKey::SPACE, EKey::d, [&]() {
                mapping_mode_active = true;
                current_mapping = MapName::number_pulldown;
                key_used_to_start_mapping = EKey::d;
            });
            simultaneous_keypresses.register_combo(EKey::SPACE, EKey::k, [&]() {
                mapping_mode_active = true;
                current_mapping = MapName::number_pulldown;
                key_used_to_start_mapping = EKey::k;
            });

            simultaneous_keypresses.register_combo(EKey::SPACE, EKey::s, [&]() {
                mapping_mode_active = true;
                current_mapping = MapName::programming;
                key_used_to_start_mapping = EKey::s;
            });
            simultaneous_keypresses.register_combo(EKey::SPACE, EKey::l, [&]() {
                mapping_mode_active = true;
                current_mapping = MapName::programming;
                key_used_to_start_mapping = EKey::l;
            });

            simultaneous_keypresses.register_combo(EKey::SPACE, EKey::v, [&]() {
                mapping_mode_active = true;
                current_mapping = MapName::vim_arrows;
                key_used_to_start_mapping = EKey::v;
            });

            simultaneous_keypresses.register_combo(EKey::SPACE, EKey::z, [&]() {
                mapping_mode_active = true;
                current_mapping = MapName::shift_lock;
                key_used_to_start_mapping = EKey::z;
            });
            simultaneous_keypresses.register_combo(EKey::SPACE, EKey::SLASH, [&]() {
                mapping_mode_active = true;
                current_mapping = MapName::shift_lock;
                key_used_to_start_mapping = EKey::SLASH;
            });
        }
    }

    KeyInterceptor key_interceptor;

    bool timer_started_at_least_once = false;
    Timer mapping_mode_activation_timer{0.2};

    bool mapping_mode_active = false;
    EKey key_used_to_start_mapping;

    bool possibly_going_into_mapping_mode = false;

    bool logging_enabled = false;

    bool space_tap_mapping_activation_mode = false;

    std::chrono::steady_clock::time_point space_pressed_time;
    std::chrono::steady_clock::time_point f_pressed_time;

    void per_iteration_logic() {

        GlobalLogSection _("tick", logging_enabled);

        if (space_tap_mapping_activation_mode) {
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
            if (not mapping_mode_active and mapping_mode_activation_timer.time_up() and
                possibly_going_into_mapping_mode) {
                key_interceptor.send_key_to_virtual_keyboard(EKey::SPACE, LinuxInputAdapter::press_value);
                key_interceptor.send_key_to_virtual_keyboard(EKey::SPACE, LinuxInputAdapter::release_value);
                // you took too long so we're longer trying to
                possibly_going_into_mapping_mode = false;
            }

            // TODO: this doesn't work because it needs to not be reset per iteration because it doesn't have any effect
            // because it cannot effect more than one iteration and chord keys come through on different iterations
            int num_consecutive_keys_to_modify = 1;

            // chord ends here
            if (input_state.is_just_released(EKey::SPACE) and mapping_mode_active) {
                mapping_mode_active = false;
                // turn off all possible output keys from the chord mapping so they don't repeat if they were held down
                // when space was released.
                for (auto &km : map_name_to_key_map.at(MapName::homesick).key_mappings) {

                    // leave actively pressed keys on.
                    if (input_state.is_pressed(km.input_key))
                        continue;

                    // release all other keys
                    km.active = false;

                    global_logger->info("about to turn off key: {}",
                                        input_state.key_enum_to_object.at(km.input_key)->string_repr);

                    key_interceptor.send_key_to_virtual_keyboard(km.output_key, LinuxInputAdapter::release_value);
                }
            }

            auto just_pressed_keys = input_state.get_just_pressed_keys();
            bool used_non_space_key =
                not collection_utils::contains(just_pressed_keys, EKey::SPACE) and not just_pressed_keys.empty();
            // when you type somethign like  "<space>a" we immediately emit the space key before this key so that you
            // can type at full speed.
            if (not mapping_mode_active and used_non_space_key and possibly_going_into_mapping_mode) {
                key_interceptor.send_key_to_virtual_keyboard(EKey::SPACE, LinuxInputAdapter::press_value);
                key_interceptor.send_key_to_virtual_keyboard(EKey::SPACE, LinuxInputAdapter::release_value);
                possibly_going_into_mapping_mode = false;
            }

        } else {

            simultaneous_keypresses.process();

            // when you do space-f and then let go of f we still want to ignore space
            if (mapping_mode_active) {
                if (input_state.is_pressed(EKey::SPACE)) {
                    key_interceptor.keys_to_ignore_this_update.push_back(EKey::SPACE);
                }
            }

            if (input_state.is_just_released(EKey::SPACE) and mapping_mode_active) {
                mapping_mode_active = false;
                // turn off all possible output keys from the chord mapping so they don't repeat if they were held down
                // when space was released.
                for (auto &km : map_name_to_key_map.at(current_mapping).key_mappings) {

                    // leave actively pressed keys on.
                    if (input_state.is_pressed(km.input_key))
                        continue;

                    // release all other keys
                    km.active = false;

                    global_logger->info("about to turn off key: {}",
                                        input_state.key_enum_to_object.at(km.input_key)->string_repr);

                    // std::cout << "about to turn off key: {}"
                    //           << input_state.key_enum_to_object.at(km.input_key)->string_repr << std::endl;

                    key_interceptor.send_key_to_virtual_keyboard(km.output_key, LinuxInputAdapter::release_value);
                }
            }
        }

        // TODO: generalize with more stuff later
        if (mapping_mode_active) {
            for (auto &cm : map_name_to_key_map.at(current_mapping).key_mappings) {
                cm.active = true;
                // transform_input_key_to_output_key(cm.input_key, cm.output_key);
            }
        }

        // this does the mappings
        for (auto &cm : map_name_to_key_map.at(current_mapping).key_mappings) {

            // if you do space-f then don't run the f function
            if (not cm.active or cm.input_key == key_used_to_start_mapping)
                continue;

            int value;
            switch (input_state.get_current_state(cm.input_key)) {
            case TemporalBinarySwitch::State::just_switched_on:
                possibly_going_into_mapping_mode = false;
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

            key_interceptor.send_key_to_virtual_keyboard(cm.output_key, value);
            // transform_input_key_to_output_key(cm.input_key, cm.output_key);
        }
    }
};

class LinuxTerminalCanvas {
  public:
    LinuxTerminalCanvas() {
        clear();
        hide_cursor();
    }

    ~LinuxTerminalCanvas() {
        clear();
        show_cursor();
        flush();
    }

    void clear() const { std::cout << "\033[2J\033[H"; }

    // now takes x, y in Cartesian style
    void move_cursor_xy(int x, int y) const { std::cout << "\033[" << y << ";" << x << "H"; }

    void render_text_block(int x, int y, const std::string &text) const {
        std::istringstream stream(text);
        std::string line;
        int row = y;
        while (std::getline(stream, line)) {
            move_cursor_xy(x, row);
            std::cout << "\033[K"; // clear to end of line
            std::cout << line;
            ++row;
        }
    }

    void flush() const { std::cout.flush(); }
    void hide_cursor() const { std::cout << "\033[?25l"; }
    void show_cursor() const { std::cout << "\033[?25h"; }

    // -------------------------
    // Drawing Primitives
    // -------------------------

    void draw_line(int x0, int y0, int x1, int y1, char ch = '*') const {
        int dx = std::abs(x1 - x0);
        int dy = -std::abs(y1 - y0);
        int sx = (x0 < x1) ? 1 : -1;
        int sy = (y0 < y1) ? 1 : -1;
        int err = dx + dy;

        while (true) {
            move_cursor_xy(x0, y0);
            std::cout << ch;
            if (x0 == x1 && y0 == y1)
                break;
            int e2 = 2 * err;
            if (e2 >= dy) {
                err += dy;
                x0 += sx;
            }
            if (e2 <= dx) {
                err += dx;
                y0 += sy;
            }
        }
    }

    void draw_rect(int x, int y, int width, int height, char ch = '#') const {
        for (int i = 0; i < width; ++i) {
            move_cursor_xy(x + i, y);
            std::cout << ch;
            move_cursor_xy(x + i, y + height - 1);
            std::cout << ch;
        }
        for (int i = 0; i < height; ++i) {
            move_cursor_xy(x, y + i);
            std::cout << ch;
            move_cursor_xy(x + width - 1, y + i);
            std::cout << ch;
        }
    }

    void draw_circle(int cx, int cy, int r, char ch = 'o') const {
        int x = r, y = 0;
        int err = 0;
        while (x >= y) {
            plot_circle_points(cx, cy, x, y, ch);
            ++y;
            if (err <= 0)
                err += 2 * y + 1;
            if (err > 0) {
                --x;
                err -= 2 * x + 1;
            }
        }
    }

    void draw_arrow(int x0, int y0, int x1, int y1, char ch = '*') const {
        draw_line(x0, y0, x1, y1, ch); // draw the main line

        int arrow_x = x1;
        int arrow_y = y1;

        if (x0 == x1) { // vertical
            move_cursor_xy(arrow_x, arrow_y);
            std::cout << 'v';
        } else if (y0 == y1) { // horizontal
            move_cursor_xy(arrow_x, arrow_y);
            std::cout << '>';
        } else { // diagonal
            move_cursor_xy(arrow_x, arrow_y);
            std::cout << '>'; // placeholder
        }
    }

  private:
    void plot_circle_points(int cx, int cy, int x, int y, char ch) const {
        move_cursor_xy(cx + x, cy + y);
        std::cout << ch;
        move_cursor_xy(cx - x, cy + y);
        std::cout << ch;
        move_cursor_xy(cx + x, cy - y);
        std::cout << ch;
        move_cursor_xy(cx - x, cy - y);
        std::cout << ch;
        move_cursor_xy(cx + y, cy + x);
        std::cout << ch;
        move_cursor_xy(cx - y, cy + x);
        std::cout << ch;
        move_cursor_xy(cx + y, cy - x);
        std::cout << ch;
        move_cursor_xy(cx - y, cy - x);
        std::cout << ch;
    }
};

int main() {

    global_logger->remove_all_sinks();
    // global_logger->add_file_sink("logs/logs.txt");

    FixedFrequencyLoop ffl;
    ChordSystem chord_system;

    auto term = []() { return false; };
    ffl.logging_enabled = false;

    LinuxTerminalCanvas canvas;
    canvas.hide_cursor();

    ffl.start(
        [&](double dt) {
            chord_system.key_interceptor.update();
            canvas.render_text_block(0, 0, chord_system.mapping_mode_active ? "mapping" : "not mapping");
            canvas.render_text_block(0, 20, std::to_string(chord_system.simultaneous_keypresses.last_duration.count()));
            canvas.render_text_block(10, 4, input_state.get_visual_keyboard_state());
            canvas.render_text_block(100, 4, virtual_input_state.get_visual_keyboard_state());
            canvas.draw_arrow(71, 8, 99, 8);
            // canvas.render_text_block(80, 25, "mapped");
            canvas.flush();
        },
        term);

    canvas.clear();
}
