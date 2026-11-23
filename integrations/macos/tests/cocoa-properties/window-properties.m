/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Exercise the installed window preference API, without replacing methods. */
#import <Cocoa/Cocoa.h>
#include <stdio.h>

int main(void)
{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    [NSApplication sharedApplication];
    NSWindow *first = [[NSWindow alloc] initWithContentRect:NSMakeRect(20, 20, 180, 100)
        styleMask:NSTitledWindowMask backing:NSBackingStoreBuffered defer:YES];
    NSWindow *second = [[NSWindow alloc] initWithContentRect:NSMakeRect(40, 40, 180, 100)
        styleMask:NSTitledWindowMask backing:NSBackingStoreBuffered defer:YES];

    BOOL defaults = [first tabbingMode] == NSWindowTabbingModeAutomatic
        && [second tabbingMode] == NSWindowTabbingModeAutomatic
        && ![first titlebarAppearsTransparent] && ![second titlebarAppearsTransparent];
    [first setTabbingMode:NSWindowTabbingModeDisallowed];
    [second setTabbingMode:NSWindowTabbingModePreferred];
    [first setTitlebarAppearsTransparent:YES];
    [second setTitlebarAppearsTransparent:NO];
    BOOL independent = [first tabbingMode] == NSWindowTabbingModeDisallowed
        && [second tabbingMode] == NSWindowTabbingModePreferred
        && [first titlebarAppearsTransparent] && ![second titlebarAppearsTransparent];
    [first setTabbingMode:NSWindowTabbingModeAutomatic];
    [first setTitlebarAppearsTransparent:NO];
    BOOL reset = [first tabbingMode] == NSWindowTabbingModeAutomatic
        && [second tabbingMode] == NSWindowTabbingModePreferred
        && ![first titlebarAppearsTransparent] && ![second titlebarAppearsTransparent];

    BOOL passed = defaults && independent && reset;
    printf("{\"probe\":\"window-properties\",\"passed\":%s,\"defaults\":%s,"
        "\"independent\":%s,\"reset\":%s,\"tabGroupsAccepted\":false,"
        "\"transparentFramesAccepted\":false}\n",
        passed ? "true" : "false", defaults ? "true" : "false",
        independent ? "true" : "false", reset ? "true" : "false");
    fflush(stdout);
    [first release];
    [second release];
    [pool drain];
    return passed ? 0 : 1;
}
