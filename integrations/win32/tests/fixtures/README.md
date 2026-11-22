# Frozen prototype launcher fixture

legacy-win32-v012.py is the original own-code prototype launcher from donor
26bad17cef413d26c36c117f2f1123dca4edded7, path
distro/distro/packages/openkaryon-win32/usr/bin/openkaryon-win32.
Original SHA256: e98f0c5ca047b611a7421d7a10f8ee00d20d7f53187964bba9c1b62ed1e855fb.
Only the two leading license/fixture comments were added. The unchanged body
retains the old application/session-slot storage contract for real migration
acceptance; it is not the current runtime or a privileged migration helper.

The actor runs it only as the disposable ordinary QA account, without network
or capabilities. Windows fixture inputs and prefixes are synthetic/explicit.
No personal configuration or old package installation hook is included.
