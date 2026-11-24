/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Ordinary NSView backing-layer overrides and public CALayer display demand. */
#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
#import <IOSurface/IOSurface.h>
#include <stdio.h>

@interface BackedView : NSView <CALayerDelegate> {
@public
    IOSurfaceRef surface;
    unsigned setCalls, makeCalls, displayCalls, drawCalls;
}
@end
@implementation BackedView
- (CALayer *)makeBackingLayer { ++makeCalls; return [super makeBackingLayer]; }
- (void)setLayer:(CALayer *)value {
    ++setCalls;[value setDelegate:self];[super setLayer:value];
}
- (void)displayLayer:(CALayer *)layer { ++displayCalls;[layer setContents:(id)surface]; }
- (void)drawRect:(NSRect)rect {
    ++drawCalls;[[NSColor purpleColor] set];NSRectFill(rect);
}
@end

@interface DisplayCounter : NSObject <CALayerDelegate> { @public unsigned calls; }
@end
@implementation DisplayCounter
- (void)displayLayer:(CALayer *)layer { (void)layer;++calls; }
@end

static BOOL fill(IOSurfaceRef value,unsigned phase)
{
    if (IOSurfaceLock(value,0,NULL)!=0) return NO;
    const unsigned char colors[2][3]={{32,64,224},{192,176,32}};
    unsigned char *pixels=IOSurfaceGetBaseAddress(value);size_t stride=IOSurfaceGetBytesPerRow(value);
    if (pixels) for (size_t y=0;y<9;++y) for (size_t x=0;x<17;++x) {
        unsigned char *p=pixels+y*stride+x*4;
        p[0]=colors[phase-1][0];p[1]=colors[phase-1][1];p[2]=colors[phase-1][2];p[3]=255;
    }
    return IOSurfaceUnlock(value,0,NULL)==0 && pixels;
}
static BOOL phaseReport(const char *path,unsigned phase)
{
    FILE *f=fopen(path,"w");if (!f) return NO;fprintf(f,"{\"phase\":%u}\n",phase);return fclose(f)==0;
}
int main(int argc,char **argv)
{
    if (argc!=2) return 2;
    NSAutoreleasePool *pool=[NSAutoreleasePool new];[NSApplication sharedApplication];
    NSAutoreleasePool *objects=[NSAutoreleasePool new];
    DisplayCounter *counter=[DisplayCounter new];CALayer *detached=[CALayer layer];
    [detached setDelegate:counter];[detached setNeedsDisplay];[detached displayIfNeeded];
    BOOL one=counter->calls==1 && ![detached needsDisplay];
    [detached displayIfNeeded];BOOL coalesced=counter->calls==1;
    [detached setNeedsDisplay];[detached displayIfNeeded];BOOL repeated=counter->calls==2;
    [detached setDelegate:nil];[counter release];
    size_t stride=IOSurfaceAlignProperty(kIOSurfaceBytesPerRow,68);
    NSDictionary *properties=@{(id)kIOSurfaceWidth:@17,(id)kIOSurfaceHeight:@9,
        (id)kIOSurfaceBytesPerElement:@4,(id)kIOSurfacePixelFormat:@((unsigned)'BGRA'),
        (id)kIOSurfaceBytesPerRow:@(stride),
        (id)kIOSurfaceAllocSize:@(IOSurfaceAlignProperty(kIOSurfaceAllocSize,stride*9))};
    IOSurfaceRef surface=IOSurfaceCreate((CFDictionaryRef)properties);if (!surface || !fill(surface,1)) return 1;
    NSWindow *window=[[NSWindow alloc] initWithContentRect:NSMakeRect(140,150,340,180)
        styleMask:NSTitledWindowMask|NSClosableWindowMask backing:NSBackingStoreBuffered defer:NO];
    [window setTitle:@"Karyo IOSurface pixels"];
    BackedView *view=[[BackedView alloc] initWithFrame:NSMakeRect(0,0,340,180)];
    view->surface=surface;[window setContentView:view];
    [view setWantsLayer:YES];[view setNeedsDisplay:YES];
    [window makeKeyAndOrderFront:nil];[NSApp finishLaunching];
    BOOL valid=phaseReport(argv[1],1);unsigned phase=1;CFAbsoluteTime begin=CFAbsoluteTimeGetCurrent();
    while (CFAbsoluteTimeGetCurrent()-begin<6) {
        NSEvent *event=[NSApp nextEventMatchingMask:NSAnyEventMask
            untilDate:[NSDate dateWithTimeIntervalSinceNow:0.02] inMode:NSDefaultRunLoopMode dequeue:YES];
        if (event) [NSApp sendEvent:event];
        if (phase==1 && CFAbsoluteTimeGetCurrent()-begin>3) {
            valid=fill(surface,2)&&valid;[view setNeedsDisplay:YES];phase=2;
            valid=phaseReport(argv[1],2)&&valid;
        }
        [NSApp updateWindows];
    }
    BOOL configured=view->setCalls>0 && view->makeCalls==1 && [[view layer] delegate]==view;
    BOOL delegated=view->displayCalls>=2 && view->drawCalls==0;
    valid=valid && configured && delegated && one && coalesced && repeated
        && [[view layer] contents]==(id)surface && phase==2;
    unsigned setCalls=view->setCalls,makeCalls=view->makeCalls,displays=view->displayCalls,draws=view->drawCalls;
    [view setWantsLayer:NO];[view setLayer:nil];[view release];CFRelease(surface);
    [window orderOut:nil];[window release];[objects drain];
    printf("{\"probe\":\"iosurface-layer\",\"passed\":%s,\"phases\":%u,"
        "\"setLayerCalls\":%u,\"makeBackingLayerCalls\":%u,\"displayLayerCalls\":%u,\"drawRectCalls\":%u,"
        "\"directDisplay\":%s,\"coalesced\":%s,\"redisplay\":%s,\"visualReviewRequired\":true}\n",
        valid?"true":"false",phase,setCalls,makeCalls,displays,draws,
        one?"true":"false",coalesced?"true":"false",repeated?"true":"false");
    fflush(stdout);[pool drain];return valid?0:1;
}
