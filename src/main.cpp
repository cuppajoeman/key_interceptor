// aboutodo
// - generlized key sequence logic?
// - we could preface with space, so that x y would never pick up as space y
// - then usage would be tap hold, need a limited vector of it.
// once two spaces are detected it then checks for chords while spaces tillh eld
// down, either two consecutivet ticks or on, off on,
//

// needed this for clock to work
#define _POSIX_C_SOURCE 199309L

#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <iostream>
#include <string>
#include <vector>

#include <dirent.h>

#define CHORD_TIMEOUT_MS 200

// Helper to read "device name" from /sys
std::string get_device_name(const std::string &event_path) {
  std::string filename =
      event_path.substr(event_path.find_last_of('/') + 1); // event3
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

  std::cout << "Testing device: " << device_path << " ("
            << get_device_name(device_path) << ")\n";
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

static long now_ms() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (ts.tv_sec * 1000L) + (ts.tv_nsec / 1000000L);
}

static void send_key(int ufd, int key, int value) {
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

int main(int argc, char **argv) {

  auto devices = get_event_devices();
  if (devices.empty()) {
    std::cerr << "No input devices found!\n";
    return 1;
  }

  int selected_index = -1;
  while (true) {
    std::cout << "Available input devices:\n\n";
    for (size_t i = 0; i < devices.size(); i++) {
      std::cout << "  [" << i << "] " << devices[i] << " ("
                << get_device_name(devices[i]) << ")\n";
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

  /* -------- open real keyboard -------- */
  int fd = open(selected_device.c_str(), O_RDONLY);
  if (fd < 0) {
    perror("open real input device");
    return 1;
  }

  /* -------- grab exclusive control -------- */
  if (ioctl(fd, EVIOCGRAB, 1) < 0) {
    perror("EVIOCGRAB");
    return 1;
  }

  /* -------- setup uinput virtual keyboard -------- */
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

  /* -------- chord state -------- */
  int space_pending = 0;
  long space_time_ms = 0;

  struct input_event ev;

  bool logging_enabled = true;

#define KEY_STATE_SIZE (KEY_MAX + 1)
  static int key_state[KEY_STATE_SIZE]; // 0=up, 1=pressed, 2=repeat

  // Initialize key_state
  memset(key_state, 0, sizeof(key_state));

  bool command_run_since_last_space_relese = false;
  int iteration = 0;
  while (1) {
    fprintf(stdout, "LOOP (%d) START VVVV\n", iteration);
    ssize_t n = read(fd, &ev, sizeof(ev));
    if (n != sizeof(ev)) {
      if (logging_enabled) {
        fprintf(stderr, "[LOG] Read incomplete or failed: %zd bytes\n", n);
      }
      break;
    }

    if (logging_enabled) {
      fprintf(stderr, "[LOG] Event read: type=%d, code=%d, value=%d\n", ev.type,
              ev.code, ev.value);
    }

    // ---- update key_state ----
    if (ev.type == EV_KEY && ev.code >= 0 && ev.code < KEY_STATE_SIZE) {
      key_state[ev.code] = ev.value;
    }

    // startfold print key states
    fprintf(stderr, "[LOG] Key states: ");
    for (int k = 0; k < KEY_STATE_SIZE; k++) {
      if (key_state[k] != 0) {
        fprintf(stderr, "%d=%d ", k, key_state[k]);
      }
    }
    fprintf(stderr, "\n");

    // endfold

    // NOTE: for some reason trying to forward KEY_ENTER doesn't work idk why
    // because other mappings work, I don't undersatdn why this occurs but I
    // know how to fix it, can be looked at later
    if (ev.type == EV_KEY && ev.code == KEY_ENTER) {
      if (logging_enabled)
        fprintf(stderr, "[LOG] Remapping KEY_ENTER → KEY_KPENTER\n");
      ev.code = KEY_KPENTER;
    }

    bool is_programmer = true;
    // eventually want hotkeys for [] {} and ()

    bool ignore_current_event = false;

    // NOTE: we ignore space initial prcess, we only allow space on space up if
    // no command run see later down
    if (ev.type == EV_KEY && ev.code == KEY_SPACE && ev.value == 1) {
      ignore_current_event = true;
    }

    // also need to make sure multiple command can run at once so we can get alt
    // tabbing working properly NOTE: these commands are run "continuously" as
    // they passthrough the 2 value from the key used alogn with space
    if (key_state[KEY_SPACE]) {
      if (key_state[KEY_Z]) {
        if (logging_enabled)
          fprintf(stderr, "[LOG] SPACE+Z chord active, sending SHIFT\n");
        send_key(ufd, KEY_LEFTSHIFT, key_state[KEY_Z]);
        command_run_since_last_space_relese = true;
        ignore_current_event = ev.code == KEY_SPACE || ev.code == KEY_Z;
      } else if (key_state[KEY_X]) {
        if (logging_enabled)
          fprintf(stderr, "[LOG] SPACE+X chord active, sending CTRL\n");
        send_key(ufd, KEY_LEFTCTRL, key_state[KEY_X]);
        command_run_since_last_space_relese = true;
        ignore_current_event = ev.code == KEY_SPACE || ev.code == KEY_X;
      } else if (key_state[KEY_C]) {
        if (logging_enabled)
          fprintf(stderr, "[LOG] SPACE+C chord active, sending SUPER\n");
        send_key(ufd, KEY_LEFTMETA, key_state[KEY_C]);
        command_run_since_last_space_relese = true;
        ignore_current_event = ev.code == KEY_SPACE || ev.code == KEY_C;
      } else if (key_state[KEY_V]) {
        if (logging_enabled)
          fprintf(stderr, "[LOG] SPACE+V chord active, sending ALT\n");
        send_key(ufd, KEY_LEFTALT, key_state[KEY_V]);
        command_run_since_last_space_relese = true;
        ignore_current_event = ev.code == KEY_SPACE || ev.code == KEY_V;
      } else if (key_state[KEY_A]) {
        if (logging_enabled)
          fprintf(stderr, "[LOG] SPACE+A chord active, sending ESCAPE\n");
        send_key(ufd, KEY_ESC, key_state[KEY_A]);
        command_run_since_last_space_relese = true;
        ignore_current_event = ev.code == KEY_SPACE || ev.code == KEY_A;
      } else if (key_state[KEY_Q]) {
        if (logging_enabled)
          fprintf(stderr, "[LOG] SPACE+Q chord active, sending TAB\n");
        send_key(ufd, KEY_TAB, key_state[KEY_Q]);
        command_run_since_last_space_relese = true;
        ignore_current_event = ev.code == KEY_SPACE || ev.code == KEY_Q;
      } else if (key_state[KEY_P]) {
        if (logging_enabled)
          fprintf(stderr, "[LOG] SPACE+P chord active, sending BACKSPACE\n");
        send_key(ufd, KEY_BACKSPACE, key_state[KEY_P]);
        command_run_since_last_space_relese = true;
        ignore_current_event = ev.code == KEY_SPACE || ev.code == KEY_P;
      } else if (key_state[KEY_SEMICOLON]) {
        if (logging_enabled)
          fprintf(stderr, "[LOG] SPACE+; chord active, sending ENTER\n");
        send_key(ufd, KEY_KPENTER, key_state[KEY_SEMICOLON]);
        command_run_since_last_space_relese = true;
        ignore_current_event = ev.code == KEY_SPACE || ev.code == KEY_SEMICOLON;
      } else if (key_state[KEY_SLASH]) {
        if (logging_enabled)
          fprintf(stderr, "[LOG] SPACE+/ chord active, sending RIGHTSHIFT\n");
        send_key(ufd, KEY_RIGHTSHIFT, key_state[KEY_SLASH]);
        command_run_since_last_space_relese = true;
        ignore_current_event = ev.code == KEY_SPACE || ev.code == KEY_SLASH;
      }
    } else {
      // Release all modifier keys if not held by chord
      if (!key_state[KEY_LEFTCTRL]) {
        send_key(ufd, KEY_LEFTCTRL, 0);
      }
      if (!key_state[KEY_LEFTSHIFT]) {
        send_key(ufd, KEY_LEFTSHIFT, 0);
      }
      if (!key_state[KEY_LEFTMETA]) {
        send_key(ufd, KEY_LEFTMETA, 0);
      }
      if (!key_state[KEY_LEFTALT]) {
        send_key(ufd, KEY_LEFTALT, 0);
      }
      if (!key_state[KEY_ESC]) {
        send_key(ufd, KEY_ESC, 0);
      }
      if (!key_state[KEY_TAB]) {
        send_key(ufd, KEY_TAB, 0);
      }
      if (!key_state[KEY_BACKSPACE]) {
        send_key(ufd, KEY_BACKSPACE, 0);
      }
      // BUG?
      if (!key_state[KEY_KPENTER]) {
        send_key(ufd, KEY_KPENTER, 0);
      }
      if (!key_state[KEY_RIGHTSHIFT]) {
        send_key(ufd, KEY_RIGHTSHIFT, 0);
      }
    }

    // NOTE: when space comes back up and no command run then we get our space.
    if (ev.type == EV_KEY && ev.code == KEY_SPACE && ev.value == 0) {
      if (!command_run_since_last_space_relese) {
        printf("didn't run ommand sending space");
        ignore_current_event = true;
        // send a single space
        send_key(ufd, KEY_SPACE, 1);
        send_key(ufd, KEY_SPACE, 0);
      } else {
        printf("ran command not sending space");
      }
      command_run_since_last_space_relese = false;
    }

    // ---- forward normally ----
    if (logging_enabled) {
      fprintf(stderr, "[LOG] Forwarding event: type=%d, code=%d, value=%d\n",
              ev.type, ev.code, ev.value);
    }
    if (!ignore_current_event) {
      write(ufd, &ev, sizeof(ev));
      struct input_event syn = {0};
      syn.type = EV_SYN;
      syn.code = SYN_REPORT;
      syn.value = 0;
      write(ufd, &syn, sizeof(syn));
    }
    fprintf(stdout, "LOOP (%d) END ^^^^\n", iteration);
    iteration++;
  }

  /* cleanup */
  ioctl(fd, EVIOCGRAB, 0);
  ioctl(ufd, UI_DEV_DESTROY);
  close(fd);
  close(ufd);
  return 0;
}
