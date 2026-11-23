/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Ordinary single-plane BGRA buffer: no API overrides or replacement classes. */
#import <Foundation/Foundation.h>
#import <IOSurface/IOSurface.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    @autoreleasepool {
        const size_t width = 17, height = 9, bytesPerElement = 4;
        size_t stride = IOSurfaceAlignProperty(kIOSurfaceBytesPerRow,
                                              width * bytesPerElement);
        size_t allocation = IOSurfaceAlignProperty(kIOSurfaceAllocSize,
                                                  height * stride);
        if (stride < width * bytesPerElement || allocation < height * stride) {
            printf("{\"probe\":\"iosurface-buffer\",\"passed\":false,"
                   "\"stage\":\"layout\",\"stride\":%zu,\"allocation\":%zu}\n",
                   stride, allocation);
            return 1;
        }
        NSDictionary *properties = @{
            (id)kIOSurfaceWidth: @(width), (id)kIOSurfaceHeight: @(height),
            (id)kIOSurfaceBytesPerElement: @(bytesPerElement),
            (id)kIOSurfaceBytesPerRow: @(stride),
            (id)kIOSurfaceAllocSize: @(allocation),
            (id)kIOSurfacePixelFormat: @((unsigned)'BGRA')
        };
        IOSurfaceRef surface = IOSurfaceCreate((CFDictionaryRef)properties);
        if (!surface) {
            puts("{\"probe\":\"iosurface-buffer\",\"passed\":false,\"stage\":\"create\"}");
            return 1;
        }
        BOOL layout = IOSurfaceGetWidth(surface) == width
            && IOSurfaceGetHeight(surface) == height
            && IOSurfaceGetBytesPerRow(surface) == stride
            && IOSurfaceGetAllocSize(surface) >= allocation;
        BOOL locked = layout && IOSurfaceLock(surface, 0, NULL) == 0;
        BOOL pixels = NO, unlocked = NO;
        if (locked) {
            unsigned char *base = IOSurfaceGetBaseAddress(surface);
            if (base) {
                for (size_t row = 0; row < height; ++row)
                    memset(base + row * stride, 0x5a, width * bytesPerElement);
                pixels = base[0] == 0x5a
                    && base[(height - 1) * stride + width * bytesPerElement - 1] == 0x5a;
            }
            unlocked = IOSurfaceUnlock(surface, 0, NULL) == 0;
        }
        BOOL passed = layout && locked && pixels && unlocked;
        CFRelease(surface);
        printf("{\"probe\":\"iosurface-buffer\",\"passed\":%s,"
               "\"stage\":\"pixels\",\"layout\":%s,\"locked\":%s,"
               "\"pixels\":%s,\"unlocked\":%s}\n",
               passed ? "true" : "false", layout ? "true" : "false",
               locked ? "true" : "false", pixels ? "true" : "false",
               unlocked ? "true" : "false");
        return passed ? 0 : 1;
    }
}
