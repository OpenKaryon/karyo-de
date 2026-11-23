/* SPDX-License-Identifier: GPL-3.0-or-later */
/* A normal Cocoa view presents and updates one public IOSurface object. */
#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
#import <IOSurface/IOSurface.h>
#include <stdio.h>
#include <string.h>

static BOOL fill(IOSurfaceRef surface, unsigned phase)
{
    if (IOSurfaceLock(surface, 0, NULL) != 0) return NO;
    unsigned char *p = IOSurfaceGetBaseAddress(surface);
    size_t stride = IOSurfaceGetBytesPerRow(surface);
    const unsigned char colors[2][4] = {{32, 64, 224, 255}, {192, 176, 32, 255}};
    if (p) for (size_t row = 0; row < 9; ++row)
        for (size_t x = 0; x < 17; ++x) memcpy(p + row * stride + 4 * x, colors[phase - 1], 4);
    return IOSurfaceUnlock(surface, 0, NULL) == 0 && p != NULL;
}

static BOOL phaseReport(const char *path, unsigned phase, uint32_t seed)
{
    FILE *file = fopen(path, "w");
    if (!file) return NO;
    fprintf(file, "{\"phase\":%u,\"seed\":%u}\n", phase, seed);
    return fclose(file) == 0;
}

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    NSAutoreleasePool *pool = [NSAutoreleasePool new];
    [NSApplication sharedApplication];
    NSAutoreleasePool *objects = [NSAutoreleasePool new];
    size_t stride = IOSurfaceAlignProperty(kIOSurfaceBytesPerRow, 68);
    size_t allocation = IOSurfaceAlignProperty(kIOSurfaceAllocSize, 9 * stride);
    if (stride < 68 || allocation < 9 * stride) return 1;
    NSDictionary *properties = @{
        (id)kIOSurfaceWidth: @17, (id)kIOSurfaceHeight: @9,
        (id)kIOSurfaceBytesPerElement: @4, (id)kIOSurfacePixelFormat: @((unsigned)'BGRA'),
        (id)kIOSurfaceBytesPerRow: @(stride), (id)kIOSurfaceAllocSize: @(allocation)
    };
    IOSurfaceRef surface = IOSurfaceCreate((CFDictionaryRef)properties);
    if (!surface || !fill(surface, 1)) return 1;
    IOSurfaceID identity = IOSurfaceGetID(surface);
    uint32_t firstSeed = IOSurfaceGetSeed(surface);
    NSWindow *window = [[NSWindow alloc] initWithContentRect:NSMakeRect(140, 150, 340, 180)
        styleMask:NSTitledWindowMask | NSClosableWindowMask backing:NSBackingStoreBuffered defer:NO];
    [window setTitle:@"Karyo IOSurface pixels"];
    [window makeKeyAndOrderFront:nil];
    [NSApp finishLaunching];
    NSView *view = [window contentView];
    // Layer hosting requires assigning the developer's layer before wantsLayer.
    // AppKit owns the contents of an automatically created backing layer.
    CALayer *host = [CALayer layer];
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    [host setFrame:[view bounds]];
    BOOL opacityDefault = [host opacity] == 1.0f;
    [host setOpacity:0.75f];
    BOOL opacityRoundTrip = [host opacity] == 0.75f;
    [host setOpacity:1.0f];
    [host setContents:(id)surface];
    [view setLayer:host];
    [view setWantsLayer:YES];
    [view setNeedsDisplay:NO];
    [CATransaction commit];
    BOOL valid = [view layer] == host && [host contentsScale] == 1.0
        && opacityDefault && opacityRoundTrip;
    [host setNeedsDisplay];
    [CATransaction flush];
    valid = phaseReport(argv[1], 1, firstSeed) && valid;
    CFAbsoluteTime begin = CFAbsoluteTimeGetCurrent();
    unsigned phase = 1, iterations = 0;
    BOOL seedChanged = NO;
    while (CFAbsoluteTimeGetCurrent() - begin < 6) {
        NSEvent *event = [NSApp nextEventMatchingMask:NSAnyEventMask
            untilDate:[NSDate dateWithTimeIntervalSinceNow:0.02]
            inMode:NSDefaultRunLoopMode dequeue:YES];
        if (event) [NSApp sendEvent:event];
        if (phase == 1 && CFAbsoluteTimeGetCurrent() - begin > 3) {
            valid = fill(surface, 2) && valid;
            // The same retained contents object is updated without reassigning it.
            [host setNeedsDisplay];
            [CATransaction flush];
            phase = 2;
            seedChanged = IOSurfaceGetSeed(surface) != firstSeed;
            valid = seedChanged && phaseReport(argv[1], 2, IOSurfaceGetSeed(surface)) && valid;
        }
        [NSApp updateWindows];
        ++iterations;
    }
    BOOL preserved = [view layer] == host && [host contents] == (id)surface;
    valid = valid && [window isVisible] && phase == 2 && iterations > 0 && preserved;
    CFRelease(surface);
    [view setWantsLayer:NO];
    [view setLayer:nil];
    [window orderOut:nil];
    [window release];
    [objects drain];
    IOSurfaceRef remaining = IOSurfaceLookup(identity);
    BOOL released = remaining == NULL;
    if (remaining) CFRelease(remaining);
    valid = valid && released;
    printf("{\"probe\":\"iosurface-layer\",\"passed\":%s,\"phases\":%u,"
           "\"seedChanged\":%s,\"surfaceReleasedAfterDetach\":%s,\"opacityDefault\":%s,"
           "\"opacityRoundTrip\":%s,\"hostContentsPreserved\":%s,\"visualReviewRequired\":true}\n",
           valid ? "true" : "false", phase, seedChanged ? "true" : "false", released ? "true" : "false",
           opacityDefault ? "true" : "false", opacityRoundTrip ? "true" : "false", preserved ? "true" : "false");
    [pool drain];
    return valid ? 0 : 1;
}
