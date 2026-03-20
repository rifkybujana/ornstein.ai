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

/* Register global hotkey Cmd+Shift+O. Call once after window setup. */
void platform_register_hotkey(void);

/* Returns 1 and clears the flag if the hotkey was pressed since last check. */
int platform_hotkey_pending(void);

/* Show the window and bring it to front. */
void platform_show_window(GLFWwindow *window);

/* Hide the window (order out). */
void platform_hide_window(GLFWwindow *window);

#endif
