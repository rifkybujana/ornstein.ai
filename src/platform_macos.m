#import <Cocoa/Cocoa.h>

#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include "platform.h"

/* Transparent view that lets the window be dragged from the titlebar area. */
@interface TitlebarDragView : NSView
@end

@implementation TitlebarDragView
- (BOOL)mouseDownCanMoveWindow { return YES; }
- (NSView *)hitTest:(NSPoint)point {
    /* Pass clicks on traffic light buttons through; capture everything else. */
    NSView *hit = [super hitTest:point];
    if (hit != self) return hit;
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
    nswin.movableByWindowBackground = YES;

    /* Add an invisible drag view covering the titlebar region. */
    NSView *content = nswin.contentView;
    CGFloat titlebarH = 28.0;
    NSRect dragFrame = NSMakeRect(0,
                                  content.frame.size.height - titlebarH,
                                  content.frame.size.width,
                                  titlebarH);
    TitlebarDragView *dragView = [[TitlebarDragView alloc] initWithFrame:dragFrame];
    dragView.autoresizingMask = NSViewWidthSizable | NSViewMinYMargin;
    [content addSubview:dragView];
}
