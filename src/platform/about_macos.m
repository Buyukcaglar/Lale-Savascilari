/*
 * about_macos.m - the About box on macOS (see about.h): the counterpart of
 * about_win32.c.
 *
 * SDL builds the menu bar; its application menu starts with "About <name>",
 * which opens AppKit's standard (empty) About panel.  about_install() points
 * that item at about_show() instead: a native alert with the program icon,
 * the two credit lines, the repository link, the scrolling licence text and
 * an OK button.  AppKit draws it light or dark with the system.
 *
 * The menu action arrives while the game pumps events (host_pump, on the
 * main thread); the alert is modal, so the game does not run while it is
 * open, and pc_resync_clock() afterwards keeps the emulated clock from
 * catching up on that time - the game is paused, as on Windows.
 */
#ifdef __APPLE__

#import <Cocoa/Cocoa.h>

#include "platform/about.h"
#include "platform/pc.h"
#include "platform/uitext.h"

@interface LaleAboutTarget : NSObject
- (void)showAbout:(id)sender;
@end

@implementation LaleAboutTarget
- (void)showAbout:(id)sender
{
    (void)sender;
    about_show();
}
@end

static LaleAboutTarget *g_target;
static int              g_open;

static NSString *utf8(const char *s)
{
    return [NSString stringWithUTF8String:s] ?: @"";
}

static NSTextField *label(NSString *text, NSRect frame)
{
    NSTextField *f = [[NSTextField alloc] initWithFrame:frame];
    f.stringValue = text;
    f.editable = NO;
    f.bezeled = NO;
    f.drawsBackground = NO;
    f.font = [NSFont systemFontOfSize:[NSFont smallSystemFontSize]];
    return f;
}

void about_show(void)
{
    if (g_open)
        return;
    g_open = 1;
    @autoreleasepool {
        enum { W = 440, LINK_H = 18, LABEL_H = 16, TEXT_H = 220, GAP = 6 };
        NSView *box = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, W, LINK_H + GAP + LABEL_H + TEXT_H)];

        /* the repository link: a selectable field with a link attribute opens it */
        NSTextField *link = label(@"", NSMakeRect(0, LABEL_H + TEXT_H + GAP, W, LINK_H));
        NSString *url = utf8(about_url);
        NSMutableParagraphStyle *centred = [[NSMutableParagraphStyle alloc] init];
        centred.alignment = NSTextAlignmentCenter;
        NSURL *target = [NSURL URLWithString:url];
        NSMutableDictionary *attrs = [@{
            NSFontAttributeName : [NSFont systemFontOfSize:[NSFont systemFontSize]],
            NSForegroundColorAttributeName : [NSColor linkColor],
            NSUnderlineStyleAttributeName : @(NSUnderlineStyleSingle),
            NSParagraphStyleAttributeName : centred,
        } mutableCopy];
        if (target)
            attrs[NSLinkAttributeName] = target;
        link.attributedStringValue = [[NSAttributedString alloc] initWithString:url attributes:attrs];
        link.selectable = YES;                  /* links are followed only in selectable fields */
        link.allowsEditingTextAttributes = YES;
        [box addSubview:link];

        [box addSubview:label(utf8(T_ABOUT_LICENSES), NSMakeRect(0, TEXT_H, W, LABEL_H))];

        /* AppKit's own scroll view + text view pair (container sizes and
           resizing set up as text in a scroll view needs them) */
        NSScrollView *scroll = [NSTextView scrollableTextView];
        scroll.frame = NSMakeRect(0, 0, W, TEXT_H - 2);
        scroll.hasVerticalScroller = YES;
        scroll.hasHorizontalScroller = NO;
        scroll.borderType = NSBezelBorder;
        NSTextView *text = scroll.documentView;
        text.editable = NO;
        text.selectable = YES;
        text.textContainerInset = NSMakeSize(4, 4);
        text.font = [NSFont systemFontOfSize:[NSFont smallSystemFontSize]];
        text.textColor = [NSColor labelColor];
        text.string = utf8(about_licenses);
        [box addSubview:scroll];

        NSAlert *alert = [[NSAlert alloc] init];
        alert.alertStyle = NSAlertStyleInformational;
        alert.messageText = utf8(about_title);
        alert.informativeText = [NSString stringWithFormat:@"%@\n%@", utf8(about_line1), utf8(about_line2)];
        alert.accessoryView = box;
        if (NSApp.applicationIconImage)
            alert.icon = NSApp.applicationIconImage;
        [alert addButtonWithTitle:utf8(T_OK)];
        [alert layout];
        alert.window.initialFirstResponder = alert.buttons.firstObject;

        /* The licence text must open at its first line, but AppKit scrolls it
           to the middle (animated) while the alert comes up.  Until the user
           does something - a click, the wheel, a key - the text is held at
           the top. */
        NSClipView *clip = scroll.contentView;
        __block BOOL user_acted = NO, holding = NO;
        clip.postsBoundsChangedNotifications = YES;
        id watch = [[NSNotificationCenter defaultCenter]
            addObserverForName:NSViewBoundsDidChangeNotification object:clip queue:nil
                    usingBlock:^(NSNotification *note) {
                        (void)note;
                        if (user_acted || holding || clip.bounds.origin.y == 0)
                            return;
                        holding = YES;
                        [clip scrollToPoint:NSZeroPoint];
                        [scroll reflectScrolledClipView:clip];
                        holding = NO;
                    }];
        id monitor = [NSEvent
            addLocalMonitorForEventsMatchingMask:NSEventMaskScrollWheel | NSEventMaskLeftMouseDown |
                                                 NSEventMaskKeyDown | NSEventMaskSwipe
                                         handler:^NSEvent *(NSEvent *e) {
                                             user_acted = YES;
                                             return e;
                                         }];
        [alert runModal];
        [NSEvent removeMonitor:monitor];
        [[NSNotificationCenter defaultCenter] removeObserver:watch];
    }
    g_open = 0;
    pc_resync_clock();                          /* the game was paused: no catching up */
}

void about_install(SDL_Window *window)
{
    (void)window;
    if (g_target)
        return;
    @autoreleasepool {
        NSMenu *app = [[NSApp.mainMenu itemAtIndex:0] submenu];
        if (!app)
            return;                             /* no menu bar (a dummy video driver) */
        g_target = [[LaleAboutTarget alloc] init];
        for (NSMenuItem *item in app.itemArray) {
            if (item.action == @selector(orderFrontStandardAboutPanel:)) {
                item.title = utf8(T_ABOUT_MENU_MAC);
                item.target = g_target;
                item.action = @selector(showAbout:);
                return;
            }
        }
        /* a menu bar without the standard item: add ours at the top */
        NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:utf8(T_ABOUT_MENU_MAC)
                                                      action:@selector(showAbout:)
                                               keyEquivalent:@""];
        item.target = g_target;
        [app insertItem:item atIndex:0];
        [app insertItem:[NSMenuItem separatorItem] atIndex:1];
    }
}

#endif /* __APPLE__ */
