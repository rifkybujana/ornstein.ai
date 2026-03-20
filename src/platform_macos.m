#import <Cocoa/Cocoa.h>

#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include "platform.h"

void platform_style_window(GLFWwindow *window) {
    NSWindow *nswin = glfwGetCocoaWindow(window);

    nswin.titlebarAppearsTransparent = YES;
    nswin.titleVisibility = NSWindowTitleHidden;
    nswin.styleMask |= NSWindowStyleMaskFullSizeContentView;
    nswin.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
    nswin.backgroundColor = [NSColor colorWithRed:0.11 green:0.11 blue:0.12 alpha:1.0];
}
