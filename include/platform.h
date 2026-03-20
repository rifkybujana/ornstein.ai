#ifndef PLATFORM_H
#define PLATFORM_H

#include <GLFW/glfw3.h>

/* Style the native window: transparent titlebar, dark mode, full-size content. */
void platform_style_window(GLFWwindow *window);

/* Returns battery level 0.0-1.0, or -1.0 if not available (e.g. desktop Mac). */
float platform_battery_level(void);

/* Returns 1 if WiFi is connected, 0 otherwise. */
int platform_wifi_connected(void);

/* Returns CPU usage 0.0-1.0 (system-wide). */
float platform_cpu_usage(void);

#endif
