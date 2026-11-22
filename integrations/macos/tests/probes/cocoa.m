/* SPDX-License-Identifier: GPL-3.0-or-later */
/* A real Cocoa bundle with ordinary controls and a bounded native event loop. */
#import <Cocoa/Cocoa.h>
#include <stdio.h>

@interface KaryoProbe : NSObject { @public BOOL clicked; }
- (void)clicked:(id)sender;
@end
@implementation KaryoProbe
- (void)clicked:(id)sender { clicked = YES; }
@end

int main(int argc, char **argv)
{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    if (argc != 2) { fprintf(stderr, "Expected one absolute host report path\n"); return 2; }
    NSString *report = [NSString stringWithUTF8String:argv[1]];
    [NSApplication sharedApplication];
    NSWindow *window = [[NSWindow alloc] initWithContentRect:NSMakeRect(80, 80, 460, 220)
        styleMask:(NSTitledWindowMask | NSClosableWindowMask | NSMiniaturizableWindowMask)
        backing:NSBackingStoreBuffered defer:NO];
    [window setTitle:@"Karyo Cocoa — Привет"];
    NSTextField *label = [[NSTextField alloc] initWithFrame:NSMakeRect(24, 142, 400, 38)];
    [label setStringValue:@"Привет из настоящего Cocoa-приложения"];
    [label setEditable:NO];
    [label setBezeled:NO];
    [label setDrawsBackground:NO];
    [[window contentView] addSubview:label];
    NSButton *button = [[NSButton alloc] initWithFrame:NSMakeRect(24, 64, 200, 44)];
    KaryoProbe *target = [[KaryoProbe alloc] init];
    [button setTitle:@"Проверить кнопку"];
    [button setBezelStyle:NSRoundedBezelStyle];
    [button setTarget:target];
    [button setAction:@selector(clicked:)];
    [[window contentView] addSubview:button];
    [window makeKeyAndOrderFront:nil];
    [NSApp finishLaunching];
    [button performClick:nil];
    NSDate *end = [NSDate dateWithTimeIntervalSinceNow:4.0];
    unsigned iterations = 0;
    while ([end timeIntervalSinceNow] > 0) {
        NSEvent *event = [NSApp nextEventMatchingMask:NSAnyEventMask
            untilDate:[NSDate dateWithTimeIntervalSinceNow:0.05]
            inMode:NSDefaultRunLoopMode dequeue:YES];
        if (event) [NSApp sendEvent:event];
        [NSApp updateWindows];
        ++iterations;
    }
    BOOL passed = [window isVisible] && target->clicked && iterations > 0
        && [[label stringValue] isEqualToString:@"Привет из настоящего Cocoa-приложения"];
    NSDictionary *record = [NSDictionary dictionaryWithObjectsAndKeys:
        [NSNumber numberWithInt:1], @"schema", @"cocoa", @"probe", @"macos", @"target",
        @"x86_64", @"architecture", [NSNumber numberWithBool:passed], @"passed",
        [NSNumber numberWithBool:[window isVisible]], @"windowVisible",
        [NSNumber numberWithBool:target->clicked], @"buttonSignal",
        [NSNumber numberWithUnsignedInt:iterations], @"eventIterations", nil];
    NSError *error = nil;
    NSData *json = [NSJSONSerialization dataWithJSONObject:record options:0 error:&error];
    BOOL saved = json && [json writeToFile:report atomically:YES];
    if (!saved) fprintf(stderr, "Cocoa report could not be saved\n");
    [window orderOut:nil];
    [window release];
    [label release];
    [button release];
    [target release];
    [pool drain];
    return passed && saved ? 0 : 1;
}
