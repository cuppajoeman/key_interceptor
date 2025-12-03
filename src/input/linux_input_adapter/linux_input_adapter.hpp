#ifndef LINUX_INPUT_ADAPTER_HPP
#define LINUX_INPUT_ADAPTER_HPP

#include <string>
#include <unordered_map>

#include "sbpt_generated_includes.hpp"

/**
 */
class LinuxInputAdapter {
  public:
    static const int release_value = 0;
    static const int press_value = 1;
    static const int repeat_value = 2;
    std::unordered_map<int, EKey> linux_code_to_key_enum;

    LinuxInputAdapter(InputState &input_state, const std::string &device_path, bool exclusive_control);
    ~LinuxInputAdapter();

    // Poll the device for new events and update InputState
    void poll_events();

  private:
    InputState &input_state;
    int fd = -1;

    // mapping from linux evdev key/mouse codes to your ekey enum
};

#endif // LINUX_INPUT_ADAPTER_HPP
