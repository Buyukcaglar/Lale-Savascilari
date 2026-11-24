/*
 * ui_macos.m - the import's progress on macOS (see ui.h): a small titled
 * panel with two lines, a progress bar and a cancel button, in the system's
 * controls and therefore in its light or dark appearance.  The message
 * dialog is SDL's NSAlert (ui.c).
 *
 * The job runs on the main thread and calls ui_progress() often;
 * SDL_PumpEvents() there runs the application's events, which is what
 * delivers the button's click.
 *
 * NOT YET BUILT OR RUN: written on Windows on 2026-11-25 against AppKit's
 * documented interfaces; to be checked at the next build on the Mac.
 */
#ifdef __APPLE__

#import <Cocoa/Cocoa.h>

#include "platform/ui.h"
#include "platform/uitext.h"

@interface LaleProgressTarget : NSObject
@property (nonatomic) BOOL cancelled;
- (void)cancel:(id)sender;
@end

@implementation LaleProgressTarget
- (void)cancel:(id)sender
{
    (void)sender;
    self.cancelled = YES;
}
@end

static NSPanel             *g_panel;
static NSTextField         *g_what, *g_doing;
static NSProgressIndicator *g_bar;
static LaleProgressTarget  *g_target;
static Uint64               g_last;

static NSString *utf8(const char *s)
{
    return (s ? [NSString stringWithUTF8String:s] : nil) ?: @"";
}

static NSTextField *label(NSRect frame, NSColor *color)
{
    NSTextField *t = [NSTextField labelWithString:@""];
    t.frame = frame;
    t.textColor = color;
    t.lineBreakMode = NSLineBreakByTruncatingMiddle;
    return t;
}

int ui_progress(SDL_Window *parent, const char *what, const char *doing, double fraction)
{
    (void)parent;
    @autoreleasepool {
        if (!g_panel) {
            const CGFloat W = 460, H = 136, PAD = 20;
            g_target = [[LaleProgressTarget alloc] init];
            g_panel = [[NSPanel alloc] initWithContentRect:NSMakeRect(0, 0, W, H)
                                                 styleMask:NSWindowStyleMaskTitled
                                                   backing:NSBackingStoreBuffered
                                                     defer:NO];
            g_panel.title = utf8(T_TITLE);
            g_panel.releasedWhenClosed = NO;
            g_what = label(NSMakeRect(PAD, H - PAD - 18, W - 2 * PAD, 18), [NSColor labelColor]);
            g_doing = label(NSMakeRect(PAD, H - PAD - 40, W - 2 * PAD, 18), [NSColor secondaryLabelColor]);
            g_bar = [[NSProgressIndicator alloc] initWithFrame:NSMakeRect(PAD, H - PAD - 66, W - 2 * PAD, 16)];
            g_bar.style = NSProgressIndicatorStyleBar;
            g_bar.minValue = 0.0;
            g_bar.maxValue = 1.0;
            g_bar.indeterminate = YES;
            [g_bar startAnimation:nil];
            NSButton *button = [NSButton buttonWithTitle:utf8(T_CANCEL) target:g_target action:@selector(cancel:)];
            button.keyEquivalent = @"\033";                 /* Esc */
            [button sizeToFit];
            NSRect f = button.frame;
            if (f.size.width < 90)
                f.size.width = 90;
            f.origin.x = W - PAD - f.size.width;
            f.origin.y = 12;
            button.frame = f;
            NSView *v = g_panel.contentView;
            [v addSubview:g_what];
            [v addSubview:g_doing];
            [v addSubview:g_bar];
            [v addSubview:button];
            [g_panel center];
            [g_panel makeKeyAndOrderFront:nil];
            g_last = 0;
        }
        Uint64 now = SDL_GetTicks();
        if (now - g_last >= 40) {                           /* the controls: 25 times a second at most */
            g_last = now;
            g_what.stringValue = utf8(what);
            g_doing.stringValue = utf8(doing);
            if (fraction < 0.0) {
                if (!g_bar.indeterminate) {
                    g_bar.indeterminate = YES;
                    [g_bar startAnimation:nil];
                }
            } else {
                if (g_bar.indeterminate) {
                    [g_bar stopAnimation:nil];
                    g_bar.indeterminate = NO;
                }
                g_bar.doubleValue = fraction > 1.0 ? 1.0 : fraction;
            }
        }
        SDL_PumpEvents();
        return g_target.cancelled ? 1 : 0;
    }
}

void ui_progress_close(void)
{
    @autoreleasepool {
        if (g_panel)
            [g_panel orderOut:nil];
        g_panel = nil;
        g_what = g_doing = nil;
        g_bar = nil;
        g_target = nil;
    }
}

#endif /* __APPLE__ */
