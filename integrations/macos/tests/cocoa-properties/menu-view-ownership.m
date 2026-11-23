/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Exercise real NSMenuItem ownership, without subclassing the menu item. */
#import <Cocoa/Cocoa.h>
#include <stdio.h>

static unsigned destroyedViews;

@interface KaryoTrackedMenuView : NSView
@end
@implementation KaryoTrackedMenuView
- (void)dealloc
{
    ++destroyedViews;
    [super dealloc];
}
@end

int main(void)
{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    [NSApplication sharedApplication];
    NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:@"Ordinary item"
        action:NULL keyEquivalent:@""];
    BOOL empty = [item view] == nil;
    NSView *view = [[KaryoTrackedMenuView alloc] initWithFrame:NSMakeRect(0, 0, 80, 24)];
    [item setView:view];
    [view release];
    BOOL retained = [item view] != nil && destroyedViews == 0;
    [item setView:[item view]];
    BOOL selfAssignment = destroyedViews == 0;
    NSMenuItem *copy = [item copy];
    BOOL copied = [copy view] == [item view];
    [item setView:nil];
    BOOL cleared = [item view] == nil && [copy view] != nil && destroyedViews == 0;
    [copy release];
    BOOL released = destroyedViews == 1;
    [item setView:nil];
    [item release];
    BOOL noExtraRelease = destroyedViews == 1;

    BOOL passed = empty && retained && selfAssignment && copied && cleared
        && released && noExtraRelease;
    printf("{\"probe\":\"menu-view-ownership\",\"passed\":%s,\"defaultNil\":%s,"
        "\"retained\":%s,\"selfAssignment\":%s,\"copied\":%s,\"cleared\":%s,"
        "\"released\":%s,\"noExtraRelease\":%s,\"customViewRenderingAccepted\":false}\n",
        passed ? "true" : "false", empty ? "true" : "false", retained ? "true" : "false",
        selfAssignment ? "true" : "false", copied ? "true" : "false",
        cleared ? "true" : "false", released ? "true" : "false",
        noExtraRelease ? "true" : "false");
    fflush(stdout);
    [pool drain];
    return passed ? 0 : 1;
}
