# Native VMware drag surfaces

This optional integration rebuilds each distribution's own open-vm-tools source.
It makes the mapped Wayland drag-detection surface transparent and suppresses
the duplicate GTK drag image. The detection surface keeps its input behavior;
the native X11 paths remain unchanged. This component does not grant device
access or establish successful host clipboard, file transfer or display resize.

| Base | Native source | Rebuilt version |
| --- | --- | --- |
| Debian 13 | 2:12.5.0-2+deb13u1 | 2:12.5.0-2+deb13u1+karyo1 |
| Ubuntu 26.04 | 2:13.0.10-1ubuntu1.1 | 2:13.0.10-1ubuntu1.1+karyo1 |

## Build

Prepare the corresponding Builder source image first. From the DE checkout:

    docker build -t karyo-source-builder:trixie ../karyo-builder/environments/source-debian13
    docker build -f integrations/vmware/native-tools/environments/debian.Dockerfile -t karyo-vmware-source-builder:debian .
    python integrations/vmware/native-tools/fetch_inputs.py --base debian --image karyo-vmware-source-builder:debian
    karyo-builder source-package integrations/vmware/native-tools/recipes/debian.json --image karyo-vmware-source-builder:debian --output out/vmware/debian/packages --jobs 1

For Ubuntu use source-ubuntu2604, the source image tag ubuntu2604,
environments/ubuntu.Dockerfile, --base ubuntu, recipes/ubuntu.json and a separate
Ubuntu output directory. Native preparation/download uses signed APT indexes.
The downloader checks every archive against the reviewed recipe before publishing
absent inputs; differing existing files are retained for inspection. Source
archives and binary results stay in ignored out/vmware directories.

Builder captures the immutable image identity and all source/patch hashes, keeps
the native quilt series and builds without networking. Install only the matching
open-vm-tools and open-vm-tools-desktop pair into a disposable acceptance guest.
Other binary outputs of the native source build are not required by this feature.
No package or running desktop is replaced by the commands above.

## Ordinary drawing observations

Extract the recipe's source with dpkg-source --require-strong-checksums -x.
Inside the matching prepared image run:

    python3 tests/native_drag.py --source /work/source --expect stock

Apply both ordered patches with patch --fuzz=0 -p1, then run with --expect patched.
Run from this integration directory, with read-only inputs, a private writable
temporary directory, network disabled, no capabilities and no-new-privileges.
The script owns and stops its Xvfb instance. Build/probe jobs can use one CPU.

The probe compiles the real upstream DragDetWnd class and extracted
OnGtkDragBegin callback. It observes the mapped detection window, its input
region and actual alpha pixels, then the drag popup. A patched Wayland surface
must be depth32 with zero painted pixels. The native X11 detection window stays
InputOnly and its drag image stays painted. These are ordinary drawing checks,
not a substitute for an actual Windows-to-guest transfer or duplicate-file test.

## Source provenance

The reviewed prototype revision is 709484a0. Its transparent detection-window
and single drag-image changes are carried as separate ordered patches. Recipe
checksums identify the native distribution source and the exact reviewed patch
bytes. Native package copyright data and upstream source notices are preserved.
The modified upstream files retain their LGPL-2.1 licensing; the independent
Python runner and C++ observation fixtures are GPL-3.0-or-later.
