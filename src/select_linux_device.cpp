#include "select_linux_device.hpp"
#include <stdexcept>

// Helper to read "device name" from /sys
std::string get_device_name(const std::string &event_path) {
    std::string filename = event_path.substr(event_path.find_last_of('/') + 1); // event3
    std::string sys_path = "/sys/class/input/" + filename + "/device/name";

    int fd = open(sys_path.c_str(), O_RDONLY);
    if (fd < 0)
        return "Unknown Device";

    char buf[256] = {};
    read(fd, buf, sizeof(buf) - 1);
    close(fd);

    std::string name(buf);
    while (!name.empty() && (name.back() == '\n' || name.back() == '\r'))
        name.pop_back();

    return name;
}

// Helper to list all event devices
std::vector<std::string> get_event_devices() {
    std::vector<std::string> devices;
    DIR *dir = opendir("/dev/input");
    if (!dir) {
        perror("opendir /dev/input");
        return devices;
    }

    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (strncmp(entry->d_name, "event", 5) == 0) {
            devices.push_back("/dev/input/" + std::string(entry->d_name));
        }
    }
    closedir(dir);
    return devices;
}

// Test device by printing incoming key events
bool test_device(const std::string &device_path) {
    int fd = open(device_path.c_str(), O_RDONLY);
    if (fd < 0) {
        perror("open device");
        return false;
    }

    std::cout << "Testing device: " << device_path << " (" << get_device_name(device_path) << ")\n";
    std::cout << "Press keys, or ESC to stop testing...\n";

    while (true) {
        struct input_event ev{};
        ssize_t r = read(fd, &ev, sizeof(ev));
        if (r != sizeof(ev))
            continue;

        if (ev.type == EV_KEY) {
            std::cout << "Key " << ev.code << " state " << ev.value << "\n";

            // ESC key (KEY_ESC = 1?) to break testing
            if (ev.code == KEY_ESC && ev.value == 1)
                std::cout << "done testing" << std::endl;
            break;
        }
    }

    close(fd);
    return true;
}

std::string interactively_select_linux_device_name() {

    auto devices = get_event_devices();
    if (devices.empty()) {
        std::cerr << "No input devices found!\n";
        throw std::runtime_error("no input devices found");
    }

    int selected_index = -1;
    while (true) {
        std::cout << "Available input devices:\n\n";
        for (size_t i = 0; i < devices.size(); i++) {
            std::cout << "  [" << i << "] " << devices[i] << " (" << get_device_name(devices[i]) << ")\n";
        }

        std::cout << "\nSelect a device index to test: ";
        int choice;
        std::cin >> choice;

        if (choice < 0 || choice >= (int)devices.size()) {
            std::cerr << "Invalid selection.\n";
            continue;
        }

        // Test the chosen device
        test_device(devices[choice]);

        // Ask for confirmation
        std::cout << "Use this device? (y/n): ";
        char confirm;
        std::cin >> confirm;
        if (confirm == 'y' || confirm == 'Y') {
            selected_index = choice;
            break;
        }
    }

    std::string selected_device = devices[selected_index];
    std::cout << "Using device: " << selected_device << "\n";
    return selected_device;
}

void send_key(int ufd, int key, int value) {
    struct input_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = EV_KEY;
    ev.code = key;
    ev.value = value;
    write(ufd, &ev, sizeof(ev));

    memset(&ev, 0, sizeof(ev));
    ev.type = EV_SYN;
    ev.code = SYN_REPORT;
    ev.value = 0;
    write(ufd, &ev, sizeof(ev));
}

int create_virtual_keyboard_device() {
    int ufd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (ufd < 0) {
        perror("open /dev/uinput");
        return 1;
    }

    ioctl(ufd, UI_SET_EVBIT, EV_KEY);
    ioctl(ufd, UI_SET_EVBIT, EV_SYN);

    for (int k = 0; k < KEY_MAX; k++)
        ioctl(ufd, UI_SET_KEYBIT, k);

    struct uinput_setup us;
    memset(&us, 0, sizeof(us));
    us.id.bustype = BUS_USB;
    us.id.vendor = 0x1234;
    us.id.product = 0x5678;
    strcpy(us.name, "Forwarded Virtual Keyboard");

    ioctl(ufd, UI_DEV_SETUP, &us);
    ioctl(ufd, UI_DEV_CREATE);
    return ufd;
}
