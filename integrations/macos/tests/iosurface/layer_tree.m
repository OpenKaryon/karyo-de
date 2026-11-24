/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Compare the same public surface at two ordinary layer-tree depths. */
#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
#import <IOSurface/IOSurface.h>
#include <stdio.h>

static IOSurfaceRef surface(unsigned char r, unsigned char g, unsigned char b)
{
    size_t stride = IOSurfaceAlignProperty(kIOSurfaceBytesPerRow, 68);
    size_t bytes = IOSurfaceAlignProperty(kIOSurfaceAllocSize, stride * 9);
    NSDictionary *properties = @{
        (id)kIOSurfaceWidth:@17, (id)kIOSurfaceHeight:@9,
        (id)kIOSurfaceBytesPerElement:@4, (id)kIOSurfacePixelFormat:@((unsigned)'BGRA'),
        (id)kIOSurfaceBytesPerRow:@(stride), (id)kIOSurfaceAllocSize:@(bytes)
    };
    IOSurfaceRef value = IOSurfaceCreate((CFDictionaryRef)properties);
    if (!value || IOSurfaceLock(value,0,NULL) != 0) return NULL;
    unsigned char *pixels = IOSurfaceGetBaseAddress(value);
    if (pixels) for (size_t y=0;y<9;++y) for (size_t x=0;x<17;++x) {
        unsigned char *p=pixels+y*stride+x*4;p[0]=b;p[1]=g;p[2]=r;p[3]=255;
    }
    BOOL unlocked=IOSurfaceUnlock(value,0,NULL)==0;
    return pixels && unlocked ? value : NULL;
}

static BOOL report(const char *path,unsigned phase)
{
    FILE *file=fopen(path,"w");if (!file) return NO;
    fprintf(file,"{\"phase\":%u}\n",phase);return fclose(file)==0;
}

int main(int argc,char **argv)
{
    if (argc!=2) return 2;
    NSAutoreleasePool *pool=[NSAutoreleasePool new];[NSApplication sharedApplication];
    NSAutoreleasePool *objects=[NSAutoreleasePool new];
    IOSurfaceRef red=surface(224,64,32),green=surface(32,192,80),blue=surface(32,96,224),yellow=surface(224,192,32);
    if (!red || !green || !blue || !yellow) return 1;
    NSWindow *window=[[NSWindow alloc] initWithContentRect:NSMakeRect(140,150,340,180)
        styleMask:NSTitledWindowMask|NSClosableWindowMask backing:NSBackingStoreBuffered defer:NO];
    [window setTitle:@"Karyo IOSurface pixels"];[window makeKeyAndOrderFront:nil];[NSApp finishLaunching];
    NSView *view=[window contentView];
    CALayer *root=[CALayer layer],*child=[CALayer layer],*grand=[CALayer layer],*later=[CALayer layer];
    [CATransaction begin];[CATransaction setDisableActions:YES];
    [root setFrame:[view bounds]];[root setContents:(id)red];
    [child setFrame:CGRectMake(30,20,260,140)];[child setContents:(id)green];
    [grand setFrame:CGRectMake(40,30,120,70)];[grand setContents:(id)blue];
    [child addSublayer:grand];[root addSublayer:child];
    [view setLayer:root];[view setWantsLayer:YES];[view setNeedsDisplay:NO];
    [CATransaction commit];[root setNeedsDisplay];[CATransaction flush];
    BOOL valid=[grand superlayer]==child && [child superlayer]==root;
    valid=report(argv[1],1)&&valid;
    CFAbsoluteTime begin=CFAbsoluteTimeGetCurrent();unsigned phase=1;
    while (CFAbsoluteTimeGetCurrent()-begin<9) {
        NSEvent *event=[NSApp nextEventMatchingMask:NSAnyEventMask
            untilDate:[NSDate dateWithTimeIntervalSinceNow:0.02] inMode:NSDefaultRunLoopMode dequeue:YES];
        if (event) [NSApp sendEvent:event];
        if (phase==1 && CFAbsoluteTimeGetCurrent()-begin>3) {
            [CATransaction begin];[CATransaction setDisableActions:YES];
            [grand removeFromSuperlayer];[grand setFrame:CGRectMake(70,50,120,70)];
            [root addSublayer:grand];[CATransaction commit];
            [root setNeedsDisplay];[CATransaction flush];phase=2;
            valid=[grand superlayer]==root && [root contents]==(id)red
                && [child contents]==(id)green && [grand contents]==(id)blue && valid;
            valid=report(argv[1],2)&&valid;
        }
        if (phase==2 && CFAbsoluteTimeGetCurrent()-begin>6) {
            [CATransaction begin];[CATransaction setDisableActions:YES];
            [grand removeFromSuperlayer];[grand setFrame:CGRectMake(40,30,120,70)];
            [child addSublayer:grand];[later setFrame:CGRectMake(70,50,120,70)];
            [later setContents:(id)yellow];[root addSublayer:later];
            [CATransaction commit];[root setNeedsDisplay];[CATransaction flush];phase=3;
            valid=[grand superlayer]==child&&[later superlayer]==root&&valid;
            valid=report(argv[1],3)&&valid;
        }
        [NSApp updateWindows];
    }
    valid=valid&&phase==3&&[window isVisible];
    fprintf(stderr,"TREE before cleanup validity=%d\n",valid);fflush(stderr);
    CFRelease(red);CFRelease(green);CFRelease(blue);CFRelease(yellow);
    [view setWantsLayer:NO];fprintf(stderr,"TREE layering disabled\n");fflush(stderr);
    [view setLayer:nil];fprintf(stderr,"TREE layer detached\n");fflush(stderr);
    [window orderOut:nil];fprintf(stderr,"TREE window hidden\n");fflush(stderr);
    [window release];fprintf(stderr,"TREE window released\n");fflush(stderr);
    [objects drain];fprintf(stderr,"TREE objects released\n");fflush(stderr);
    printf("{\"probe\":\"iosurface-layer\",\"passed\":%s,\"phases\":%u,"
           "\"scope\":\"nested, flat and later-sibling composition at unchanged visible geometry\","
           "\"visualReviewRequired\":true}\n",valid?"true":"false",phase);
    fflush(stdout);[pool drain];return valid?0:1;
}
