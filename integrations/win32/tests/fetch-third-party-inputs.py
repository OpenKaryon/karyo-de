#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fetch optional checksum-locked QA releases; never execute or package them."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import tempfile
import urllib.request


def verify(path,row):
    if path.is_symlink() or not path.is_file() or path.stat().st_size!=row['bytes']:
        raise ValueError('Existing QA input differs; preserve and inspect it: '+str(path))
    if hashlib.sha256(path.read_bytes()).hexdigest()!=row['sha256']:
        raise ValueError('Existing QA input checksum differs; preserve and inspect it: '+str(path))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    manifest=Path(__file__).with_name('third-party-inputs.json')
    rows=json.loads(manifest.read_text())['inputs']
    output=args.output.resolve();output.mkdir(parents=True,exist_ok=True)
    # Refuse an inconsistent existing set before any download or replacement.
    for row in rows:
        if not re.fullmatch(r'[A-Za-z0-9.+_-]+',row['file']) or not re.fullmatch(r'[a-f0-9]{64}',row['sha256']):
            raise ValueError('Invalid pinned QA input metadata')
        if not 0<row['bytes']<=32*1024*1024 or not row['url'].startswith('https://github.com/'):
            raise ValueError('Invalid pinned QA release URL/size')
        path=output/row['file']
        if path.exists() or path.is_symlink():verify(path,row)
    for row in rows:
        destination=output/row['file']
        if destination.exists():
            print('Verified existing '+row['file'],flush=True);continue
        request=urllib.request.Request(row['url'],headers={'User-Agent':'Karyo-DE-compatibility-QA'})
        with urllib.request.urlopen(request,timeout=60) as response:
            data=response.read(row['bytes']+1)
        if len(data)!=row['bytes'] or hashlib.sha256(data).hexdigest()!=row['sha256']:
            raise ValueError('Publisher release does not match pinned SHA256: '+row['file'])
        with tempfile.NamedTemporaryFile(prefix='.qa-input-',dir=output,delete=False) as stream:
            stream.write(data);temporary=Path(stream.name)
        # Another fetch must not overwrite differing bytes if it won the race.
        if destination.exists():
            verify(destination,row);temporary.unlink()
        else:
            temporary.rename(destination)
        print('Downloaded and verified '+row['file'],flush=True)


if __name__=='__main__':main()
