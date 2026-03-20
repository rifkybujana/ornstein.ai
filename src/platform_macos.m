#import <Cocoa/Cocoa.h>
#import <IOKit/ps/IOPowerSources.h>
#import <IOKit/ps/IOPSKeys.h>
#import <CoreWLAN/CoreWLAN.h>
#import <mach/mach.h>

#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include "platform.h"

/* Drag view that explicitly starts a window drag on mouse-down.
   This works even when an OpenGL view covers the entire window. */
@interface TitlebarDragView : NSView
@end

@implementation TitlebarDragView
- (BOOL)mouseDownCanMoveWindow { return YES; }
- (void)mouseDown:(NSEvent *)event {
    [self.window performWindowDragWithEvent:event];
}
- (NSView *)hitTest:(NSPoint)point {
    NSView *hit = [super hitTest:point];
    if (hit != self) return hit;   /* pass through to traffic-light buttons */
    return self;
}
@end

void platform_style_window(GLFWwindow *window) {
    NSWindow *nswin = glfwGetCocoaWindow(window);

    nswin.titlebarAppearsTransparent = YES;
    nswin.titleVisibility = NSWindowTitleHidden;
    nswin.styleMask |= NSWindowStyleMaskFullSizeContentView;
    nswin.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
    nswin.backgroundColor = [NSColor colorWithRed:0.11 green:0.11 blue:0.12 alpha:1.0];

    /* Add a drag view covering the titlebar region. */
    NSView *content = nswin.contentView;
    CGFloat titlebarH = 28.0;
    NSRect dragFrame = NSMakeRect(0,
                                  content.frame.size.height - titlebarH,
                                  content.frame.size.width,
                                  titlebarH);
    TitlebarDragView *dragView = [[TitlebarDragView alloc] initWithFrame:dragFrame];
    dragView.autoresizingMask = NSViewWidthSizable | NSViewMinYMargin;
    [content addSubview:dragView positioned:NSWindowAbove relativeTo:nil];
}

/* ---------- Battery (IOKit power sources) ---------- */

float platform_battery_level(void) {
    CFTypeRef info = IOPSCopyPowerSourcesInfo();
    if (!info) return -1.0f;
    CFArrayRef list = IOPSCopyPowerSourcesList(info);
    if (!list || CFArrayGetCount(list) == 0) {
        if (list) CFRelease(list);
        CFRelease(info);
        return -1.0f;
    }
    CFDictionaryRef ps = IOPSGetPowerSourceDescription(info, CFArrayGetValueAtIndex(list, 0));
    float level = -1.0f;
    if (ps) {
        CFNumberRef capacity = CFDictionaryGetValue(ps, CFSTR(kIOPSCurrentCapacityKey));
        CFNumberRef max_cap  = CFDictionaryGetValue(ps, CFSTR(kIOPSMaxCapacityKey));
        if (capacity && max_cap) {
            int cur, max;
            CFNumberGetValue(capacity, kCFNumberIntType, &cur);
            CFNumberGetValue(max_cap, kCFNumberIntType, &max);
            if (max > 0) level = (float)cur / (float)max;
        }
    }
    CFRelease(list);
    CFRelease(info);
    return level;
}

/* ---------- WiFi (CoreWLAN) ---------- */

int platform_wifi_connected(void) {
    CWWiFiClient *client = [CWWiFiClient sharedWiFiClient];
    CWInterface *iface = [client interface];
    if (!iface) return 0;
    return iface.ssid != nil ? 1 : 0;
}

/* ---------- CPU (mach host_statistics) ---------- */

static uint64_t prev_total = 0;
static uint64_t prev_idle = 0;

float platform_cpu_usage(void) {
    host_cpu_load_info_data_t cpuinfo;
    mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
    if (host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO,
                        (host_info_t)&cpuinfo, &count) != KERN_SUCCESS)
        return 0.0f;

    uint64_t total = 0;
    for (int i = 0; i < CPU_STATE_MAX; i++)
        total += cpuinfo.cpu_ticks[i];
    uint64_t idle = cpuinfo.cpu_ticks[CPU_STATE_IDLE];

    uint64_t d_total = total - prev_total;
    uint64_t d_idle  = idle - prev_idle;
    prev_total = total;
    prev_idle = idle;

    if (d_total == 0) return 0.0f;
    return 1.0f - (float)d_idle / (float)d_total;
}
