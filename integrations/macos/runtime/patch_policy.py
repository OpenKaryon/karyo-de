# SPDX-License-Identifier: GPL-3.0-or-later
"""Reviewed Darling patch footprint; adapted from the prototype shadow checker."""
from pathlib import PurePosixPath
import re
import shlex

DARLING_REVISION = '60ba801decee7a00782f74f6be4c8ffb013f79ff'
COCOTRON_REVISION = 'c8d38d16a9f613d300157bebbab2b9501bc0c274'
OBJC_REVISION = '1a12df76d12bfc9fdfffadb290f7742763568765'
FOUNDATION_REVISION = '55a4341b470c2a56fea667c1d2167fb074226f04'
RUBY_REVISION = '423f2439478d2e24d7c19b2e0ea4a67ee3e80c3d'
UNWIND_REVISION = 'a91da1a0e262e04eb601152a84228ff733e48422'
DYLD_REVISION = '63f667cf06d7ed59553adebb0c8d70a117135ac9'
LIBSYSTEM_REVISION = '08df454b6eb0df9400aa4c39839a7efd6efd2c3c'
SCREEN_REVISION = '4bed52587563ede850bf9ff834567478ac1e616b'
COCOTRON_PREFIX = 'src/external/cocotron/'
EXPECTED_PATCH_COUNT = 31
EXACT_ROOT_FILES = {'src/external/dyld/src/dyldInitialization.cpp', 'src/external/libsystem/init.c', 'src/external/libunwind/CMakeLists.txt', 'src/external/screen/screen/attacher.c', 'src/external/screen/screen/display.c', 'src/external/screen/screen/fileio.c', 'src/external/screen/screen/misc.c', 'src/external/screen/screen/process.c', 'src/external/screen/screen/resize.c', 'src/external/screen/screen/screen.c', 'src/external/screen/screen/socket.c', 'src/external/screen/screen/termcap.c', 'src/external/screen/screen/window.c', 'src/external/screen/screen/ansi.c', 'src/external/ruby/ruby/debug_counter.h', 'src/external/foundation/src/NSConcreteMapTable.m',
                    'src/external/foundation/src/NSConcreteHashTable.m', 'src/startup/darling.c', 'src/CMakeLists.txt', 'CMakeLists.txt', 'cmake/use_ld64.cmake', 'src/external/objc4/runtime/CMakeLists.txt',
                    'src/frameworks/CoreServices/src/FSEvents/CMakeLists.txt',
                    'src/external/cocotron/AppKit/NSApplication.m',
                    'src/external/cocotron/AppKit/NSView.m',
                    'src/external/cocotron/AppKit/include/AppKit/NSView.h',
                    'src/external/cocotron/AppKit/NSTextInput.m',
                    'src/external/cocotron/AppKit/include/AppKit/NSTextInput.h',
                    'src/external/cocotron/AppKit/include/AppKit/NSEvent.h',
                    'src/external/cocotron/AppKit/NSEvent_mouse.h',
                    'src/external/cocotron/AppKit/include/AppKit/NSApplication.h',
                    'src/external/cocotron/AppKit/NSScroller.m',
                    'src/external/cocotron/AppKit/include/AppKit/NSScroller.h',
                    'src/external/cocotron/AppKit/NSWindow.m',
                    'src/external/cocotron/AppKit/NSGraphics.m',
                    'src/external/cocotron/AppKit/NSBitmapImageRep.m',
                    'src/external/cocotron/AppKit/NSImage.m',
                    'src/external/cocotron/AppKit/include/AppKit/NSImage.h',
                    'src/external/cocotron/AppKit/include/AppKit/NSGraphics.h',
                    'src/external/cocotron/AppKit/NSWindow-Private.h',
                    'src/external/cocotron/AppKit/include/AppKit/NSWindow.h',
                    'src/external/cocotron/AppKit/NSCachedImageRep.m',
                    'src/external/cocotron/AppKit/X11.backend/X11Display.m',
                    'src/external/cocotron/AppKit/X11.backend/X11Window.m'}
COCOTRON_DIRECTORIES = ('AppKit/NSColor.subproj/', 'AppKit/NSEvent.subproj/', 'CoreText/', 'CoreGraphics/', 'Onyx2D/')
PRIVATE_HEADER = COCOTRON_PREFIX + 'CoreText/CTFontDescriptorInternal.h'
HUNK = re.compile(r'^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@')
MAX_FILE_BYTES = 8 * 1024 * 1024



def confined_path(value, side):
    if value == '/dev/null':
        return None
    if not value.startswith(side + '/'):
        raise ValueError('Patch path lacks its explicit a/ or b/ prefix: ' + value)
    name = value[2:]
    path = PurePosixPath(name)
    if (not name or '\\' in name or '\0' in name or path.is_absolute()
            or any(part in ('', '.', '..') or part.startswith('.') for part in name.split('/'))):
        raise ValueError('Unsafe patch path: ' + value)
    permitted = name in EXACT_ROOT_FILES or (
        name.startswith(COCOTRON_PREFIX)
        and name[len(COCOTRON_PREFIX):].startswith(COCOTRON_DIRECTORIES))
    if not permitted:
        raise ValueError('Path outside the explicit runtime source directories: ' + name)
    if path.name != 'CMakeLists.txt' and path.suffix not in ('.c', '.m', '.h') and name not in ('cmake/use_ld64.cmake', 'src/external/dyld/src/dyldInitialization.cpp'):
        raise ValueError('Unexpected source file kind: ' + name)
    return name


def tokens(line, count):
    values = shlex.split(line, posix=True)
    if len(values) != count:
        raise ValueError('Unsupported patch header: ' + line)
    return values


def parse_paths(data):
    """Read git/unified headers, excluding source lines inside counted hunks."""
    text = data.decode('utf-8', errors='strict')
    records = []
    diff_pair = None
    old_header = None
    old_left = new_left = 0
    awaiting_hunk = False
    for line in text.splitlines():
        if old_left or new_left:
            if line.startswith('\\ No newline at end of file'):
                continue
            if not line or line[0] not in (' ', '+', '-'):
                raise ValueError('Malformed counted patch hunk.')
            old_left -= int(line[0] in (' ', '-'))
            new_left -= int(line[0] in (' ', '+'))
            if old_left < 0 or new_left < 0:
                raise ValueError('Patch hunk exceeds declared line counts.')
            continue
        if line.startswith('@@ '):
            match = HUNK.match(line)
            if not match or not awaiting_hunk:
                raise ValueError('Patch hunk lacks valid file headers.')
            old_left = int(match.group(2)) if match.group(2) is not None else 1
            new_left = int(match.group(4)) if match.group(4) is not None else 1
            continue
        if line.startswith('diff --git '):
            if old_header is not None:
                raise ValueError('Missing +++ header.')
            values = tokens(line, 4)
            diff_pair = (confined_path(values[2], 'a'), confined_path(values[3], 'b'))
            if None in diff_pair or diff_pair[0] != diff_pair[1]:
                raise ValueError('Renames or ambiguous git paths are unsupported.')
            awaiting_hunk = False
        elif line.startswith('--- '):
            if old_header is not None:
                raise ValueError('Duplicate --- header.')
            old_header = confined_path(tokens(line, 2)[1], 'a')
            # /dev/null needs a sentinel because None also means no header.
            if old_header is None:
                old_header = '/dev/null'
        elif line.startswith('+++ '):
            if old_header is None:
                raise ValueError('+++ without --- header.')
            old = None if old_header == '/dev/null' else old_header
            new = confined_path(tokens(line, 2)[1], 'b')
            if old is None and new is None or old is not None and new is not None and old != new:
                raise ValueError('Renames or null/null file patches are unsupported.')
            name = new or old
            if diff_pair is not None and name not in diff_pair:
                raise ValueError('Git and unified paths disagree.')
            records.append({'path': name, 'added': old is None, 'deleted': new is None})
            old_header = None
            diff_pair = None
            awaiting_hunk = True
        elif line.startswith(('rename ', 'copy ', 'similarity ', 'dissimilarity ', 'GIT binary patch', 'Binary files ')):
            raise ValueError('Binary/copy/rename patch metadata is unsupported.')
        elif line.startswith(('old mode ', 'new mode ', 'new file mode ', 'deleted file mode ')):
            if line.rsplit(' ', 1)[1] not in ('100644', '100755'):
                raise ValueError('Symlink/special file mode in patch.')
    if old_left or new_left or old_header is not None or not records:
        raise ValueError('Incomplete/empty patch headers or hunks.')
    if len({row['path'] for row in records}) != len(records):
        raise ValueError('Repeated file sections within a patch are unsupported.')
    return records
