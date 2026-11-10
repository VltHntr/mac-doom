/* Minimal native AppKit display/input backend for the classic C renderer. */
#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* AppKit imports stdbool macros; the legacy Doom headers use enum values. */
#undef true
#undef false

#include "doomdef.h"
#include "d_event.h"
#include "d_main.h"
#include "i_system.h"
#include "i_video.h"
#include "v_video.h"

static byte *macos_framebuffer;
static byte macos_palette[256][3];
static NSWindow *doomWindow;
static NSBitmapImageRep *doomBitmap;
static NSObject<NSWindowDelegate> *doomWindowDelegate;
static NSEventModifierFlags previousModifierFlags;
static int macosMouseButtons;
static BOOL macosMouseCaptured;

static void CaptureMouse(void)
{
    if (macosMouseCaptured)
        return;
    CGAssociateMouseAndMouseCursorPosition(false);
    [NSCursor hide];
    macosMouseCaptured = YES;
}

static void ReleaseMouse(void)
{
    if (!macosMouseCaptured)
        return;
    CGAssociateMouseAndMouseCursorPosition(true);
    [NSCursor unhide];
    macosMouseCaptured = NO;
}

static int DoomKeyCode(NSEvent *event)
{
    switch ([event keyCode])
    {
        case 53: return KEY_ESCAPE;
        case 36: case 76: return KEY_ENTER;
        case 48: return KEY_TAB;
        case 51: return KEY_BACKSPACE;
        case 123: return KEY_LEFTARROW;
        case 124: return KEY_RIGHTARROW;
        case 125: return KEY_DOWNARROW;
        case 126: return KEY_UPARROW;
        case 122: return KEY_F1; case 120: return KEY_F2;
        case 99: return KEY_F3; case 118: return KEY_F4;
        case 96: return KEY_F5; case 97: return KEY_F6;
        case 98: return KEY_F7; case 100: return KEY_F8;
        case 101: return KEY_F9; case 109: return KEY_F10;
        case 103: return KEY_F11; case 111: return KEY_F12;
        case 56: case 60: return KEY_RSHIFT;
        case 59: case 62: return KEY_RCTRL;
        case 58: case 61: return KEY_RALT;
        default: break;
    }

    NSString *characters = [event charactersIgnoringModifiers];
    if ([characters length] == 0)
        return 0;
    unichar character = [characters characterAtIndex:0];
    if (character <= 0x7f)
        return tolower((int)character);
    return 0;
}

static void PostDoomKey(int key, evtype_t type)
{
    event_t event;
    if (!key)
        return;
    event.type = type;
    event.data1 = key;
    event.data2 = 0;
    event.data3 = 0;
    D_PostEvent(&event);
}

static void PostKeyEvent(NSEvent *nativeEvent, evtype_t type)
{
    PostDoomKey(DoomKeyCode(nativeEvent), type);
}

static void PostMouseEvent(int deltaX, int deltaY)
{
    event_t event;
    event.type = ev_mouse;
    event.data1 = macosMouseButtons;
    event.data2 = deltaX;
    event.data3 = deltaY;
    D_PostEvent(&event);
}

@interface DoomWindowDelegate : NSObject <NSWindowDelegate>
@end

@implementation DoomWindowDelegate
- (void)windowDidBecomeKey:(NSNotification *)notification
{
    (void)notification;
    CaptureMouse();
}
- (void)windowDidResignKey:(NSNotification *)notification
{
    (void)notification;
    ReleaseMouse();
}
- (BOOL)windowShouldClose:(NSWindow *)sender
{
    (void)sender;
    ReleaseMouse();
    I_Quit();
    return NO;
}
@end

@interface DoomView : NSView
@end

@implementation DoomView
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    if (doomBitmap)
        [doomBitmap drawInRect:[self bounds]];
}
- (void)keyDown:(NSEvent *)event { PostKeyEvent(event, ev_keydown); }
- (void)keyUp:(NSEvent *)event { PostKeyEvent(event, ev_keyup); }
- (void)mouseDown:(NSEvent *)event
{
    (void)event;
    macosMouseButtons |= 1;
    PostMouseEvent(0, 0);
}
- (void)mouseUp:(NSEvent *)event
{
    (void)event;
    macosMouseButtons &= ~1;
    PostMouseEvent(0, 0);
}
- (void)rightMouseDown:(NSEvent *)event
{
    (void)event;
    macosMouseButtons |= 4;
    PostMouseEvent(0, 0);
}
- (void)rightMouseUp:(NSEvent *)event
{
    (void)event;
    macosMouseButtons &= ~4;
    PostMouseEvent(0, 0);
}
- (void)otherMouseDown:(NSEvent *)event
{
    int mask = [event buttonNumber] == 2 ? 2 : 4;
    macosMouseButtons |= mask;
    PostMouseEvent(0, 0);
}
- (void)otherMouseUp:(NSEvent *)event
{
    int mask = [event buttonNumber] == 2 ? 2 : 4;
    macosMouseButtons &= ~mask;
    PostMouseEvent(0, 0);
}
- (void)mouseMoved:(NSEvent *)event
{
    PostMouseEvent((int)([event deltaX] * 4.0), (int)(-[event deltaY] * 4.0));
}
- (void)mouseDragged:(NSEvent *)event
{
    [self mouseMoved:event];
}
- (void)rightMouseDragged:(NSEvent *)event
{
    [self mouseMoved:event];
}
- (void)otherMouseDragged:(NSEvent *)event
{
    [self mouseMoved:event];
}
- (void)flagsChanged:(NSEvent *)event
{
    NSEventModifierFlags current = [event modifierFlags];
    const struct { NSEventModifierFlags flag; int doomKey; } modifiers[] = {
        {NSEventModifierFlagControl, KEY_RCTRL},
        {NSEventModifierFlagShift, KEY_RSHIFT},
        {NSEventModifierFlagOption, KEY_RALT}
    };

    for (NSUInteger i = 0; i < sizeof(modifiers) / sizeof(modifiers[0]); ++i)
    {
        BOOL wasDown = (previousModifierFlags & modifiers[i].flag) != 0;
        BOOL isDown = (current & modifiers[i].flag) != 0;
        if (wasDown != isDown)
            PostDoomKey(modifiers[i].doomKey, isDown ? ev_keydown : ev_keyup);
    }
    previousModifierFlags = current;
}
@end

void I_InitGraphics(void)
{
    if (doomWindow)
        return;

    macos_framebuffer = (byte *)malloc(SCREENWIDTH * SCREENHEIGHT);
    if (!macos_framebuffer)
        I_Error("Could not allocate macOS framebuffer");
    memset(macos_framebuffer, 0, SCREENWIDTH * SCREENHEIGHT);
    screens[0] = macos_framebuffer;

    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp finishLaunching];

    NSRect frame = NSMakeRect(0, 0, SCREENWIDTH * 3, SCREENHEIGHT * 3);
    NSUInteger style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                       NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;
    doomWindow = [[NSWindow alloc] initWithContentRect:frame
                                             styleMask:style
                                               backing:NSBackingStoreBuffered
                                                 defer:NO];
    [doomWindow setTitle:@"mac-doom"];
    [doomWindow setContentView:[[DoomView alloc] initWithFrame:frame]];
    doomWindowDelegate = [[DoomWindowDelegate alloc] init];
    [doomWindow setDelegate:(id<NSWindowDelegate>)doomWindowDelegate];
    [doomWindow setAcceptsMouseMovedEvents:YES];
    [doomWindow makeKeyAndOrderFront:nil];
    [doomWindow makeFirstResponder:[doomWindow contentView]];
    [NSApp activateIgnoringOtherApps:YES];

    doomBitmap = [[NSBitmapImageRep alloc]
        initWithBitmapDataPlanes:NULL
                      pixelsWide:SCREENWIDTH
                      pixelsHigh:SCREENHEIGHT
                   bitsPerSample:8
                 samplesPerPixel:4
                        hasAlpha:YES
                        isPlanar:NO
                  colorSpaceName:NSDeviceRGBColorSpace
                     bytesPerRow:SCREENWIDTH * 4
                    bitsPerPixel:32];
    if (!doomBitmap)
        I_Error("Could not create macOS framebuffer image");
}

void I_ShutdownGraphics(void)
{
    ReleaseMouse();
    [doomWindow setDelegate:nil];
    [doomWindow close];
    doomWindow = nil;
    doomWindowDelegate = nil;
    doomBitmap = nil;
    free(macos_framebuffer);
    macos_framebuffer = NULL;
    screens[0] = NULL;
}

void I_SetPalette(byte *palette)
{
    if (!palette)
        return;
    for (int i = 0; i < 256; ++i)
    {
        macos_palette[i][0] = gammatable[usegamma][*palette++];
        macos_palette[i][1] = gammatable[usegamma][*palette++];
        macos_palette[i][2] = gammatable[usegamma][*palette++];
    }
}

void I_UpdateNoBlit(void) {}

void I_FinishUpdate(void)
{
    if (!macos_framebuffer || !doomBitmap)
        return;

    byte *pixels = [doomBitmap bitmapData];
    for (int y = 0; y < SCREENHEIGHT; ++y)
    {
        for (int x = 0; x < SCREENWIDTH; ++x)
        {
            int source = (SCREENHEIGHT - 1 - y) * SCREENWIDTH + x;
            int destination = (y * SCREENWIDTH + x) * 4;
            const byte *color = macos_palette[macos_framebuffer[source]];
            pixels[destination + 0] = color[0];
            pixels[destination + 1] = color[1];
            pixels[destination + 2] = color[2];
            pixels[destination + 3] = 255;
        }
    }

    [NSApp updateWindows];
    [[doomWindow contentView] setNeedsDisplay:YES];
    [doomWindow displayIfNeeded];
}

void I_StartFrame(void) {}

void I_StartTic(void)
{
    NSEvent *event;
    do
    {
        event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                  untilDate:[NSDate distantPast]
                                     inMode:NSDefaultRunLoopMode
                                    dequeue:YES];
        if (event)
            [NSApp sendEvent:event];
    } while (event);
}

void I_ReadScreen(byte *scr)
{
    if (macos_framebuffer && scr)
        memcpy(scr, macos_framebuffer, SCREENWIDTH * SCREENHEIGHT);
}
