# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise the real PTY, working directory and argv forwarding."""
from pathlib import Path
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    marker = root / "pty-result"
    script = 'printf "%s\\n" "$PWD" "$1" > "$2"'
    result = subprocess.run([sys.argv[1], "-w", directory, "-e", "/bin/sh", "-c",
                             script, "fixture", "argument with spaces", str(marker)],
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=15)
    assert result.returncode == 0, result.stderr.decode(errors="replace")
    assert marker.read_text().splitlines() == [directory, "argument with spaces"]
