/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Two ordinary public queries for the same registered IOSurface service. */
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    io_iterator_t iterator = MACH_PORT_NULL;
    // Both matching calls consume their own newly created dictionary.
    kern_return_t enumerated = IOServiceGetMatchingServices(kIOMasterPortDefault,
        IOServiceNameMatching("IOSurfaceRoot"), &iterator);
    io_service_t first = enumerated == KERN_SUCCESS ? IOIteratorNext(iterator) : MACH_PORT_NULL;
    io_service_t single = IOServiceGetMatchingService(kIOMasterPortDefault,
        IOServiceNameMatching("IOSurfaceRoot"));
    io_name_t firstClass = {0}, singleClass = {0};
    kern_return_t firstResult = first ? IOObjectGetClass(first, firstClass) : kIOReturnNoDevice;
    kern_return_t singleResult = single ? IOObjectGetClass(single, singleClass) : kIOReturnNoDevice;
    int passed = enumerated == KERN_SUCCESS && first && single
        && firstResult == KERN_SUCCESS && singleResult == KERN_SUCCESS
        && strcmp(firstClass, "IOSurfaceRoot") == 0 && strcmp(singleClass, firstClass) == 0;
    printf("{\"probe\":\"iosurface-matching\",\"passed\":%s,\"iteratorStatus\":%d,"
           "\"iteratorFound\":%s,\"singleFound\":%s,\"iteratorClassStatus\":%d,"
           "\"singleClassStatus\":%d}\n", passed ? "true" : "false", enumerated,
           first ? "true" : "false", single ? "true" : "false", firstResult, singleResult);
    if (single) IOObjectRelease(single);
    if (first) IOObjectRelease(first);
    if (iterator) IOObjectRelease(iterator);
    return passed ? 0 : 1;
}
