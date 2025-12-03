#ifndef SELECT_LINUX_DEVICE_HPP
#define SELECT_LINUX_DEVICE_HPP

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

// helper to read "device name" from /sys
std::string get_device_name(const std::string &event_path);

// helper to list all event devices
std::vector<std::string> get_event_devices();

// test device by printing incoming key events
bool test_device(const std::string &device_path);

std::string interactively_select_linux_device_name();

void send_key(int ufd, int key, int value);

int create_virtual_keyboard_device();

#endif // SELECT_LINUX_DEVICE_HPP
