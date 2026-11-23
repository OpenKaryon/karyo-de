/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Ordinary cooperating processes, using only public IOSurface APIs. */
#import <Foundation/Foundation.h>
#import <IOSurface/IOSurface.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static BOOL pixelsMatch(IOSurfaceRef surface)
{
    if (IOSurfaceLock(surface, kIOSurfaceLockReadOnly, NULL) != 0) return NO;
    unsigned char *p = IOSurfaceGetBaseAddress(surface);
    size_t stride = IOSurfaceGetBytesPerRow(surface);
    BOOL valid = p && IOSurfaceGetWidth(surface) == 17 && IOSurfaceGetHeight(surface) == 9
        && p[0] == 0x5a && p[8 * stride + 67] == 0x5a;
    return IOSurfaceUnlock(surface, kIOSurfaceLockReadOnly, NULL) == 0 && valid;
}

int main(int argc, char **argv)
{
    @autoreleasepool {
        if (argc < 3) return 2;
        if (!strcmp(argv[1], "--owner")) {
            if (argc != 4) return 2;
            size_t stride = IOSurfaceAlignProperty(kIOSurfaceBytesPerRow, 17 * 4);
            size_t allocation = IOSurfaceAlignProperty(kIOSurfaceAllocSize, 9 * stride);
            if (stride < 68 || allocation < 9 * stride) return 1;
            NSDictionary *properties = @{
                (id)kIOSurfaceWidth: @17, (id)kIOSurfaceHeight: @9,
                (id)kIOSurfaceBytesPerElement: @4, (id)kIOSurfacePixelFormat: @((unsigned)'BGRA'),
                (id)kIOSurfaceBytesPerRow: @(stride), (id)kIOSurfaceAllocSize: @(allocation),
                (id)kIOSurfaceIsGlobal: @YES
            };
            IOSurfaceRef surface = IOSurfaceCreate((CFDictionaryRef)properties);
            if (!surface || IOSurfaceLock(surface, 0, NULL) != 0) return 1;
            unsigned char *bytes = IOSurfaceGetBaseAddress(surface);
            if (!bytes) { CFRelease(surface); return 1; }
            for (size_t row = 0; row < 9; ++row) memset(bytes + row * stride, 0x5a, 68);
            uint32_t seed = 0;
            BOOL valid = IOSurfaceUnlock(surface, 0, &seed) == 0 && seed != 0;
            IOSurfaceIncrementUseCount(surface);
            IOSurfaceIncrementUseCount(surface);
            IOSurfaceID identity = IOSurfaceGetID(surface);
            valid = valid && identity != 0 && IOSurfaceGetUseCount(surface) == 2;
            FILE *ready = fopen(argv[2], "w");
            if (!ready) { CFRelease(surface); return 1; }
            fprintf(ready, "{\"id\":%u,\"seed\":%u,\"pid\":%d}\n", identity, seed, getpid());
            fclose(ready);
            CFAbsoluteTime deadline = CFAbsoluteTimeGetCurrent() + 25;
            while (access(argv[3], F_OK) != 0 && CFAbsoluteTimeGetCurrent() < deadline) usleep(50000);
            valid = valid && access(argv[3], F_OK) == 0 && pixelsMatch(surface)
                && IOSurfaceGetUseCount(surface) == 2;
            IOSurfaceDecrementUseCount(surface);
            IOSurfaceDecrementUseCount(surface);
            valid = valid && IOSurfaceGetUseCount(surface) == 0 && !IOSurfaceIsInUse(surface);
            CFRelease(surface);
            printf("{\"probe\":\"iosurface-lifecycle\",\"role\":\"owner\",\"passed\":%s,\"id\":%u,\"pid\":%d}\n",
                   valid ? "true" : "false", identity, getpid());
            return valid ? 0 : 1;
        }
        IOSurfaceID identity = (IOSurfaceID)strtoul(argv[2], NULL, 10);
        if (argc != 3 || !identity) return 2;
        IOSurfaceRef surface = IOSurfaceLookup(identity);
        if (!strcmp(argv[1], "--after-release")) {
            BOOL valid = surface == NULL;
            if (surface) CFRelease(surface);
            printf("{\"probe\":\"iosurface-lifecycle\",\"role\":\"after-release\",\"passed\":%s,\"id\":%u}\n",
                   valid ? "true" : "false", identity);
            return valid ? 0 : 1;
        }
        if (strcmp(argv[1], "--reader")) { if (surface) CFRelease(surface); return 2; }
        BOOL valid = surface && IOSurfaceGetID(surface) == identity && pixelsMatch(surface)
            && IOSurfaceGetUseCount(surface) == 0 && IOSurfaceIsInUse(surface);
        if (surface) {
            IOSurfaceIncrementUseCount(surface);
            IOSurfaceRef another = IOSurfaceLookup(identity);
            valid = valid && another && IOSurfaceGetUseCount(surface) == 1
                && IOSurfaceGetUseCount(another) == 1 && pixelsMatch(another);
            if (another) CFRelease(another);
            IOSurfaceDecrementUseCount(surface);
            valid = valid && IOSurfaceGetUseCount(surface) == 0 && IOSurfaceIsInUse(surface);
            CFRelease(surface);
        }
        printf("{\"probe\":\"iosurface-lifecycle\",\"role\":\"reader\",\"passed\":%s,\"id\":%u,\"pid\":%d}\n",
               valid ? "true" : "false", identity, getpid());
        return valid ? 0 : 1;
    }
}
