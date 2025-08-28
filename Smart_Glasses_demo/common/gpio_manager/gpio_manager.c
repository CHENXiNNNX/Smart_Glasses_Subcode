#include "gpio_manager.h"
#include <unistd.h>
#include <errno.h>

static int write_to_file(const char *path, const char *value) {
#if LV_USE_SIMULATOR == 0 
    FILE *file = fopen(path, "w");
    if (!file) return errno;
    fprintf(file, "%s", value);
    fclose(file);
    return 0;
#endif
    return -1;
}

static int read_from_file(const char *path, char *value, size_t len) {
#if LV_USE_SIMULATOR == 0 
    FILE *file = fopen(path, "r");
    if (!file) return errno;
    if (fgets(value, len, file) == NULL) {
        fclose(file);
        return errno;
    }
    fclose(file);
    return 0;
#endif
    return -1;
}

int calculate_gpio_pin(int bank, int group, int x) {
    return bank * 32 + (group * 8 + x);
}

int gpio_export(int gpio_pin) {
    char export_path[50];
    snprintf(export_path, sizeof(export_path), "/sys/class/gpio/export");
    char command[10];
    snprintf(command, sizeof(command), "%d", gpio_pin);
    return write_to_file(export_path, command);
}

int gpio_unexport(int gpio_pin) {
    char unexport_path[50];
    snprintf(unexport_path, sizeof(unexport_path), "/sys/class/gpio/unexport");
    char command[10];
    snprintf(command, sizeof(command), "%d", gpio_pin);
    return write_to_file(unexport_path, command);
}

int gpio_set_direction(int gpio_pin, const char *direction) {
    char direction_path[50];
    snprintf(direction_path, sizeof(direction_path), "/sys/class/gpio/gpio%d/direction", gpio_pin);
    return write_to_file(direction_path, direction);
}

int gpio_set_value(int gpio_pin, int value) {
    char value_path[50];
    snprintf(value_path, sizeof(value_path), "/sys/class/gpio/gpio%d/value", gpio_pin);
    char command[2];
    snprintf(command, sizeof(command), "%d", value);
    return write_to_file(value_path, command);
}

int gpio_get_value(int gpio_pin) {
    char value_path[50];
    char value_str[2] = "0";
    snprintf(value_path, sizeof(value_path), "/sys/class/gpio/gpio%d/value", gpio_pin);
    if(read_from_file(value_path, value_str, sizeof(value_str)) != 0) return -1;
    return atoi(value_str);
}

void gpio_init(int gpio_pin, const char *direction) {
    gpio_export(gpio_pin);
    gpio_set_direction(gpio_pin, direction);
}

void gpio_deinit(int gpio_pin) {
    gpio_unexport(gpio_pin);
}
